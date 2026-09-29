#include "mdIosDiagnosticsPanel.h"

#if JUCE_IOS

#include "mdPluginProcessor.h"

namespace mdJucePlugin
{
	namespace
	{
		constexpr int g_rowButton = 52;		// finger-sized touch targets
		constexpr int g_rowLabel = 44;
		constexpr int g_rowHeader = 34;
		constexpr int g_rowGap = 8;

		juce::String formatNanoseconds(const uint64_t _ns)
		{
			return juce::String(static_cast<double>(_ns) / 1000000.0, 2) + " ms";
		}

		juce::String formatAverageNanoseconds(const uint64_t _totalNs, const uint64_t _count)
		{
			if(_count == 0)
				return "n/a";
			return formatNanoseconds(_totalNs / _count);
		}
	}

	IosDiagnosticsPanel::IosDiagnosticsPanel(AudioPluginAudioProcessor& _processor, std::function<void()> _onClose)
		: m_processor(_processor)
		, m_onClose(std::move(_onClose))
	{
		setOpaque(false);

		m_viewport.setViewedComponent(&m_content, false);
		m_viewport.setScrollBarsShown(true, false);
		addAndMakeVisible(m_viewport);

		addLabel("Performance & Emulator Log", 22.0f, true, /*rowHeight*/40);
		addButton("< Back", [this]
		{
			// Destroys this component; defer past the callback that
			// triggered it, matching IosControlPanel's own close pattern.
			juce::MessageManager::callAsync([onClose = m_onClose] { if(onClose) onClose(); });
		});

		addSectionHeader("DEVICE");
		m_deviceLine = addLabel({}, 15.0f, false, 60);

		addSectionHeader("AUDIO");
		m_audioLine = addLabel({}, 15.0f, false, 60);
		m_callbackLine = addLabel({}, 15.0f, false, 60);

		addSectionHeader("SCHEDULER");
		m_schedulerLine = addLabel({}, 15.0f, false, 84);
		m_ucCyclesLine = addLabel({}, 15.0f, false, 30);
		m_dsp1CyclesLine = addLabel({}, 15.0f, false, 30);
		m_dsp2CyclesLine = addLabel({}, 15.0f, false, 30);
		m_schedulerTimeLine = addLabel({}, 15.0f, false, 30);

		addSectionHeader("UI / DEVICE LOCK");
		m_lockLine = addLabel({}, 15.0f, false, 60);

		addSectionHeader("LAST ERRORS / WARNINGS");
		m_errorsLine = addLabel({}, 14.0f, false, 70);

		addSectionHeader("LOG");
		m_logStatusLine = addLabel({}, 14.0f, false, 60);
		addButton("Start Log", [this] { m_processor.setPerformanceDiagnosticsEnabled(true); refreshStats(); });
		addButton("Stop Log", [this] { m_processor.setPerformanceDiagnosticsEnabled(false); refreshStats(); });
		addButton("Clear Log", [this]
		{
			// Resets every on-screen counter immediately. If a capture is
			// currently running, it keeps running with a fresh baseline;
			// this never touches the .jsonl file already written to disk.
			m_processor.getPlugin().getRealtimeInstrumentation().reset();
			refreshStats();
		});
		m_exportButton = addButton("Export Log...", [this] { exportLog(); });

		refreshStats();
		startTimer(300);
	}

	IosDiagnosticsPanel::~IosDiagnosticsPanel()
	{
		stopTimer();
	}

	juce::Label* IosDiagnosticsPanel::addLabel(const juce::String& _text, const float _fontHeight, const bool _bold)
	{
		auto label = std::make_unique<juce::Label>(juce::String(), _text);
		label->setFont(juce::Font(_fontHeight, _bold ? juce::Font::bold : juce::Font::plain));
		label->setJustificationType(juce::Justification::topLeft);
		label->setMinimumHorizontalScale(1.0f);
		label->setColour(juce::Label::textColourId, juce::Colours::white);
		m_content.addAndMakeVisible(*label);
		m_rows.push_back({ label.get(), g_rowLabel });
		auto* result = label.get();
		m_widgets.emplace_back(std::move(label));
		return result;
	}

	juce::Label* IosDiagnosticsPanel::addLabel(const juce::String& _text, const float _fontHeight, const bool _bold, const int _rowHeight)
	{
		auto* label = addLabel(_text, _fontHeight, _bold);
		m_rows.back().height = _rowHeight;
		return label;
	}

	juce::TextButton* IosDiagnosticsPanel::addButton(const juce::String& _text, std::function<void()> _onClick)
	{
		auto button = std::make_unique<juce::TextButton>(_text);
		button->onClick = std::move(_onClick);
		m_content.addAndMakeVisible(*button);
		m_rows.push_back({ button.get(), g_rowButton });
		auto* result = button.get();
		m_widgets.emplace_back(std::move(button));
		return result;
	}

	void IosDiagnosticsPanel::addSectionHeader(const juce::String& _text)
	{
		auto* label = addLabel(_text, 17.0f, true, g_rowHeader);
		label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
	}

	void IosDiagnosticsPanel::timerCallback()
	{
		// Non-blocking: proves (rather than merely hopes) that polling this
		// panel never waits for the realtime audio thread, even if it is
		// currently holding the device lock during a CPU overload.
		m_processor.getPlugin().tryLockDeviceForDiagnostics();
		refreshStats();
	}

	void IosDiagnosticsPanel::refreshStats()
	{
		using synthLib::RealtimeInstrumentationSnapshot;
		const RealtimeInstrumentationSnapshot snap = m_processor.getPlugin().getRealtimeInstrumentation().snapshot();

		const auto modelName = m_processor.getModel() == md::MachineModel::Monomachine ? "Monomachine" : "Machinedrum";
		m_deviceLine->setText(
			juce::String("Model: ") + modelName
			+ "\nDevice valid: " + (m_processor.isPluginValid() ? "yes" : "no"),
			juce::dontSendNotification);

		m_audioLine->setText(
			"Sample rate: " + (m_processor.getSampleRate() > 0
				? juce::String(m_processor.getSampleRate(), 0) + " Hz" : juce::String("n/a"))
			+ "\nBuffer size: " + juce::String(m_processor.getBlockSize()) + " frames",
			juce::dontSendNotification);

		m_callbackLine->setText(
			"Callback duration: avg " + formatAverageNanoseconds(snap.outerHostCallbackNanoseconds, snap.outerHostCallbackCount)
			+ ", max " + formatNanoseconds(snap.outerHostCallbackMaxNanoseconds)
			+ "\nDeadline overruns: " + juce::String(snap.outerHostCallbackOverrunCount)
			+ " (max " + formatNanoseconds(snap.outerHostCallbackMaxOverrunNanoseconds) + ")",
			juce::dontSendNotification);

		m_schedulerLine->setText(
			"Scheduler advances: " + juce::String(snap.schedulerAdvanceCount)
			+ "\nMachine frames: " + juce::String(snap.schedulerMachineFrames)
			+ "\nUC underruns: " + juce::String(snap.schedulerUcUnderrunCount),
			juce::dontSendNotification);

		m_ucCyclesLine->setText(
			"UC cycles: executed " + juce::String(snap.schedulerUcCyclesExecuted)
			+ ", max/advance " + juce::String(snap.schedulerUcCyclesMax),
			juce::dontSendNotification);
		m_dsp1CyclesLine->setText(
			"DSP1 cycles: executed " + juce::String(snap.schedulerDsp1CyclesExecuted)
			+ ", max/advance " + juce::String(snap.schedulerDsp1CyclesMax),
			juce::dontSendNotification);
		m_dsp2CyclesLine->setText(
			"DSP2 cycles: executed " + juce::String(snap.schedulerDsp2CyclesExecuted)
			+ ", max/advance " + juce::String(snap.schedulerDsp2CyclesMax),
			juce::dontSendNotification);
		m_schedulerTimeLine->setText(
			"Scheduler time: " + formatNanoseconds(snap.schedulerNanoseconds)
			+ ", max " + formatNanoseconds(snap.schedulerMaxNanoseconds),
			juce::dontSendNotification);

		m_lockLine->setText(
			"Try-lock: " + juce::String(snap.uiTryLockSuccessCount) + " ok / "
			+ juce::String(snap.uiTryLockFailureCount) + " failed (would have blocked)"
			+ "\nExisting UI lock wait (withDeviceLocked): " + juce::String(snap.controlLockWaitCount)
			+ " calls, max " + formatNanoseconds(snap.controlLockWaitMaxNanoseconds),
			juce::dontSendNotification);

		{
			const auto restoreError = m_processor.getProjectStateRestoreError();
			juce::String text = restoreError.empty()
				? juce::String("No project-restore errors recorded.")
				: "Project restore: " + juce::String::fromUTF8(restoreError.c_str());
			m_errorsLine->setText(text, juce::dontSendNotification);
		}

		{
			m_logStatusLine->setText(
				juce::String::fromUTF8(m_processor.performanceDiagnosticsStatus().c_str())
				+ (snap.enabled ? "" : "\n(Counters above are frozen at their last value until Start Log is pressed.)"),
				juce::dontSendNotification);
			if(m_exportButton)
				m_exportButton->setEnabled(m_processor.performanceDiagnosticsFile().existsAsFile());
		}

		resized();
	}

	void IosDiagnosticsPanel::exportLog()
	{
		const auto file = m_processor.performanceDiagnosticsFile();
		if(!file.existsAsFile())
			return;

		// Standard iOS share sheet (UIActivityViewController via JUCE's
		// ContentSharer) - the user can save the .jsonl to Files, AirDrop it,
		// mail it, etc. No custom export plumbing.
		m_shareSession = juce::ContentSharer::shareFilesScoped(
			juce::Array<juce::URL>{ juce::URL(file) },
			[](bool, const juce::String&) {},
			this);
	}

	void IosDiagnosticsPanel::resized()
	{
		const auto area = getLocalBounds();
		m_viewport.setBounds(area);

		const auto w = m_viewport.getWidth() - 12;	// room for the scrollbar
		int y = 0;
		for(const auto& row : m_rows)
		{
			row.component->setBounds(0, y, w, row.height);
			y += row.height + g_rowGap;
		}
		m_content.setSize(w, y);
	}
}

#endif // JUCE_IOS
