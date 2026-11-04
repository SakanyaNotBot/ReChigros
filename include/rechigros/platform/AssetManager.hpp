#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace rechigros::platform {
class AssetManager final {
public:
  static std::vector<std::uint8_t> readBinary(const std::filesystem::path& assetRoot,
      std::string_view relativePath);
  static std::filesystem::path materialize(const std::filesystem::path& assetRoot,
      std::string_view relativePath);
};
}

