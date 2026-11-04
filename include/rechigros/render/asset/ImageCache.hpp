#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace rechigros::render::asset {

struct Image {
  int width{};
  int height{};
  std::vector<std::uint8_t> pixels;
  bool premultiplied{};
  // Premultiplied gray and alpha pairs, expanded to RGBA on upload.
  bool dualChannel{};
  [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0 && !pixels.empty(); }
};

[[nodiscard]] Image decodeImage(const std::filesystem::path& path);
[[nodiscard]] Image decodeImageMemory(const std::uint8_t* data, std::size_t size);
// Creates the fast, lossless sidecar for a large image, or reuses the existing one.
bool prepareImageCache(const std::filesystem::path& path);
void forceWhite(Image& image);
void premultiply(Image& image);
void optimizeChannels(Image& image);
void expandChannels(Image& image);

} // namespace rechigros::render::asset
