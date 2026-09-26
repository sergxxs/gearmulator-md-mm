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
	class IosControlPanel : public juce::Component
	{
	public:
		IosControlPanel(PluginEditorState& _state, pluginLib::Processor& _processor, std::function<void()> _onClose);
		~IosControlPanel() override;

		void resized() override;
		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _event) override;

		// Re-reads firmware status and output gain from their existing sources.
		void refresh();

	private:
		juce::TextButton* addButton(const juce::String& _text, std::function<void()> _onClick);
		juce::Label* addLabel(const juce::String& _text, float _fontHeight, bool _bold, int _rowHeight);
		void addSectionHeader(const juce::String& _text);

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
		juce::Label* m_importStatus = nullptr;
		juce::Slider* m_volume = nullptr;

		std::unique_ptr<juce::FileChooser> m_fileChooser;

		juce::Rectangle<int> m_panelBounds;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IosControlPanel)
	};
}

#endif // JUCE_IOS
