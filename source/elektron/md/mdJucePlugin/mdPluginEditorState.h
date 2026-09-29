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

		// Self-contained "Performance & Emulator Log" diagnostics screen,
		// iOS Standalone MM only (see .cpp for the exact gate); inert
		// (returns false/nullptr) for MD and every other product/platform.
		bool hasDiagnosticsPanel() const override;
		std::unique_ptr<juce::Component> createDiagnosticsPanel(std::function<void()> _onClose) override;
	};
}
