#include "audioRecorder.h"

namespace pluginLib
{
	namespace
	{
		// 8 seconds of stereo float headroom between the realtime producer
		// and the file writer; ~3 MB at 48 kHz, allocated once per start().
		constexpr double g_fifoSeconds = 8.0;
		// Drain granularity; also the unit for periodic header refreshes.
		constexpr size_t g_ioChunkFloats = 65536;
		// Refresh the header roughly every ~2 s of audio so an interrupted
		// process leaves a playable file up to the last refresh instead of a
		// corrupted one.
		constexpr uint32_t g_headerRefreshEveryChunks = 16;

		constexpr uint32_t g_headerBytes = 56;	// RIFF+fmt(IEEE float)+fact+data
		constexpr uint32_t g_bytesPerFrame = 2 * sizeof(float);
	}

	AudioRecorder::AudioRecorder()
	{
		m_worker = std::thread([this] { workerLoop(); });
	}

	AudioRecorder::~AudioRecorder()
	{
		requestStop();
		{
			std::lock_guard lock(m_workerMutex);
			m_workerExit = true;
		}
		m_workerCv.notify_all();
		if(m_worker.joinable())
			m_worker.join();
	}

	bool AudioRecorder::start(const juce::File& _folder, const double _sampleRate, juce::String& _error)
	{
		const auto state = m_state.load(std::memory_order_relaxed);
		if(state != State::Idle && state != State::Saved && state != State::Error)
		{
			_error = "A recording is already in progress.";
			return false;
		}
		if(_sampleRate <= 0.0)
		{
			_error = "Audio is not running.";
			return false;
		}

		if(!_folder.createDirectory())
		{
			_error = "Could not create the recordings folder.";
			return false;
		}

		auto file = _folder.getChildFile("recording-"
			+ juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".wav");
		if(file.existsAsFile())
			file = file.getNonexistentSibling();

		auto stream = std::make_unique<juce::FileOutputStream>(file);
		if(!stream->openedOk())
		{
			_error = "Could not create " + file.getFileName();
			return false;
		}

		m_file = file;
		m_stream = std::move(stream);
		m_sampleRate = _sampleRate;
		m_framesWritten.store(0, std::memory_order_relaxed);
		m_droppedBlocks.store(0, std::memory_order_relaxed);
		m_writesSinceHeaderRefresh = 0;
		{
			const juce::ScopedLock sl(m_statusLock);
			m_savedFileName.clear();
			m_error.clear();
		}

		if(!writeHeader(false))
		{
			m_stream.reset();
			m_file.deleteFile();
			_error = "Could not write the WAV header.";
			return false;
		}

		const auto fifoFloats = juce::nextPowerOfTwo(
			static_cast<int>(_sampleRate * 2.0 * g_fifoSeconds));
		if(m_buffer.size() < static_cast<size_t>(fifoFloats))
			m_buffer.resize(static_cast<size_t>(fifoFloats));
		if(m_ioChunk.size() < g_ioChunkFloats)
			m_ioChunk.resize(g_ioChunkFloats);
		m_fifo.setTotalSize(fifoFloats);
		m_fifo.reset();

		// Publish last: the realtime tap only runs once this store is seen.
		m_state.store(State::Recording, std::memory_order_release);
		m_workerCv.notify_all();
		return true;
	}

	void AudioRecorder::requestStop()
	{
		auto expected = State::Recording;
		if(m_state.compare_exchange_strong(expected, State::Stopping,
			std::memory_order_acq_rel, std::memory_order_relaxed))
			m_workerCv.notify_all();
	}

	AudioRecorder::Status AudioRecorder::getStatus() const
	{
		Status s;
		s.state = m_state.load(std::memory_order_relaxed);
		const auto frames = m_framesWritten.load(std::memory_order_relaxed);
		s.seconds = m_sampleRate > 0.0 ? static_cast<double>(frames) / m_sampleRate : 0.0;
		s.fileBytes = static_cast<juce::int64>(g_headerBytes)
			+ static_cast<juce::int64>(frames) * g_bytesPerFrame;
		s.droppedBlocks = m_droppedBlocks.load(std::memory_order_relaxed);
		const juce::ScopedLock sl(m_statusLock);
		s.savedFileName = m_savedFileName;
		s.error = m_error;
		return s;
	}

	void AudioRecorder::workerLoop()
	{
		for(;;)
		{
			{
				std::unique_lock lock(m_workerMutex);
				m_workerCv.wait_for(lock, std::chrono::milliseconds(100));
				if(m_workerExit)
					break;
			}

			const auto state = m_state.load(std::memory_order_acquire);
			if(state != State::Recording && state != State::Stopping)
				continue;

			drainToFile();

			if(m_state.load(std::memory_order_acquire) == State::Stopping)
			{
				drainToFile();	// producer already stopped; empty the FIFO
				m_state.store(State::Finalizing, std::memory_order_release);
				finalize();
			}
		}

		// Shutdown while a recording was still live: finalize what we have so
		// no corrupted file is left behind.
		if(m_state.load(std::memory_order_acquire) == State::Stopping
			|| m_state.load(std::memory_order_acquire) == State::Recording)
		{
			m_state.store(State::Stopping, std::memory_order_release);
			drainToFile();
			m_state.store(State::Finalizing, std::memory_order_release);
			finalize();
		}
	}

	void AudioRecorder::drainToFile()
	{
		if(!m_stream)
			return;

		for(;;)
		{
			const auto ready = m_fifo.getNumReady();
			if(ready <= 0)
				return;

			const auto todo = juce::jmin(ready, static_cast<int>(g_ioChunkFloats));

			int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
			m_fifo.prepareToRead(todo, start1, size1, start2, size2);
			if(size1 > 0)
				std::memcpy(m_ioChunk.data(), m_buffer.data() + start1,
					static_cast<size_t>(size1) * sizeof(float));
			if(size2 > 0)
				std::memcpy(m_ioChunk.data() + size1, m_buffer.data() + start2,
					static_cast<size_t>(size2) * sizeof(float));
			m_fifo.finishedRead(size1 + size2);

			const auto floats = static_cast<size_t>(size1 + size2);
			if(!m_stream->write(m_ioChunk.data(), floats * sizeof(float)))
			{
				failRecording("Write failed (disk full?).");
				return;
			}
			m_framesWritten.fetch_add(floats / 2u, std::memory_order_relaxed);

			if(++m_writesSinceHeaderRefresh >= g_headerRefreshEveryChunks)
			{
				m_writesSinceHeaderRefresh = 0;
				m_stream->flush();
				(void)writeHeader(false);
			}
		}
	}

	bool AudioRecorder::writeHeader(const bool _final)
	{
		if(!m_stream)
			return false;

		const auto frames = m_framesWritten.load(std::memory_order_relaxed);
		const auto dataBytes = static_cast<uint32_t>(frames * g_bytesPerFrame);
		const auto sampleRate = static_cast<uint32_t>(m_sampleRate);
		const auto byteRate = sampleRate * g_bytesPerFrame;

		uint8_t header[g_headerBytes];
		const auto put32 = [&](const size_t _pos, const uint32_t _v)
		{
			header[_pos] = static_cast<uint8_t>(_v);
			header[_pos + 1] = static_cast<uint8_t>(_v >> 8);
			header[_pos + 2] = static_cast<uint8_t>(_v >> 16);
			header[_pos + 3] = static_cast<uint8_t>(_v >> 24);
		};
		const auto put16 = [&](const size_t _pos, const uint16_t _v)
		{
			header[_pos] = static_cast<uint8_t>(_v);
			header[_pos + 1] = static_cast<uint8_t>(_v >> 8);
		};
		const auto putTag = [&](const size_t _pos, const char* _tag)
		{
			std::memcpy(header + _pos, _tag, 4);
		};

		putTag(0, "RIFF");
		put32(4, g_headerBytes - 8 + dataBytes);
		putTag(8, "WAVE");
		putTag(12, "fmt ");
		put32(16, 16);
		put16(20, 3);					// IEEE float
		put16(22, 2);					// stereo
		put32(24, sampleRate);
		put32(28, byteRate);
		put16(32, g_bytesPerFrame);		// block align
		put16(34, 32);					// bits per sample
		putTag(36, "fact");
		put32(40, 4);
		put32(44, static_cast<uint32_t>(frames));
		putTag(48, "data");
		put32(52, dataBytes);

		const auto endPosition = m_stream->getPosition();
		if(!m_stream->setPosition(0) || !m_stream->write(header, g_headerBytes))
			return false;
		if(_final)
			return true;
		return m_stream->setPosition(juce::jmax<juce::int64>(endPosition, g_headerBytes));
	}

	void AudioRecorder::finalize()
	{
		if(!m_stream)
		{
			m_state.store(State::Error, std::memory_order_release);
			return;
		}

		m_stream->flush();
		const bool headerOk = writeHeader(true);
		m_stream.reset();	// flush + close

		if(!headerOk)
		{
			// A file whose header could not be completed is not a recording.
			m_file.deleteFile();
			const juce::ScopedLock sl(m_statusLock);
			m_error = "Could not finalize the WAV file.";
			m_state.store(State::Error, std::memory_order_release);
			return;
		}

		{
			const juce::ScopedLock sl(m_statusLock);
			m_savedFileName = m_file.getFileName();
		}
		m_state.store(State::Saved, std::memory_order_release);
	}

	void AudioRecorder::failRecording(const juce::String& _error)
	{
		// Stop accepting samples, then try to salvage what was written so
		// far; if even the header cannot be completed the file is removed.
		m_state.store(State::Finalizing, std::memory_order_release);
		{
			const juce::ScopedLock sl(m_statusLock);
			m_error = _error;
		}
		finalize();
		if(m_state.load(std::memory_order_relaxed) == State::Saved)
		{
			// Salvaged partial file - keep it, but report the truncation.
			m_state.store(State::Error, std::memory_order_release);
		}
	}
}
