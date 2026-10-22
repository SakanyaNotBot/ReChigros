#pragma once

#include <filesystem>
#include <vector>

namespace rechigros::platform {

// Native file and folder pickers.
//
// Each platform shows its own dialog: the desktop shells use the system file browser, a
// packaged mobile build shows the platform document picker. Both entry points block the
// calling thread until the user answers or dismisses the dialog, so they must not be called
// from the render or event thread. An empty result means the user cancelled.
class FileDialog final {
public:
  // Shows a picker for a chart file (JSON or plain text) and returns the chosen path.
  [[nodiscard]] static std::filesystem::path openChartFile();

  // Shows a picker for a chart folder and returns the chosen directory.
  [[nodiscard]] static std::filesystem::path openChartFolder();

  // Shows a picker for a chart package and returns the chosen archive.
  [[nodiscard]] static std::filesystem::path openChartPackage();

  // Shows a picker for a replay package or replay document: a `.cgrp`/`.zip` archive
  // or a plain replay JSON file.
  [[nodiscard]] static std::filesystem::path openReplayFile();

  // Shows a picker for a skin / resource package archive.
  [[nodiscard]] static std::filesystem::path openSkinPackage();

  // Shows a multi-selection picker over the loose files a chart is built from (chart
  // document, audio track, illustration). Returns the chosen files; empty means the
  // dialog was cancelled or the platform has no multi-selection picker.
  [[nodiscard]] static std::vector<std::filesystem::path> openChartSourceFiles();

  // True when the current platform implements the pickers. A caller can fall back to a
  // typed path when this is false.
  [[nodiscard]] static bool available() noexcept;
};

} // namespace rechigros::platform
