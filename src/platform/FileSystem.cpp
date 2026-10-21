#include "rechigros/platform/FileSystem.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

namespace rechigros::platform {
namespace {

std::filesystem::path g_assetRoot;
std::filesystem::path g_assetOverride;
std::filesystem::path g_userDataRoot;
std::once_flag g_resolveOnce;
std::mutex g_rootMutex;

std::filesystem::path pathFromUtf8(const std::string& value) {
  return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::filesystem::path resolveAssetRoot() {
  if (!g_assetOverride.empty()) return g_assetOverride;
  if (const char* overridePath = std::getenv("RECHIGROS_ASSETS")) {
    if (*overridePath) return pathFromUtf8(overridePath);
  }
  if (char* base = SDL_GetBasePath()) {
    const std::filesystem::path candidate = pathFromUtf8(base) / "assets";
    SDL_free(base);
    std::error_code error;
    if (std::filesystem::is_directory(candidate, error)) return candidate;
  }
  std::error_code error;
  const auto working = std::filesystem::current_path(error) / "assets";
  if (!error && std::filesystem::is_directory(working, error)) return working;
  return std::filesystem::path("assets");
}

std::filesystem::path resolveUserDataRoot() {
  if (char* preference = SDL_GetPrefPath("com.wuying", "ReChigros")) {
    const std::filesystem::path result = pathFromUtf8(preference);
    SDL_free(preference);
    std::error_code error;
    std::filesystem::create_directories(result, error);
    return result;
  }
  std::error_code error;
  const auto fallback = std::filesystem::temp_directory_path(error) / "ReChigros";
  std::filesystem::create_directories(fallback, error);
  return fallback;
}

} // namespace

const std::filesystem::path& assetRoot() {
  std::call_once(g_resolveOnce, [] { g_assetRoot = resolveAssetRoot(); });
  return g_assetRoot;
}

void setAssetRootOverride(const std::filesystem::path& root) {
  std::lock_guard<std::mutex> guard(g_rootMutex);
  g_assetOverride = root;
  g_assetRoot = root;
}

std::filesystem::path userDataRoot() {
  std::lock_guard<std::mutex> guard(g_rootMutex);
  if (g_userDataRoot.empty()) g_userDataRoot = resolveUserDataRoot();
  return g_userDataRoot;
}

std::filesystem::path cacheRoot() {
  const auto root = userDataRoot() / "cache";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  return root;
}

std::filesystem::path executablePath() {
  std::error_code error;
  const auto self = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error && !self.empty()) return self;
  char* base = SDL_GetBasePath();
  if (!base) return {};
  const std::filesystem::path result = pathFromUtf8(base);
  SDL_free(base);
  return result;
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const auto size = input.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return input ? bytes : std::vector<std::uint8_t>{};
}

bool writeTextFile(const std::filesystem::path& path, std::string_view content) {
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(output);
}

bool writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& content) {
  std::error_code error;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(reinterpret_cast<const char*>(content.data()),
      static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(output);
}

bool fileExists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

bool directoryExists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error);
}

std::uint64_t fileSize(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  return error ? 0 : static_cast<std::uint64_t>(size);
}

std::vector<std::filesystem::path> listFiles(const std::filesystem::path& directory) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) return files;
  for (std::filesystem::recursive_directory_iterator iterator(directory, error), end;
       iterator != end; iterator.increment(error)) {
    if (error) break;
    if (!iterator->is_regular_file(error)) continue;
    files.push_back(iterator->path());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool removeRecursively(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  return !std::filesystem::exists(path, error);
}

std::string sanitiseFileName(const std::string& name) {
  std::string safe;
  safe.reserve(name.size());
  for (const char character : name) {
    switch (character) {
      case '\\': case '/': case ':': case '*': case '?':
      case '"': case '<': case '>': case '|': case '\0':
        safe.push_back('_');
        break;
      default:
        safe.push_back(character);
        break;
    }
  }
  if (safe.size() > 160) safe = safe.substr(safe.size() - 160);
  if (safe.empty() || safe == "." || safe == "..") return "unnamed";
  return safe;
}

std::filesystem::path joinPortable(const std::filesystem::path& base, std::string_view relative) {
  std::filesystem::path result = base;
  std::string component;
  component.reserve(relative.size());
  for (const char character : relative) {
    if (character == '/' || character == '\\') {
      if (!component.empty()) result /= component;
      component.clear();
      continue;
    }
    component.push_back(character);
  }
  if (!component.empty()) result /= component;
  return result;
}

} // namespace rechigros::platform
