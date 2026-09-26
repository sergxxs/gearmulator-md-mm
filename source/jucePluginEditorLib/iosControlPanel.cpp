#include "iosControlPanel.h"

#if JUCE_IOS

#include "pluginEditor.h"
#include "pluginEditorState.h"

#include "jucePluginLib/processor.h"

namespace jucePluginEditorLib
{
	namespace
	{
		constexpr int g_rowButton = 52;		// finger-sized touch targets
		constexpr int g_rowLabel = 44;
		constexpr int g_rowHeader = 34;
		constexpr int g_rowGap = 8;
		constexpr int g_maxPanelWidth = 520;
	}

	IosControlPanel::IosControlPanel(PluginEditorState& _state, pluginLib::Processor& _processor, std::function<void()> _onClose)
		: m_state(_state)
		, m_processor(_processor)
		, m_onClose(std::move(_onClose))
	{
		setOpaque(false);

		m_viewport.setViewedComponent(&m_content, false);
		m_viewport.setScrollBarsShown(true, false);
		addAndMakeVisible(m_viewport);

		// MAIN
		addLabel("Control Panel", 26.0f, true, 40);
		addButton("Back to " + juce::String::fromUTF8(m_processor.getProperties().name.c_str()),
			[this] { requestClose(); });

		// FIRMWARE
		addSectionHeader("FIRMWARE");
		m_romStatus = addLabel({}, 16.0f, false, g_rowLabel);
		addButton("Import firmware ROM (.bin)...", [this] { importRom(); });
		m_importStatus = addLabel({}, 15.0f, false, g_rowLabel);
		{
			// Display where imported firmware lives; the same folder is visible
			// in the Files app through UIFileSharingEnabled.
			auto path = juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str());
			const auto docs = path.indexOf("/Documents/");
			if(docs >= 0)
				path = "Documents" + path.substring(docs + 10);
			addLabel("ROM folder (also reachable via the Files app):\n" + path, 13.0f, false, g_rowLabel);
		}

		// AUDIO
		addSectionHeader("AUDIO");
		addLabel("Master Volume", 16.0f, false, 28);
		{
			auto slider = std::make_unique<juce::Slider>(
				juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
			slider->setRange(0.0, 1.0, 0.0);
			// Exactly the same output-gain path the synth UI's master volume
			// encoder uses - there is only one volume system.
			slider->onValueChange = [this]
			{
				m_processor.setOutputGain(static_cast<float>(m_volume->getValue()));
			};
			m_volume = slider.get();
			m_content.addAndMakeVisible(*slider);
			m_rows.push_back({ slider.get(), 44 });
			m_widgets.emplace_back(std::move(slider));
		}
		addButton("Audio Settings...", [this] { openSettings(); });

		// MIDI
		addSectionHeader("MIDI");
		addButton("MIDI Settings...", [this] { openSettings(); });

		// APPEARANCE
		addSectionHeader("APPEARANCE");
		addButton("Skin & GUI Settings...", [this] { openSettings(); });

		// DATA
		addSectionHeader("DATA");
		addButton("Copy Patch to Clipboard", [this]
		{
			if(const auto* editor = m_state.getEditor())
				editor->copyCurrentPatchToClipboard();
		});
		addButton("Paste Patch from Clipboard", [this]
		{
			if(const auto* editor = m_state.getEditor())
				(void)editor->replaceCurrentPatchFromClipboard();
		});
		addLabel("SysEx import/export and further actions:\nlong-press the synth panel for the context menu.", 13.0f, false, g_rowLabel);

		// INFO
		addSectionHeader("ABOUT");
		{
			const auto& props = m_processor.getProperties();
			auto text = juce::String::fromUTF8(props.name.c_str())
				+ " - " + juce::String::fromUTF8(props.vendor.c_str());
			// Standalone-only, safe: reports the version the app was built with.
			if(const auto* app = juce::JUCEApplicationBase::getInstance())
			{
				const auto version = app->getApplicationVersion();
				if(version.isNotEmpty())
					text += "\nVersion " + version;
			}
			addLabel(text, 15.0f, false, g_rowLabel);
		}

		refresh();
	}

	IosControlPanel::~IosControlPanel() = default;

	juce::TextButton* IosControlPanel::addButton(const juce::String& _text, std::function<void()> _onClick)
	{
		auto button = std::make_unique<juce::TextButton>(_text);
		button->onClick = std::move(_onClick);
		m_content.addAndMakeVisible(*button);
		m_rows.push_back({ button.get(), g_rowButton });
		auto* result = button.get();
		m_widgets.emplace_back(std::move(button));
		return result;
	}

	juce::Label* IosControlPanel::addLabel(const juce::String& _text, const float _fontHeight, const bool _bold, const int _rowHeight)
	{
		auto label = std::make_unique<juce::Label>(juce::String(), _text);
		label->setFont(juce::Font(_fontHeight, _bold ? juce::Font::bold : juce::Font::plain));
		label->setJustificationType(juce::Justification::centredLeft);
		label->setColour(juce::Label::textColourId, juce::Colours::white);
		m_content.addAndMakeVisible(*label);
		m_rows.push_back({ label.get(), _rowHeight });
		auto* result = label.get();
		m_widgets.emplace_back(std::move(label));
		return result;
	}

	void IosControlPanel::addSectionHeader(const juce::String& _text)
	{
		auto* label = addLabel(_text, 18.0f, true, g_rowHeader);
		label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
	}

	void IosControlPanel::requestClose(std::function<void()> _afterClose)
	{
		// The close callback destroys this component; never run it from inside
		// one of our own child callbacks - defer to the next message-loop turn.
		juce::MessageManager::callAsync(
			[onClose = m_onClose, afterClose = std::move(_afterClose)]
			{
				if(onClose)
					onClose();
				if(afterClose)
					afterClose();
			});
	}

	void IosControlPanel::openSettings()
	{
		// Navigate to the existing RmlUi settings dialog (audio/MIDI/skin/GUI
		// pages); the control panel closes so the dialog is fully visible.
		auto* const state = &m_state;	// outlives this panel
		requestClose([state]
		{
			if(auto* editor = state->getEditor())
				editor->showSettings(true);
		});
	}

	void IosControlPanel::importRom()
	{
		// Same native-picker pattern the project already uses for storage
		// images and SysEx files: juce::FileChooser, which is the native
		// UIDocumentPickerViewController on iOS. Any file may be picked - the
		// product's existing ROM validation decides whether it is accepted.
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Select firmware image (.bin)", juce::File(), "*", true);

		const auto safeThis = juce::Component::SafePointer<IosControlPanel>(this);

		m_fileChooser->launchAsync(
			juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[safeThis](const juce::FileChooser& _chooser)
			{
				if(safeThis == nullptr)
					return;
				auto& panel = *safeThis;

				const auto file = _chooser.getResult();
				if(file == juce::File())
				{
					panel.m_importStatus->setText("Import cancelled.", juce::dontSendNotification);
					return;
				}

				// Validation first, through the product's existing ROM loader
				// checks (exact size + firmware fingerprint). Invalid files
				// are rejected and never copied into the app storage.
				const auto result = panel.m_state.validateRomFile(file.getFullPathName().toStdString());
				if(!result.valid)
				{
					panel.m_importStatus->setText(juce::String::fromUTF8(result.message.c_str()), juce::dontSendNotification);
					return;
				}

				// JUCE holds security-scoped access on the picked URL for this
				// callback; copy the file into the existing ROM search folder
				// (app-sandbox Documents) so it persists across app restarts.
				// The external URL itself is not retained.
				const juce::File destDir(juce::String::fromUTF8(panel.m_processor.getPublicRomFolder().c_str()));
				(void)destDir.createDirectory();
				const auto dest = destDir.getChildFile(file.getFileName());
				if(!file.copyFileTo(dest))
				{
					panel.m_importStatus->setText("Failed to copy the firmware into the app storage.", juce::dontSendNotification);
					return;
				}

				// Existing hot-reload mechanism: recreates the device, which
				// picks up the new ROM through the unchanged RomLoader search
				// paths - no app restart needed on success.
				const bool rebooted = panel.m_processor.rebootDevice();

				panel.m_importStatus->setText(
					juce::String::fromUTF8(result.message.c_str())
						+ (rebooted ? " Firmware is now active."
									: " Saved; restart the app to activate it."),
					juce::dontSendNotification);
				panel.refresh();
			});
	}

	void IosControlPanel::refresh()
	{
		if(m_romStatus)
			m_romStatus->setText(juce::String::fromUTF8(m_state.getRomStatusText().c_str()), juce::dontSendNotification);
		if(m_volume)
			m_volume->setValue(m_processor.getOutputGain(), juce::dontSendNotification);
	}

	juce::Rectangle<int> IosControlPanel::safeArea() const
	{
		// Same safe-area source Prompt 2's viewport fit uses; no new transform.
		auto area = getLocalBounds();
		if(const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()))
			display->safeAreaInsets.subtractFrom(area);
		return area;
	}

	void IosControlPanel::resized()
	{
		const auto area = safeArea();
		if(area.isEmpty())
			return;

		const auto panelW = juce::jmin(area.getWidth() - 24, g_maxPanelWidth);
		m_panelBounds = juce::Rectangle<int>(panelW, area.getHeight() - 24).withCentre(area.getCentre());

		m_viewport.setBounds(m_panelBounds.reduced(12));

		const auto w = m_viewport.getWidth() - 12;	// room for the scrollbar
		int y = 0;
		for(const auto& row : m_rows)
		{
			row.component->setBounds(0, y, w, row.height);
			y += row.height + g_rowGap;
		}
		m_content.setSize(w, y);
	}

	void IosControlPanel::paint(juce::Graphics& _g)
	{
		_g.fillAll(juce::Colour(0xc0000000));	// dim the synth UI behind us
		_g.setColour(juce::Colour(0xff202126));
		_g.fillRoundedRectangle(m_panelBounds.toFloat(), 12.0f);
		_g.setColour(juce::Colour(0xff3a3b40));
		_g.drawRoundedRectangle(m_panelBounds.toFloat().reduced(0.5f), 12.0f, 1.0f);
	}

	void IosControlPanel::mouseDown(const juce::MouseEvent& _event)
	{
		// Tapping outside the panel returns to the synth UI.
		if(!m_panelBounds.contains(_event.getPosition()))
			requestClose();
	}
}

#endif // JUCE_IOS
