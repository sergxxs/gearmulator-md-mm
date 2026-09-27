#include "mdPluginEditorState.h"

#include "mdEditor.h"
#include "mdPluginProcessor.h"
#include "mdProductSkinPolicy.h"
#include "mdStandaloneRendererPolicy.h"

#include "mdProductSkins.h"
#include "mdRecordingsPlayer.h"

#include "mdLib/mdromloader.h"

#include "juce_events/juce_events.h"
#include "jucePluginEditorLib/rendererPreferenceKeys.h"
#include "juceRmlUi/rmlMenu.h"

namespace mdJucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor)
		: jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(),
			productSkins(_processor.getModel()))
	{
		#if JUCE_MAC
		constexpr bool isMacOS = true;
		#else
		constexpr bool isMacOS = false;
		#endif

		auto& config = _processor.getConfig();
		using namespace jucePluginEditorLib;
		if(shouldRemoveLegacyStandaloneSoftwareRenderer(isMacOS,
			juce::JUCEApplicationBase::isStandaloneApp(),
			_processor.getForceSoftwareRendererForSession().has_value(),
			config.getBoolValue(forceSoftwareRendererUserSelectedKey, false),
			config.containsKey(forceSoftwareRendererKey),
			config.getBoolValue(forceSoftwareRendererKey, false)))
		{
			config.removeValue(forceSoftwareRendererKey);
			config.saveIfNeeded();
		}

		const auto configuredSkin = readSkinFromConfig();
		if(configuredSkin.isValid() && isSkinCompatible(_processor.getModel(),
			configuredSkin.displayName, configuredSkin.filename))
		{
			loadSkin(configuredSkin);
			return;
		}

		const auto* const defaultSkin = defaultSkinName(_processor.getModel());

		for(const auto& skin : getIncludedSkins())
		{
			if(skin.displayName == defaultSkin)
			{
				loadSkin(skin);
				return;
			}
		}

		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new Editor(m_processor, _skin);
	}

	void PluginEditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		if(processor.getModel() == md::MachineModel::Machinedrum)
		{
			const bool available = processor.isRamRecordingModeAvailable();
			const auto mode = processor.getRamRecordingMode();
			juceRmlUi::Menu ramRecording;
			ramRecording.addEntry("Complete tails (recommended)", available,
				mode == md::RamRecordingMode::CompleteTail, [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::CompleteTail);
				});
			ramRecording.addEntry("Original finalization", available,
				mode == md::RamRecordingMode::Original, [&processor]
				{
					processor.setRamRecordingMode(md::RamRecordingMode::Original);
				});
			_menu.addSubMenu("RAM recording", std::move(ramRecording));
		}
		juceRmlUi::Menu diagnostics;
		diagnostics.addEntry(processor.performanceDiagnosticsActive()
			? "Stop performance capture" : "Start performance capture", [this]
			{
				auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
				processor.setPerformanceDiagnosticsEnabled(!processor.performanceDiagnosticsActive());
			});
		diagnostics.addEntry("Open logs folder", [folder = processor.performanceDiagnosticsFolder()]
			{
				// Open Finder/Explorer after the Rml menu has closed.
				juce::MessageManager::callAsync([folder]
					{
						if(folder.createDirectory().wasOk()) folder.revealToUser();
					});
			});
		diagnostics.addSeparator();
		diagnostics.addEntry(processor.performanceDiagnosticsStatus(), false, false, {});
		_menu.addSubMenu("Performance diagnostics", std::move(diagnostics));

		auto* const editor = dynamic_cast<Editor*>(getEditor());
		if(!editor)
			return;

		const bool active = editor->isUserSysexTransferActive();
		if(editor->canResumeUserSysexTransfer())
			_menu.addEntry("Resume SysEx Transfer - machine is ready", true, false,
				[editor] { editor->resumeUserSysexTransfer(); });
		const bool cancellable = editor->canCancelUserSysexTransfer();
		_menu.addEntry(editor->getUserSysexMenuText(),
			!active || cancellable, false, [this, editor, cancellable]
			{
				if(cancellable)
				{
					editor->cancelUserSysexTransfer();
					return;
				}
				// Menu actions run before the Rml menu closes. Defer the native picker
				// until that teardown has completed.
				const auto lifetime = editor->getLifetimeToken();
				juce::MessageManager::callAsync([lifetime, editor]
				{
					if(!lifetime.expired())
						editor->chooseUserSysexFile();
				});
			});
	}

	namespace
	{
		// The fingerprint checks accept exactly one known-good image per model,
		// so a passing file's firmware version is a fact, not a guess.
		const char* expectedFirmwareName(const md::MachineModel _model)
		{
			return _model == md::MachineModel::Monomachine
				? "Monomachine OS 1.32b"
				: "Machinedrum OS 1.63";
		}
	}

	jucePluginEditorLib::PluginEditorState::RomImportResult PluginEditorState::validateRomFile(const std::string& _path)
	{
		const auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		const auto model = processor.getModel();

		// Existing validation path only: md::Rom performs the exact-size load,
		// md::RomLoader::isRomForModel the firmware fingerprint check. This is
		// the same code the device's ROM discovery uses - nothing duplicated.
		const md::Rom rom(_path);
		if(!rom.isValid())
			return { false, "Not a valid firmware image: an exact 8 MB .bin file is required." };

		if(!md::RomLoader::isRomForModel(rom.data(), model))
			return { false, std::string("This file is not the supported ")
				+ expectedFirmwareName(model) + " firmware image." };

		return { true, std::string("Validated: ") + expectedFirmwareName(model) + "." };
	}

	std::string PluginEditorState::getRomStatusText()
	{
		const auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		const auto model = processor.getModel();

		if(m_processor.isPluginValid())
			return std::string("Firmware loaded: ") + expectedFirmwareName(model) + ".";

		const auto rom = md::RomLoader::findROM(model);
		if(rom.isValid())
			return std::string("Firmware found (") + rom.getFilename()
				+ ") but the device is not running.";

		return std::string("No firmware installed. Import the ")
			+ expectedFirmwareName(model) + " 8 MB .bin image.";
	}

	std::string PluginEditorState::getFirmwareDetailsText()
	{
		const auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		const auto model = processor.getModel();

		// Real state only: device validity from the processor, the discovered
		// image from the same md::RomLoader search the device boots with.
		std::string text = m_processor.isPluginValid()
			? std::string("Device: running - ") + expectedFirmwareName(model)
			: "Device: not running";

		const auto rom = md::RomLoader::findROM(model);
		if(rom.isValid())
		{
			auto name = rom.getFilename();
			if(const auto slash = name.find_last_of("/\\"); slash != std::string::npos)
				name = name.substr(slash + 1);
			text += "\nROM in folder: " + name;
			text += "\nSize: 8 MiB - valid ";
			text += expectedFirmwareName(model);
			text += " image";
			if(!m_processor.isPluginValid())
				text += "\nTap 'Reload device' to activate it.";
		}
		else
		{
			text += "\nROM in folder: none found";
		}

		return text;
	}

	namespace
	{
		Editor* mdEditorOf(jucePluginEditorLib::Editor* _editor)
		{
			return dynamic_cast<Editor*>(_editor);
		}
	}

	bool PluginEditorState::sendSysexFile(const juce::File& _file)
	{
		auto* const editor = mdEditorOf(getEditor());
		return editor && editor->sendUserSysexFromFile(_file);
	}

	std::string PluginEditorState::getSysexTransferStatusText()
	{
		auto* const editor = mdEditorOf(getEditor());
		return editor ? editor->getUserSysexMenuText() : std::string{};
	}

	bool PluginEditorState::isSysexTransferActive()
	{
		auto* const editor = mdEditorOf(getEditor());
		return editor && editor->isUserSysexTransferActive();
	}

	bool PluginEditorState::canCancelSysexTransfer()
	{
		auto* const editor = mdEditorOf(getEditor());
		return editor && editor->canCancelUserSysexTransfer();
	}

	void PluginEditorState::cancelSysexTransfer()
	{
		if(auto* const editor = mdEditorOf(getEditor()))
			editor->cancelUserSysexTransfer();
	}

	bool PluginEditorState::canResumeSysexTransfer()
	{
		auto* const editor = mdEditorOf(getEditor());
		return editor && editor->canResumeUserSysexTransfer();
	}

	void PluginEditorState::resumeSysexTransfer()
	{
		if(auto* const editor = mdEditorOf(getEditor()))
			editor->resumeUserSysexTransfer();
	}

	bool PluginEditorState::playRecording(const juce::File& _file, std::string& _error)
	{
		if(!juce::JUCEApplicationBase::isStandaloneApp())
		{
			_error = "Playback is only available in the standalone app.";
			return false;
		}
		juce::String error;
		if(RecordingsPlayer::instance().play(_file, error))
			return true;
		_error = error.toStdString();
		return false;
	}

	void PluginEditorState::stopRecordingPlayback()
	{
		if(juce::JUCEApplicationBase::isStandaloneApp())
			RecordingsPlayer::instance().stop();
	}

	jucePluginEditorLib::PluginEditorState::PlaybackStatus PluginEditorState::getRecordingPlaybackStatus()
	{
		PlaybackStatus status;
		if(!juce::JUCEApplicationBase::isStandaloneApp())
			return status;
		const auto s = RecordingsPlayer::instance().getStatus();
		status.available = true;
		status.playing = s.playing;
		status.positionSeconds = s.positionSeconds;
		status.lengthSeconds = s.lengthSeconds;
		return status;
	}

	bool PluginEditorState::isPerformanceCaptureActive()
	{
		return static_cast<AudioPluginAudioProcessor&>(m_processor).performanceDiagnosticsActive();
	}

	void PluginEditorState::setPerformanceCaptureActive(const bool _active)
	{
		static_cast<AudioPluginAudioProcessor&>(m_processor).setPerformanceDiagnosticsEnabled(_active);
	}

	std::string PluginEditorState::getPerformanceStatusText()
	{
		return static_cast<AudioPluginAudioProcessor&>(m_processor).performanceDiagnosticsStatus();
	}

	juce::File PluginEditorState::getDiagnosticsLogsFolder()
	{
		return static_cast<AudioPluginAudioProcessor&>(m_processor).performanceDiagnosticsFolder();
	}
}
