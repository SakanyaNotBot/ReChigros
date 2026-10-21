#include "rechigros/render/scene/HudPass.hpp"

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/text/TextRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rechigros::render::scene {
namespace {
// HUD layout in the reference 1000-unit tall canvas space.
constexpr float kPauseXOffset = 50.5887985F;
constexpr float kPauseY = 444.2999878F;
constexpr float kPauseW = 34.262F;
constexpr float kPauseH = 37.966F;
constexpr float kScoreXOffset = -237.3887939F;
constexpr float kScoreY = 445.7000122F;
constexpr float kScoreTextSize = 50.0F;
constexpr float kAccuracyTextSize = 26.0F;
constexpr float kComboY = 452.0F;
constexpr float kComboTextY = 405.0F;
constexpr float kComboNumberTextSize = 70.0F;
constexpr float kComboLabelTextSize = 24.0F;
constexpr float kProgressY = 500.0F;
constexpr float kProgressH = 11.0F;

float stageUnit(const StageLayout& layout) {
  return layout.stage.height > 1.0e-6F ? layout.stage.height / 1000.0F : 1.0F;
}

// HUD x is measured from the stage centre in canvas units.
float hudX(const StageLayout& layout, float uiX) {
  return layout.centerX() + uiX * stageUnit(layout);
}

// HUD y grows upwards in canvas space, so it subtracts from the screen y.
float hudY(const StageLayout& layout, float uiY) {
  return layout.centerY() - uiY * stageUnit(layout);
}

std::string formatScore(int value) {
  const int clamped = std::min(9999999, std::max(0, value));
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", clamped);
  return buffer;
}

std::string formatAccuracy(double accuracy) {
  const int hundredths = static_cast<int>(std::lround(std::clamp(accuracy, 0.0, 1.0) * 10000.0));
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d.%02d%%", hundredths / 100, hundredths % 100);
  return buffer;
}

// Draws one string. Each string has its own texture, so the batch is flushed
// immediately after it is filled.
void drawString(batch::SpriteBatch& batch, text::TextRenderer& renderer, const std::string& content,
    float pixelSize, float x, float y, float red, float green, float blue, float alpha,
    bool centered, const math::Mat4& projection) {
  auto* texture = renderer.get(content, pixelSize, false);
  if (texture == nullptr) return;
  const auto width = static_cast<float>(texture->width);
  const auto height = static_cast<float>(texture->height);
  batch.begin();
  const float drawX = centered ? x - width * 0.5F : x;
  batch.addQuadTopLeft(drawX, y, width, height, red, green, blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, texture->texture, nullptr);
}
} // namespace

void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, float alpha, const math::Mat4& projection) {
  if (alpha <= 0.001F) return;
  const float unit = stageUnit(layout);

  // Progress bar along the top edge of the play field.
  const float barY = hudY(layout, kProgressY);
  const float barH = kProgressH * unit;
  batch.begin();
  batch.addQuadTopLeft(layout.stage.x, barY, layout.stage.width, barH, 1.0F, 1.0F, 1.0F,
      0.18F * alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  const auto progress = static_cast<float>(std::clamp(values.progress, 0.0, 1.0));
  if (progress > 0.0F) {
    batch.addQuadTopLeft(layout.stage.x, barY, layout.stage.width * progress, barH, 1.0F, 1.0F,
        1.0F, 0.85F * alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  }
  // Pause glyph placeholder at the reference UI position.
  const float pauseCenterX = hudX(layout, kPauseXOffset - layout.stage.width / unit * 0.5F);
  const float pauseCenterY = hudY(layout, kPauseY);
  batch.addQuad(pauseCenterX, pauseCenterY, kPauseW * unit, kPauseH * unit, 0.0F, 1.0F, 1.0F,
      1.0F, 0.55F * alpha, 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, gl::whiteTexture(), nullptr);

  // Score, accuracy and combo all rasterise their own textures.
  const float scoreSize = kScoreTextSize * unit;
  drawString(batch, text, formatScore(values.score), scoreSize, hudX(layout, kScoreXOffset),
      hudY(layout, kScoreY), colors.scoreR, colors.scoreG, colors.scoreB, colors.textA * alpha,
      false, projection);

  const float accuracySize = kAccuracyTextSize * unit;
  drawString(batch, text, formatAccuracy(values.accuracy), accuracySize,
      hudX(layout, kScoreXOffset), hudY(layout, kScoreY - kScoreTextSize - 6.0F), colors.textR,
      colors.textG, colors.textB, colors.textA * 0.8F * alpha, false, projection);

  if (values.combo > 2) {
    const float comboSize = kComboNumberTextSize * unit;
    drawString(batch, text, std::to_string(values.combo) + "x", comboSize, layout.centerX(),
        hudY(layout, kComboY) - comboSize, colors.textR, colors.textG, colors.textB,
        colors.textA * alpha, true, projection);
    const float labelSize = kComboLabelTextSize * unit;
    drawString(batch, text, "COMBO", labelSize, layout.centerX(), hudY(layout, kComboTextY),
        colors.textR, colors.textG, colors.textB, colors.textA * 0.75F * alpha, true, projection);
  }
}

} // namespace rechigros::render::scene
