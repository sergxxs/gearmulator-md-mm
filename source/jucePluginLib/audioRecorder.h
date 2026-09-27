#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <juce_core/juce_core.h>

namespace pluginLib
{
	// Realtime-safe stereo WAV recorder for the final synth output.
	//
	// Audio thread:   processAudio() only - one relaxed atomic load when idle;
	//                 while recording, a bounded interleave-copy into a
	//                 preallocated lock-free FIFO (juce::AbstractFifo). No
	//                 allocation, no locks, no file I/O, no waiting, ever.
	// Worker thread:  drains the FIFO and performs all file work (header,
	//                 writes, periodic header refresh, finalize). The audio
	//                 thread never signals it; the worker uses a timed wait,
	//                 which only runs while a recording is active.
	// Message thread: start()/requestStop()/getStatus().
	//
	// Format: IEEE float32, 2 channels, native sample rate - a byte-exact
	// copy of the samples the device outputs, no conversion in the callback.
	class AudioRecorder
	{
	public:
		enum class State : uint8_t
		{
			Idle,
			Recording,
			Stopping,	// no longer accepting samples; worker draining
			Finalizing,
			Saved,
			Error
		};

		struct Status
		{
			State state = State::Idle;
			double seconds = 0.0;
			juce::int64 fileBytes = 0;
			uint32_t droppedBlocks = 0;
			juce::String savedFileName;
			juce::String error;
		};

		AudioRecorder();
		~AudioRecorder();

		AudioRecorder(const AudioRecorder&) = delete;
		AudioRecorder& operator=(const AudioRecorder&) = delete;

		// Message thread. Creates the file with a placeholder header, resets
		// all state and atomically enables the realtime tap.
		bool start(const juce::File& _folder, double _sampleRate, juce::String& _error);

		// Message thread (or the iOS background hook). Atomically stops
		// accepting samples; the worker drains and finalizes asynchronously.
		void requestStop();

		Status getStatus() const;

		// The file currently being written (or last written). Used by the UI
		// to exclude an in-progress recording from play/rename/delete.
		juce::File getActiveFile() const
		{
			const juce::ScopedLock sl(m_statusLock);
			return m_file;
		}

		bool isRecordingOrBusy() const
		{
			const auto s = m_state.load(std::memory_order_relaxed);
			return s == State::Recording || s == State::Stopping || s == State::Finalizing;
		}

		// AUDIO THREAD. _r may be null (mono fallback duplicates _l).
		void processAudio(const float* _l, const float* _r, size_t _numFrames) noexcept
		{
			if(m_state.load(std::memory_order_relaxed) != State::Recording || !_l || !_numFrames)
				return;

			const auto needed = static_cast<int>(_numFrames * 2u);

			int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
			m_fifo.prepareToWrite(needed, start1, size1, start2, size2);

			if(size1 + size2 < needed)
			{
				// Worker fell behind: drop this whole block, never wait.
				m_droppedBlocks.fetch_add(1, std::memory_order_relaxed);
				return;
			}

			const float* r = _r ? _r : _l;
			size_t frame = 0;
			int channel = 0;
			const auto fill = [&](const int _start, const int _size)
			{
				for(int i = 0; i < _size; ++i)
				{
					m_buffer[static_cast<size_t>(_start + i)] = channel == 0 ? _l[frame] : r[frame];
					if(++channel == 2)
					{
						channel = 0;
						++frame;
					}
				}
			};
			fill(start1, size1);
			fill(start2, needed - size1);

			m_fifo.finishedWrite(needed);
		}

	private:
		void workerLoop();
		void drainToFile();
		bool writeHeader(bool _final);
		void finalize();
		void failRecording(const juce::String& _error);

		juce::AbstractFifo m_fifo{ 2 };
		std::vector<float> m_buffer;
		std::vector<float> m_ioChunk;

		std::atomic<State> m_state{ State::Idle };
		std::atomic<uint32_t> m_droppedBlocks{ 0 };
		std::atomic<uint64_t> m_framesWritten{ 0 };

		double m_sampleRate = 0.0;
		juce::File m_file;
		std::unique_ptr<juce::FileOutputStream> m_stream;
		uint32_t m_writesSinceHeaderRefresh = 0;

		mutable juce::CriticalSection m_statusLock;
		juce::String m_savedFileName;
		juce::String m_error;

		std::mutex m_workerMutex;
		std::condition_variable m_workerCv;
		bool m_workerExit = false;
		std::thread m_worker;
	};
}
