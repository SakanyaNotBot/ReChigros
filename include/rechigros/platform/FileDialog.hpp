#pragma once

#include <filesystem>

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

  // True when the current platform implements the pickers. A caller can fall back to a
  // typed path when this is false.
  [[nodiscard]] static bool available() noexcept;
};

} // namespace rechigros::platform
