#include "rechigros/platform/AssetManager.hpp"

#include <SDL.h>

#include <fstream>
#include <stdexcept>

namespace rechigros::platform {
namespace {
std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const auto size = input.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return input ? bytes : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> readSdlAsset(const std::string& path) {
  SDL_RWops* stream = SDL_RWFromFile(path.c_str(), "rb");
  if (!stream) return {};
  const auto size = SDL_RWsize(stream);
  if (size <= 0) { SDL_RWclose(stream); return {}; }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  const auto count = SDL_RWread(stream, bytes.data(), 1, bytes.size());
  SDL_RWclose(stream);
  if (count != bytes.size()) return {};
  return bytes;
}
} // namespace

std::vector<std::uint8_t> AssetManager::readBinary(const std::filesystem::path& root,
    std::string_view relative) {
  const auto relativePath = std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(relative.data()),
      reinterpret_cast<const char8_t*>(relative.data() + relative.size())));
  if (auto bytes = readFile(root / relativePath); !bytes.empty()) return bytes;
  auto generic = relativePath.generic_string();
  if (auto bytes = readSdlAsset(generic); !bytes.empty()) return bytes;
  return readSdlAsset("assets/" + generic);
}

std::filesystem::path AssetManager::materialize(const std::filesystem::path& root,
    std::string_view relative) {
  const auto relativePath = std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t*>(relative.data()),
      reinterpret_cast<const char8_t*>(relative.data() + relative.size())));
  const auto direct = root / relativePath;
  if (std::filesystem::is_regular_file(direct)) return direct;
  const auto bytes = readBinary(root, relative);
  if (bytes.empty()) return {};
  char* preference = SDL_GetPrefPath("com.wuying", "ReChigros");
  if (!preference) return {};
  const auto target = std::filesystem::path(preference) / "asset-cache" / relativePath;
  SDL_free(preference);
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) return {};
  if (std::filesystem::is_regular_file(target) && std::filesystem::file_size(target, error) == bytes.size()) return target;
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return output ? target : std::filesystem::path{};
}
} // namespace rechigros::platform

