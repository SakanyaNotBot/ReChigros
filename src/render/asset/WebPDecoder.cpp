// WebP decoding is provided by Wuffs' single-file C release. Some chart packs carry WebP
// images under a .png name, so the extension-based decoder in ImageCache rejects them even
// though the bytes are a valid illustration.
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__VP8
#define WUFFS_CONFIG__MODULE__WEBP
#include <wuffs-v0.4.c>

#include "WebPDecoder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace rechigros::render::asset {
namespace {

bool isWebp(const std::uint8_t* data, std::size_t size) {
  return data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0
      && std::memcmp(data + 8, "WEBP", 4) == 0;
}

} // namespace

Image decodeWebpMemory(const std::uint8_t* data, std::size_t size) {
  Image image;
  if (!isWebp(data, size)) return image;

  std::unique_ptr<wuffs_webp__decoder, decltype(&std::free)> decoder(
      wuffs_webp__decoder__alloc(), &std::free);
  if (!decoder) return image;

  wuffs_base__io_buffer source = wuffs_base__ptr_u8__reader(
      const_cast<std::uint8_t*>(data), size, true);
  wuffs_base__image_config config{};
  wuffs_base__status status =
      wuffs_webp__decoder__decode_image_config(decoder.get(), &config, &source);
  if (status.repr != nullptr) return image;

  const std::uint32_t width = wuffs_base__pixel_config__width(&config.pixcfg);
  const std::uint32_t height = wuffs_base__pixel_config__height(&config.pixcfg);
  if (width == 0 || height == 0 || width > 0xFFFFFFU || height > 0xFFFFFFU) return image;
  const std::uint64_t pixelBytes =
      static_cast<std::uint64_t>(width) * height * 4U;
  if (pixelBytes > static_cast<std::uint64_t>(SIZE_MAX)) return image;

  const wuffs_base__range_ii_u64 workRange =
      wuffs_webp__decoder__workbuf_len(decoder.get());
  if (wuffs_base__range_ii_u64__is_empty(&workRange)) return image;
  std::vector<std::uint8_t> work(static_cast<std::size_t>(workRange.max_incl));
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(pixelBytes));

  wuffs_base__pixel_config pixelConfig = wuffs_base__null_pixel_config();
  wuffs_base__pixel_config__set(&pixelConfig, WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
      WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, width, height);
  wuffs_base__pixel_buffer pixelBuffer = wuffs_base__null_pixel_buffer();
  status = wuffs_base__pixel_buffer__set_from_slice(&pixelBuffer, &pixelConfig,
      wuffs_base__make_slice_u8(pixels.data(), pixels.size()));
  if (status.repr != nullptr) return image;

  status = wuffs_webp__decoder__decode_frame(decoder.get(), &pixelBuffer, &source,
      WUFFS_BASE__PIXEL_BLEND__SRC,
      wuffs_base__make_slice_u8(work.data(), work.size()), nullptr);
  if (status.repr != nullptr) return image;

  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.pixels = std::move(pixels);
  return image;
}

} // namespace rechigros::render::asset
