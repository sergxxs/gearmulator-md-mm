#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "baseLib/event.h"
#include "editorWindowScaleRestore.h"

namespace jucePluginEditorLib
{
	class PluginEditorState;
#if JUCE_IOS
	class IosControlPanel;
#endif

	//==============================================================================
	class EditorWindow : public juce::AudioProcessorEditor, juce::Timer
	{
	public:
	    explicit EditorWindow (juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config);
	    ~EditorWindow() override;

		void paint(juce::Graphics& g) override
		{
#if JUCE_IOS
			// iOS aspect-fits the UI root inside the host-given bounds; fill the
			// letterbox bars around it. Desktop draws nothing here, as before.
			g.fillAll(juce::Colours::black);
#else
			juce::ignoreUnused(g);
#endif
		}

		void resized() override;

		int getControlParameterIndex(Component&) override;

		// A combined product may host this editor inside a larger editor. In that
		// case the parent owns sizing and the normal standalone parent-size repair
		// must not expand the entire composite to this panel's saved scale.
		void setEmbedded(bool _embedded);

	private:
		void setGuiScale(float _percent);
		void setUiRoot(juce::Component* _component);
#if JUCE_IOS
		// Aspect-fits and centers the UI root inside the current, safe-area
		// reduced viewport. iOS only; desktop sizing is driven by the GUI scale.
		void layoutUiRootFitViewport();

		// iOS-only application control panel (firmware import, volume,
		// settings access, about) plus the button that opens it.
		void openIosControlPanel();
		void closeIosControlPanel();
#endif

		void timerCallback() override;
		void fixParentWindowSize() const;

		PluginEditorState& m_state;
		juce::PropertiesFile& m_config;
		baseLib::EventListener<juce::Component*> m_skinLoadedListener;
		baseLib::EventListener<int> m_guiScaleListener;

	    juce::ComponentBoundsConstrainer m_sizeConstrainer;
		EditorWindowScaleRestore m_scaleRestore;

#if JUCE_IOS
		std::unique_ptr<juce::TextButton> m_iosMenuButton;
		std::unique_ptr<IosControlPanel> m_iosControlPanel;
#endif

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
	};
}
