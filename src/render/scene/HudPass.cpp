#include "rechigros/render/scene/HudPass.hpp"

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/text/TextRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rechigros::render::scene {
namespace {
// Layout of a 1000-unit tall canvas. Every value is a canvas coordinate: x grows
// right from the stage centre, y grows up from it.
constexpr float kPauseXOffset = 50.5887985F;
constexpr float kPauseY = 444.2999878F;
constexpr float kPauseH = 37.966F;
// The pause sprite is drawn at its own aspect ratio rather than stretched.
constexpr float kPauseVisualAspect = 35.0F / 41.0F;

constexpr float kScoreXOffset = -237.3887939F;
constexpr float kScoreY = 445.7000122F;
constexpr float kScoreWidth = 400.0F;
constexpr float kScoreTextSize = 50.0F;
constexpr float kAccuracyTextSize = 26.0F;

constexpr float kComboY = 452.0F;
constexpr float kComboTextY = 405.0F;
constexpr float kComboNumberTextSize = 70.0F;
constexpr float kComboLabelTextSize = 24.0F;

constexpr float kSongXOffset = 40.0F;
constexpr float kSongY = -473.2000122F;
constexpr float kLevelXOffset = -40.0F;
constexpr float kBottomTextSize = 36.0F;
constexpr float kBottomTextFixAt1080 = 5.0F;

constexpr float kProgressY = 500.0F;
constexpr float kProgressWidth = 1919.0F;
constexpr float kProgressHeight = 11.0F;

std::string formatScore(int value) {
  const int clamped = std::min(9999999, std::max(0, value));
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", clamped);
  return buffer;
}

// Accuracy is shown with two decimals, matching the score readout's precision.
std::string formatAccuracy(double accuracy) {
  const int hundredths = static_cast<int>(std::lround(std::clamp(accuracy, 0.0, 1.0) * 10000.0));
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d.%02d%%", hundredths / 100, hundredths % 100);
  return buffer;
}

// Draws one string with its top-left corner at (x, y). Each string owns its own
// texture, so the batch is flushed immediately after it is filled.
void drawString(batch::SpriteBatch& batch, text::TextRenderer& renderer, const std::string& content,
    float pixelSize, float x, float y, float red, float green, float blue, float alpha,
    const math::Mat4& projection) {
  if (content.empty() || !(alpha > 0.001F)) return;
  auto* glyphs = renderer.get(content, pixelSize, false);
  if (glyphs == nullptr) return;
  batch.begin();
  batch.addQuadTopLeft(x, y, static_cast<float>(glyphs->width),
      static_cast<float>(glyphs->height), red, green, blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, glyphs->texture, nullptr);
}

// Same, but anchored on the horizontal centre of the string.
void drawStringCentered(batch::SpriteBatch& batch, text::TextRenderer& renderer,
    const std::string& content, float pixelSize, float centerX, float y, float red, float green,
    float blue, float alpha, const math::Mat4& projection) {
  if (content.empty() || !(alpha > 0.001F)) return;
  auto* glyphs = renderer.get(content, pixelSize, false);
  if (glyphs == nullptr) return;
  batch.begin();
  batch.addQuadTopLeft(centerX - static_cast<float>(glyphs->width) * 0.5F, y,
      static_cast<float>(glyphs->width), static_cast<float>(glyphs->height), red, green, blue,
      alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, glyphs->texture, nullptr);
}

// Right-aligned string: `right` is where the string's trailing edge sits.
void drawStringRightAligned(batch::SpriteBatch& batch, text::TextRenderer& renderer,
    const std::string& content, float pixelSize, float right, float y, float red, float green,
    float blue, float alpha, const math::Mat4& projection) {
  if (content.empty() || !(alpha > 0.001F)) return;
  auto* glyphs = renderer.get(content, pixelSize, false);
  if (glyphs == nullptr) return;
  batch.begin();
  batch.addQuadTopLeft(right - static_cast<float>(glyphs->width), y,
      static_cast<float>(glyphs->width), static_cast<float>(glyphs->height), red, green, blue,
      alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, glyphs->texture, nullptr);
}
} // namespace

float hudUnit(const StageLayout& layout) noexcept {
  return layout.stage.height > 1.0e-6F ? layout.stage.height / 1000.0F : 1.0F;
}

float hudX(const StageLayout& layout, float uiX) noexcept {
  return layout.centerX() + uiX * hudUnit(layout);
}

float hudY(const StageLayout& layout, float uiY) noexcept {
  // Canvas y grows upwards, screen y grows downwards.
  return layout.centerY() - uiY * hudUnit(layout);
}

math::Rect hudBounds(const StageLayout& layout) noexcept {
  // The HUD never leaves the play field: the bar is clipped to it and the captions
  // sit just inside the bottom edge.
  return layout.stage;
}

void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, const HudTextures& textures, float alpha,
    const math::Mat4& projection) {
  if (!(alpha > 0.001F)) return;
  const float unit = hudUnit(layout);
  // The canvas half-width follows the stage, so corner widgets keep their distance
  // from the play field's edges on every aspect ratio.
  const float halfUiW = layout.stage.width * 0.5F / unit;

  // Progress bar: a full-width strip whose right pivot tracks the playback
  // progress, so the filled part always ends at the progress position.
  if (textures.timerLine != nullptr && textures.timerLine->valid()) {
    const float progress = static_cast<float>(std::clamp(values.progress, 0.0, 1.0));
    const float barWidth = kProgressWidth * unit;
    const float barHeight = kProgressHeight * unit;
    const float pivotRight = hudX(layout, halfUiW * (progress * 2.0F - 1.0F));
    const float top = hudY(layout, kProgressY);
    const float left = pivotRight - barWidth;
    // Clip the strip to the play field so it never spills past the stage edge.
    const float clipLeft = layout.stage.x;
    const float clipRight = std::min(layout.stage.right(), pivotRight);
    if (clipRight > clipLeft) {
      const float drawLeft = std::max(left, clipLeft);
      const float drawWidth = clipRight - drawLeft;
      if (drawWidth > 0.5F) {
        const float u0 = std::clamp((drawLeft - left) / barWidth, 0.0F, 1.0F);
        const float u1 = std::clamp((clipRight - left) / barWidth, 0.0F, 1.0F);
        batch.begin();
        batch.addQuadTopLeft(drawLeft, top, drawWidth, barHeight, 1.0F, 1.0F, 1.0F, alpha, u0,
            0.0F, u1, 1.0F);
        batch.flush(projection, *textures.timerLine, nullptr);
      }
    }
  }

  // Pause button in the top-left corner of the canvas.
  {
    const float centerX = hudX(layout, kPauseXOffset - halfUiW);
    const float centerY = hudY(layout, kPauseY);
    const float drawHeight = kPauseH * unit;
    const float drawWidth = drawHeight * kPauseVisualAspect;
    batch.begin();
    if (textures.pause != nullptr && textures.pause->valid()) {
      batch.addQuad(centerX, centerY, drawWidth, drawHeight, 0.0F, 1.0F, 1.0F, 1.0F, alpha, 0.0F,
          0.0F, 1.0F, 1.0F);
      batch.flush(projection, *textures.pause, nullptr);
    } else {
      batch.addQuad(centerX, centerY, drawWidth, drawHeight, 0.0F, 1.0F, 1.0F, 1.0F,
          0.55F * alpha, 0.0F, 0.0F, 1.0F, 1.0F);
      batch.flush(projection, gl::whiteTexture(), nullptr);
    }
  }

  // Score sits in the top-right corner. Its rect is centred on the canvas offset and
  // the number is right-aligned inside it, so the digits stay clear of the pause
  // button as the score grows.
  {
    const float rectCenterX = hudX(layout, halfUiW + kScoreXOffset);
    const float rectRight = rectCenterX + kScoreWidth * unit * 0.5F;
    const float scoreSize = kScoreTextSize * unit;
    const float scoreTop = hudY(layout, kScoreY) - scoreSize * 0.5F;
    drawStringRightAligned(batch, text, formatScore(values.score), scoreSize, rectRight, scoreTop,
        colors.scoreR, colors.scoreG, colors.scoreB, colors.textA * alpha, projection);

    if (values.showAccuracy) {
      // Accuracy sits one combo-line below the score, in the same column.
      const float accCenterY = hudY(layout, kScoreY - (kComboY - kComboTextY));
      const float accSize = kAccuracyTextSize * unit;
      drawStringRightAligned(batch, text, formatAccuracy(values.accuracy), accSize, rectRight,
          accCenterY - accSize * 0.5F, colors.textR, colors.textG, colors.textB,
          colors.textA * 0.85F * alpha, projection);
    }
  }

  // Combo number and label, both centred on the canvas.
  if (values.combo >= 3) {
    const float numberSize = kComboNumberTextSize * unit;
    drawStringCentered(batch, text, std::to_string(values.combo), numberSize, hudX(layout, 0.0F),
        hudY(layout, kComboY) - numberSize * 0.5F, colors.textR, colors.textG, colors.textB,
        colors.textA * alpha, projection);
    const float labelSize = kComboLabelTextSize * unit;
    drawStringCentered(batch, text, "COMBO", labelSize, hudX(layout, 0.0F),
        hudY(layout, kComboTextY) - labelSize * 0.5F, colors.textR, colors.textG, colors.textB,
        colors.textA * 0.85F * alpha, projection);
  }

  // Bottom captions: song title on the left, difficulty on the right. They are
  // pushed up from the stage edge by a resolution-independent offset.
  const float bottomOffset = kBottomTextFixAt1080 * layout.stage.height / 1080.0F;
  const float captionSize = kBottomTextSize * unit;
  {
    const float baseline = hudY(layout, kSongY) - bottomOffset - captionSize;
    drawString(batch, text, values.songName, captionSize, hudX(layout, kSongXOffset - halfUiW),
        baseline, colors.textR, colors.textG, colors.textB, colors.textA * 0.9F * alpha,
        projection);
    const float rightEdge = hudX(layout, halfUiW + kLevelXOffset);
    drawStringRightAligned(batch, text, values.levelName, captionSize, rightEdge, baseline,
        colors.textR, colors.textG, colors.textB, colors.textA * 0.9F * alpha, projection);
  }
}

} // namespace rechigros::render::scene
