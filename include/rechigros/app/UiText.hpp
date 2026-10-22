#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

// Per-glyph rasterisation shared by every interface screen. The interface draws text at
// many sizes; baking a separate atlas once and scaling it produces soft glyphs, so each
// (codepoint, size, weight) triple is rasterised once at its final pixel size into a shared
// atlas texture and drawn by the interface draw list.
namespace rechigros::app::ui {

class UiFontCache final {
public:
  // One rasterised glyph placed in the atlas. Offsets are in pixels relative to the pen
  // position on the baseline.
  struct Glyph {
    float u0{};
    float v0{};
    float u1{};
    float v1{};
    float width{};
    float height{};
    float offsetX{};
    float offsetY{};
    float advance{};
  };

  UiFontCache();
  ~UiFontCache();
  UiFontCache(const UiFontCache&) = delete;
  UiFontCache& operator=(const UiFontCache&) = delete;

  // Reads the bundled typeface and allocates the atlas texture. Safe to call without a
  // graphics context: it then reports not ready and the draw helpers fall back.
  bool initialize(const std::filesystem::path& assetRoot);
  void shutdown() noexcept;
  [[nodiscard]] bool ready() const noexcept;

  // Glyph for one codepoint at a pixel size. Returns nullptr when the cache cannot
  // rasterise (no context, missing font, exhausted atlas).
  const Glyph* glyph(std::uint32_t codepoint, float pixelSize, bool bold);
  float advance(std::uint32_t codepoint, float pixelSize, bool bold);
  float kerning(std::uint32_t left, std::uint32_t right, float pixelSize, bool bold) const;

  // Distances from the baseline in pixels, derived from the vertical font metrics.
  [[nodiscard]] float ascent(float pixelSize) const;
  [[nodiscard]] float descent(float pixelSize) const;
  [[nodiscard]] float lineGap(float pixelSize) const;

  [[nodiscard]] std::uint64_t textureId() const noexcept;

  // UTF-8 helpers shared by the text layout code.
  static std::uint32_t decode(std::string_view text, std::size_t index, std::size_t& length);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Process-wide cache used by the interface helpers.
UiFontCache& fontCache();

} // namespace rechigros::app::ui
