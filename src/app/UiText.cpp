#include "rechigros/app/UiText.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include "imstb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace rechigros::app::ui {
namespace {

// The atlas holds every glyph the interface shows at a given window size. Interface text
// is a few hundred distinct codepoints per screen at a handful of sizes, so this stays
// mostly empty unless a very large library list is scrolled quickly.
constexpr int kAtlasWidth = 2048;
constexpr int kAtlasHeight = 1024;
constexpr int kGlyphPadding = 1;

std::uint32_t quantiseSize(float pixelSize) {
  if (!(pixelSize > 0.0F)) return 1;
  const auto scaled = static_cast<std::uint32_t>(std::lround(pixelSize * 8.0F));
  return std::max<std::uint32_t>(1, scaled);
}

// Extra thickness applied on each side of a glyph for the bold weight. The interface uses a
// synthesised bold face, whose stroke width scales with the text size.
int boldRadius(float pixelSize) {
  return std::max(1, static_cast<int>(std::lround(pixelSize / 48.0F)));
}

struct Key {
  std::uint32_t codepoint{};
  std::uint32_t sizeKey{};
  std::uint32_t bold{};
  bool operator==(const Key& other) const noexcept {
    return codepoint == other.codepoint && sizeKey == other.sizeKey && bold == other.bold;
  }
};

struct KeyHash {
  std::size_t operator()(const Key& key) const noexcept {
    std::size_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint32_t value) {
      hash ^= value;
      hash *= 1099511628211ULL;
    };
    mix(key.codepoint);
    mix(key.sizeKey);
    mix(key.bold);
    return hash;
  }
};

} // namespace

struct UiFontCache::Impl {
  std::vector<std::uint8_t> fontData;
  stbtt_fontinfo info{};
  bool valid{};
  bool ready{};
  GLuint texture{};
  int shelfX{};
  int shelfY{};
  int rowHeight{};
  std::vector<std::uint8_t> scratch;
  std::unordered_map<Key, Glyph, KeyHash> glyphs;

  float emScale(float pixelSize) const {
    return stbtt_ScaleForMappingEmToPixels(&info, pixelSize);
  }

  const Glyph* rasterise(const Key& key, float pixelSize);
  void clearAtlas() {
    if (texture == 0 || !render::gl::contextAvailable()) return;
    std::vector<std::uint8_t> empty(
        static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasWidth, kAtlasHeight, GL_RGBA,
        GL_UNSIGNED_BYTE, empty.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    glyphs.clear();
    shelfX = 0;
    shelfY = 0;
    rowHeight = 0;
  }
};

UiFontCache::UiFontCache() : impl_(std::make_unique<Impl>()) {}

UiFontCache::~UiFontCache() { shutdown(); }

bool UiFontCache::initialize(const std::filesystem::path& assetRoot) {
  shutdown();
  impl_->fontData = platform::AssetManager::readBinary(assetRoot, "res/font.ttf");
  if (impl_->fontData.empty()) return false;
  const int offset = stbtt_GetFontOffsetForIndex(impl_->fontData.data(), 0);
  if (offset < 0 || stbtt_InitFont(&impl_->info, impl_->fontData.data(), offset) == 0) {
    impl_->fontData.clear();
    return false;
  }
  impl_->valid = true;
  if (!render::gl::contextAvailable()) return false;
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (texture == 0) return false;
  impl_->texture = texture;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasWidth, kAtlasHeight, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
  impl_->ready = true;
  return true;
}

void UiFontCache::shutdown() noexcept {
  if (impl_ == nullptr) return;
  if (impl_->texture != 0) {
    if (render::gl::contextAvailable()) glDeleteTextures(1, &impl_->texture);
    impl_->texture = 0;
  }
  impl_->glyphs.clear();
  impl_->fontData.clear();
  impl_->valid = false;
  impl_->ready = false;
}

bool UiFontCache::ready() const noexcept { return impl_ != nullptr && impl_->ready; }

std::uint64_t UiFontCache::textureId() const noexcept {
  return impl_ != nullptr ? static_cast<std::uint64_t>(impl_->texture) : 0;
}

std::uint32_t UiFontCache::decode(std::string_view text, std::size_t index, std::size_t& length) {
  length = 0;
  if (index >= text.size()) return 0;
  const auto lead = static_cast<unsigned char>(text[index]);
  if ((lead & 0x80U) == 0) {
    length = 1;
    return lead;
  }
  if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) {
    length = 2;
    return ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
  }
  if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) {
    length = 3;
    return ((lead & 0x0FU) << 12U)
        | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6U)
        | (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
  }
  if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) {
    length = 4;
    return ((lead & 0x07U) << 18U)
        | ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12U)
        | ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6U)
        | (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
  }
  length = 1;
  return lead;
}

float UiFontCache::ascent(float pixelSize) const {
  if (impl_ == nullptr || !impl_->valid) return pixelSize * 0.88F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(ascent) * impl_->emScale(pixelSize);
}

float UiFontCache::descent(float pixelSize) const {
  if (impl_ == nullptr || !impl_->valid) return pixelSize * 0.22F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(-descent) * impl_->emScale(pixelSize);
}

float UiFontCache::lineGap(float pixelSize) const {
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(lineGap) * impl_->emScale(pixelSize);
}

float UiFontCache::advance(std::uint32_t codepoint, float pixelSize, bool bold) {
  const Glyph* item = glyph(codepoint, pixelSize, bold);
  if (item != nullptr) return item->advance;
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  int advance = 0;
  int leftSideBearing = 0;
  stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(codepoint), &advance,
      &leftSideBearing);
  return static_cast<float>(advance) * impl_->emScale(pixelSize);
}

float UiFontCache::kerning(std::uint32_t left, std::uint32_t right, float pixelSize,
    bool bold) const {
  (void)bold;
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  const int kern = stbtt_GetCodepointKernAdvance(&impl_->info, static_cast<int>(left),
      static_cast<int>(right));
  if (kern == 0) return 0.0F;
  return static_cast<float>(kern) * impl_->emScale(pixelSize);
}

const UiFontCache::Glyph* UiFontCache::glyph(std::uint32_t codepoint, float pixelSize,
    bool bold) {
  if (impl_ == nullptr || !impl_->valid || !impl_->ready) return nullptr;
  if (!(pixelSize > 0.0F)) return nullptr;
  Key key;
  key.codepoint = codepoint;
  key.sizeKey = quantiseSize(pixelSize);
  key.bold = bold ? 1U : 0U;
  const auto found = impl_->glyphs.find(key);
  if (found != impl_->glyphs.end()) return &found->second;
  return impl_->rasterise(key, pixelSize);
}

const UiFontCache::Glyph* UiFontCache::Impl::rasterise(const Key& key, float pixelSize) {
  const auto codepoint = static_cast<int>(key.codepoint);
  const float scale = emScale(pixelSize);
  int advance = 0;
  int leftSideBearing = 0;
  stbtt_GetCodepointHMetrics(&info, codepoint, &advance, &leftSideBearing);
  Glyph result;
  result.advance = static_cast<float>(advance) * scale;

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  stbtt_GetCodepointBitmapBox(&info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
  const int glyphWidth = x1 - x0;
  const int glyphHeight = y1 - y0;
  if (glyphWidth <= 0 || glyphHeight <= 0) {
    // Whitespace and other empty glyphs still occupy an advance.
    result.width = 0.0F;
    result.height = 0.0F;
    const auto inserted = glyphs.emplace(key, result);
    return &inserted.first->second;
  }

  std::vector<std::uint8_t> bitmap(
      static_cast<std::size_t>(glyphWidth) * static_cast<std::size_t>(glyphHeight), 0);
  stbtt_MakeCodepointBitmap(&info, bitmap.data(), glyphWidth, glyphHeight, glyphWidth, scale,
      scale, codepoint);

  // Synthesised bold: dilate the coverage by a size-proportional radius, which is how a
  // stroked pass thickens a glyph without a second face in the font file.
  int radius = 0;
  if (key.bold != 0) radius = boldRadius(pixelSize);
  const int width = glyphWidth + radius * 2;
  const int height = glyphHeight + radius * 2;
  std::vector<std::uint8_t> expanded(static_cast<std::size_t>(width) * height, 0);
  for (int row = 0; row < glyphHeight; ++row) {
    for (int column = 0; column < glyphWidth; ++column) {
      const int value = bitmap[static_cast<std::size_t>(row) * glyphWidth + column];
      if (value == 0) continue;
      for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
        for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
          if (offsetX * offsetX + offsetY * offsetY > radius * radius) continue;
          const int targetX = column + offsetX + radius;
          const int targetY = row + offsetY + radius;
          auto& target = expanded[static_cast<std::size_t>(targetY) * width + targetX];
          if (value > target) target = static_cast<std::uint8_t>(value);
        }
      }
    }
  }

  // Shelf packing. The atlas is cleared when it runs out of room, which only happens when
  // a very large amount of distinct text is drawn at once.
  if (shelfX + width + kGlyphPadding > kAtlasWidth) {
    shelfX = 0;
    shelfY += rowHeight + kGlyphPadding;
    rowHeight = 0;
  }
  if (shelfY + height + kGlyphPadding > kAtlasHeight) clearAtlas();

  const int targetX = shelfX;
  const int targetY = shelfY;
  scratch.assign(static_cast<std::size_t>(width) * height * 4, 0);
  for (std::size_t index = 0; index < expanded.size(); ++index) {
    const std::uint8_t coverage = expanded[index];
    scratch[index * 4 + 0] = 255;
    scratch[index * 4 + 1] = 255;
    scratch[index * 4 + 2] = 255;
    scratch[index * 4 + 3] = coverage;
  }
  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, targetX, targetY, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
      scratch.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  result.width = static_cast<float>(width);
  result.height = static_cast<float>(height);
  result.offsetX = static_cast<float>(x0 - radius);
  result.offsetY = static_cast<float>(y0 - radius);
  result.u0 = static_cast<float>(targetX) / static_cast<float>(kAtlasWidth);
  result.v0 = static_cast<float>(targetY) / static_cast<float>(kAtlasHeight);
  result.u1 = static_cast<float>(targetX + width) / static_cast<float>(kAtlasWidth);
  result.v1 = static_cast<float>(targetY + height) / static_cast<float>(kAtlasHeight);

  shelfX += width + kGlyphPadding;
  rowHeight = std::max(rowHeight, height);
  const auto inserted = glyphs.emplace(key, result);
  return &inserted.first->second;
}

UiFontCache& fontCache() {
  static UiFontCache cache;
  return cache;
}

} // namespace rechigros::app::ui
