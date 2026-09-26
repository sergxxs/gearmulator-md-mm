#pragma once

// iOS standalone only: persists the plugin/session state whenever the app
// resigns active. The desktop standalone saves state in the window's
// close-button path, which never runs on iOS - without this, a session is
// lost when iOS terminates the suspended process.
//
// The implementation (mdIosStateSaver.mm) is only compiled into iOS builds
// and every call site is guarded with JUCE_IOS.
namespace mdJucePlugin
{
	void installIosBackgroundStateSaver();
}
