#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "baseLib/event.h"

#include "client/serverList.h"

#include "skin.h"

namespace pluginLib
{
	class Controller;
}

namespace juceRmlUi
{
	class Menu;
}

namespace Rml
{
	class Event;
}

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class Editor;
	class Processor;

	class PluginEditorState
	{
	public:
		explicit PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins);
		virtual ~PluginEditorState();

		PluginEditorState(PluginEditorState&&) = delete;
		PluginEditorState(const PluginEditorState&) = delete;

		PluginEditorState& operator = (PluginEditorState&&) = delete;
		PluginEditorState& operator = (const PluginEditorState&) = delete;

		Skin readSkinFromConfig() const;
		void writeSkinToConfig(const Skin& _skin) const;

		float getRootScale() const { return m_rootScale; }

		int getWidth() const;
		int getHeight() const;

		bool resizeEditor(int _width, int _height) const;

		const Skin& getCurrentSkin() const { return m_currentSkin; }
		const std::vector<Skin>& getIncludedSkins();

		static std::string createSkinDisplayName(std::string _filename);

		virtual void openMenu(const Rml::Event& _event);

		baseLib::Event<int> evSetGuiScale;
		baseLib::Event<juce::Component*> evSkinLoaded;

		juce::Component* getUiRoot() const;

		void loadDefaultSkin();

		virtual void initContextMenu(juceRmlUi::Menu& _menu) {}

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		std::string getSkinFolder() const;

		static std::string getSkinFolder(const std::string& _processorDataFolder);
		static std::string getSkinSubfolder(const Skin& _skin, const std::string& _folder);

		bool hasSkin() const
		{
			return m_currentSkin.isValid();
		}

		bool loadSkin(const Skin& _skin, uint32_t _fallbackIndex = 0);
		std::string exportSkinToFolder(const Skin& _skin, const std::string& _folder) const;

		Editor* getEditor() const;

		void enableDspBridge(bool _enable);
		bridgeClient::ServerList* getRemoteServerList() const { return m_remoteServerList.get(); }

		// --- firmware/ROM hooks for the iOS control panel ---------------------
		// Only invoked by the iOS-only control panel; inert on desktop builds.
		struct RomImportResult
		{
			bool valid = false;
			std::string message;
		};
		// Validates a firmware image candidate through the product's existing
		// ROM validation. The default has no product validator and rejects.
		virtual RomImportResult validateRomFile(const std::string& _path);
		// Human-readable firmware/device status line.
		virtual std::string getRomStatusText();
		// Multi-line firmware details for the control panel's firmware
		// manager: device state, the ROM image the existing loader currently
		// discovers, size/validity. Only real loader/processor state - no
		// invented fields.
		virtual std::string getFirmwareDetailsText();

		// --- diagnostics hooks for the iOS control panel ---------------------
		// Only invoked by the iOS-only control panel; inert on desktop builds.
		// Products with performance diagnostics (see mdPluginEditorState)
		// forward to their existing implementation - nothing is duplicated.
		virtual bool isPerformanceCaptureActive() { return false; }
		virtual void setPerformanceCaptureActive(bool) {}
		virtual std::string getPerformanceStatusText() { return "Performance capture is not supported for this product."; }
		virtual juce::File getDiagnosticsLogsFolder() { return {}; }

		// --- user SysEx library hooks for the iOS control panel --------------
		// The library folder is generic (filesystem is the source of truth);
		// transfer control forwards to the product's existing SysEx transfer
		// implementation - no second parser or transport exists.
		virtual juce::File getSysexLibraryFolder();
		virtual bool sendSysexFile(const juce::File&) { return false; }
		virtual std::string getSysexTransferStatusText() { return {}; }
		virtual bool isSysexTransferActive() { return false; }
		virtual bool canCancelSysexTransfer() { return false; }
		virtual void cancelSysexTransfer() {}
		virtual bool canResumeSysexTransfer() { return false; }
		virtual void resumeSysexTransfer() {}

		// --- WAV recording playback hooks (iOS control panel) ----------------
		// Forwarded to the product's standalone playback helper, which mixes
		// through the existing AudioDeviceManager as a second callback - the
		// synth audio path is untouched. Inert defaults for plugin builds.
		struct PlaybackStatus
		{
			bool available = false;
			bool playing = false;
			double positionSeconds = 0.0;
			double lengthSeconds = 0.0;
		};
		virtual bool playRecording(const juce::File&, std::string& _error)
		{
			_error = "Playback is not available in this build.";
			return false;
		}
		virtual void stopRecordingPlayback() {}
		virtual PlaybackStatus getRecordingPlaybackStatus() { return {}; }

		// Live metrics/warnings from the existing realtime instrumentation
		// snapshot (lock-free counters read on the message thread; never
		// touches the plugin lock and never serializes device state).
		std::string getRealtimeMetricsText();
		std::string getRealtimeWarningsText();

		// Existing 'refreshRateLimitHz' config key; read once at editor
		// creation, so a change applies after an app restart. -1 = default.
		int getFpsLimitConfig() const;
		void setFpsLimitConfig(int _hz);

		// Clears only the application's settings file. ROM images, logs,
		// diagnostics and other data folders are untouched.
		void resetApplicationSettings();


	protected:
		virtual Editor* createEditor(const Skin& _skin) = 0;

		Processor& m_processor;

	private:
		void setGuiScale(int _scale) const;

		std::unique_ptr<Editor> m_editor;
		Skin m_currentSkin;
		float m_rootScale = 1.0f;
		std::vector<Skin> m_includedSkins;
		std::vector<uint8_t> m_instanceConfig;
		std::string m_skinFolderName;
		std::unique_ptr<bridgeClient::ServerList> m_remoteServerList;
	};
}
