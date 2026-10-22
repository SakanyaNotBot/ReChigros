#pragma once

#include "rechigros/render/gl/Texture.hpp"

#include <array>
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

// One glyph of a cached character strip.
struct GlyphStripGlyph {
  std::uint32_t codepoint{};
  // Raw bitmap box in pixels at the strip's pixel size.
  int bitmapX0{};
  int bitmapY0{};
  int bitmapX1{};
  int bitmapY1{};
  // Rect of the glyph inside the strip texture.
  int stripX{};
  int stripY{};
  int width{};
  int height{};
  // Pen advance of the character in pixels (kerning is applied per string when composing, so
  // the result matches the full-string rasteriser).
  float advance{};
};

// A character set rasterised once per pixel size into one texture. The HUD composes changing
// numbers from this strip instead of rasterising a new string texture for every value, which
// removes the per-hit allocation and texture-upload churn.
struct GlyphStrip {
  gl::Texture texture;
  int width{};
  int height{};
  float pixelSize{};
  float ascentPx{};
  float descentPx{};
  std::vector<GlyphStripGlyph> glyphs;
};

// Metrics of a string composed from a strip; identical to what the rasteriser reports for the
// same string (same pen/kerning math, same rounding, same one-pixel ink border).
struct ComposedTextMetrics {
  float advanceWidth{};
  float inkLeft{};
  float inkTop{};
  float inkRight{};
  float inkBottom{};
  float ascentPx{};
  float descentPx{};
  int textureWidth{};
  int textureHeight{};
};

// One glyph of a composed string: source rect in the strip texture and its top-left pixel in
// the composed (virtual) string texture.
struct ComposedGlyphPlacement {
  int sourceX{};
  int sourceY{};
  int width{};
  int height{};
  int targetX{};
  int targetY{};
};

inline constexpr int kMaxComposedGlyphs = 32;

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

  // Rasterises a character set into one strip texture, recording each glyph's bitmap box and
  // placement. Used once per text size by the HUD number composer.
  [[nodiscard]] std::unique_ptr<GlyphStrip> rasterizeStrip(const std::string& characters,
      float pixelSize) const;

  // Lays out `text` from the strip's glyphs and reports the same metrics the full-string
  // rasteriser would report. Returns false when a character is missing from the strip or the
  // text is too long; no heap allocation happens in either case.
  [[nodiscard]] bool layoutStrip(const GlyphStrip& strip, const std::string& text,
      ComposedTextMetrics& metrics,
      std::array<ComposedGlyphPlacement, kMaxComposedGlyphs>& placements,
      int& placementCount) const;

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

  // Cached glyph strip for number-like HUD labels at the given size; rasterised on first use
  // and reused afterwards. Null when the font failed to load.
  [[nodiscard]] const GlyphStrip* glyphStrip(float pixelSize);

  // Metrics and per-glyph placement for composing `text` from the strip. Returns the strip
  // the placements refer to, or null when the text contains a character the strip does not
  // cover (the caller then falls back to `get`).
  [[nodiscard]] const GlyphStrip* compose(const std::string& text, float pixelSize,
      ComposedTextMetrics& metrics,
      std::array<ComposedGlyphPlacement, kMaxComposedGlyphs>& placements, int& placementCount);

  [[nodiscard]] bool ready() const noexcept { return font_.valid(); }
  [[nodiscard]] float measure(const std::string& text, float pixelSize) const;

private:
  struct CacheEntry {
    std::string text;
    float pixelSize{};
    bool bold{};
    std::unique_ptr<TextTexture> texture;
  };
  struct StripEntry {
    float pixelSize{};
    std::unique_ptr<GlyphStrip> strip;
  };

  FontAtlas font_;
  std::vector<CacheEntry> cache_;
  std::vector<StripEntry> strips_;
  std::size_t cacheLimit_{512};
  std::size_t stripLimit_{8};
};

} // namespace rechigros::render::text
