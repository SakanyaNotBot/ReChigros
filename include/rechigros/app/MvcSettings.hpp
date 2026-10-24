#pragma once

// Bridge between the application's screen layer and the ObjectivelyMVC view hierarchy.
//
// The framework is GNU C and is compiled as C, so its headers cannot be included from the
// C++ screen files directly: the framework's headers define `interface` as a member, and
// several C++ standard headers cannot be combined with them in one translation unit. This
// header therefore exposes an opaque handle and plain C-style entry points, and keeps every
// framework type inside MvcSettings.cpp.

namespace rechigros::app {

struct AppContext;

// Builds (or rebuilds) the settings view hierarchy for the given tab, bound to the live
// settings in `context`. Returns true when the framework is available and the view was
// created; false means the caller must fall back to the immediate-mode screen.
bool mvcSettingsAvailable();

// Brings the settings hierarchy up to date with the context and renders it into the current
// GPU frame. Returns true when it drew; false means the caller should draw the old screen.
bool mvcSettingsRender(AppContext& context, int tab);

// Routes one SDL event to the settings hierarchy.
void mvcSettingsHandleEvent(const void* sdlEvent);

// Releases the settings hierarchy. Called on shutdown or when leaving the screen.
void mvcSettingsShutdown();

} // namespace rechigros::app
