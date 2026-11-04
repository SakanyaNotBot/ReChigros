#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rechigros::platform {

// One fully composited animation frame. Every frame is the size of the logical screen,
// so a consumer can upload it directly.
struct GifFrame {
  std::vector<std::uint8_t> rgba;
  int delayMs{100};
};

struct GifImage {
  int width{};
  int height{};
  std::vector<GifFrame> frames;
  int totalDurationMs{};
};

// Decodes a GIF87a/GIF89a stream into fully composited frames. Disposal methods, the
// transparent index, local colour tables and interlaced images are all handled. Returns
// false when the stream is not a decodable GIF.
[[nodiscard]] bool decodeGif(std::span<const std::uint8_t> bytes, GifImage& out);

} // namespace rechigros::platform
