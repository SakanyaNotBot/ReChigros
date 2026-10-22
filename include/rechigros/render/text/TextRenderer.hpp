#pragma once

#include "rechigros/render/gl/Texture.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rechigros::render::text {

// A single rasterised line of text with its own texture. Textures are cached by
// (text, pixel size, bold) because gameplay text changes only a few times per frame.
class TextTexture final {
public:
  gl::Texture texture;
  int width{};
  int height{};
  // Pixels of transparent padding added below the glyph baseline box.
  int paddingBottom{};
  float advanceWidth{};
  // Font metrics at the rasterisation size, in pixels: the distance from the baseline
  // up to the ascent line (positive) and down to the descent line (positive).
  float ascentPx{};
  float descentPx{};
  // Ink extents relative to the text origin on the baseline, in pixels: x grows right,
  // y grows down. The rasterised texture starts one pixel before the ink.
  float inkLeft{};
  float inkTop{};
  float inkRight{};
  float inkBottom{};
};

// Glyph atlas rasteriser backed by stb_truetype. Loads the bundled font once and
// keeps a per-size ASCII + Latin-1 advance cache.
class FontAtlas final {
public:
  FontAtlas() = default;
  ~FontAtlas();
  FontAtlas(const FontAtlas&) = delete;
  FontAtlas& operator=(const FontAtlas&) = delete;

  // Reads and parses the font file. Returns false when the file is unreadable or
  // the font data is invalid.
  bool load(const std::filesystem::path& path);
  [[nodiscard]] bool valid() const noexcept { return fontInfo_ != nullptr; }

  // Rasterises `text` at `pixelSize` into a tightly cropped RGBA texture.
  // A fake-bold pass thickens the glyphs when requested.
  [[nodiscard]] std::unique_ptr<TextTexture> rasterize(const std::string& text, float pixelSize,
      bool bold) const;

  // Horizontal advance of `text` in pixels at the given size.
  [[nodiscard]] float measure(const std::string& text, float pixelSize) const;

private:
  std::vector<std::uint8_t> fontData_;
  std::unique_ptr<struct FontInfo> fontInfo_;
};

// Owns the font plus a cache of rasterised strings.
class TextRenderer final {
public:
  TextRenderer();
  ~TextRenderer();
  TextRenderer(const TextRenderer&) = delete;
  TextRenderer& operator=(const TextRenderer&) = delete;

  bool initialize(const std::filesystem::path& assetRoot);
  void shutdown() noexcept;

  // Text texture for the given string; nullptr when the font failed to load.
  TextTexture* get(const std::string& text, float pixelSize, bool bold = false);

  [[nodiscard]] bool ready() const noexcept { return font_.valid(); }
  [[nodiscard]] float measure(const std::string& text, float pixelSize) const;

private:
  FontAtlas font_;
  std::vector<std::pair<std::string, std::unique_ptr<TextTexture>>> cache_;
  std::size_t cacheLimit_{512};
};

} // namespace rechigros::render::text
