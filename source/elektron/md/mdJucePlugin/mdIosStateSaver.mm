#include "mdIosStateSaver.h"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include "jucePluginLib/processor.h"

#if JUCE_IOS

#import <UIKit/UIKit.h>

namespace mdJucePlugin
{
	void installIosBackgroundStateSaver()
	{
		static bool installed = false;
		if(installed)
			return;
		installed = true;

		// Saving serializes the complete MM state via Plugin::getState(),
		// which holds the same mutex the realtime audio callback takes every
		// block - so it must never run while the user is actively playing.
		// UIApplicationWillResignActiveNotification would fire on every
		// Control Center pull (including starting a screen recording),
		// notification banner and Siri overlay, audibly glitching the audio.
		// UIApplicationDidEnterBackgroundNotification is the correct trigger:
		// iOS can only suspend/terminate a process after it entered the
		// background, so kill-safety coverage is identical, and audio stops
		// shortly after backgrounding anyway (no background audio mode), so
		// the one legitimate save can no longer interrupt a performance.
		// UIKit delivers it on the main thread, which is JUCE's message
		// thread; the write itself is the exact mechanism the desktop
		// standalone uses in its close-button path
		// (StandalonePluginHolder::savePluginState -> versioned processor
		// state blob in the app's PropertiesFile), and the matching restore
		// already runs unconditionally at startup (reloadPluginState).
		id observer = [[NSNotificationCenter defaultCenter]
			addObserverForName:UIApplicationDidEnterBackgroundNotification
			object:nil
			queue:[NSOperationQueue mainQueue]
			usingBlock:^(NSNotification* _notification)
			{
				(void)_notification;
				if(auto* holder = juce::StandalonePluginHolder::getInstance())
				{
					// A live WAV recording is finalized before iOS may
					// suspend the process, so no corrupted file is left
					// behind (the recorder worker drains during the
					// background grace period; audio stops anyway).
					if(auto* proc = dynamic_cast<pluginLib::Processor*>(holder->processor.get()))
						proc->getAudioRecorder().requestStop();

					holder->savePluginState();

					// The PropertiesFile save timer may never fire if iOS
					// suspends the process right after backgrounding; flush
					// synchronously (atomic temp-file/replace write).
					if(auto* props = dynamic_cast<juce::PropertiesFile*>(holder->settings.get()))
						props->saveIfNeeded();
				}
			}];

		// App-lifetime observer, intentionally never removed.
#if __has_feature(objc_arc)
		static id retainedObserver;
		retainedObserver = observer;
#else
		[observer retain];
#endif

		// The standalone shows a permanent "Audio input is muted to avoid
		// feedback loop" banner because the MM processor declares an input
		// bus. This iOS build never auto-opens audio input
		// (JUCE_STANDALONE_PLUGIN_AUTO_OPEN_AUDIO_INPUT=0), so with no live
		// input the warning is meaningless - and its 30pt strip shrinks the
		// height-limited aspect-fit of the whole panel by ~9% on iPhone.
		// Unmute (which hides the banner via the holder's Value listener)
		// only while the user has not explicitly enabled audio input; once
		// input is enabled the stock feedback protection stays untouched.
		// Deferred: the holder/window do not exist yet during processor
		// construction, which is when this install function runs.
		juce::MessageManager::callAsync([]
		{
			if(auto* holder = juce::StandalonePluginHolder::getInstance())
			{
				const bool inputExplicitlyEnabled = holder->settings != nullptr
					&& holder->settings->getBoolValue("standaloneAudioInputExplicitlyEnabled", false);

				if(!inputExplicitlyEnabled)
					holder->getMuteInputValue().setValue(false);
			}
		});
	}
}

#else // JUCE_IOS

namespace mdJucePlugin
{
	void installIosBackgroundStateSaver() {}
}

#endif // JUCE_IOS
