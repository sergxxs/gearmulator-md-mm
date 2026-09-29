#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_IOS

#include <functional>
#include <memory>
#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	// Self-contained "Performance & Emulator Log" diagnostics screen for iOS
	// Standalone MM (see PluginEditorState::createDiagnosticsPanel for the
	// exact gate - this is never constructed for MD or any other
	// product/platform).
	//
	// Goal: remove the need to find the hidden "Performance diagnostics"
	// long-press context menu or dig through the Files app for the .jsonl
	// report. Every value shown here comes from infrastructure that already
	// exists: synthLib::RealtimeInstrumentation::snapshot() (the
	// scheduler/DSP/callback/control-lock counters), juce::AudioProcessor's
	// own sample rate/block size, and AudioPluginAudioProcessor's existing
	// performance-diagnostics start/stop/status/file API - nothing here
	// duplicates that machinery.
	//
	// The single new probe is a non-blocking device-lock try-lock
	// (Plugin::tryLockDeviceForDiagnostics), polled from this panel's
	// juce::Timer on the message thread only - never from the realtime audio
	// callback, never a new thread - and it never waits even while the audio
	// thread holds the lock, so this screen stays live under CPU overload.
	class IosDiagnosticsPanel final : public juce::Component, private juce::Timer
	{
	public:
		IosDiagnosticsPanel(AudioPluginAudioProcessor& _processor, std::function<void()> _onClose);
		~IosDiagnosticsPanel() override;

		void resized() override;

	private:
		void timerCallback() override;
		void refreshStats();

		juce::Label* addLabel(const juce::String& _text, float _fontHeight, bool _bold);
		juce::Label* addLabel(const juce::String& _text, float _fontHeight, bool _bold, int _rowHeight);
		juce::TextButton* addButton(const juce::String& _text, std::function<void()> _onClick);
		void addSectionHeader(const juce::String& _text);

		void exportLog();

		AudioPluginAudioProcessor& m_processor;
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

		juce::Label* m_deviceLine = nullptr;
		juce::Label* m_audioLine = nullptr;
		juce::Label* m_callbackLine = nullptr;
		juce::Label* m_schedulerLine = nullptr;
		juce::Label* m_ucCyclesLine = nullptr;
		juce::Label* m_dsp1CyclesLine = nullptr;
		juce::Label* m_dsp2CyclesLine = nullptr;
		juce::Label* m_schedulerTimeLine = nullptr;
		juce::Label* m_lockLine = nullptr;
		juce::Label* m_errorsLine = nullptr;
		juce::Label* m_logStatusLine = nullptr;

		juce::TextButton* m_exportButton = nullptr;

		juce::ScopedMessageBox m_shareSession;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IosDiagnosticsPanel)
	};
}

#endif // JUCE_IOS
