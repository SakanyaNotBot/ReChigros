#include "rechigros/platform/FileDialog.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rechigros::platform {
namespace {

// Answers from an asynchronous picker are produced on the interface thread and consumed by
// the application loop. The mutex keeps the queue safe if a platform ever answers off-thread.
std::mutex g_pickerResultMutex;
std::vector<PickerResult> g_pickerResults;

} // namespace

void queuePickerResult(std::string tag, std::filesystem::path path) {
  std::lock_guard<std::mutex> guard(g_pickerResultMutex);
  g_pickerResults.push_back(PickerResult{std::move(tag), std::move(path)});
}

std::optional<PickerResult> takePickerResult() {
  std::lock_guard<std::mutex> guard(g_pickerResultMutex);
  for (auto iterator = g_pickerResults.begin(); iterator != g_pickerResults.end(); ++iterator) {
    if (iterator->tag == "pickdir") continue;
    PickerResult result = std::move(*iterator);
    g_pickerResults.erase(iterator);
    return result;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> takePickedDirectory() {
  std::lock_guard<std::mutex> guard(g_pickerResultMutex);
  for (auto iterator = g_pickerResults.begin(); iterator != g_pickerResults.end(); ++iterator) {
    if (iterator->tag != "pickdir") continue;
    std::filesystem::path path = std::move(iterator->path);
    g_pickerResults.erase(iterator);
    return path;
  }
  return std::nullopt;
}

} // namespace rechigros::platform

#if defined(_WIN32) || defined(__ANDROID__)

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <atomic>
#include <functional>

namespace rechigros::platform {
namespace {

// The platform dialogs report their answer through a callback the platform may invoke on a thread
// of its own choosing: the desktop backend runs the dialog on a worker, the packaged one answers
// on the interface thread. These wrappers turn that into the blocking answer the callers are
// written against, and they only ever wait on the interface thread.
struct PickRequest {
  std::mutex mutex;
  std::atomic<bool> done{false};
  std::vector<std::filesystem::path> paths;
};

void pickCallback(void* userdata, const char* const* filelist, int filter) {
  (void)filter;
  auto* request = static_cast<PickRequest*>(userdata);
  if (request == nullptr) return;
  {
    std::lock_guard<std::mutex> guard(request->mutex);
    if (filelist != nullptr) {
      for (const char* const* entry = filelist; *entry != nullptr; ++entry) {
        // An entry that is not a path is the platform reporting why it could not show the dialog,
        // which is not a selection and must not be handed back as one.
        if (**entry == 0) continue;
        request->paths.emplace_back(pathFromDialogResult(*entry));
      }
    }
  }
  request->done.store(true, std::memory_order_release);
}

int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

// Filters are extension lists, not glob patterns: the shared validator accepts only letters,
// digits, underscore, hyphen, dot and the semicolon separator, and rejects "*." and "*.*".
// Android asks the system MIME map for each extension. The packaged formats have no MIME
// registration there, so pez/cgrp resolve to nothing and the picker would hide them; the
// `*` fallback keeps every document visible while still preferring the archive MIME.
#if defined(__ANDROID__)
// Chart packages use extensions the Android MIME map does not know, and a narrow MIME filter
// would hide .pec/.rpe documents. The importer validates the picked file, so the chart picker
// keeps every document visible.
SDL_DialogFileFilter chartFilters[] = { {"Chart", "*"} };
SDL_DialogFileFilter packageFilters[] = { {"Chart package", "zip;pez;cgrp"},
    {"All files", "*"} };
SDL_DialogFileFilter replayFilters[] = { {"Replay", "cgrp;zip;json"}, {"All files", "*"} };
SDL_DialogFileFilter skinFilters[] = { {"Skin package", "zip;pez"}, {"All files", "*"} };
#else
SDL_DialogFileFilter chartFilters[] = { {"Chart", "json;pec;rpe"} };
SDL_DialogFileFilter packageFilters[] = { {"Chart package", "zip;pez;cgrp"} };
SDL_DialogFileFilter replayFilters[] = { {"Replay", "cgrp;zip;json"} };
SDL_DialogFileFilter skinFilters[] = { {"Skin package", "zip;pez"} };
#endif
SDL_DialogFileFilter audioFilters[] = { {"Audio", "ogg;mp3;wav;m4a;aac;flac"} };
SDL_DialogFileFilter imageFilters[] = { {"Image", "png;jpg;jpeg;webp"} };

// Runs one dialog and waits for its answer.
std::vector<std::filesystem::path> runDialog(
    const std::function<void(SDL_DialogFileCallback, void*)>& show) {
  PickRequest request;
  show(pickCallback, &request);
  // The callback is the only completion signal, and the platform calls it exactly once per
  // request, including when the dialog is dismissed. Pumping while waiting matters on
  // Android: the lifecycle transition that rebuilds the EGL surface when the picker's
  // activity closes is driven by the event pump, and a caller that only sleeps leaves the
  // app drawing into a destroyed surface (a black screen until the next resume).
  // Pumping gathers events but does not consume them, so nothing aimed at the interface is
  // lost; the loop still yields to keep the wait from spinning.
  while (!request.done.load(std::memory_order_acquire)) {
    SDL_PumpEvents();
    SDL_Delay(1);
  }
  std::lock_guard<std::mutex> guard(request.mutex);
  return request.paths;
}

std::filesystem::path firstOf(std::vector<std::filesystem::path> paths) {
  return paths.empty() ? std::filesystem::path{} : paths.front();
}

std::filesystem::path pickWithFilter(const SDL_DialogFileFilter* filters, int count) {
  return firstOf(runDialog([filters, count](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, filters, count, nullptr, false);
  }));
}

} // namespace

std::filesystem::path pathFromDialogResult(std::string_view result) {
  constexpr std::string_view fileScheme = "file://";
  if (result.substr(0, fileScheme.size()) != fileScheme) {
    return std::filesystem::u8path(result);
  }
  result.remove_prefix(fileScheme.size());
  std::string decoded;
  decoded.reserve(result.size());
  for (std::size_t index = 0; index < result.size(); ++index) {
    if (result[index] == '%' && index + 2 < result.size()) {
      const int high = hexDigit(result[index + 1]);
      const int low = hexDigit(result[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    decoded.push_back(result[index]);
  }
  return std::filesystem::u8path(decoded);
}

std::filesystem::path FileDialog::openChartFile() {
  return pickWithFilter(chartFilters,
      static_cast<int>(sizeof(chartFilters) / sizeof(chartFilters[0])));
}

std::filesystem::path FileDialog::openChartFolder() {
#if defined(__ANDROID__)
  // The packaged backend has no folder dialog: it answers this request with SDL_Unsupported and
  // never calls back, so the caller falls back to picking a package instead.
  return {};
#else
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFolderDialog(callback, userdata, nullptr, nullptr, false);
  }));
#endif
}

std::filesystem::path FileDialog::openChartPackage() {
  return pickWithFilter(packageFilters,
      static_cast<int>(sizeof(packageFilters) / sizeof(packageFilters[0])));
}

std::filesystem::path FileDialog::openAudioFile() {
  return pickWithFilter(audioFilters,
      static_cast<int>(sizeof(audioFilters) / sizeof(audioFilters[0])));
}

std::filesystem::path FileDialog::openImageFile() {
  return pickWithFilter(imageFilters,
      static_cast<int>(sizeof(imageFilters) / sizeof(imageFilters[0])));
}

std::filesystem::path FileDialog::openReplayFile() {
  return pickWithFilter(replayFilters,
      static_cast<int>(sizeof(replayFilters) / sizeof(replayFilters[0])));
}

std::filesystem::path FileDialog::openSkinPackage() {
  return pickWithFilter(skinFilters,
      static_cast<int>(sizeof(skinFilters) / sizeof(skinFilters[0])));
}

std::filesystem::path FileDialog::pickDirectory() {
#if defined(__ANDROID__)
  return {};
#else
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFolderDialog(callback, userdata, nullptr, nullptr, false);
  }));
#endif
}

bool FileDialog::asynchronous() noexcept { return false; }

bool FileDialog::supportsChartFolder() noexcept {
#if defined(__ANDROID__)
  return false;
#else
  return true;
#endif
}

bool FileDialog::supportsDirectoryPicker() noexcept {
#if defined(__ANDROID__)
  return false;
#else
  return true;
#endif
}

bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform

#elif defined(__APPLE__)

#include <TargetConditionals.h>

#if !TARGET_OS_IPHONE

// macOS keeps the typed-path fallback: the picker there would have to be presented by a
// window this layer does not own.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
std::filesystem::path FileDialog::openAudioFile() { return {}; }
std::filesystem::path FileDialog::openImageFile() { return {}; }
std::filesystem::path FileDialog::openReplayFile() { return {}; }
std::filesystem::path FileDialog::openSkinPackage() { return {}; }
std::filesystem::path FileDialog::pickDirectory() { return {}; }
bool FileDialog::asynchronous() noexcept { return false; }
bool FileDialog::supportsChartFolder() noexcept { return false; }
bool FileDialog::supportsDirectoryPicker() noexcept { return false; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#endif // !TARGET_OS_IPHONE

#else

// No picker is implemented for this target, so callers fall back to a typed path.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
std::filesystem::path FileDialog::openAudioFile() { return {}; }
std::filesystem::path FileDialog::openImageFile() { return {}; }
std::filesystem::path FileDialog::openReplayFile() { return {}; }
std::filesystem::path FileDialog::openSkinPackage() { return {}; }
std::filesystem::path FileDialog::pickDirectory() { return {}; }
bool FileDialog::asynchronous() noexcept { return false; }
bool FileDialog::supportsChartFolder() noexcept { return false; }
bool FileDialog::supportsDirectoryPicker() noexcept { return false; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#endif
