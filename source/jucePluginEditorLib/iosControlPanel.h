#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_IOS

#include <functional>
#include <memory>
#include <vector>

namespace pluginLib
{
	class Processor;
}

namespace jucePluginEditorLib
{
	class PluginEditorState;

	// iOS-only application control panel. It overlays the editor window and
	// hosts application/system functions that are impractical to reach through
	// the hardware-style synth UI: firmware/ROM import, master volume, access
	// to the existing settings dialog, patch clipboard and about information.
	//
	// It is an additional layer on top of the untouched synth UI: opening and
	// closing it never touches the emulator or its state, and every action
	// forwards to an existing implementation - no settings, validation, audio
	// or MIDI logic is duplicated here.
	class IosControlPanel : public juce::Component,
		private juce::Timer,
		private juce::ListBoxModel
	{
	public:
		IosControlPanel(PluginEditorState& _state, pluginLib::Processor& _processor, std::function<void()> _onClose);
		~IosControlPanel() override;

		void resized() override;
		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _event) override;

		// Re-reads firmware status and output gain from their existing sources.
		void refresh();

		// Scrolls the existing panel content to one of its section headers
		// ("FIRMWARE", "PERFORMANCE", "DATA", "ABOUT", ...). Used by the iPad
		// quick bar; no second panel implementation exists.
		void scrollToSection(const juce::String& _header);

	private:
		juce::TextButton* addButton(const juce::String& _text, std::function<void()> _onClick);
		juce::Label* addLabel(const juce::String& _text, float _fontHeight, bool _bold, int _rowHeight);
		void addSectionHeader(const juce::String& _text);

		void togglePerformanceCapture();
		void exportDiagnostics();
		void applyFpsCap(int _hz);
		void onResetSettings();

		// --- user SysEx library ------------------------------------------
		// Polls the existing transfer status only while a transfer runs.
		void timerCallback() override;
		int getNumRows() override;
		void paintListBoxItem(int _row, juce::Graphics& _g, int _width, int _height, bool _selected) override;
		void selectedRowsChanged(int) override;
		juce::File selectedSysexFile() const;
		void refreshSysexLibrary();
		void updateSysexControls();
		void importSysex();
		void sendSelectedSysex();
		void shareSelectedSysex();
		void deleteSelectedSysex();
		void toggleSysexReceive();
		void saveReceivedSysex(const std::vector<uint8_t>& _message);

		// --- WAV recordings ----------------------------------------------
		juce::File getRecordingsFolder() const;
		void toggleRecording();
		void updateRecorderControls();
		void refreshRecordingsList();
		void shareSelectedRecording();
		juce::File selectedRecordingFile() const;
		bool isSelectedRecordingBusy() const;	// the file the recorder writes
		void togglePlayback();
		void renameSelectedRecording();
		void deleteSelectedRecording();
		static double readWavDurationSeconds(const juce::File& _file);

		// Closing destroys this component, so it must never happen from within
		// one of our own button callbacks - always deferred to the next
		// message-loop iteration.
		void requestClose(std::function<void()> _afterClose = {});

		void importRom();
		void openSettings();

		juce::Rectangle<int> safeArea() const;

		PluginEditorState& m_state;
		pluginLib::Processor& m_processor;
		const std::function<void()> m_onClose;

		juce::Viewport m_viewport;
		juce::Component m_content;

		struct Row
		{
			juce::Component* component = nullptr;
			int height = 0;
		};
		std::vector<Row> m_rows;
		std::vector<std::unique_ptr<juce::Component>> m_widgets;

		juce::Label* m_romStatus = nullptr;
		juce::Label* m_romDetails = nullptr;
		juce::Label* m_importStatus = nullptr;
		juce::TextButton* m_importButton = nullptr;
		juce::TextButton* m_reloadButton = nullptr;
		bool m_importBusy = false;
		juce::Slider* m_volume = nullptr;
		juce::Label* m_audioInfo = nullptr;
		juce::Label* m_perfStatus = nullptr;
		juce::Label* m_perfMetrics = nullptr;
		juce::Label* m_perfWarnings = nullptr;
		juce::TextButton* m_captureButton = nullptr;
		juce::TextButton* m_fps30 = nullptr;
		juce::TextButton* m_fps60 = nullptr;
		juce::TextButton* m_resetButton = nullptr;
		bool m_resetArmed = false;

		// SysEx library (filesystem is the source of truth; scanned on
		// demand - open/refresh/after operations - never per frame).
		struct SysexEntry
		{
			juce::File file;
			juce::String label;
		};
		std::vector<SysexEntry> m_sysexEntries;
		juce::ListBox* m_sysexList = nullptr;
		juce::Label* m_sysexStatus = nullptr;
		juce::TextButton* m_sysexImport = nullptr;
		juce::TextButton* m_sysexSend = nullptr;
		juce::TextButton* m_sysexShare = nullptr;
		juce::TextButton* m_sysexDelete = nullptr;
		juce::TextButton* m_sysexCancel = nullptr;
		juce::TextButton* m_sysexResume = nullptr;
		bool m_sysexDeleteArmed = false;
		bool m_sysexImportBusy = false;
		juce::TextButton* m_sysexReceive = nullptr;
		bool m_sysexReceiveActive = false;
		int m_sysexReceivedCount = 0;
		juce::String m_sysexReceiveInfo;

		// WAV recordings (list has its own model; the filesystem is the
		// source of truth, scanned on demand only).
		struct RecordingsModel final : juce::ListBoxModel
		{
			explicit RecordingsModel(IosControlPanel& _panel) : panel(_panel) {}
			int getNumRows() override;
			void paintListBoxItem(int _row, juce::Graphics& _g, int _width, int _height, bool _selected) override;
			void selectedRowsChanged(int) override;
			IosControlPanel& panel;
		};
		RecordingsModel m_recordingsModel{ *this };
		struct RecordingEntry
		{
			juce::File file;
			juce::String label;
		};
		std::vector<RecordingEntry> m_recordingEntries;
		juce::ListBox* m_recordingsList = nullptr;
		juce::Label* m_recorderStatus = nullptr;
		juce::Label* m_playbackStatus = nullptr;
		juce::TextButton* m_recordButton = nullptr;
		juce::TextButton* m_playButton = nullptr;
		juce::TextButton* m_recShareButton = nullptr;
		juce::TextButton* m_recRenameButton = nullptr;
		juce::TextButton* m_recDeleteButton = nullptr;
		bool m_recorderWasBusy = false;
		bool m_recDeleteArmed = false;
		bool m_playbackWasActive = false;
		std::unique_ptr<juce::AlertWindow> m_renameWindow;

		// Keeps the native iOS share sheet alive while it is presented.
		juce::ScopedMessageBox m_shareHandle;

		std::unique_ptr<juce::FileChooser> m_fileChooser;

		juce::Rectangle<int> m_panelBounds;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IosControlPanel)
	};
}

#endif // JUCE_IOS
