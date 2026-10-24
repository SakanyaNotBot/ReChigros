#include "rechigros/render/fx/BlockedComboField.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/math/Mat4.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "stb_truetype.h"

namespace rechigros::render::fx {
namespace {

// The mask font is a separate face shipped with the level assets: the block pattern is
// authored against its blocky letterforms, not against the interface font.
constexpr const char* kMaskFontAsset = "official_fonts/blocked_ui.ttf";
// Displacement map. Missing it degrades the field to hash noise rather than removing it.
constexpr const char* kNoiseAsset = "official_textures/BlockNoise1.png";

// Mask font and displacement map are shared by every field and loaded once, on the first
// prepare() call, from the asset root the renderer was initialised with.
struct MaskAssets {
  std::vector<std::uint8_t> fontData;
  stbtt_fontinfo font{};
  bool fontValid{};
  std::vector<std::uint8_t> noisePixels;
  int noiseWidth{};
  int noiseHeight{};
  bool loaded{};
};

std::filesystem::path gAssetRoot;

MaskAssets& maskAssets(const std::filesystem::path& assetRoot) {
  static MaskAssets assets;
  static std::once_flag once;
  static bool rootSet = false;
  if (!rootSet) { gAssetRoot = assetRoot; rootSet = true; }
  std::call_once(once, [&assetRoot] {
    assets.fontData = platform::AssetManager::readBinary(assetRoot, kMaskFontAsset);
    if (!assets.fontData.empty()) {
      const int offset = stbtt_GetFontOffsetForIndex(assets.fontData.data(), 0);
      if (offset >= 0 && stbtt_InitFont(&assets.font, assets.fontData.data(), offset) != 0) {
        assets.fontValid = true;
      }
    }
    // The map is decoded with the same decoder the texture cache uses, so the sampling here
    // matches what the GPU would see for the same asset.
    const auto bytes = platform::AssetManager::readBinary(assetRoot, kNoiseAsset);
    if (!bytes.empty()) {
      const auto image = asset::decodeImageMemory(bytes.data(), bytes.size());
      if (image.valid()) {
        assets.noisePixels = image.pixels;
        assets.noiseWidth = image.width;
        assets.noiseHeight = image.height;
      }
    }
    assets.loaded = true;
  });
  return assets;
}

// Decodes one UTF-8 codepoint and advances `index` past it.
int decodeCodepoint(const std::string& text, std::size_t& index) noexcept {
  if (index >= text.size()) return 0;
  const auto lead = static_cast<unsigned char>(text[index]);
  std::size_t length = 1;
  int codepoint = lead;
  if ((lead & 0x80U) == 0) {
    length = 1;
  } else if ((lead & 0xE0U) == 0xC0U) {
    length = 2;
    codepoint = lead & 0x1FU;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3;
    codepoint = lead & 0x0FU;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4;
    codepoint = lead & 0x07U;
  }
  for (std::size_t offset = 1; offset < length && index + offset < text.size(); ++offset) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[index + offset]) & 0x3FU);
  }
  index += length;
  return codepoint;
}

// Displacement field tuning. The values come from the authored BlockCompose material: two
// scrolling samples per cell combine into one offset, which is what gives the field its
// slow diagonal drift rather than a per-frame jitter.
constexpr float kSampleScaleX = 2.13F;
constexpr float kSampleScaleY = 1.02F;
constexpr float kScrollSpeed = 2.59F;
constexpr float kDisplaceStrength = 0.1F;
constexpr float kDirectionX = 0.70710678F;
constexpr float kDirectionY = 0.70710678F;
// The field advances 30 steps per second, so the flicker locks to the frame cadence.
constexpr double kFlickerStepsPerSec = 30.0;
// A cell outside the screen by this margin is skipped: it cannot contribute a pixel.
constexpr float kCullMargin = 96.0F;

// Deterministic per-cell flicker. A hash rather than a random source keeps a replayed frame
// identical to the original.
float cellNoise(int x, int y, int frame) noexcept {
  std::int32_t hash = x * 374761393 + y * 668265263 + frame * 1442695041;
  hash = (hash ^ (hash >> 13)) * 1274126177;
  hash = hash ^ (hash >> 16);
  return static_cast<float>(hash & 0xFFFFFF) / static_cast<float>(0x1000000);
}

// Sample of the shipped displacement map. The texture is stored with point sampling and a
// mirrored wrap, so an out-of-range coordinate reflects rather than clamps.
float noiseSample(const std::vector<std::uint8_t>& pixels, int width, int height,
    float u, float v) noexcept {
  if (pixels.empty() || width <= 0 || height <= 0) {
    // Without the map the same hash shape drives the field, so the effect degrades rather
    // than disappearing.
    const int ix = static_cast<int>(u * 64.0F);
    const int iy = static_cast<int>(v * 64.0F);
    const std::int32_t value = (ix * 374761393 + iy * 668265263) >> 8;
    return static_cast<float>(value & 0xFFFF) / 65535.0F;
  }
  const auto wrap = [](int value, int span) {
    const int period = span * 2;
    int wrapped = ((value % period) + period) % period;
    if (wrapped >= span) wrapped = period - 1 - wrapped;
    return wrapped;
  };
  const int px = wrap(static_cast<int>(std::abs(u) * static_cast<float>(width)), width);
  const int py = wrap(static_cast<int>(std::abs(v) * static_cast<float>(height)), height);
  const std::size_t index =
      (static_cast<std::size_t>(py) * static_cast<std::size_t>(width)
          + static_cast<std::size_t>(px)) * 4U;
  if (index + 1 >= pixels.size()) return 0.0F;
  return static_cast<float>(pixels[index]) / 255.0F;
}

} // namespace

void BlockedComboField::prepare(const std::filesystem::path& assetRoot, const std::string& text, float pixelSizePx) {
  if (text == sourceText_ && std::abs(pixelSizePx - sourceSize_) < 0.01F && valid()) return;
  sourceText_ = text;
  sourceSize_ = pixelSizePx;
  mask_.clear();
  width_ = 0;
  height_ = 0;
  contentWidth_ = 0.0F;
  contentHeight_ = 0.0F;

  const auto& assets = maskAssets(assetRoot);
  if (!assets.fontValid || text.empty() || pixelSizePx <= 0.0F) return;

  const float scale = stbtt_ScaleForPixelHeight(&assets.font, pixelSizePx);
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  stbtt_GetFontVMetrics(&assets.font, &ascent, &descent, &lineGap);
  const float ascentPx = static_cast<float>(ascent) * scale;
  const float descentPx = static_cast<float>(descent) * scale;

  // Text width from the advance of every codepoint.
  float textWidth = 0.0F;
  int previous = 0;
  for (std::size_t index = 0; index < text.size();) {
    const int codepoint = decodeCodepoint(text, index);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&assets.font, codepoint, &advance, &bearing);
    if (previous != 0) {
      textWidth += static_cast<float>(stbtt_GetCodepointKernAdvance(&assets.font, previous, codepoint))
          * scale;
    }
    textWidth += static_cast<float>(advance) * scale;
    previous = codepoint;
  }
  const float textHeight = ascentPx - descentPx;
  if (textWidth <= 0.0F || textHeight <= 0.0F) return;

  const int textCellsW = static_cast<int>(std::ceil(textWidth)) / CELL + 1;
  const int textCellsH = static_cast<int>(std::ceil(textHeight)) / CELL + 1;
  width_ = textCellsW + PADDING_CELLS * 2;
  height_ = textCellsH + PADDING_CELLS * 2;
  contentWidth_ = textWidth;
  contentHeight_ = textHeight;
  mask_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0U);

  // Rasterise the string at cell resolution: the official pipeline renders the string into a
  // screen/8 target and reads one point per cell, so a cell is set when the glyph covers it.
  const float cellScale = scale / static_cast<float>(CELL);
  for (std::size_t index = 0; index < text.size();) {
    const int codepoint = decodeCodepoint(text, index);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&assets.font, codepoint, &advance, &bearing);

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&assets.font, codepoint, cellScale, cellScale, &x0, &y0, &x1, &y1);
    const int glyphW = x1 - x0;
    const int glyphH = y1 - y0;
    if (glyphW > 0 && glyphH > 0) {
      std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(glyphW) * glyphH, 0U);
      stbtt_MakeCodepointBitmap(&assets.font, bitmap.data(), glyphW, glyphH, glyphW, cellScale,
          cellScale, codepoint);
      // The pen position is tracked in cell units; the padding offset centres the text.
      const int penCells = PADDING_CELLS;
      for (int row = 0; row < glyphH; ++row) {
        for (int column = 0; column < glyphW; ++column) {
          if (bitmap[static_cast<std::size_t>(row) * glyphW + column] < 96U) continue;
          const int targetX = penCells + x0 + column;
          const int targetY = PADDING_CELLS + static_cast<int>(std::lround(ascentPx / CELL))
              + y0 + row;
          if (targetX < 0 || targetX >= width_ || targetY < 0 || targetY >= height_) continue;
          mask_[static_cast<std::size_t>(targetY) * width_ + targetX] = 1U;
        }
      }
    }
    textWidth += static_cast<float>(advance) * scale;
  }
}

int BlockedComboField::coveredCells() const noexcept {
  int count = 0;
  for (const auto value : mask_) {
    if (value != 0U) ++count;
  }
  return count;
}


void BlockedComboField::draw(batch::SpriteBatch& batch, const math::Mat4& projection,
    const gl::Texture& whiteTexture, float centerX, float centerY, float alpha,
    double visualTimeSec, float screenWidth, float screenHeight) const {
  if (!valid() || alpha <= 0.0F) return;
  const float cell = static_cast<float>(CELL);
  const float left = centerX - (static_cast<float>(width_) * cell) * 0.5F
      + static_cast<float>(PADDING_CELLS) * cell;
  const float top = centerY - (static_cast<float>(height_) * cell) * 0.5F
      + static_cast<float>(PADDING_CELLS) * cell;

  const float safeW = std::max(1.0F, screenWidth);
  const float safeH = std::max(1.0F, screenHeight);
  const float time = static_cast<float>(visualTimeSec);
  const int step = static_cast<int>(std::floor(static_cast<double>(time) * kFlickerStepsPerSec));
  const float scroll = (time / 20.0F) * kScrollSpeed;
  const float perpX = -kDirectionY;
  const float perpY = kDirectionX;
  const float edgeWidth = std::max(1.6F, cell * 0.30F);

  batch.begin();
  for (int row = 0; row < height_; ++row) {
    for (int column = 0; column < width_; ++column) {
      if (mask_[static_cast<std::size_t>(row) * width_ + column] == 0U) continue;
      const float screenX = left + (static_cast<float>(column) + 0.5F) * cell;
      const float screenY = top + (static_cast<float>(row) + 0.5F) * cell;
      if (screenX < -kCullMargin || screenX > safeW + kCullMargin
          || screenY < -kCullMargin || screenY > safeH + kCullMargin) {
        continue;
      }

      // Two scrolling samples, one along the travel direction and one across it, combine
      // into the cell's displacement.
      const float u = screenX / safeW;
      const float v = screenY / safeH;
      const float sampleX = u * kSampleScaleX;
      const float sampleY = v * kSampleScaleY;
      const auto& assetData = maskAssets(gAssetRoot);
      const float n1 = noiseSample(assetData.noisePixels, assetData.noiseWidth,
          assetData.noiseHeight, sampleX + kDirectionX * scroll, sampleY + kDirectionY * scroll);
      const float n2 = noiseSample(assetData.noisePixels, assetData.noiseWidth,
          assetData.noiseHeight, sampleX + perpX * scroll, sampleY + perpY * scroll);
      const float offsetU =
          (kDirectionX * (n1 - 0.5F) + perpX * (n2 - 0.5F)) * kDisplaceStrength;
      const float offsetV =
          (kDirectionY * (n1 - 0.5F) + perpY * (n2 - 0.5F)) * kDisplaceStrength;
      const float drawX = screenX - offsetU * safeW;
      const float drawY = screenY - offsetV * safeH;

      const float breathe = cellNoise(column, row, 0);
      const float live = cellNoise(column, row, step);
      // The mask dilates by one texel and the quarter-resolution composite rounds it out, so
      // an authored square reads about one and a half mask cells wide and merges along a
      // chain of neighbouring cells.
      const float outer = cell * (0.78F - (breathe - 0.5F) * 0.12F);
      const float fillHalf = std::max(1.0F, outer - edgeWidth);
      const float a = alpha;

      const auto quad = [&](float x, float y, float w, float h, float r, float g, float b,
                            float opacity) {
        (void)batch.addQuad(x, y, w, h, 0.0F, r, g, b, opacity, 0.0F, 0.0F, 1.0F, 1.0F);
      };
      // Glow: three concentric soft squares standing in for the five-pass ring.
      quad(drawX, drawY, outer * 2.0F + cell * 1.7F, outer * 2.0F + cell * 1.7F,
          0.55F, 0.10F, 0.10F, a * (0.07F + 0.05F * live));
      quad(drawX, drawY, outer * 2.0F + cell * 1.05F, outer * 2.0F + cell * 1.05F,
          0.60F, 0.11F, 0.11F, a * (0.14F + 0.08F * live));
      quad(drawX, drawY, outer * 2.0F + cell * 0.55F, outer * 2.0F + cell * 0.55F,
          0.65F, 0.12F, 0.12F, a * (0.22F + 0.10F * live));
      // Fill and edge, both modulated by the cell's own slow breathe value.
      quad(drawX, drawY, fillHalf * 2.0F, fillHalf * 2.0F,
          0.48F, 0.157F, 0.157F, a * (0.80F + 0.15F * breathe));
      const float edgeAlpha = a * (0.72F + 0.20F * breathe);
      const float ey = outer - edgeWidth * 0.5F;
      quad(drawX, drawY - ey, outer * 2.0F, edgeWidth, 0.80F, 0.264F, 0.264F, edgeAlpha);
      quad(drawX, drawY + ey, outer * 2.0F, edgeWidth, 0.80F, 0.264F, 0.264F, edgeAlpha);
      quad(drawX - ey, drawY, edgeWidth, outer * 2.0F, 0.80F, 0.264F, 0.264F, edgeAlpha);
      quad(drawX + ey, drawY, edgeWidth, outer * 2.0F, 0.80F, 0.264F, 0.264F, edgeAlpha);
    }
  }
  if (!batch.empty()) batch.flush(projection, whiteTexture, nullptr);
}

} // namespace rechigros::render::fx
