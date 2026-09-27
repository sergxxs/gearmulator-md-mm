#include "mdRecordingsPlayer.h"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

namespace mdJucePlugin
{
	RecordingsPlayer& RecordingsPlayer::instance()
	{
		static RecordingsPlayer player;
		return player;
	}

	RecordingsPlayer::RecordingsPlayer()
	{
		m_formats.registerBasicFormats();
	}

	RecordingsPlayer::~RecordingsPlayer()
	{
		stop();
		if(m_callbackAttached)
		{
			if(auto* holder = juce::StandalonePluginHolder::getInstance())
				holder->deviceManager.removeAudioCallback(&m_sourcePlayer);
			m_callbackAttached = false;
		}
		m_readThread.stopThread(1000);
	}

	bool RecordingsPlayer::play(const juce::File& _file, juce::String& _error)
	{
		auto* holder = juce::StandalonePluginHolder::getInstance();
		if(!holder)
		{
			_error = "Playback is only available in the standalone app.";
			return false;
		}

		stop();

		// Message-thread file open; the reader source below is buffered via
		// the read-ahead thread, so the audio callback never does file I/O.
		auto* reader = m_formats.createReaderFor(_file);
		if(!reader)
		{
			_error = "Cannot read " + _file.getFileName();
			return false;
		}

		if(!m_readThread.isThreadRunning())
			m_readThread.startThread();

		m_readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
		m_transport.setSource(m_readerSource.get(), 65536, &m_readThread,
			reader->sampleRate, static_cast<int>(reader->numChannels));

		if(!m_callbackAttached)
		{
			// Registered once; the device manager mixes all of its callbacks,
			// so the synth output continues unchanged underneath.
			holder->deviceManager.addAudioCallback(&m_sourcePlayer);
			m_callbackAttached = true;
		}
		m_sourcePlayer.setSource(&m_transport);

		m_transport.setPosition(0.0);
		m_transport.start();
		return true;
	}

	void RecordingsPlayer::stop()
	{
		m_transport.stop();
		m_sourcePlayer.setSource(nullptr);
		m_transport.setSource(nullptr);
		m_readerSource.reset();
	}

	RecordingsPlayer::Status RecordingsPlayer::getStatus()
	{
		Status s;
		s.playing = m_transport.isPlaying();
		s.positionSeconds = m_transport.getCurrentPosition();
		s.lengthSeconds = m_transport.getLengthInSeconds();
		return s;
	}
}
