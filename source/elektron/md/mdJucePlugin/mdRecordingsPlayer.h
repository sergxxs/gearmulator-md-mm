#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace mdJucePlugin
{
	// Standalone-only WAV playback for the recordings manager. Reuses the
	// existing JUCE playback infrastructure end to end:
	//
	//   AudioFormatReaderSource -> AudioTransportSource (read-ahead buffering
	//   on a TimeSliceThread, so the realtime callback never touches the
	//   file) -> AudioSourcePlayer, registered as an ADDITIONAL callback on
	//   the standalone's existing AudioDeviceManager, which mixes it with the
	//   synth output. The MM audio path is untouched; sample-rate conversion
	//   is handled by AudioTransportSource. Message-thread only API.
	class RecordingsPlayer
	{
	public:
		struct Status
		{
			bool playing = false;
			double positionSeconds = 0.0;
			double lengthSeconds = 0.0;
		};

		static RecordingsPlayer& instance();

		bool play(const juce::File& _file, juce::String& _error);
		void stop();
		Status getStatus();

	private:
		RecordingsPlayer();
		~RecordingsPlayer();

		juce::AudioFormatManager m_formats;
		juce::TimeSliceThread m_readThread{ "Recording playback disk reader" };
		std::unique_ptr<juce::AudioFormatReaderSource> m_readerSource;
		juce::AudioTransportSource m_transport;
		juce::AudioSourcePlayer m_sourcePlayer;
		bool m_callbackAttached = false;
	};
}
