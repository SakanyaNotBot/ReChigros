#pragma once

#include <filesystem>
#include <string>
#include <vector>

// File import flows for the user-facing import actions.
//
// This module is deliberately free of interface state: the pickers, dialogs and screen
// transitions live next to the screens, so the same import logic can be exercised by a
// batch check. Every entry point returns the user-visible feedback message, written with
// the wording the reference interface uses for the same action.
namespace rechigros::app {

class ChartLibrary;
class ReplayStore;

struct ImportOutcome {
  bool imported{};
  // Toast text to show; empty when the action was cancelled before reaching this module.
  std::string message;
  // Installed chart identifier for chart imports; empty otherwise.
  std::string chartId;
  // True when the imported replay is already present in the replay store.
  bool duplicate{};
};

// What an archive received from the system (an "open with" document, a shared file or a
// drop) contains. The caller routes it to the matching store.
enum class PackageKind { Chart, Replay, Skin, Unknown };
[[nodiscard]] PackageKind classifyPackageArchive(const std::filesystem::path& archive);

// Installs one chart package archive. `requireComplete` mirrors the reference: a package
// picked from the browser may be accepted as-is while an incoming document must carry
// every required file.
[[nodiscard]] ImportOutcome importChartPackage(ChartLibrary& library,
    const std::filesystem::path& archive, bool requireComplete = false);

// Installs a chart pack that already exists as an unpacked directory.
[[nodiscard]] ImportOutcome importChartDirectory(ChartLibrary& library,
    const std::filesystem::path& directory);

// Builds one pack from loose files (one chart document, one audio track, one illustration)
// and installs it.
[[nodiscard]] ImportOutcome importLooseChartFiles(ChartLibrary& library,
    const std::vector<std::filesystem::path>& files);

// Imports a replay from an archive (`replay.json` anywhere inside, as the reference replay
// package stores it) or from a plain replay document. This is the single entry point the
// replay model may adapt without touching interface code.
[[nodiscard]] ImportOutcome importReplayArchive(const std::filesystem::path& archive,
    ReplayStore& replays);

// Imports one skin / resource package into <skinsRoot>/<name>, flattening the archive so
// the renderer finds <skinsRoot>/<name>/respack.json and its sibling assets.
struct SkinImportOutcome {
  bool imported{};
  std::string message;
  std::string skinId;
};
[[nodiscard]] SkinImportOutcome importSkinPackage(const std::filesystem::path& archive,
    const std::filesystem::path& skinsRoot);

} // namespace rechigros::app
