#include "rechigros/app/UiText.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include "imstb_truetype.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace rechigros::app::ui {
namespace {

// The atlas holds every glyph the interface shows at a given window size. Interface text
// is a few hundred distinct codepoints per screen at a handful of sizes, so this stays
// mostly empty unless a very large library list is scrolled quickly.
constexpr int kAtlasInitialWidth = 2048;
constexpr int kAtlasInitialHeight = 1024;
// Upper bound on growth. A larger atlas than this is not a plausible amount of distinct text
// for this interface, so growth stops and the atlas falls back to clearing.
constexpr int kAtlasMaxWidth = 8192;
constexpr int kAtlasMaxHeight = 8192;
constexpr int kGlyphPadding = 1;
// Coverage is accumulated from 4x4 subpixel positions. A single stb_truetype sample uses the
// integer grid inside the glyph, which leaves stems and diagonal joins visibly soft at the
// sizes used by the interface; the same multisample coverage the gameplay text uses keeps the
// antialiased edge aligned with the requested pixel size.
constexpr int kSubpixelsX = 4;
constexpr int kSubpixelsY = 4;

// Glyph boxes and pen advances are integer pixels. Rasterising a fractional pixel size and then
// placing the result on the integer device grid scales the atlas by a fraction of a pixel, which
// softens every stem and corner. The interface therefore snaps each requested size to the nearest
// device pixel before it computes metrics or rasterises the glyph.
float rasterPixelSize(float pixelSize) {
  if (!(pixelSize > 0.0F)) return 1.0F;
  return std::max(1.0F, std::round(pixelSize));
}

std::uint32_t quantiseSize(float pixelSize) {
  const auto scaled = static_cast<std::uint32_t>(std::lround(rasterPixelSize(pixelSize) * 8.0F));
  return std::max<std::uint32_t>(1, scaled);
}

// Extra coverage applied to a glyph for the bold weight, as a fraction of its own ink. The font
// carries no bold face, so the weight is synthesised; a fraction keeps the stroke proportional at
// every size, where a whole-pixel dilation is a third of a stroke at interface text sizes.
float boldWeight(float pixelSize) {
  // A stroke is roughly a twelfth of the em, so the same absolute thickening is a larger share of
  // a small glyph: the weight grows as the glyph shrinks, keeping the perceived contrast even.
  const float reference = 16.0F;
  return std::clamp(0.55F * reference / std::max(1.0F, pixelSize), 0.18F, 0.55F);
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
  std::vector<std::uint8_t> fallbackData;
  stbtt_fontinfo fallbackInfo{};
  bool fallbackValid{};
  bool valid{};
  bool ready{};
  GLuint texture{};
  int shelfX{};
  int shelfY{};
  int rowHeight{};
  // Current atlas dimensions. They start at the initial size and grow when the packed text
  // outgrows them; every cached glyph records UVs against these, so a growth that moved
  // glyphs would invalidate them, and growth therefore only extends the shelf space.
  int atlasWidth{kAtlasInitialWidth};
  int atlasHeight{kAtlasInitialHeight};
  std::vector<std::uint8_t> scratch;
  std::unordered_map<Key, Glyph, KeyHash> glyphs;

  const stbtt_fontinfo* fontFor(std::uint32_t codepoint) const {
    if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) > 0) return &info;
    if (fallbackValid &&
        stbtt_FindGlyphIndex(&fallbackInfo, static_cast<int>(codepoint)) > 0) {
      return &fallbackInfo;
    }
    return &info;
  }

  static float emScale(const stbtt_fontinfo* font, float pixelSize) {
    return stbtt_ScaleForMappingEmToPixels(font, pixelSize);
  }

  const Glyph* rasterise(const Key& key, float pixelSize);

  // Allocates a new, larger atlas texture and clears it. Growth doubles the smaller of the
  // two dimensions so the shelf area keeps up with the glyph count; it stops at the maximum.
  bool reallocateAtlas(int width, int height) {
    if (texture == 0 || !render::gl::contextAvailable()) return false;
    if (width <= 0 || height <= 0) return false;
    std::vector<std::uint8_t> empty(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
        empty.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    atlasWidth = width;
    atlasHeight = height;
    shelfX = 0;
    shelfY = 0;
    rowHeight = 0;
    // Every glyph's recorded UVs are relative to the previous size, so they all have to be
    // re-rasterised into the new one. The entries keep their identity, so pointers handed out
    // earlier stay valid and simply receive their new coordinates.
    const auto cached = std::move(glyphs);
    glyphs.clear();
    for (const auto& [key, glyph] : cached) {
      (void)glyph;
      rasterise(key, static_cast<float>(key.sizeKey) / 8.0F);
    }
    return true;
  }

  // Grows the atlas by one doubling step. Returns false when it is already at the maximum and
  // the caller has to fall back to discarding the contents.
  bool growAtlas() {
    if (atlasWidth >= kAtlasMaxWidth && atlasHeight >= kAtlasMaxHeight) return false;
    int width = atlasWidth;
    int height = atlasHeight;
    if (width <= height && width < kAtlasMaxWidth) {
      width *= 2;
    } else if (height < kAtlasMaxHeight) {
      height *= 2;
    } else {
      return false;
    }
    return reallocateAtlas(std::min(width, kAtlasMaxWidth), std::min(height, kAtlasMaxHeight));
  }

  void clearAtlas() {
    if (!reallocateAtlas(atlasWidth, atlasHeight)) return;
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
  const auto fallbackPath = findSymbolFontPath();
  if (!fallbackPath.empty()) {
    std::error_code error;
    const auto size = std::filesystem::file_size(fallbackPath, error);
    if (!error && size > 0) {
      impl_->fallbackData.resize(static_cast<std::size_t>(size));
      std::ifstream input(fallbackPath, std::ios::binary);
      if (input.read(reinterpret_cast<char*>(impl_->fallbackData.data()),
              static_cast<std::streamsize>(size))) {
        const int fallbackOffset =
            stbtt_GetFontOffsetForIndex(impl_->fallbackData.data(), 0);
        impl_->fallbackValid = fallbackOffset >= 0 &&
            stbtt_InitFont(&impl_->fallbackInfo, impl_->fallbackData.data(), fallbackOffset) != 0;
      }
      if (!impl_->fallbackValid) impl_->fallbackData.clear();
    }
  }
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, impl_->atlasWidth, impl_->atlasHeight, 0, GL_RGBA,
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
  impl_->fallbackData.clear();
  impl_->fallbackValid = false;
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
  pixelSize = rasterPixelSize(pixelSize);
  if (impl_ == nullptr || !impl_->valid) return pixelSize * 0.88F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(ascent) * Impl::emScale(&impl_->info, pixelSize);
}

float UiFontCache::descent(float pixelSize) const {
  pixelSize = rasterPixelSize(pixelSize);
  if (impl_ == nullptr || !impl_->valid) return pixelSize * 0.22F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(-descent) * Impl::emScale(&impl_->info, pixelSize);
}

float UiFontCache::lineGap(float pixelSize) const {
  pixelSize = rasterPixelSize(pixelSize);
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
  return static_cast<float>(lineGap) * Impl::emScale(&impl_->info, pixelSize);
}

float UiFontCache::advance(std::uint32_t codepoint, float pixelSize, bool bold) {
  const Glyph* item = glyph(codepoint, pixelSize, bold);
  if (item != nullptr) return item->advance;
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  const float size = rasterPixelSize(pixelSize);
  const stbtt_fontinfo* font = impl_->fontFor(codepoint);
  int advance = 0;
  int leftSideBearing = 0;
  stbtt_GetCodepointHMetrics(font, static_cast<int>(codepoint), &advance, &leftSideBearing);
  return static_cast<float>(advance) * Impl::emScale(font, size);
}

float UiFontCache::kerning(std::uint32_t left, std::uint32_t right, float pixelSize,
    bool bold) const {
  (void)bold;
  if (impl_ == nullptr || !impl_->valid) return 0.0F;
  const stbtt_fontinfo* font = impl_->fontFor(left);
  if (font != impl_->fontFor(right)) return 0.0F;
  const int kern = stbtt_GetCodepointKernAdvance(font, static_cast<int>(left),
      static_cast<int>(right));
  if (kern == 0) return 0.0F;
  return static_cast<float>(kern) * Impl::emScale(font, rasterPixelSize(pixelSize));
}

const UiFontCache::Glyph* UiFontCache::glyph(std::uint32_t codepoint, float pixelSize,
    bool bold) {
  if (impl_ == nullptr || !impl_->valid || !impl_->ready) return nullptr;
  if (!(pixelSize > 0.0F)) return nullptr;
  const float size = rasterPixelSize(pixelSize);
  Key key;
  key.codepoint = codepoint;
  key.sizeKey = quantiseSize(size);
  key.bold = bold ? 1U : 0U;
  const auto found = impl_->glyphs.find(key);
  if (found != impl_->glyphs.end()) return &found->second;
  return impl_->rasterise(key, size);
}

const UiFontCache::Glyph* UiFontCache::Impl::rasterise(const Key& key, float pixelSize) {
  pixelSize = static_cast<float>(key.sizeKey) / 8.0F;
  const auto codepoint = static_cast<int>(key.codepoint);
  const stbtt_fontinfo* font = fontFor(key.codepoint);
  const float scale = emScale(font, pixelSize);
  int advance = 0;
  int leftSideBearing = 0;
  stbtt_GetCodepointHMetrics(font, codepoint, &advance, &leftSideBearing);
  Glyph result;
  result.advance = static_cast<float>(advance) * scale;

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  stbtt_GetCodepointBitmapBox(font, codepoint, scale, scale, &x0, &y0, &x1, &y1);
  const int glyphWidth = x1 - x0;
  const int glyphHeight = y1 - y0;
  if (glyphWidth <= 0 || glyphHeight <= 0) {
    // Whitespace and other empty glyphs still occupy an advance.
    result.width = 0.0F;
    result.height = 0.0F;
    const auto inserted = glyphs.emplace(key, result);
    return &inserted.first->second;
  }

  std::vector<float> coverage(
      static_cast<std::size_t>(glyphWidth) * static_cast<std::size_t>(glyphHeight), 0.0F);
  std::vector<std::uint8_t> bitmap(
      static_cast<std::size_t>(glyphWidth) * static_cast<std::size_t>(glyphHeight), 0);
  constexpr float sampleWeight = 1.0F / (kSubpixelsX * kSubpixelsY * 255.0F);
  for (int subY = 0; subY < kSubpixelsY; ++subY) {
    for (int subX = 0; subX < kSubpixelsX; ++subX) {
      const float shiftX = static_cast<float>(subX) / kSubpixelsX;
      const float shiftY = static_cast<float>(subY) / kSubpixelsY;
      stbtt_MakeCodepointBitmapSubpixel(font, bitmap.data(), glyphWidth, glyphHeight, glyphWidth,
          scale, scale, shiftX, shiftY, codepoint);
      for (std::size_t index = 0; index < bitmap.size(); ++index) {
        coverage[index] += static_cast<float>(bitmap[index]) * sampleWeight;
      }
    }
  }
  for (std::size_t index = 0; index < bitmap.size(); ++index) {
    bitmap[index] = static_cast<std::uint8_t>(
        std::clamp(coverage[index] * 255.0F + 0.5F, 0.0F, 255.0F));
  }

  // Synthesised bold. Stamping the glyph at whole-pixel offsets and keeping the largest coverage
  // widens every stroke by a full pixel on each side, which at interface text sizes (16-24 px) is
  // a third of a stroke: counters fill in and the label reads as a blurred block. The weight is
  // added as a fraction of the glyph's own coverage instead, so the stroke thickens without
  // moving the antialiased edge by a whole pixel.
  const float weight = key.bold != 0 ? boldWeight(pixelSize) : 0.0F;
  const int width = glyphWidth;
  const int height = glyphHeight;
  std::vector<std::uint8_t> expanded(bitmap);
  if (weight > 0.001F) {
    std::vector<std::uint8_t> thickened(bitmap.size(), 0);
    // One ring of neighbours: a pixel's own coverage plus a share of the four edge neighbours
    // and half that of the diagonals. Summing coverage this way is what a slightly heavier
    // stroke looks like after rasterisation, and it never saturates a thin gap.
    static constexpr int kNeighbours[8][3] = {
        {1, 0, 4}, {-1, 0, 4}, {0, 1, 4}, {0, -1, 4},
        {1, 1, 2}, {1, -1, 2}, {-1, 1, 2}, {-1, -1, 2}};
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        const auto value = bitmap[static_cast<std::size_t>(row) * width + column];
        float sum = static_cast<float>(value) * 8.0F;
        for (const auto& neighbour : kNeighbours) {
          const int sourceX = column - neighbour[0];
          const int sourceY = row - neighbour[1];
          if (sourceX < 0 || sourceY < 0 || sourceX >= width || sourceY >= height) continue;
          sum += static_cast<float>(bitmap[static_cast<std::size_t>(sourceY) * width + sourceX])
              * static_cast<float>(neighbour[2]) * 0.25F;
        }
        const float boosted = sum / 8.0F * (1.0F + weight);
        thickened[static_cast<std::size_t>(row) * width + column] =
            static_cast<std::uint8_t>(std::clamp(boosted, 0.0F, 255.0F));
      }
    }
    expanded.swap(thickened);
  }

  // Shelf packing. When a row does not fit, continue on the next shelf; when the atlas runs
  // out of shelves it is grown rather than cleared. Clearing would drop every cached glyph
  // and force the next frames to re-rasterise the whole interface, which showed up as a
  // multi-second stall the first time a screen introduced new text sizes.
  if (shelfX + width + kGlyphPadding > atlasWidth) {
    shelfX = 0;
    shelfY += rowHeight + kGlyphPadding;
    rowHeight = 0;
  }
  if (shelfY + height + kGlyphPadding > atlasHeight) {
    if (!growAtlas()) {
      clearAtlas();
    } else if (shelfX + width + kGlyphPadding > atlasWidth) {
      shelfX = 0;
      shelfY += rowHeight + kGlyphPadding;
      rowHeight = 0;
    }
  }

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
  // The bitmap is expanded in place rather than padded by a dilation radius, so the glyph's origin
  // is the one the rasteriser reported.
  result.offsetX = static_cast<float>(x0);
  result.offsetY = static_cast<float>(y0);
  result.u0 = static_cast<float>(targetX) / static_cast<float>(atlasWidth);
  result.v0 = static_cast<float>(targetY) / static_cast<float>(atlasHeight);
  result.u1 = static_cast<float>(targetX + width) / static_cast<float>(atlasWidth);
  result.v1 = static_cast<float>(targetY + height) / static_cast<float>(atlasHeight);

  shelfX += width + kGlyphPadding;
  rowHeight = std::max(rowHeight, height);
  const auto inserted = glyphs.emplace(key, result);
  return &inserted.first->second;
}

UiFontCache& fontCache() {
  static UiFontCache cache;
  return cache;
}

std::filesystem::path findSymbolFontPath() {
#if defined(_WIN32)
  static const std::filesystem::path candidates[] = {
      L"C:/Windows/Fonts/seguisym.ttf", L"C:/Windows/Fonts/arialuni.ttf"};
#elif defined(__ANDROID__)
  static const std::filesystem::path candidates[] = {
      "/system/fonts/NotoSansSymbols2-Regular.ttf",
      "/system/fonts/NotoSansSymbols-Regular-Subsetted2.ttf",
      "/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf",
      "/system/fonts/NotoSansSymbols-Regular.ttf",
      "/system/fonts/NotoSansCJK-Regular.ttc"};
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
  static const std::filesystem::path candidates[] = {
      "/System/Library/Fonts/Core/AppleSymbols.ttf",
      "/System/Library/Fonts/Apple Symbols.ttf",
      "/System/Library/Fonts/Core/ArialUnicode.ttf"};
#else
  static const std::filesystem::path candidates[] = {
      "/System/Library/Fonts/Supplemental/Apple Symbols.ttf",
      "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"};
#endif
#else
  static const std::filesystem::path candidates[] = {
      "/usr/share/fonts/opentype/noto/NotoSansSymbols2-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
#endif
  for (const auto& candidate : candidates) {
    if (platform::fileExists(candidate)) return candidate;
  }
  return {};
}

} // namespace rechigros::app::ui
