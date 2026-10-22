#include "rechigros/render/text/TextRenderer.hpp"

#include "rechigros/platform/AssetManager.hpp"

#include <SDL_log.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace rechigros::render::text {
namespace {
// Multisample coverage is accumulated at 4x4 subpixel positions to keep small
// sizes legible without a full SDF pipeline.
constexpr int kSubpixelsX = 4;
constexpr int kSubpixelsY = 4;
constexpr float kPixelPadding = 1.0F;

std::uint8_t coverageToAlpha(float coverage) {
  const float clamped = std::clamp(coverage, 0.0F, 1.0F);
  return static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
}

// Splits UTF-8 into code points. The font API addresses glyphs by code point; iterating
// the bytes of a string instead would draw every continuation byte as its own missing
// glyph, which turns one non-ASCII separator into a row of placeholder boxes.
std::vector<std::uint32_t> decodeUtf8(const std::string& text) {
  std::vector<std::uint32_t> codepoints;
  codepoints.reserve(text.size());
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t code = lead;
    std::size_t length = 1;
    if ((lead & 0x80U) == 0) {
      code = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
      code = lead & 0x1FU;
      length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      code = lead & 0x0FU;
      length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      code = lead & 0x07U;
      length = 4;
    }
    bool valid = length > 1 && index + length <= text.size();
    for (std::size_t offset = 1; valid && offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(text[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        valid = false;
        break;
      }
      code = (code << 6U) | (continuation & 0x3FU);
    }
    if (!valid) {
      // A stray byte is drawn as itself rather than dropped, so a malformed string stays
      // visible instead of silently shrinking.
      codepoints.push_back(lead);
      ++index;
      continue;
    }
    codepoints.push_back(code);
    index += length;
  }
  return codepoints;
}
} // namespace

struct FontInfo {
  stbtt_fontinfo info{};
  float scale{1.0F};
};

FontAtlas::~FontAtlas() = default;

bool FontAtlas::load(const std::filesystem::path& path) {
  fontInfo_.reset();
  fontData_.clear();
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0) return false;
  fontData_.resize(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fontData_.clear();
    return false;
  }
  input.read(reinterpret_cast<char*>(fontData_.data()), static_cast<std::streamsize>(size));
  if (!input) {
    fontData_.clear();
    return false;
  }
  auto info = std::make_unique<FontInfo>();
  const int offset = stbtt_GetFontOffsetForIndex(fontData_.data(), 0);
  if (offset < 0 || stbtt_InitFont(&info->info, fontData_.data(), offset) == 0) {
    fontData_.clear();
    return false;
  }
  fontInfo_ = std::move(info);
  return true;
}

float FontAtlas::measure(const std::string& text, float pixelSize) const {
  if (fontInfo_ == nullptr || text.empty()) return 0.0F;
  const float scale = stbtt_ScaleForPixelHeight(&fontInfo_->info, pixelSize);
  float width = 0.0F;
  int previous = 0;
  for (const std::uint32_t character : decodeUtf8(text)) {
    const int codepoint = static_cast<int>(character);
    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&fontInfo_->info, codepoint, &advance, &leftSideBearing);
    const int kern = previous != 0
        ? stbtt_GetCodepointKernAdvance(&fontInfo_->info, previous, codepoint)
        : 0;
    width += static_cast<float>(advance + kern) * scale;
    previous = codepoint;
  }
  return width;
}

std::unique_ptr<TextTexture> FontAtlas::rasterize(const std::string& text, float pixelSize,
    bool bold) const {
  if (fontInfo_ == nullptr) return nullptr;
  const std::string content = text.empty() ? std::string(" ") : text;
  const float scale = stbtt_ScaleForPixelHeight(&fontInfo_->info, pixelSize);

  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&fontInfo_->info, &ascent, &descent, &lineGap);

  // First pass: advances and bounding box.
  const auto codepoints = decodeUtf8(content);
  float penX = 0.0F;
  int previous = 0;
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool hasBounds = false;
  for (const std::uint32_t character : codepoints) {
    const int codepoint = static_cast<int>(character);
    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&fontInfo_->info, codepoint, &advance, &leftSideBearing);
    const int kern = previous != 0
        ? stbtt_GetCodepointKernAdvance(&fontInfo_->info, previous, codepoint)
        : 0;
    penX += static_cast<float>(kern) * scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&fontInfo_->info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
    if (x1 > x0 && y1 > y0) {
      const float glyphLeft = penX + static_cast<float>(x0);
      const float glyphRight = penX + static_cast<float>(x1);
      const float glyphTop = static_cast<float>(y0);
      const float glyphBottom = static_cast<float>(y1);
      if (!hasBounds) {
        minX = glyphLeft;
        maxX = glyphRight;
        minY = glyphTop;
        maxY = glyphBottom;
        hasBounds = true;
      } else {
        minX = std::min(minX, glyphLeft);
        maxX = std::max(maxX, glyphRight);
        minY = std::min(minY, glyphTop);
        maxY = std::max(maxY, glyphBottom);
      }
    }
    penX += static_cast<float>(advance) * scale;
    previous = codepoint;
  }

  const float advanceWidth = penX;
  if (!hasBounds) {
    // Whitespace only: keep a one-pixel-wide transparent texture.
    auto result = std::make_unique<TextTexture>();
    const std::uint8_t transparent[4] = {0, 0, 0, 0};
    result->texture.uploadRgba(1, 1, transparent);
    result->width = 1;
    result->height = 1;
    result->advanceWidth = advanceWidth;
    return result;
  }

  const int width = std::max(1, static_cast<int>(std::ceil(maxX - minX)) + 2);
  const int height = std::max(1, static_cast<int>(std::ceil(maxY - minY)) + 2);
  std::vector<float> coverage(static_cast<std::size_t>(width) * height, 0.0F);

  // Second pass: sample each glyph at subpixel offsets and accumulate coverage.
  penX = 0.0F;
  previous = 0;
  for (const std::uint32_t character : codepoints) {
    const int codepoint = static_cast<int>(character);
    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&fontInfo_->info, codepoint, &advance, &leftSideBearing);
    const int kern = previous != 0
        ? stbtt_GetCodepointKernAdvance(&fontInfo_->info, previous, codepoint)
        : 0;
    penX += static_cast<float>(kern) * scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&fontInfo_->info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
    const int glyphW = x1 - x0;
    const int glyphH = y1 - y0;
    if (glyphW > 0 && glyphH > 0) {
      // Coverage is accumulated from subpixel-shifted samples, which keeps small
      // sizes legible without a distance field. Sample values arrive in 0..255, so
      // they are normalised to 0..1 before being weighted.
      constexpr float weight = 1.0F / (kSubpixelsX * kSubpixelsY * 255.0F);
      for (int subY = 0; subY < kSubpixelsY; ++subY) {
        for (int subX = 0; subX < kSubpixelsX; ++subX) {
          const float shiftX = static_cast<float>(subX) / kSubpixelsX;
          const float shiftY = static_cast<float>(subY) / kSubpixelsY;
          std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(glyphW) * glyphH, 0);
          stbtt_MakeCodepointBitmapSubpixel(&fontInfo_->info, bitmap.data(), glyphW, glyphH,
              glyphW, scale, scale, shiftX, shiftY, codepoint);
          const int originX =
              static_cast<int>(std::lround(penX + static_cast<float>(x0) - minX + 1.0F));
          const int originY = static_cast<int>(std::lround(static_cast<float>(y0) - minY + 1.0F));
          for (int row = 0; row < glyphH; ++row) {
            const int targetY = originY + row;
            if (targetY < 0 || targetY >= height) continue;
            for (int column = 0; column < glyphW; ++column) {
              const int targetX = originX + column;
              if (targetX < 0 || targetX >= width) continue;
              const auto value =
                  static_cast<float>(bitmap[static_cast<std::size_t>(row) * glyphW + column]);
              coverage[static_cast<std::size_t>(targetY) * width + targetX] += value * weight;
              if (bold && targetX + 1 < width) {
                coverage[static_cast<std::size_t>(targetY) * width + targetX + 1] +=
                    value * weight * 0.35F;
              }
            }
          }
        }
      }
    }
    penX += static_cast<float>(advance) * scale;
    previous = codepoint;
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
  for (std::size_t index = 0; index < coverage.size(); ++index) {
    const std::uint8_t alpha = coverageToAlpha(coverage[index]);
    // Premultiplied white: the sprite pipeline blends with (ONE, ONE_MINUS_SRC_ALPHA).
    rgba[index * 4 + 0] = alpha;
    rgba[index * 4 + 1] = alpha;
    rgba[index * 4 + 2] = alpha;
    rgba[index * 4 + 3] = alpha;
  }

  auto result = std::make_unique<TextTexture>();
  result->texture.uploadRgba(width, height, rgba.data());
  result->texture.setFilter(true);
  result->width = width;
  result->height = height;
  result->advanceWidth = advanceWidth;
  // Distance from the glyph box bottom to the font descent line.
  const float bottom = maxY;
  const float descentPx = static_cast<float>(descent) * scale;
  const float extra = std::max(0.0F, descentPx - bottom);
  result->paddingBottom = static_cast<int>(std::ceil(extra));
  // Metrics the HUD layout needs to place a string the way a text renderer with the
  // font's real ascent/descent would: stb reports the descent as a negative value.
  result->ascentPx = static_cast<float>(ascent) * scale;
  result->descentPx = -descentPx;
  result->inkLeft = minX;
  result->inkTop = minY;
  result->inkRight = maxX;
  result->inkBottom = maxY;
  return result;
}

TextRenderer::TextRenderer() = default;
TextRenderer::~TextRenderer() = default;

bool TextRenderer::initialize(const std::filesystem::path& assetRoot) {
  // A packaged build keeps its assets inside the package, where they are not files. The
  // font is materialised to a readable path first, the same way the interface font is;
  // opening the packaged path directly fails on the device, which used to report the
  // whole scene renderer as uninitialised and leave the play field blank.
  auto fontPath = platform::AssetManager::materialize(assetRoot, "res/font.ttf");
  if (fontPath.empty()) fontPath = assetRoot / "res" / "font.ttf";
  const bool loaded = font_.load(fontPath);
  if (!loaded) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render] text font unavailable: %s",
        fontPath.string().c_str());
  }
  return loaded;
}

void TextRenderer::shutdown() noexcept { cache_.clear(); }

float TextRenderer::measure(const std::string& text, float pixelSize) const {
  return font_.measure(text, pixelSize);
}

TextTexture* TextRenderer::get(const std::string& text, float pixelSize, bool bold) {
  char key[64];
  std::snprintf(key, sizeof(key), "|%.2f|%d|", static_cast<double>(pixelSize), bold ? 1 : 0);
  const std::string cacheKey = text + key;
  for (auto& entry : cache_) {
    if (entry.first == cacheKey) return entry.second.get();
  }
  auto texture = font_.rasterize(text, pixelSize, bold);
  if (texture == nullptr) return nullptr;
  if (cache_.size() >= cacheLimit_) cache_.erase(cache_.begin());
  cache_.emplace_back(cacheKey, std::move(texture));
  return cache_.back().second.get();
}

} // namespace rechigros::render::text
