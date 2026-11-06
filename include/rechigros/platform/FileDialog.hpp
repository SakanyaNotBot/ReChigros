#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rechigros::platform {

[[nodiscard]] std::filesystem::path pathFromDialogResult(std::string_view result);

// One answer from a picker that does not block the interface thread. The tag identifies
// what was requested; the path is the application-owned copy handed to the importer.
struct PickerResult {
  std::string tag;
  std::filesystem::path path;
};

// Queues a picker answer for the application loop. The mobile picker runs in the UIKit
// run loop, so it cannot return through the synchronous FileDialog calls.
void queuePickerResult(std::string tag, std::filesystem::path path);

// Takes the first queued picker answer. Directory answers are left for
// takePickedDirectory() so the output-directory row owns that result.
[[nodiscard]] std::optional<PickerResult> takePickerResult();

// Takes a queued output-directory answer, if one is waiting.
[[nodiscard]] std::optional<std::filesystem::path> takePickedDirectory();

// Native file and folder pickers.
//
// The desktop and Android entry points block until the user answers, so they must not be
// called from the render or event thread. iOS presents the system picker and returns
// immediately; its answer is delivered through takePickerResult()/takePickedDirectory().
// An empty synchronous result means the user cancelled.
class FileDialog final {
public:
  // Shows a picker for a chart file (JSON, PEC or RPE) and returns the chosen path.
  [[nodiscard]] static std::filesystem::path openChartFile();

  // Shows a picker for a chart folder and returns the chosen directory.
  [[nodiscard]] static std::filesystem::path openChartFolder();

  // Shows a picker for a chart package and returns the chosen archive.
  [[nodiscard]] static std::filesystem::path openChartPackage();

  // Shows a picker for an audio track and returns the chosen file.
  [[nodiscard]] static std::filesystem::path openAudioFile();

  // Shows a picker for a chart illustration and returns the chosen image.
  [[nodiscard]] static std::filesystem::path openImageFile();

  // Shows a picker for a replay package or replay document: a `.cgrp`/`.zip` archive
  // or a plain replay JSON file.
  [[nodiscard]] static std::filesystem::path openReplayFile();

  // Shows a picker for a skin / resource package archive.
  [[nodiscard]] static std::filesystem::path openSkinPackage();

  // Shows a picker for an output directory and returns the chosen folder. On iOS the
  // answer arrives through takePickedDirectory() instead.
  [[nodiscard]] static std::filesystem::path pickDirectory();

  // True when the current platform reports picker answers asynchronously rather than
  // returning the chosen path. A multi-step import uses this to chain its pickers.
  [[nodiscard]] static bool asynchronous() noexcept;

  // True when the platform exposes a chart-folder picker.
  [[nodiscard]] static bool supportsChartFolder() noexcept;

  // True when the platform exposes an output-directory picker.
  [[nodiscard]] static bool supportsDirectoryPicker() noexcept;

  // True when the current platform implements the pickers. A caller can fall back to a
  // typed path when this is false.
  [[nodiscard]] static bool available() noexcept;
};

} // namespace rechigros::platform
