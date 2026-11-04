#include "rechigros/platform/Gif.hpp"

#include <algorithm>
#include <cstring>

namespace rechigros::platform {
namespace {

// Frame delays below this are raised so a stream cannot stall on a zero-delay frame.
constexpr int kMinimumDelayMs = 10;
// A hard cap on decoded frames keeps a hostile stream from exhausting memory.
constexpr std::size_t kMaximumFrames = 4096;
constexpr int kMaximumCodes = 4096;

class Reader final {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool atEnd() const noexcept { return position_ >= bytes_.size(); }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - std::min(position_, bytes_.size());
  }
  std::uint8_t readByte() noexcept {
    return position_ < bytes_.size() ? bytes_[position_++] : 0;
  }
  std::uint16_t readShort() noexcept {
    const auto low = static_cast<std::uint16_t>(readByte());
    const auto high = static_cast<std::uint16_t>(readByte());
    return static_cast<std::uint16_t>(low | (high << 8));
  }
  [[nodiscard]] std::span<const std::uint8_t> view(std::size_t count) noexcept {
    if (count > remaining()) count = remaining();
    const auto slice = bytes_.subspan(position_, count);
    position_ += count;
    return slice;
  }
  bool skip(std::size_t count) noexcept {
    if (count > remaining()) return false;
    position_ += count;
    return true;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{};
};

// Delivers LSB-first bit fields for the LZW stream.
class BitReader final {
public:
  explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  // Reads `count` bits (at most 12). Returns false once the data runs out.
  bool read(int count, int& value) noexcept {
    while (bitCount_ < count) {
      if (bytePosition_ >= bytes_.size()) return false;
      buffer_ |= static_cast<std::uint32_t>(bytes_[bytePosition_++]) << bitCount_;
      bitCount_ += 8;
    }
    value = static_cast<int>(buffer_ & ((1U << count) - 1U));
    buffer_ >>= count;
    bitCount_ -= count;
    return true;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::uint32_t buffer_{};
  int bitCount_{};
  std::size_t bytePosition_{};
};

struct ColorTable {
  std::vector<std::uint8_t> rgb;  // Three bytes per entry.
  [[nodiscard]] std::size_t size() const noexcept { return rgb.size() / 3; }
  void read(Reader& reader, int entries) {
    rgb.assign(static_cast<std::size_t>(entries) * 3, 0);
    const auto data = reader.view(rgb.size());
    std::copy(data.begin(), data.end(), rgb.begin());
  }
};

void writePixel(std::vector<std::uint8_t>& rgba, int width, int x, int y, std::uint8_t red,
    std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) noexcept {
  const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)) * 4;
  if (offset + 3 >= rgba.size()) return;
  rgba[offset] = red;
  rgba[offset + 1] = green;
  rgba[offset + 2] = blue;
  rgba[offset + 3] = alpha;
}

// Row order of an interlaced image: four passes over the rows.
constexpr int kInterlaceStart[4] = {0, 4, 2, 1};
constexpr int kInterlaceStep[4] = {8, 8, 4, 2};

// Decodes one LZW-compressed image into colour indices. `expected` is the pixel count.
bool decodeIndices(std::span<const std::uint8_t> compressed, int minimumCodeSize,
    std::size_t expected, std::vector<std::uint8_t>& indices) {
  if (minimumCodeSize < 1 || minimumCodeSize > 11) return false;
  indices.assign(expected, 0);

  const int clearCode = 1 << minimumCodeSize;
  const int endCode = clearCode + 1;
  int codeSize = minimumCodeSize + 1;
  int nextCode = endCode + 1;

  // Dictionary entries are (prefix, suffix, length); a string is expanded by walking the
  // prefix chain backwards.
  std::vector<std::int32_t> prefix(kMaximumCodes, -1);
  std::vector<std::uint8_t> suffix(kMaximumCodes, 0);
  std::vector<std::int32_t> length(kMaximumCodes, 1);
  for (int code = 0; code < clearCode; ++code) {
    suffix[static_cast<std::size_t>(code)] = static_cast<std::uint8_t>(code);
  }

  std::vector<std::uint8_t> expansion;
  BitReader bits(compressed);
  int previousCode = -1;
  int code = 0;
  std::size_t written = 0;

  while (bits.read(codeSize, code)) {
    if (code == clearCode) {
      codeSize = minimumCodeSize + 1;
      nextCode = endCode + 1;
      previousCode = -1;
      continue;
    }
    if (code == endCode) break;

    expansion.clear();
    if (code < nextCode && code < kMaximumCodes && length[static_cast<std::size_t>(code)] > 0) {
      for (int walk = code; walk >= 0; walk = prefix[static_cast<std::size_t>(walk)]) {
        expansion.push_back(suffix[static_cast<std::size_t>(walk)]);
      }
      std::reverse(expansion.begin(), expansion.end());
    } else if (previousCode >= 0) {
      // The deferred case: the code is one past the current table, so it expands to the
      // previous string followed by its own first byte.
      for (int walk = previousCode; walk >= 0; walk = prefix[static_cast<std::size_t>(walk)]) {
        expansion.push_back(suffix[static_cast<std::size_t>(walk)]);
      }
      std::reverse(expansion.begin(), expansion.end());
      if (expansion.empty()) break;
      expansion.push_back(expansion.front());
    } else {
      break;
    }

    for (const std::uint8_t value : expansion) {
      if (written < indices.size()) indices[written++] = value;
    }
    if (written >= indices.size()) break;

    if (previousCode >= 0 && nextCode < kMaximumCodes) {
      prefix[static_cast<std::size_t>(nextCode)] = previousCode;
      suffix[static_cast<std::size_t>(nextCode)] = expansion.front();
      length[static_cast<std::size_t>(nextCode)] =
          length[static_cast<std::size_t>(previousCode)] + 1;
      ++nextCode;
      // The code width grows once the table no longer fits in it.
      if (nextCode == (1 << codeSize) && codeSize < 12) ++codeSize;
    }
    previousCode = code;
  }
  return true;
}

} // namespace

bool decodeGif(std::span<const std::uint8_t> bytes, GifImage& out) {
  out = GifImage{};
  if (bytes.size() < 13) return false;
  Reader reader(bytes);

  const auto signature = reader.view(6);
  if (signature.size() < 6) return false;
  const bool isGif = std::memcmp(signature.data(), "GIF87a", 6) == 0 ||
      std::memcmp(signature.data(), "GIF89a", 6) == 0;
  if (!isGif) return false;

  const int screenWidth = reader.readShort();
  const int screenHeight = reader.readShort();
  const std::uint8_t packed = reader.readByte();
  reader.readByte();  // Background colour index; frames are composited, so it is unused.
  reader.readByte();  // Pixel aspect ratio; unused.

  if (screenWidth <= 0 || screenHeight <= 0) return false;
  if (screenWidth > 16384 || screenHeight > 16384) return false;
  out.width = screenWidth;
  out.height = screenHeight;

  ColorTable globalTable;
  if ((packed & 0x80U) != 0) globalTable.read(reader, 2 << (packed & 0x07U));

  // The canvas carries alpha so transparent pixels stay transparent. It is the state the
  // next frame is composed onto, and it survives across frames.
  const auto canvasBytes = static_cast<std::size_t>(screenWidth) *
      static_cast<std::size_t>(screenHeight) * 4;
  std::vector<std::uint8_t> canvas(canvasBytes, 0);
  std::vector<std::uint8_t> canvasBeforeFrame;
  std::vector<std::uint8_t> indices;

  // Graphic control state that applies to the next image block.
  int pendingDelayMs = 100;
  int pendingTransparentIndex = -1;
  int pendingDisposal = 0;

  while (!reader.atEnd() && out.frames.size() < kMaximumFrames) {
    const std::uint8_t block = reader.readByte();
    if (block == 0x3B) break;  // Trailer.

    if (block == 0x21) {  // Extension.
      if (reader.readByte() == 0xF9) {  // Graphic control.
        const std::uint8_t size = reader.readByte();
        const auto data = reader.view(size);
        if (data.size() >= 4U) {
          pendingDisposal = (data[0] >> 2) & 0x07;
          pendingTransparentIndex = (data[0] & 0x01U) != 0 ? data[3] : -1;
          pendingDelayMs = static_cast<int>(data[1]) | (static_cast<int>(data[2]) << 8);
        }
        while (!reader.atEnd() && reader.readByte() != 0x00) {
        }
      } else {
        // Skip every sub-block of any other extension.
        while (!reader.atEnd()) {
          const std::uint8_t size = reader.readByte();
          if (size == 0x00) break;
          if (!reader.skip(size)) break;
        }
      }
      continue;
    }

    if (block != 0x2C) break;  // Anything else is corrupt.

    const int left = reader.readShort();
    const int top = reader.readShort();
    const int width = reader.readShort();
    const int height = reader.readShort();
    const std::uint8_t imagePacked = reader.readByte();
    if (width <= 0 || height <= 0) break;
    if (left + width > screenWidth || top + height > screenHeight) break;

    ColorTable localTable;
    const ColorTable* table = &globalTable;
    if ((imagePacked & 0x80U) != 0) {
      localTable.read(reader, 2 << (imagePacked & 0x07U));
      table = &localTable;
    }
    if (table->size() == 0) break;

    const int minimumCodeSize = reader.readByte();
    std::vector<std::uint8_t> compressed;
    while (!reader.atEnd()) {
      const std::uint8_t size = reader.readByte();
      if (size == 0x00) break;
      const auto chunk = reader.view(size);
      compressed.insert(compressed.end(), chunk.begin(), chunk.end());
    }

    if (!decodeIndices(compressed, minimumCodeSize,
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height), indices)) {
      break;
    }

    // Disposal 3 restores the canvas to how it looked before this frame was drawn.
    if (pendingDisposal == 3) canvasBeforeFrame = canvas;

    const bool interlaced = (imagePacked & 0x40U) != 0;
    int pass = 0;
    int rowInPass = kInterlaceStart[0];
    for (int row = 0; row < height; ++row) {
      int targetRow = row;
      if (interlaced) {
        targetRow = rowInPass;
        rowInPass += kInterlaceStep[pass];
        if (rowInPass >= height && pass < 3) {
          ++pass;
          rowInPass = kInterlaceStart[pass];
        }
        if (targetRow >= height) break;
      }
      for (int column = 0; column < width; ++column) {
        const auto sourceIndex = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(column);
        if (sourceIndex >= indices.size()) continue;
        const int colorIndex = indices[sourceIndex];
        if (colorIndex == pendingTransparentIndex) continue;
        if (static_cast<std::size_t>(colorIndex) >= table->size()) continue;
        const auto tableOffset = static_cast<std::size_t>(colorIndex) * 3;
        writePixel(canvas, screenWidth, left + column, top + targetRow, table->rgb[tableOffset],
            table->rgb[tableOffset + 1], table->rgb[tableOffset + 2], 255);
      }
    }

    GifFrame frame;
    frame.rgba = canvas;
    // A zero delay is treated as the conventional tenth of a second so playback advances.
    frame.delayMs = std::max(pendingDelayMs > 0 ? pendingDelayMs : 100, kMinimumDelayMs);
    out.totalDurationMs += frame.delayMs;
    out.frames.push_back(std::move(frame));

    // Apply the disposal method before the next frame is composed.
    if (pendingDisposal == 2) {
      // Clear the image rectangle to the background, which is transparent here.
      for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
          writePixel(canvas, screenWidth, left + column, top + row, 0, 0, 0, 0);
        }
      }
    } else if (pendingDisposal == 3 && !canvasBeforeFrame.empty()) {
      canvas = canvasBeforeFrame;
    }

    pendingDelayMs = 100;
    pendingTransparentIndex = -1;
    pendingDisposal = 0;
  }

  return !out.frames.empty();
}

} // namespace rechigros::platform
