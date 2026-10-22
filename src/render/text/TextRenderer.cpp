#include "rechigros/render/text/TextRenderer.hpp"

#include "rechigros/platform/AssetManager.hpp"

#include <SDL3/SDL_log.h>

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
    // Difficulty labels and a few interface strings separate their parts with typographic
    // spaces. The bundled font has no glyph for those code points, and the fallback glyph
    // would show as a placeholder box, so they resolve to the plain space advance.
    if ((code >= 0x2000U && code <= 0x200AU) || code == 0x00A0U || code == 0x202FU
        || code == 0x205FU || code == 0x3000U) {
      code = 0x20U;
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

namespace {
// Decodes UTF-8 into a fixed buffer so the composition path never allocates. The rules match
// decodeUtf8 above, including the typographic-space fallback.
bool decodeUtf8Into(const std::string& text, std::uint32_t* out, int capacity, int& count) {
  count = 0;
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
      code = lead;
      length = 1;
    }
    if ((code >= 0x2000U && code <= 0x200AU) || code == 0x00A0U || code == 0x202FU
        || code == 0x205FU || code == 0x3000U) {
      code = 0x20U;
    }
    if (count >= capacity) return false;
    out[count++] = code;
    index += length;
  }
  return true;
}
} // namespace

std::unique_ptr<GlyphStrip> FontAtlas::rasterizeStrip(const std::string& characters,
    float pixelSize) const {
  if (fontInfo_ == nullptr || characters.empty()) return nullptr;
  const float scale = stbtt_ScaleForPixelHeight(&fontInfo_->info, pixelSize);

  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&fontInfo_->info, &ascent, &descent, &lineGap);

  const auto codepoints = decodeUtf8(characters);
  if (codepoints.empty()) return nullptr;

  // First pass: advances, bitmap boxes and the strip's ink bounds, exactly like the
  // full-string rasteriser.
  struct GlyphLayout {
    int x0{};
    int y0{};
    int x1{};
    int y1{};
    int advance{};
    float penX{};
  };
  std::vector<GlyphLayout> layouts(codepoints.size());
  float penX = 0.0F;
  int previous = 0;
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool hasBounds = false;
  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    const int codepoint = static_cast<int>(codepoints[index]);
    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&fontInfo_->info, codepoint, &advance, &leftSideBearing);
    const int kern = previous != 0
        ? stbtt_GetCodepointKernAdvance(&fontInfo_->info, previous, codepoint)
        : 0;
    penX += static_cast<float>(kern) * scale;
    auto& layout = layouts[index];
    layout.advance = advance;
    layout.penX = penX;
    stbtt_GetCodepointBitmapBox(&fontInfo_->info, codepoint, scale, scale, &layout.x0,
        &layout.y0, &layout.x1, &layout.y1);
    if (layout.x1 > layout.x0 && layout.y1 > layout.y0) {
      const float glyphLeft = penX + static_cast<float>(layout.x0);
      const float glyphRight = penX + static_cast<float>(layout.x1);
      const float glyphTop = static_cast<float>(layout.y0);
      const float glyphBottom = static_cast<float>(layout.y1);
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

  auto result = std::make_unique<GlyphStrip>();
  result->pixelSize = pixelSize;
  result->ascentPx = static_cast<float>(ascent) * scale;
  result->descentPx = -static_cast<float>(descent) * scale;
  result->glyphs.reserve(codepoints.size());
  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    GlyphStripGlyph glyph;
    glyph.codepoint = codepoints[index];
    glyph.bitmapX0 = layouts[index].x0;
    glyph.bitmapY0 = layouts[index].y0;
    glyph.bitmapX1 = layouts[index].x1;
    glyph.bitmapY1 = layouts[index].y1;
    glyph.width = layouts[index].x1 - layouts[index].x0;
    glyph.height = layouts[index].y1 - layouts[index].y0;
    glyph.advance = static_cast<float>(layouts[index].advance) * scale;
    result->glyphs.push_back(glyph);
  }

  if (!hasBounds) {
    const std::uint8_t transparent[4] = {0, 0, 0, 0};
    result->texture.uploadRgba(1, 1, transparent);
    result->width = 1;
    result->height = 1;
    return result;
  }

  // Pack the glyphs horizontally, each with a one-pixel transparent border. A composed quad
  // then samples the same transparent neighbourhood the full-string texture has at its glyph
  // edges, so the two paths rasterise identical pixels at every subpixel position.
  constexpr int kStripBorder = 1;
  int stripWidth = 1;
  int stripHeight = 1;
  for (auto& glyph : result->glyphs) {
    if (glyph.width <= 0 || glyph.height <= 0) {
      glyph.stripX = 0;
      glyph.stripY = 0;
      continue;
    }
    glyph.stripX = stripWidth + kStripBorder;
    glyph.stripY = kStripBorder;
    stripWidth += glyph.width + 2 * kStripBorder;
    stripHeight = std::max(stripHeight, glyph.height + 2 * kStripBorder);
  }
  if (stripWidth <= 0) stripWidth = 1;
  if (stripHeight <= 0) stripHeight = 1;
  std::vector<float> coverage(static_cast<std::size_t>(stripWidth) * stripHeight, 0.0F);
  std::vector<std::uint8_t> bitmap;
  constexpr float weight = 1.0F / (kSubpixelsX * kSubpixelsY * 255.0F);

  // Second pass: sample each glyph at subpixel offsets and accumulate its coverage inside its
  // strip slot. The sample content is independent of the slot, so a composed glyph is
  // pixel-identical to the same glyph inside a full-string rasterisation.
  for (std::size_t index = 0; index < codepoints.size(); ++index) {
    auto& glyph = result->glyphs[index];
    const int glyphW = glyph.width;
    const int glyphH = glyph.height;
    if (glyphW <= 0 || glyphH <= 0) continue;
    const int codepoint = static_cast<int>(glyph.codepoint);
    bitmap.assign(static_cast<std::size_t>(glyphW) * glyphH, 0);
    for (int subY = 0; subY < kSubpixelsY; ++subY) {
      for (int subX = 0; subX < kSubpixelsX; ++subX) {
        const float shiftX = static_cast<float>(subX) / kSubpixelsX;
        const float shiftY = static_cast<float>(subY) / kSubpixelsY;
        stbtt_MakeCodepointBitmapSubpixel(&fontInfo_->info, bitmap.data(), glyphW, glyphH,
            glyphW, scale, scale, shiftX, shiftY, codepoint);
        for (int row = 0; row < glyphH; ++row) {
          const int targetY = glyph.stripY + row;
          if (targetY < 0 || targetY >= stripHeight) continue;
          for (int column = 0; column < glyphW; ++column) {
            const int targetX = glyph.stripX + column;
            if (targetX < 0 || targetX >= stripWidth) continue;
            const auto value =
                static_cast<float>(bitmap[static_cast<std::size_t>(row) * glyphW + column]);
            coverage[static_cast<std::size_t>(targetY) * stripWidth + targetX] += value * weight;
          }
        }
      }
    }
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(stripWidth) * stripHeight * 4, 0);
  for (std::size_t index = 0; index < coverage.size(); ++index) {
    const std::uint8_t alpha = coverageToAlpha(coverage[index]);
    rgba[index * 4 + 0] = alpha;
    rgba[index * 4 + 1] = alpha;
    rgba[index * 4 + 2] = alpha;
    rgba[index * 4 + 3] = alpha;
  }
  result->texture.uploadRgba(stripWidth, stripHeight, rgba.data());
  result->texture.setFilter(true);
  result->width = stripWidth;
  result->height = stripHeight;
  return result;
}

bool FontAtlas::layoutStrip(const GlyphStrip& strip, const std::string& text,
    ComposedTextMetrics& metrics,
    std::array<ComposedGlyphPlacement, kMaxComposedGlyphs>& placements, int& placementCount) const {
  placementCount = 0;
  metrics = ComposedTextMetrics{};
  if (fontInfo_ == nullptr || text.empty() || !(strip.pixelSize > 0.0F)) return false;
  const float scale = stbtt_ScaleForPixelHeight(&fontInfo_->info, strip.pixelSize);

  std::array<std::uint32_t, kMaxComposedGlyphs> codepoints{};
  int count = 0;
  if (!decodeUtf8Into(text, codepoints.data(), kMaxComposedGlyphs, count)) return false;

  // Same pen/kerning/rounding math as the rasteriser, so the composed ink box, texture size
  // and glyph origins are identical to a full-string rasterisation.
  std::array<float, kMaxComposedGlyphs> glyphPenX{};
  std::array<const GlyphStripGlyph*, kMaxComposedGlyphs> glyphs{};
  float penX = 0.0F;
  float measuredWidth = 0.0F;
  int previous = 0;
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool hasBounds = false;
  for (int index = 0; index < count; ++index) {
    const GlyphStripGlyph* glyph = nullptr;
    for (const auto& candidate : strip.glyphs) {
      if (candidate.codepoint == codepoints[static_cast<std::size_t>(index)]) {
        glyph = &candidate;
        break;
      }
    }
    if (glyph == nullptr) return false;
    glyphs[static_cast<std::size_t>(index)] = glyph;
    const int codepoint = static_cast<int>(glyph->codepoint);
    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&fontInfo_->info, codepoint, &advance, &leftSideBearing);
    const int kern = previous != 0
        ? stbtt_GetCodepointKernAdvance(&fontInfo_->info, previous, codepoint)
        : 0;
    measuredWidth += static_cast<float>(advance + kern) * scale;
    penX += static_cast<float>(kern) * scale;
    glyphPenX[static_cast<std::size_t>(index)] = penX;
    if (glyph->width > 0 && glyph->height > 0) {
      const float glyphLeft = penX + static_cast<float>(glyph->bitmapX0);
      const float glyphRight = penX + static_cast<float>(glyph->bitmapX1);
      const float glyphTop = static_cast<float>(glyph->bitmapY0);
      const float glyphBottom = static_cast<float>(glyph->bitmapY1);
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

  metrics.advanceWidth = measuredWidth;
  metrics.ascentPx = strip.ascentPx;
  metrics.descentPx = strip.descentPx;
  if (!hasBounds) {
    // Whitespace only, matching the rasteriser's one-pixel transparent result.
    metrics.textureWidth = 1;
    metrics.textureHeight = 1;
    return true;
  }
  metrics.inkLeft = minX;
  metrics.inkRight = maxX;
  metrics.inkTop = minY;
  metrics.inkBottom = maxY;
  metrics.textureWidth = std::max(1, static_cast<int>(std::ceil(maxX - minX)) + 2);
  metrics.textureHeight = std::max(1, static_cast<int>(std::ceil(maxY - minY)) + 2);
  for (int index = 0; index < count; ++index) {
    const auto* glyph = glyphs[static_cast<std::size_t>(index)];
    if (glyph->width <= 0 || glyph->height <= 0) continue;
    if (placementCount >= kMaxComposedGlyphs) return false;
    auto& placement = placements[static_cast<std::size_t>(placementCount)];
    // Include the strip's transparent one-pixel border so bilinear sampling at the glyph
    // edges matches the full-string texture, then shift the destination the same way.
    placement.sourceX = glyph->stripX - 1;
    placement.sourceY = glyph->stripY - 1;
    placement.width = glyph->width + 2;
    placement.height = glyph->height + 2;
    placement.targetX = static_cast<int>(std::lround(glyphPenX[static_cast<std::size_t>(index)]
        + static_cast<float>(glyph->bitmapX0) - minX + 1.0F)) - 1;
    placement.targetY =
        static_cast<int>(std::lround(static_cast<float>(glyph->bitmapY0) - minY + 1.0F)) - 1;
    ++placementCount;
  }
  return true;
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
      // they are normalised to 0..1 before being weighted. One buffer and one origin are
      // reused for every sample of the glyph instead of allocating per sample.
      constexpr float weight = 1.0F / (kSubpixelsX * kSubpixelsY * 255.0F);
      std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(glyphW) * glyphH, 0);
      const int originX =
          static_cast<int>(std::lround(penX + static_cast<float>(x0) - minX + 1.0F));
      const int originY = static_cast<int>(std::lround(static_cast<float>(y0) - minY + 1.0F));
      for (int subY = 0; subY < kSubpixelsY; ++subY) {
        for (int subX = 0; subX < kSubpixelsX; ++subX) {
          const float shiftX = static_cast<float>(subX) / kSubpixelsX;
          const float shiftY = static_cast<float>(subY) / kSubpixelsY;
          stbtt_MakeCodepointBitmapSubpixel(&fontInfo_->info, bitmap.data(), glyphW, glyphH,
              glyphW, scale, scale, shiftX, shiftY, codepoint);
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

void TextRenderer::shutdown() noexcept {
  cache_.clear();
  strips_.clear();
}

float TextRenderer::measure(const std::string& text, float pixelSize) const {
  return font_.measure(text, pixelSize);
}

TextTexture* TextRenderer::get(const std::string& text, float pixelSize, bool bold) {
  // Compare the fields directly instead of building a concatenated key string, so a cache
  // hit does not allocate (the HUD queries several labels every frame).
  for (auto& entry : cache_) {
    if (entry.bold == bold && entry.text == text
        && std::abs(entry.pixelSize - pixelSize) < 0.005F) {
      return entry.texture.get();
    }
  }
  auto texture = font_.rasterize(text, pixelSize, bold);
  if (texture == nullptr) return nullptr;
  if (cache_.size() >= cacheLimit_) cache_.erase(cache_.begin());
  cache_.push_back(CacheEntry{text, pixelSize, bold, std::move(texture)});
  return cache_.back().texture.get();
}

const GlyphStrip* TextRenderer::glyphStrip(float pixelSize) {
  if (!font_.valid()) return nullptr;
  for (auto& entry : strips_) {
    if (std::abs(entry.pixelSize - pixelSize) < 0.005F) return entry.strip.get();
  }
  // Digits and the symbols that appear in HUD readouts. Rasterised once per text size.
  static const std::string kStripCharacters = "0123456789.%+-/: FPSms";
  auto strip = font_.rasterizeStrip(kStripCharacters, pixelSize);
  if (strip == nullptr) return nullptr;
  if (strips_.size() >= stripLimit_) strips_.erase(strips_.begin());
  strips_.push_back(StripEntry{pixelSize, std::move(strip)});
  return strips_.back().strip.get();
}

const GlyphStrip* TextRenderer::compose(const std::string& text, float pixelSize,
    ComposedTextMetrics& metrics,
    std::array<ComposedGlyphPlacement, kMaxComposedGlyphs>& placements, int& placementCount) {
  const GlyphStrip* strip = glyphStrip(pixelSize);
  if (strip == nullptr) return nullptr;
  if (!font_.layoutStrip(*strip, text, metrics, placements, placementCount)) return nullptr;
  return strip;
}

} // namespace rechigros::render::text
