#include "mdIosStateSaver.h"

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
