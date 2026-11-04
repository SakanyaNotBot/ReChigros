#pragma once

// C++-callable surface of the ObjectivelyMVC settings screen.
//
// The framework is GNU C: its headers declare `interface` as a struct member name, which
// collides with the Windows SDK macro of the same name, so they are never included from a
// C++ translation unit. The implementation lives in MvcSettings.c behind these C-ABI calls.

namespace rechigros::app {

// True when the framework is linked into this binary and can host the screen.
[[nodiscard]] bool mvcSettingsAvailable();

// Releases the view hierarchy and every framework object it owns.
void mvcSettingsShutdown();

} // namespace rechigros::app
