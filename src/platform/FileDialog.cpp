#include "rechigros/platform/FileDialog.hpp"

#if defined(_WIN32) || defined(__ANDROID__)

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

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
SDL_DialogFileFilter chartFilters[] = { {"Chart", "json;pec;txt"} };
SDL_DialogFileFilter packageFilters[] = { {"Chart package", "zip;pez;cgrp"} };
SDL_DialogFileFilter replayFilters[] = { {"Replay", "cgrp;zip;json"} };
SDL_DialogFileFilter skinFilters[] = { {"Skin package", "zip;pez"} };

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
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, chartFilters,
        static_cast<int>(sizeof(chartFilters) / sizeof(chartFilters[0])), nullptr, false);
  }));
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
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, packageFilters,
        static_cast<int>(sizeof(packageFilters) / sizeof(packageFilters[0])), nullptr, false);
  }));
}

std::filesystem::path FileDialog::openReplayFile() {
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, replayFilters,
        static_cast<int>(sizeof(replayFilters) / sizeof(replayFilters[0])), nullptr, false);
  }));
}

std::filesystem::path FileDialog::openSkinPackage() {
  return firstOf(runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, skinFilters,
        static_cast<int>(sizeof(skinFilters) / sizeof(skinFilters[0])), nullptr, false);
  }));
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

std::vector<std::filesystem::path> FileDialog::openChartSourceFiles() {
  return runDialog([](SDL_DialogFileCallback callback, void* userdata) {
    SDL_ShowOpenFileDialog(callback, userdata, nullptr, nullptr, 0, nullptr, true);
  });
}

bool FileDialog::available() noexcept { return true; }

} // namespace rechigros::platform

#elif defined(__APPLE__)

// The document picker needs a presenting view controller, which this layer does not own,
// so callers fall back to a typed path on this platform.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
std::filesystem::path FileDialog::openReplayFile() { return {}; }
std::filesystem::path FileDialog::openSkinPackage() { return {}; }
std::filesystem::path FileDialog::pickDirectory() { return {}; }
std::vector<std::filesystem::path> FileDialog::openChartSourceFiles() { return {}; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#else

// No picker is implemented for this target, so callers fall back to a typed path.
namespace rechigros::platform {

std::filesystem::path FileDialog::openChartFile() { return {}; }
std::filesystem::path FileDialog::openChartFolder() { return {}; }
std::filesystem::path FileDialog::openChartPackage() { return {}; }
std::filesystem::path FileDialog::openReplayFile() { return {}; }
std::filesystem::path FileDialog::openSkinPackage() { return {}; }
std::filesystem::path FileDialog::pickDirectory() { return {}; }
std::vector<std::filesystem::path> FileDialog::openChartSourceFiles() { return {}; }
bool FileDialog::available() noexcept { return false; }

} // namespace rechigros::platform

#endif
