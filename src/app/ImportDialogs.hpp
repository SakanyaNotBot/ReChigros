#pragma once

#include "rechigros/app/AppContext.hpp"

// Dialog-driven import entry points used by the library and settings screens.
//
// Each function asks the platform picker for the required source, runs the matching
// import flow from ImportCore and reports the result through the shared toast. On a
// platform whose picker answers asynchronously (empty return value) the function reports
// "no selection" and leaves the current screen untouched; the incoming document then
// travels through the normal drop-file path instead.
namespace rechigros::app {

// Chart package archive picked from the browser. On success the library refreshes and the
// imported chart's detail page opens, matching the reference behaviour.
[[nodiscard]] bool importChartPackageFromDialog(AppContext& context);

// Unpacked chart pack directory picked from the browser.
[[nodiscard]] bool importChartFolderFromDialog(AppContext& context);

// Loose chart document, audio track and illustration picked together; they are gathered
// into one pack before installation.
[[nodiscard]] bool importChartFilesFromDialog(AppContext& context);

// Replay package or replay document picked from the browser; the replay list refreshes on
// the next settings draw.
[[nodiscard]] bool importReplayFromDialog(AppContext& context);

// Skin / resource package picked from the browser; the imported skin becomes the current
// skin so the renderer reloads it on the next run.
[[nodiscard]] bool importSkinFromDialog(AppContext& context);

// A platform picker that answers asynchronously reports the chosen document through the
// drop channel tagged with the kind of document the caller asked for. Consumes the payload
// and returns true when it was handled; a plain path returns false so the caller falls back
// to the regular drag-and-drop handling.
[[nodiscard]] bool handlePickerResult(AppContext& context, std::string_view payload);

} // namespace rechigros::app
