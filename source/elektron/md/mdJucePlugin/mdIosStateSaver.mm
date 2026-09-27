#include "mdIosStateSaver.h"

// juce_StandaloneFilterWindow.h (below) only declares StandalonePluginHolder/
// StandaloneFilterWindow; it does not include the modules those classes are
// built from (that's left to whoever includes it, exactly like every other
// JUCE module leaf header). It is normally only ever included from JUCE's own
// generated per-format wrapper translation unit
// (juce_audio_plugin_client_Standalone.cpp), which is why this file is now
// compiled into the "<target>_Standalone" wrapper target instead of the
// shared-code target (see mdJucePlugin/CMakeLists.txt) - that's the target
// that actually links juce::juce_audio_plugin_client_Standalone and has
// JucePlugin_Build_Standalone=1 set. Bring in the same modules JUCE's own
// juce_audio_plugin_client_Standalone.cpp includes immediately before this
// header, in the same order, rather than the internal-only
// juce_audio_plugin_client.h umbrella (which only pulls in juce_gui_basics/
// juce_audio_basics/juce_audio_processors - not enough for AudioIODevice*,
// AudioDeviceManager, AudioProcessorPlayer or MidiDeviceInfo, which live in
// juce_audio_devices/juce_audio_utils) or <JuceHeader.h> (a Projucer-only
// artifact this CMake-based build never generates).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

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

		// UIApplicationWillResignActiveNotification precedes every
		// backgrounding, suspension and termination, so the most recent
		// session state is always on disk before iOS may kill the process.
		// UIKit delivers it on the main thread, which is JUCE's message
		// thread; the write itself is the exact mechanism the desktop
		// standalone uses in its close-button path
		// (StandalonePluginHolder::savePluginState -> versioned processor
		// state blob in the app's PropertiesFile), and the matching restore
		// already runs unconditionally at startup (reloadPluginState).
		id observer = [[NSNotificationCenter defaultCenter]
			addObserverForName:UIApplicationWillResignActiveNotification
			object:nil
			queue:[NSOperationQueue mainQueue]
			usingBlock:^(NSNotification* _notification)
			{
				(void)_notification;
				if(auto* holder = juce::StandalonePluginHolder::getInstance())
				{
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
	}
}

#else // JUCE_IOS

namespace mdJucePlugin
{
	void installIosBackgroundStateSaver() {}
}

#endif // JUCE_IOS
