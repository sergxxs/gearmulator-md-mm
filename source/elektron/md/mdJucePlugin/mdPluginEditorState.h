#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	class PluginEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit PluginEditorState(AudioPluginAudioProcessor& _processor);

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
		void initContextMenu(juceRmlUi::Menu& _menu) override;

		// iOS control panel firmware hooks; validation goes through the
		// existing md::Rom / md::RomLoader checks (exact size + fingerprint).
		RomImportResult validateRomFile(const std::string& _path) override;
		std::string getRomStatusText() override;
		std::string getFirmwareDetailsText() override;

		// iOS control panel diagnostics hooks; forwarded to the existing
		// performance-diagnostics implementation in the MM processor.
		// iOS SysEx library hooks; forwarded to the existing user-SysEx
		// transfer implementation in mdEditor (ticket/progress/cancel/resume).
		bool sendSysexFile(const juce::File& _file) override;
		std::string getSysexTransferStatusText() override;
		bool isSysexTransferActive() override;
		bool canCancelSysexTransfer() override;
		void cancelSysexTransfer() override;
		bool canResumeSysexTransfer() override;
		void resumeSysexTransfer() override;

		// Recordings playback via the standalone RecordingsPlayer helper.
		bool playRecording(const juce::File& _file, std::string& _error) override;
		void stopRecordingPlayback() override;
		PlaybackStatus getRecordingPlaybackStatus() override;

		bool isPerformanceCaptureActive() override;
		void setPerformanceCaptureActive(bool _active) override;
		std::string getPerformanceStatusText() override;
		juce::File getDiagnosticsLogsFolder() override;
	};
}
