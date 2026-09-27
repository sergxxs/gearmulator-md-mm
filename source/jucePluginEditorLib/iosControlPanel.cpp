#include "iosControlPanel.h"

#if JUCE_IOS

#include "pluginEditor.h"
#include "pluginEditorState.h"

#include "jucePluginLib/processor.h"

#include <algorithm>

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

		// FIRMWARE - full manager over the existing loader/processor state:
		// status, discovered image details, import (validate-before-copy) and
		// a manual reload that re-scans the ROM folder via the existing
		// Processor::rebootDevice() - useful after copying a .bin in with the
		// Files app. Activation needs no separate step: the loader picks up
		// the valid image on every device (re)boot.
		addSectionHeader("FIRMWARE");
		m_romStatus = addLabel({}, 16.0f, false, 28);
		m_romDetails = addLabel({}, 14.0f, false, 78);
		m_importButton = addButton("Import firmware ROM (.bin)...", [this] { importRom(); });
		m_reloadButton = addButton("Reload device (re-scan ROM folder)", [this]
		{
			// Existing hot-reload mechanism; runs on the message thread and
			// swaps the device under the plugin lock exactly like an import.
			const bool ok = m_processor.rebootDevice();
			if(m_importStatus)
				m_importStatus->setText(ok ? "Device restarted."
					: "Device restart failed - see the message box.", juce::dontSendNotification);
			refresh();
		});
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
		m_audioInfo = addLabel({}, 14.0f, false, 62);
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

		// PERFORMANCE - thin UI over the existing realtime instrumentation /
		// PerformanceReport system; no second diagnostics system, no polling
		// (values update on open and on button presses only).
		addSectionHeader("PERFORMANCE");
		m_perfStatus = addLabel({}, 14.0f, false, g_rowLabel);
		m_captureButton = addButton({}, [this] { togglePerformanceCapture(); });
		m_perfMetrics = addLabel({}, 13.0f, false, 96);
		m_perfWarnings = addLabel({}, 13.0f, true, 56);
		m_perfWarnings->setColour(juce::Label::textColourId, juce::Colour(0xffff6b5e));
		addButton("Update metrics", [this] { refresh(); });
		addButton("Export diagnostics...", [this] { exportDiagnostics(); });
		addLabel("Reports are also reachable via the Files app (logs folder).", 12.0f, false, 24);
		m_fps30 = addButton({}, [this] { applyFpsCap(30); });
		m_fps60 = addButton({}, [this] { applyFpsCap(60); });
		addLabel("The UI FPS cap applies after an app restart.", 12.0f, false, 24);

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
		// --- user SysEx library over the existing transfer machinery -------
		addLabel("SysEx Library", 16.0f, true, 26);
		{
			auto list = std::make_unique<juce::ListBox>(juce::String(), this);
			list->setRowHeight(30);
			list->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff17181c));
			list->setColour(juce::ListBox::outlineColourId, juce::Colour(0xff3a3b40));
			list->setOutlineThickness(1);
			m_sysexList = list.get();
			m_content.addAndMakeVisible(*list);
			m_rows.push_back({ list.get(), 168 });
			m_widgets.emplace_back(std::move(list));
		}
		m_sysexImport = addButton("Import SysEx (.syx)...", [this] { importSysex(); });
		m_sysexReceive = addButton("Receive SysEx...", [this] { toggleSysexReceive(); });
		m_sysexSend = addButton("Send to Monomachine", [this] { sendSelectedSysex(); });
		m_sysexShare = addButton("Share...", [this] { shareSelectedSysex(); });
		m_sysexDelete = addButton("Delete...", [this] { deleteSelectedSysex(); });
		m_sysexCancel = addButton("Cancel transfer", [this]
		{
			m_state.cancelSysexTransfer();
			updateSysexControls();
		});
		m_sysexResume = addButton("Resume transfer - machine is ready", [this]
		{
			m_state.resumeSysexTransfer();
			startTimer(500);
			updateSysexControls();
		});
		m_sysexStatus = addLabel({}, 13.0f, false, g_rowLabel);
		addButton("Refresh library", [this]
		{
			refreshSysexLibrary();
			updateSysexControls();
		});
		addLabel("Files app: the 'sysex' folder next to 'roms'. Copy .syx files in, then Refresh.", 12.0f, false, 24);

		// RECORDINGS - raw capture of the final MM stereo output (see
		// pluginLib::AudioRecorder). All file work happens on the recorder's
		// worker thread; this UI only starts/stops and shows real state.
		addSectionHeader("RECORDINGS");
		m_recordButton = addButton("Record", [this] { toggleRecording(); });
		m_recorderStatus = addLabel({}, 14.0f, false, g_rowLabel);
		{
			auto list = std::make_unique<juce::ListBox>(juce::String(), &m_recordingsModel);
			list->setRowHeight(30);
			list->setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff17181c));
			list->setColour(juce::ListBox::outlineColourId, juce::Colour(0xff3a3b40));
			list->setOutlineThickness(1);
			m_recordingsList = list.get();
			m_content.addAndMakeVisible(*list);
			m_rows.push_back({ list.get(), 138 });
			m_widgets.emplace_back(std::move(list));
		}
		m_playButton = addButton("Play", [this] { togglePlayback(); });
		m_playbackStatus = addLabel({}, 13.0f, false, 26);
		m_recShareButton = addButton("Share recording...", [this] { shareSelectedRecording(); });
		m_recRenameButton = addButton("Rename...", [this] { renameSelectedRecording(); });
		m_recDeleteButton = addButton("Delete recording...", [this] { deleteSelectedRecording(); });
		addButton("Refresh recordings", [this]
		{
			refreshRecordingsList();
			updateRecorderControls();
		});
		addLabel("Files app: the 'recordings' folder.", 12.0f, false, 24);

		// GENERAL
		addSectionHeader("GENERAL");
		addLabel("Session autosave: on - the state is saved when the app enters the background.", 13.0f, false, g_rowLabel);
		m_resetButton = addButton("Reset Settings...", [this] { onResetSettings(); });
		addLabel("Resets application settings only; ROMs, logs and session data are kept.", 12.0f, false, 24);

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

	IosControlPanel::~IosControlPanel()
	{
		stopTimer();
		// Receive is a UI-armed mode: closing the panel disarms the capture
		// so the processor-side buffer cannot accumulate unattended.
		if(m_sysexReceiveActive)
			m_processor.setSysexCaptureEnabled(false);
	}

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
		m_sectionAnchors.emplace_back(_text, label);
	}

	void IosControlPanel::scrollToSection(const juce::String& _header)
	{
		for(const auto& anchor : m_sectionAnchors)
		{
			if(!anchor.first.equalsIgnoreCase(_header))
				continue;
			m_viewport.setViewPosition(0, juce::jmax(0, anchor.second->getY() - 4));
			return;
		}
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
		// One import at a time; the guard is released on every exit path of
		// the completion (cancel, rejection, copy failure, success).
		if(m_importBusy)
			return;
		m_importBusy = true;
		if(m_importButton)
			m_importButton->setEnabled(false);
		if(m_importStatus)
			m_importStatus->setText("Choosing firmware file...", juce::dontSendNotification);

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

				const auto finishImport = [&panel]
				{
					panel.m_importBusy = false;
					if(panel.m_importButton)
						panel.m_importButton->setEnabled(true);
				};

				const auto file = _chooser.getResult();
				if(file == juce::File())
				{
					finishImport();
					panel.m_importStatus->setText("Import cancelled.", juce::dontSendNotification);
					return;
				}

				// Validation first, through the product's existing ROM loader
				// checks (exact size + firmware fingerprint). Invalid files
				// are rejected and never copied into the app storage.
				const auto result = panel.m_state.validateRomFile(file.getFullPathName().toStdString());
				if(!result.valid)
				{
					finishImport();
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
					finishImport();
					panel.m_importStatus->setText("Failed to copy the firmware into the app storage.", juce::dontSendNotification);
					return;
				}

				// Existing hot-reload mechanism: recreates the device, which
				// picks up the new ROM through the unchanged RomLoader search
				// paths - no app restart needed on success.
				const bool rebooted = panel.m_processor.rebootDevice();

				finishImport();
				panel.m_importStatus->setText(
					juce::String::fromUTF8(result.message.c_str())
						+ (rebooted ? " Firmware is now active."
									: " Saved, but activation failed - tap 'Reload device' to retry."),
					juce::dontSendNotification);
				panel.refresh();
			});
	}

	int IosControlPanel::getNumRows()
	{
		return static_cast<int>(m_sysexEntries.size());
	}

	void IosControlPanel::paintListBoxItem(const int _row, juce::Graphics& _g, const int _width, const int _height, const bool _selected)
	{
		if(_row < 0 || _row >= getNumRows())
			return;
		if(_selected)
			_g.fillAll(juce::Colour(0xff2f3a55));
		_g.setColour(juce::Colours::white);
		_g.setFont(14.0f);
		_g.drawText(m_sysexEntries[static_cast<size_t>(_row)].label,
			8, 0, _width - 16, _height, juce::Justification::centredLeft, true);
	}

	void IosControlPanel::selectedRowsChanged(int)
	{
		m_sysexDeleteArmed = false;
		updateSysexControls();
	}

	juce::File IosControlPanel::selectedSysexFile() const
	{
		if(!m_sysexList)
			return {};
		const auto row = m_sysexList->getSelectedRow();
		if(row < 0 || row >= static_cast<int>(m_sysexEntries.size()))
			return {};
		return m_sysexEntries[static_cast<size_t>(row)].file;
	}

	void IosControlPanel::refreshSysexLibrary()
	{
		// Directory scan on demand only (open/refresh/after an operation);
		// the filesystem is the source of truth, newest files first.
		m_sysexEntries.clear();

		const auto folder = m_state.getSysexLibraryFolder();
		(void)folder.createDirectory();

		auto files = folder.findChildFiles(juce::File::findFiles, false, "*.syx;*.SYX");
		std::sort(files.begin(), files.end(),
			[](const juce::File& _a, const juce::File& _b)
			{
				return _a.getLastModificationTime() > _b.getLastModificationTime();
			});

		for(const auto& file : files)
		{
			SysexEntry entry;
			entry.file = file;
			// Real metadata only: name, size, modification date.
			entry.label = file.getFileName()
				+ "   -   " + juce::File::descriptionOfSizeInBytes(file.getSize())
				+ "   -   " + file.getLastModificationTime().formatted("%Y-%m-%d %H:%M");
			m_sysexEntries.emplace_back(std::move(entry));
		}

		if(m_sysexList)
			m_sysexList->updateContent();
		m_sysexDeleteArmed = false;
	}

	void IosControlPanel::updateSysexControls()
	{
		const bool active = m_state.isSysexTransferActive();
		const bool haveSelection = selectedSysexFile() != juce::File();

		if(m_sysexStatus)
		{
			juce::String status;
			if(m_sysexReceiveActive)
			{
				// Indeterminate by design: the total size of an incoming dump
				// is unknown until it arrived. Only real numbers are shown.
				status = "Receiving SysEx - waiting for data...";
				if(m_sysexReceivedCount > 0)
					status = "Receiving SysEx - " + juce::String(m_sysexReceivedCount)
						+ " file(s) saved, last: " + m_sysexReceiveInfo;
				if(const auto drops = m_processor.getCapturedSysexDropCount())
					status << "  (" << juce::String(static_cast<int>(drops)) << " oversized message(s) skipped)";
			}
			else
			{
				status = juce::String::fromUTF8(m_state.getSysexTransferStatusText().c_str());
				if(status.isEmpty() && m_sysexReceivedCount > 0)
					status = juce::String(m_sysexReceivedCount) + " file(s) received, last: " + m_sysexReceiveInfo;
				if(status.isEmpty())
					status = m_sysexEntries.empty()
						? "Library is empty - import a .syx file or copy one in via the Files app."
						: juce::String(m_sysexEntries.size()) + " file(s) in the library.";
			}
			m_sysexStatus->setText(status, juce::dontSendNotification);
		}

		if(m_sysexReceive)
			m_sysexReceive->setButtonText(m_sysexReceiveActive
				? "Stop receiving" : "Receive SysEx...");

		if(m_sysexImport)
			m_sysexImport->setEnabled(!m_sysexImportBusy && !active);
		if(m_sysexSend)
			m_sysexSend->setEnabled(haveSelection && !active);
		if(m_sysexShare)
			m_sysexShare->setEnabled(haveSelection);
		if(m_sysexDelete)
		{
			m_sysexDelete->setEnabled(haveSelection);
			if(!m_sysexDeleteArmed)
				m_sysexDelete->setButtonText("Delete...");
		}
		if(m_sysexCancel)
			m_sysexCancel->setVisible(m_state.canCancelSysexTransfer());
		if(m_sysexResume)
			m_sysexResume->setVisible(m_state.canResumeSysexTransfer());
	}

	void IosControlPanel::timerCallback()
	{
		// Runs only while a send transfer or an armed receive is active;
		// message thread only. The receive drain consumes complete messages
		// that the existing backends assembled - nothing partial exists here.
		if(m_sysexReceiveActive)
		{
			std::vector<uint8_t> message;
			while(m_processor.fetchCapturedSysex(message))
				saveReceivedSysex(message);
		}

		updateSysexControls();
		updateRecorderControls();
		if(!m_state.isSysexTransferActive() && !m_state.canResumeSysexTransfer()
			&& !m_sysexReceiveActive
			&& !m_processor.getAudioRecorder().isRecordingOrBusy()
			&& !m_state.getRecordingPlaybackStatus().playing)
			stopTimer();
	}

	void IosControlPanel::toggleSysexReceive()
	{
		if(!m_sysexReceiveActive)
		{
			m_sysexReceiveActive = true;
			m_sysexReceivedCount = 0;
			m_sysexReceiveInfo.clear();
			m_processor.setSysexCaptureEnabled(true);
			startTimer(500);
		}
		else
		{
			m_sysexReceiveActive = false;
			// Drain what already arrived, then disable (which clears state).
			std::vector<uint8_t> message;
			while(m_processor.fetchCapturedSysex(message))
				saveReceivedSysex(message);
			m_processor.setSysexCaptureEnabled(false);
		}
		updateSysexControls();
	}

	void IosControlPanel::saveReceivedSysex(const std::vector<uint8_t>& _message)
	{
		// Complete-message framing check (assembly happened upstream).
		if(_message.size() < 2 || _message.front() != 0xf0 || _message.back() != 0xf7)
			return;

		const auto folder = m_state.getSysexLibraryFolder();
		(void)folder.createDirectory();

		// Atomic save: temp file -> full write -> close -> rename. The final
		// .syx only ever appears complete; a failed write leaves nothing.
		const auto temp = folder.getChildFile(".receive.tmp");
		temp.deleteFile();
		{
			juce::FileOutputStream stream(temp);
			if(!stream.openedOk()
				|| !stream.write(_message.data(), _message.size()))
			{
				temp.deleteFile();
				m_sysexReceiveInfo = "Failed to write the received file.";
				return;
			}
			stream.flush();
		}

		auto target = folder.getChildFile("received-"
			+ juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + ".syx");
		if(target.existsAsFile())
			target = target.getNonexistentSibling();

		if(!temp.moveFileTo(target))
		{
			temp.deleteFile();
			m_sysexReceiveInfo = "Failed to store the received file.";
			return;
		}

		++m_sysexReceivedCount;
		m_sysexReceiveInfo = target.getFileName() + " ("
			+ juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(_message.size())) + ")";
		refreshSysexLibrary();
	}

	void IosControlPanel::importSysex()
	{
		if(m_sysexImportBusy)
			return;
		m_sysexImportBusy = true;
		updateSysexControls();

		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Select a SysEx file (.syx)", juce::File(), "*", true);

		const auto safeThis = juce::Component::SafePointer<IosControlPanel>(this);
		m_fileChooser->launchAsync(
			juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[safeThis](const juce::FileChooser& _chooser)
			{
				if(safeThis == nullptr)
					return;
				auto& panel = *safeThis;
				panel.m_sysexImportBusy = false;

				const auto setStatus = [&panel](const juce::String& _text)
				{
					if(panel.m_sysexStatus)
						panel.m_sysexStatus->setText(_text, juce::dontSendNotification);
				};

				const auto file = _chooser.getResult();
				if(file == juce::File())
				{
					panel.updateSysexControls();
					setStatus("Import cancelled.");
					return;
				}

				// Validation before any copy: extension, readability and
				// SysEx framing (F0 ... F7). Deep, model-aware validation is
				// performed by the existing transfer when the file is sent.
				if(!file.hasFileExtension("syx"))
				{
					panel.updateSysexControls();
					setStatus("Not a .syx file.");
					return;
				}

				juce::FileInputStream stream(file);
				const auto totalSize = stream.getTotalLength();
				uint8_t first = 0, last = 0;
				bool framed = stream.openedOk() && totalSize > 2
					&& stream.read(&first, 1) == 1
					&& stream.setPosition(totalSize - 1)
					&& stream.read(&last, 1) == 1
					&& first == 0xf0 && last == 0xf7;
				if(!framed)
				{
					panel.updateSysexControls();
					setStatus("Not a valid SysEx file (missing F0/F7 framing).");
					return;
				}

				const auto folder = panel.m_state.getSysexLibraryFolder();
				(void)folder.createDirectory();

				// Never overwrite an existing preset: collisions get a safe
				// unique name instead ("keep both" behavior).
				auto dest = folder.getChildFile(file.getFileName());
				if(dest.existsAsFile())
					dest = dest.getNonexistentSibling();

				if(!file.copyFileTo(dest))
				{
					panel.updateSysexControls();
					setStatus("Failed to copy the file into the library.");
					return;
				}

				panel.refreshSysexLibrary();
				panel.updateSysexControls();
				setStatus("Imported: " + dest.getFileName());
			});
	}

	void IosControlPanel::sendSelectedSysex()
	{
		const auto file = selectedSysexFile();
		if(file == juce::File() || m_state.isSysexTransferActive())
			return;

		if(!file.existsAsFile())
		{
			refreshSysexLibrary();
			updateSysexControls();
			return;
		}

		// The existing transfer machinery reads/validates the file outside
		// the audio callback and reports progress through its status text.
		if(m_state.sendSysexFile(file))
			startTimer(500);
		updateSysexControls();
	}

	void IosControlPanel::shareSelectedSysex()
	{
		const auto file = selectedSysexFile();
		if(file == juce::File())
			return;
		juce::Array<juce::URL> urls;
		urls.add(juce::URL(file));
		m_shareHandle = juce::ContentSharer::shareFilesScoped(urls,
			[](bool, const juce::String&) {}, this);
	}

	void IosControlPanel::deleteSelectedSysex()
	{
		const auto file = selectedSysexFile();
		if(file == juce::File())
			return;

		// Explicit two-tap confirmation; selection change disarms.
		if(!m_sysexDeleteArmed)
		{
			m_sysexDeleteArmed = true;
			if(m_sysexDelete)
				m_sysexDelete->setButtonText("Tap again to delete " + file.getFileName());
			return;
		}

		m_sysexDeleteArmed = false;
		const auto ok = file.deleteFile();
		refreshSysexLibrary();
		updateSysexControls();
		if(m_sysexStatus && !ok)
			m_sysexStatus->setText("Failed to delete " + file.getFileName(), juce::dontSendNotification);
	}

	int IosControlPanel::RecordingsModel::getNumRows()
	{
		return static_cast<int>(panel.m_recordingEntries.size());
	}

	void IosControlPanel::RecordingsModel::paintListBoxItem(const int _row, juce::Graphics& _g, const int _width, const int _height, const bool _selected)
	{
		if(_row < 0 || _row >= getNumRows())
			return;
		if(_selected)
			_g.fillAll(juce::Colour(0xff2f3a55));
		_g.setColour(juce::Colours::white);
		_g.setFont(14.0f);
		_g.drawText(panel.m_recordingEntries[static_cast<size_t>(_row)].label,
			8, 0, _width - 16, _height, juce::Justification::centredLeft, true);
	}

	void IosControlPanel::RecordingsModel::selectedRowsChanged(int)
	{
		panel.m_recDeleteArmed = false;
		panel.updateRecorderControls();
	}

	juce::File IosControlPanel::selectedRecordingFile() const
	{
		if(!m_recordingsList)
			return {};
		const auto row = m_recordingsList->getSelectedRow();
		if(row < 0 || row >= static_cast<int>(m_recordingEntries.size()))
			return {};
		return m_recordingEntries[static_cast<size_t>(row)].file;
	}

	bool IosControlPanel::isSelectedRecordingBusy() const
	{
		// The file the recorder is still writing must not be played, renamed,
		// shared or deleted until finalization completed.
		auto& recorder = m_processor.getAudioRecorder();
		return recorder.isRecordingOrBusy()
			&& selectedRecordingFile() == recorder.getActiveFile();
	}

	void IosControlPanel::togglePlayback()
	{
		const auto status = m_state.getRecordingPlaybackStatus();
		if(status.playing)
		{
			m_state.stopRecordingPlayback();
			m_playbackWasActive = false;
			if(m_playbackStatus)
				m_playbackStatus->setText("Playback stopped.", juce::dontSendNotification);
			updateRecorderControls();
			return;
		}

		const auto file = selectedRecordingFile();
		if(file == juce::File() || isSelectedRecordingBusy())
			return;

		std::string error;
		if(m_state.playRecording(file, error))
		{
			m_playbackWasActive = true;
			startTimer(500);
		}
		else if(m_playbackStatus)
		{
			m_playbackStatus->setText(juce::String::fromUTF8(error.c_str()), juce::dontSendNotification);
		}
		updateRecorderControls();
	}

	void IosControlPanel::renameSelectedRecording()
	{
		const auto file = selectedRecordingFile();
		if(file == juce::File() || isSelectedRecordingBusy() || m_renameWindow)
			return;

		// Existing JUCE async-modal flow; message thread only.
		m_renameWindow = std::make_unique<juce::AlertWindow>(
			"Rename recording", "New name for " + file.getFileName(),
			juce::MessageBoxIconType::NoIcon, this);
		m_renameWindow->addTextEditor("name", file.getFileNameWithoutExtension());
		m_renameWindow->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
		m_renameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

		const auto safeThis = juce::Component::SafePointer<IosControlPanel>(this);
		m_renameWindow->enterModalState(true,
			juce::ModalCallbackFunction::create([safeThis, file](const int _result)
			{
				if(safeThis == nullptr)
					return;
				auto& panel = *safeThis;
				const auto typed = panel.m_renameWindow
					? panel.m_renameWindow->getTextEditorContents("name").trim() : juce::String();
				panel.m_renameWindow.reset();

				if(_result != 1)
					return;

				const auto setStatus = [&panel](const juce::String& _text)
				{
					if(panel.m_playbackStatus)
						panel.m_playbackStatus->setText(_text, juce::dontSendNotification);
				};

				// Sanitize: legal filesystem characters only, never empty,
				// always .wav, never overwriting an existing file.
				const auto legal = juce::File::createLegalFileName(typed);
				if(legal.isEmpty())
				{
					setStatus("Rename failed: the name is empty.");
					return;
				}

				auto target = file.getSiblingFile(legal + ".wav");
				if(target == file)
					return;
				if(target.existsAsFile())
					target = target.getNonexistentSibling();

				if(!file.moveFileTo(target))
				{
					setStatus("Rename failed: " + file.getFileName());
					return;
				}

				panel.refreshRecordingsList();
				// Restore the selection on the renamed file.
				for(size_t i = 0; i < panel.m_recordingEntries.size(); ++i)
				{
					if(panel.m_recordingEntries[i].file != target)
						continue;
					if(panel.m_recordingsList)
						panel.m_recordingsList->selectRow(static_cast<int>(i));
					break;
				}
				setStatus("Renamed to " + target.getFileName());
				panel.updateRecorderControls();
			}), false);
	}

	void IosControlPanel::deleteSelectedRecording()
	{
		const auto file = selectedRecordingFile();
		if(file == juce::File() || isSelectedRecordingBusy())
			return;

		// Explicit two-tap confirmation; selection change disarms.
		if(!m_recDeleteArmed)
		{
			m_recDeleteArmed = true;
			if(m_recDeleteButton)
				m_recDeleteButton->setButtonText("Tap again to delete " + file.getFileName());
			return;
		}

		m_recDeleteArmed = false;
		// Never delete the file that is being played right now.
		if(m_state.getRecordingPlaybackStatus().playing)
			m_state.stopRecordingPlayback();

		const auto ok = file.deleteFile();
		refreshRecordingsList();
		if(m_recordingsList)
			m_recordingsList->deselectAllRows();
		if(m_playbackStatus && !ok)
			m_playbackStatus->setText("Failed to delete " + file.getFileName(), juce::dontSendNotification);
		updateRecorderControls();
	}

	juce::File IosControlPanel::getRecordingsFolder() const
	{
		return juce::File(m_processor.getDataFolder()).getChildFile("recordings");
	}

	double IosControlPanel::readWavDurationSeconds(const juce::File& _file)
	{
		// Minimal RIFF chunk scan (message thread, a few dozen bytes): byte
		// rate from 'fmt ' and the 'data' size. Returns 0 when unknown - the
		// list then simply shows no duration instead of a fake one.
		juce::FileInputStream stream(_file);
		char tag[4];
		uint32_t byteRate = 0;
		if(!stream.openedOk() || stream.read(tag, 4) != 4 || memcmp(tag, "RIFF", 4) != 0)
			return 0.0;
		stream.setPosition(8);
		if(stream.read(tag, 4) != 4 || memcmp(tag, "WAVE", 4) != 0)
			return 0.0;
		while(stream.getPosition() + 8 <= stream.getTotalLength())
		{
			if(stream.read(tag, 4) != 4)
				return 0.0;
			const auto chunkSize = static_cast<uint32_t>(stream.readInt());
			if(memcmp(tag, "fmt ", 4) == 0)
			{
				const auto chunkStart = stream.getPosition();
				stream.setPosition(chunkStart + 8);	// skip format/channels/rate
				byteRate = static_cast<uint32_t>(stream.readInt());
				stream.setPosition(chunkStart + chunkSize + (chunkSize & 1));
			}
			else if(memcmp(tag, "data", 4) == 0)
			{
				return byteRate > 0 ? static_cast<double>(chunkSize) / byteRate : 0.0;
			}
			else
			{
				stream.setPosition(stream.getPosition() + chunkSize + (chunkSize & 1));
			}
		}
		return 0.0;
	}

	void IosControlPanel::refreshRecordingsList()
	{
		m_recordingEntries.clear();

		const auto folder = getRecordingsFolder();
		auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav;*.WAV");
		std::sort(files.begin(), files.end(),
			[](const juce::File& _a, const juce::File& _b)
			{
				return _a.getLastModificationTime() > _b.getLastModificationTime();
			});

		auto& recorder = m_processor.getAudioRecorder();
		const auto activeFile = recorder.isRecordingOrBusy() ? recorder.getActiveFile() : juce::File();

		for(const auto& file : files)
		{
			RecordingEntry entry;
			entry.file = file;
			entry.label = file.getFileName()
				+ "   -   " + juce::File::descriptionOfSizeInBytes(file.getSize());
			if(file == activeFile)
			{
				// The worker is still writing this one; no header scan.
				entry.label << "   -   recording...";
			}
			else if(const auto seconds = readWavDurationSeconds(file); seconds > 0.0)
			{
				const auto total = static_cast<int>(seconds + 0.5);
				entry.label << "   -   "
					<< juce::String::formatted("%d:%02d", total / 60, total % 60);
			}
			else
			{
				entry.label << "   -   duration unknown";
			}
			entry.label << "   -   " + file.getLastModificationTime().formatted("%Y-%m-%d %H:%M");
			m_recordingEntries.emplace_back(std::move(entry));
		}
		m_recDeleteArmed = false;

		if(m_recordingsList)
			m_recordingsList->updateContent();
	}

	void IosControlPanel::toggleRecording()
	{
		auto& recorder = m_processor.getAudioRecorder();
		const auto status = recorder.getStatus();

		if(status.state == pluginLib::AudioRecorder::State::Recording)
		{
			recorder.requestStop();
			m_recorderWasBusy = true;
			startTimer(500);
		}
		else if(!recorder.isRecordingOrBusy())
		{
			juce::String error;
			if(recorder.start(getRecordingsFolder(), m_processor.getSampleRate(), error))
			{
				m_recorderWasBusy = true;
				refreshRecordingsList();	// the new file appears immediately
				startTimer(500);
			}
			else if(m_recorderStatus)
			{
				m_recorderStatus->setText(error, juce::dontSendNotification);
			}
		}
		updateRecorderControls();
	}

	void IosControlPanel::updateRecorderControls()
	{
		const auto status = m_processor.getAudioRecorder().getStatus();
		using State = pluginLib::AudioRecorder::State;

		if(m_recordButton)
			m_recordButton->setButtonText(
				status.state == State::Recording ? "Stop"
				: status.state == State::Stopping || status.state == State::Finalizing
					? "Finalizing..." : "Record");

		if(!m_recorderStatus)
			return;

		juce::String text;
		switch(status.state)
		{
		case State::Recording:
		case State::Stopping:
		case State::Finalizing:
			{
				const auto total = static_cast<int>(status.seconds);
				text << (status.state == State::Recording ? "Recording...  " : "Finalizing...  ")
					<< juce::String::formatted("%d:%02d", total / 60, total % 60)
					<< "  -  " << juce::File::descriptionOfSizeInBytes(status.fileBytes);
				if(status.droppedBlocks)
					text << "  -  Dropped blocks: " << juce::String(static_cast<int>(status.droppedBlocks));
			}
			break;
		case State::Saved:
			text << "Saved: " << status.savedFileName;
			if(status.droppedBlocks)
				text << "  (Dropped blocks: "
					<< juce::String(static_cast<int>(status.droppedBlocks))
					<< " - recording is not gapless)";
			break;
		case State::Error:
			text << "Recording failed: " << status.error;
			if(status.savedFileName.isNotEmpty())
				text << "  (partial file kept: " << status.savedFileName << ")";
			break;
		case State::Idle:
		default:
			text = "Records the Monomachine stereo output as float32 WAV.";
			break;
		}
		m_recorderStatus->setText(text, juce::dontSendNotification);

		// Refresh the list once when a recording just finished.
		if(m_recorderWasBusy
			&& (status.state == State::Saved || status.state == State::Error))
		{
			m_recorderWasBusy = false;
			refreshRecordingsList();
		}

		// Selected-file actions: disabled while nothing is selected or while
		// the selected file is the one currently being written.
		const bool haveSelection = selectedRecordingFile() != juce::File();
		const bool selectionBusy = isSelectedRecordingBusy();
		const auto playback = m_state.getRecordingPlaybackStatus();

		if(m_playButton)
		{
			m_playButton->setButtonText(playback.playing ? "Stop playback" : "Play");
			m_playButton->setEnabled(playback.playing || (haveSelection && !selectionBusy && playback.available));
		}
		if(m_recShareButton)
			m_recShareButton->setEnabled(haveSelection && !selectionBusy);
		if(m_recRenameButton)
			m_recRenameButton->setEnabled(haveSelection && !selectionBusy);
		if(m_recDeleteButton)
		{
			m_recDeleteButton->setEnabled(haveSelection && !selectionBusy);
			if(!m_recDeleteArmed)
				m_recDeleteButton->setButtonText("Delete recording...");
		}

		if(m_playbackStatus)
		{
			if(playback.playing)
			{
				const auto pos = static_cast<int>(playback.positionSeconds);
				const auto len = static_cast<int>(playback.lengthSeconds + 0.5);
				m_playbackStatus->setText("Playing...  "
					+ juce::String::formatted("%d:%02d / %d:%02d",
						pos / 60, pos % 60, len / 60, len % 60),
					juce::dontSendNotification);
			}
			else if(m_playbackWasActive)
			{
				// Real end-of-file transition, not a fake progress state.
				m_playbackWasActive = false;
				m_state.stopRecordingPlayback();
				m_playbackStatus->setText("Playback finished.", juce::dontSendNotification);
			}
		}
	}

	void IosControlPanel::shareSelectedRecording()
	{
		const auto file = selectedRecordingFile();
		if(file == juce::File() || isSelectedRecordingBusy())
			return;
		juce::Array<juce::URL> urls;
		urls.add(juce::URL(file));
		m_shareHandle = juce::ContentSharer::shareFilesScoped(urls,
			[](bool, const juce::String&) {}, this);
	}

	void IosControlPanel::togglePerformanceCapture()
	{
		m_state.setPerformanceCaptureActive(!m_state.isPerformanceCaptureActive());
		refresh();
	}

	void IosControlPanel::exportDiagnostics()
	{
		// Shares the most recent performance reports through the native iOS
		// share sheet. Reports are plain jsonl/log files written by the
		// existing PerformanceReport worker - no personal or system data.
		const auto folder = m_state.getDiagnosticsLogsFolder();

		juce::Array<juce::URL> urls;
		if(folder.isDirectory())
		{
			auto files = folder.findChildFiles(juce::File::findFiles, false, "*.jsonl;*.log");
			// Timestamped filenames sort chronologically.
			std::sort(files.begin(), files.end(),
				[](const juce::File& _a, const juce::File& _b)
				{
					return _a.getFileName() < _b.getFileName();
				});
			for(int i = juce::jmax(0, files.size() - 5); i < files.size(); ++i)
				urls.add(juce::URL(files.getReference(i)));
		}

		if(urls.isEmpty())
		{
			if(m_perfStatus)
				m_perfStatus->setText("No diagnostic reports to export yet.", juce::dontSendNotification);
			return;
		}

		m_shareHandle = juce::ContentSharer::shareFilesScoped(urls,
			[](bool, const juce::String&) {}, this);
	}

	void IosControlPanel::applyFpsCap(const int _hz)
	{
		m_state.setFpsLimitConfig(_hz);
		refresh();
	}

	void IosControlPanel::onResetSettings()
	{
		if(!m_resetArmed)
		{
			m_resetArmed = true;
			if(m_resetButton)
				m_resetButton->setButtonText("Tap again to confirm reset");
			return;
		}

		m_resetArmed = false;
		m_state.resetApplicationSettings();
		if(m_resetButton)
			m_resetButton->setButtonText("Settings cleared - restart the app");
	}

	void IosControlPanel::refresh()
	{
		if(m_romStatus)
			m_romStatus->setText(juce::String::fromUTF8(m_state.getRomStatusText().c_str()), juce::dontSendNotification);
		if(m_romDetails)
			m_romDetails->setText(juce::String::fromUTF8(m_state.getFirmwareDetailsText().c_str()), juce::dontSendNotification);
		if(m_importButton)
			m_importButton->setEnabled(!m_importBusy);
		if(m_volume)
			m_volume->setValue(m_processor.getOutputGain(), juce::dontSendNotification);

		if(m_audioInfo)
		{
			// Live values from the running JUCE audio engine - never faked.
			const auto sampleRate = m_processor.getSampleRate();
			const auto blockSize = m_processor.getBlockSize();

			juce::String text;
			text << "Sample rate: "
				<< (sampleRate > 0.0 ? juce::String(sampleRate, 0) + " Hz" : juce::String("n/a"))
				<< "\nBuffer: " << blockSize << " samples";
			if(sampleRate > 0.0 && blockSize > 0)
				text << " (" << juce::String(1000.0 * blockSize / sampleRate, 1) << " ms)";
			text << "\nOutput: stereo  -  Input: off by default";
			m_audioInfo->setText(text, juce::dontSendNotification);
		}

		if(m_perfStatus)
			m_perfStatus->setText(juce::String::fromUTF8(m_state.getPerformanceStatusText().c_str()), juce::dontSendNotification);
		if(m_captureButton)
			m_captureButton->setButtonText(m_state.isPerformanceCaptureActive()
				? "Stop performance capture" : "Start performance capture");
		if(m_perfMetrics)
			m_perfMetrics->setText(juce::String::fromUTF8(m_state.getRealtimeMetricsText().c_str()), juce::dontSendNotification);
		if(m_perfWarnings)
			m_perfWarnings->setText(juce::String::fromUTF8(m_state.getRealtimeWarningsText().c_str()), juce::dontSendNotification);

		const auto fps = m_state.getFpsLimitConfig();
		if(m_fps30)
			m_fps30->setButtonText(juce::String("UI FPS cap 30") + (fps == 30 || fps <= 0 ? " (current)" : ""));
		if(m_fps60)
			m_fps60->setButtonText(juce::String("UI FPS cap 60") + (fps == 60 ? " (current)" : ""));

		m_resetArmed = false;
		if(m_resetButton)
			m_resetButton->setButtonText("Reset Settings...");

		refreshSysexLibrary();
		updateSysexControls();
		refreshRecordingsList();
		updateRecorderControls();
		if(m_state.isSysexTransferActive() || m_state.canResumeSysexTransfer()
			|| m_processor.getAudioRecorder().isRecordingOrBusy())
			startTimer(500);
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
