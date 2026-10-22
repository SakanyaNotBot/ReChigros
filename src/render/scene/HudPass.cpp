#include "rechigros/render/scene/HudPass.hpp"

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/text/TextRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace rechigros::render::scene {
namespace {
// HUD canvas constants. Every widget position below is an offset on the 1000-unit
// tall canvas, measured from the play field centre (x right, y up).
constexpr float kPauseXOffset = 50.5887985F;
constexpr float kPauseY = 444.2999878F;
constexpr float kPauseW = 34.262F;
constexpr float kPauseH = 37.966F;
// The pause artwork is drawn with its intrinsic sprite aspect, not stretched.
constexpr float kPauseVisualAspect = 35.0F / 41.0F;

constexpr float kScoreXOffset = -237.3887939F;
constexpr float kScoreY = 445.7000122F;
constexpr float kScoreW = 400.0F;
constexpr float kAccY = kScoreY - (452.0F - 405.0F);
constexpr float kComboY = 452.0F;
constexpr float kComboTextY = 405.0F;
constexpr float kSongXOffset = 40.0F;
constexpr float kSongY = -473.2000122F;
constexpr float kSongW = 650.0F;
constexpr float kLevelXOffset = -40.0F;
constexpr float kLevelY = -473.2000122F;
constexpr float kLevelW = 650.0F;
constexpr float kProgressY = 500.0F;
constexpr float kProgressW = 1919.0F;
constexpr float kProgressH = 11.0F;

constexpr float kBottomTextYFixAt1080 = 5.0F;
constexpr float kBottomTextSize = 36.0F;
constexpr float kScoreTextSize = 50.0F;
constexpr float kAccTextSize = 26.0F;
constexpr float kComboNumberTextSize = 70.0F;
constexpr float kComboLabelTextSize = 24.0F;

// The HUD font is drawn with legacy character advances: no kerning/ligatures, a small
// positive tracking and a slightly negative space advance.
constexpr float kLegacyTrackingEm = 0.02F;
constexpr float kLegacySpaceAdjustEm = -0.012F;

constexpr float kRootScaleYStart = 1.1875F;
constexpr float kIntroDurationSec = 40.0F / 60.0F;
constexpr float kComboLabelOutroFadeSec = 35.0F / 60.0F;

constexpr float kPauseRingFadeInSec = 0.25F;
constexpr float kPauseRingVisibleSec = 1.0F;
constexpr float kPauseRingSizeUi = 63.488F;
constexpr float kPauseRingOffsetXUi = -1.7F;
constexpr float kPauseRingOffsetYUi = 0.69F;
constexpr float kPauseRingAlphaMax = 0.2F;

// Vertical root scale curve of the HUD start animation, one key per frame of the
// 40-frame tween.
constexpr float kLevelStartRootScaleY[] = {
    1.1875000F, 1.1871370F, 1.1859880F, 1.1839500F, 1.1809180F, 1.1767850F, 1.1714640F,
    1.1649030F, 1.1571190F, 1.1482240F, 1.1384360F, 1.1280640F, 1.1174500F, 1.1069190F,
    1.0967300F, 1.0870610F, 1.0780130F, 1.0696320F, 1.0619220F, 1.0548630F, 1.0484230F,
    1.0425640F, 1.0372430F, 1.0324230F, 1.0280630F, 1.0241300F, 1.0205910F, 1.0174170F,
    1.0145810F, 1.0120580F, 1.0098270F, 1.0078690F, 1.0061640F, 1.0046970F, 1.0034540F,
    1.0024200F, 1.0015830F, 1.0009330F, 1.0004580F, 1.0001500F, 1.0000000F};

float smoothStep01(float value) {
  const float p = std::clamp(value, 0.0F, 1.0F);
  return p * p * (3.0F - 2.0F * p);
}

float sampleCurve(const float* samples, int count, float t) {
  if (samples == nullptr || count <= 0) return std::clamp(t, 0.0F, 1.0F);
  if (count == 1) return samples[0];
  const float p = std::clamp(t, 0.0F, 1.0F) * static_cast<float>(count - 1);
  const int index = static_cast<int>(std::floor(p));
  if (index >= count - 1) return samples[count - 1];
  const float fraction = p - static_cast<float>(index);
  return samples[index] + (samples[index + 1] - samples[index]) * fraction;
}

std::string formatScore(int value) {
  const int clamped = std::clamp(value, 0, 1000000);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", clamped);
  return buffer;
}

std::string formatAccuracyHundredths(int hundredths) {
  const int value = std::clamp(hundredths, 0, 10000);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d.%02d%%", value / 100, value % 100);
  return buffer;
}

// Number of codepoints in a UTF-8 string; the reference counts Java chars, which only
// differs for supplementary codepoints.
int codepointCount(const std::string& text) {
  int count = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 1;
    if ((lead & 0x80U) == 0) {
      length = 1;
    } else if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
    }
    index += std::min(length, text.size() - index);
    ++count;
  }
  return count;
}

int spaceCount(const std::string& text) {
  int count = 0;
  for (const char character : text) {
    if (character == ' ') ++count;
  }
  return count;
}

// Advance width the HUD's legacy text mode produces: each character keeps the font's
// advance, adds a tracking step and the space advance is trimmed a little.
float measureLegacy(text::TextRenderer& renderer, const std::string& text, float sizePx) {
  if (text.empty()) return 0.0F;
  const int count = codepointCount(text);
  float width = renderer.measure(text, sizePx);
  if (count > 1) width += kLegacyTrackingEm * sizePx * static_cast<float>(count - 1);
  width += kLegacySpaceAdjustEm * sizePx * static_cast<float>(spaceCount(text));
  return width;
}

// Truncates a single line with an ellipsis so it fits the given width.
std::string ellipsizeLegacy(text::TextRenderer& renderer, const std::string& text,
    float maxWidthPx, float sizePx) {
  if (maxWidthPx <= 1.0F || text.empty()) return text;
  if (measureLegacy(renderer, text, sizePx) <= maxWidthPx) return text;
  const std::string ellipsis = "...";
  const float ellipsisWidth = measureLegacy(renderer, ellipsis, sizePx);
  if (ellipsisWidth >= maxWidthPx) return ellipsis;

  // Keep every cut on a codepoint boundary.
  std::vector<std::size_t> offsets;
  offsets.push_back(0);
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 1;
    if ((lead & 0x80U) == 0) {
      length = 1;
    } else if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
    }
    index += std::min(length, text.size() - index);
    offsets.push_back(index);
  }
  for (std::size_t count = offsets.size(); count-- > 1;) {
    const std::string prefix = text.substr(0, offsets[count - 1]);
    if (measureLegacy(renderer, prefix, sizePx) + ellipsisWidth <= maxWidthPx) {
      return prefix + ellipsis;
    }
  }
  return ellipsis;
}

// Canvas conversion helpers shared by the widget builders.
struct HudCanvas {
  const StageLayout& layout;
  float unit{1.0F};
  float halfUiW{500.0F};
  float rootY{0.0F};
  float rootScale{1.0F};

  [[nodiscard]] float x(float uiX) const noexcept { return layout.centerX() + uiX * unit; }
  [[nodiscard]] float y(float uiY) const noexcept { return layout.centerY() - uiY * unit; }
  [[nodiscard]] float size(float uiSize) const noexcept { return uiSize * unit; }
};

// Anchors one text line. The rasterised texture always stores the ink one pixel inside
// its border, so the target ink box maps to a top-left quad position.
struct PlacedText {
  text::TextTexture* glyphs{};
  float inkLeft{};
  float inkTop{};
};

// Draws one texture quad, applying the widget's attach-UI state and the HUD start/exit
// root scale. Every string has its own texture, so the batch is flushed right away.
void drawElementQuad(batch::SpriteBatch& batch, const gl::Texture& texture,
    const HudCanvas& canvas, HudElement element, const HudTransforms& transforms, float centerX,
    float centerY, float width, float height, float rotationDeg, float red, float green, float blue,
    float alpha, float u0, float v0, float u1, float v1, const math::Mat4& projection) {
  const auto& transform = transforms.element[static_cast<std::size_t>(element)];
  float cx = centerX;
  float cy = centerY;
  float w = width;
  float h = height;
  float rot = rotationDeg;
  float r = red;
  float g = green;
  float b = blue;
  float a = alpha;
  float uu0 = u0;
  float uu1 = u1;
  float vv0 = v0;
  float vv1 = v1;

  if (transform.valid) {
    const float scaleX = std::abs(transform.scaleX);
    const float scaleY = std::abs(transform.scaleY);
    w *= scaleX;
    h *= scaleY;
    rot += transform.rotateDeg;
    if (transform.scaleX < 0.0F) std::swap(uu0, uu1);
    if (transform.scaleY < 0.0F) std::swap(vv0, vv1);
    r *= transform.red;
    g *= transform.green;
    b *= transform.blue;
    a *= transform.alpha;
    cx += transform.translateX;
    cy += transform.translateY;
  }

  // The start and exit animations scale the whole HUD vertically about the play
  // field centre; the horizontal axis is untouched.
  cy = canvas.rootY + (cy - canvas.rootY) * canvas.rootScale;
  h *= canvas.rootScale;
  if (!(a > 0.002F) || !(w > 0.0F) || !(h > 0.0F)) return;

  batch.begin();
  batch.addQuad(cx, cy, w, h, rot, std::clamp(r, 0.0F, 1.0F), std::clamp(g, 0.0F, 1.0F),
      std::clamp(b, 0.0F, 1.0F), std::clamp(a, 0.0F, 1.0F), uu0, vv0, uu1, vv1);
  batch.flush(projection, texture, nullptr);
}

// Draws a placed single-line text at its anchored ink position.
void drawPlacedText(batch::SpriteBatch& batch, const HudCanvas& canvas, HudElement element,
    const HudTransforms& transforms, const PlacedText& placed, float red, float green, float blue,
    float alpha, const math::Mat4& projection) {
  if (placed.glyphs == nullptr || placed.glyphs->width <= 0 || placed.glyphs->height <= 0) return;
  const float textureWidth = static_cast<float>(placed.glyphs->width);
  const float textureHeight = static_cast<float>(placed.glyphs->height);
  const float left = placed.inkLeft - 1.0F;
  const float top = placed.inkTop - 1.0F;
  drawElementQuad(batch, placed.glyphs->texture, canvas, element, transforms, left + textureWidth * 0.5F,
      top + textureHeight * 0.5F, textureWidth, textureHeight, 0.0F, red, green, blue, alpha, 0.0F,
      0.0F, 1.0F, 1.0F, projection);
}

// HUD start/exit animation factor for the current frame.
float computeRootScale(float introProgress, float outroProgress) {
  if (outroProgress > 0.0F) {
    return 1.0F + (kRootScaleYStart - 1.0F) * smoothStep01(outroProgress);
  }
  const int count = static_cast<int>(sizeof(kLevelStartRootScaleY) / sizeof(float));
  return sampleCurve(kLevelStartRootScaleY, count, introProgress);
}

// Combo label fades out faster than the rest of the HUD during the exit animation.
float comboLabelOutroAlpha(float outroProgress) {
  if (outroProgress <= 0.0F) return 1.0F;
  const float progress = std::clamp(
      outroProgress * kIntroDurationSec / kComboLabelOutroFadeSec, 0.0F, 1.0F);
  return 1.0F - smoothStep01(progress);
}

// Score and accuracy catch-up state; only active when the score-jump effect is on.
struct ScoreDisplayState {
  double displayScore{std::numeric_limits<double>::quiet_NaN()};
  double displayAccuracy{std::numeric_limits<double>::quiet_NaN()};
  double lastUpdateSec{-1.0};
};

ScoreDisplayState& scoreDisplayState() {
  static ScoreDisplayState state;
  return state;
}

double approachDisplayValue(double current, double target, double stepFraction) {
  if (!std::isfinite(target)) return std::numeric_limits<double>::quiet_NaN();
  if (!std::isfinite(current)) return target;
  const double fraction = std::clamp(stepFraction, 0.0, 1.0);
  const double next = current + (target - current) * fraction;
  if (std::abs(target - next) < 0.0001) return target;
  return next;
}

// Advances the displayed score/accuracy and returns the values to draw.
void updateDisplayValues(const HudValues& values, int& scoreOut, int& accuracyHundredthsOut) {
  ScoreDisplayState& state = scoreDisplayState();
  double stepFraction = 0.5;
  double deltaMs = 50.0;
  if (std::isfinite(values.visualTimeSec)) {
    if (state.lastUpdateSec >= 0.0 && values.visualTimeSec >= state.lastUpdateSec) {
      deltaMs = (values.visualTimeSec - state.lastUpdateSec) * 1000.0;
    }
    state.lastUpdateSec = values.visualTimeSec;
  }
  deltaMs = std::clamp(deltaMs, 0.0, 100.0);
  stepFraction = deltaMs / 50.0;

  const int scoreTarget = std::clamp(values.score, 0, 1000000);
  const double accuracyTarget = values.showAccuracy ? values.accuracy
                                                    : std::numeric_limits<double>::quiet_NaN();

  if (!values.scoreJumpEffect) {
    state.displayScore = static_cast<double>(scoreTarget);
    state.displayAccuracy = accuracyTarget;
  } else {
    state.displayScore = approachDisplayValue(state.displayScore, static_cast<double>(scoreTarget),
        stepFraction);
    state.displayAccuracy = approachDisplayValue(state.displayAccuracy, accuracyTarget,
        stepFraction);
  }

  const int displayScore = std::isfinite(state.displayScore)
      ? std::clamp(static_cast<int>(std::lround(state.displayScore)), 0, 1000000)
      : scoreTarget;
  int hundredths = 0;
  if (std::isfinite(state.displayAccuracy)) {
    hundredths = static_cast<int>(std::lround(state.displayAccuracy * 100.0));
  } else if (std::isfinite(values.accuracy)) {
    hundredths = static_cast<int>(std::lround(values.accuracy * 100.0));
  }
  scoreOut = displayScore;
  accuracyHundredthsOut = std::clamp(hundredths, 0, 10000);
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

void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, const HudTextures& textures,
    const HudTransforms& transforms, float alpha, const math::Mat4& projection) {
  if (!(alpha > 0.001F) || !text.ready()) return;

  HudCanvas canvas{layout};
  canvas.unit = hudUnit(layout);
  canvas.halfUiW = layout.stage.height > 1.0e-6F
      ? layout.stage.width / layout.stage.height * 500.0F
      : 500.0F;
  canvas.rootY = layout.centerY();
  canvas.rootScale = computeRootScale(values.introProgress, values.outroProgress);
  const float comboAlpha = comboLabelOutroAlpha(values.outroProgress);

  // Pause button and its touch feedback ring.
  {
    const float centerX = canvas.x(kPauseXOffset - canvas.halfUiW);
    const float centerY = canvas.y(kPauseY);
    const float drawHeight = canvas.size(kPauseH);
    const float drawWidth = drawHeight * kPauseVisualAspect;

    if (textures.noteRing != nullptr && textures.noteRing->valid()
        && values.pauseRingAgeSec >= 0.0 && values.pauseRingAgeSec < kPauseRingVisibleSec) {
      const float progress =
          std::clamp(static_cast<float>(values.pauseRingAgeSec) / kPauseRingFadeInSec, 0.0F, 1.0F);
      const float ringAlpha = alpha * kPauseRingAlphaMax * progress;
      const float ringSize = canvas.size(kPauseRingSizeUi);
      drawElementQuad(batch, *textures.noteRing, canvas, HudElement::Pause, transforms,
          centerX + canvas.size(kPauseRingOffsetXUi), centerY - canvas.size(kPauseRingOffsetYUi),
          ringSize, ringSize, 0.0F, 1.0F, 1.0F, 1.0F, ringAlpha, 0.0F, 0.0F, 1.0F, 1.0F,
          projection);
    }

    if (textures.pause != nullptr && textures.pause->valid()) {
      drawElementQuad(batch, *textures.pause, canvas, HudElement::Pause, transforms, centerX,
          centerY, drawWidth, drawHeight, 0.0F, colors.textR, colors.textG, colors.textB,
          colors.textA * alpha, 0.0F, 0.0F, 1.0F, 1.0F, projection);
    }
  }

  // Progress bar: a full-width strip whose right pivot tracks playback, so the filled
  // part always ends at the progress position.
  if (textures.timerLine != nullptr && textures.timerLine->valid()
      && values.totalDurationSec > 0.001) {
    const float progress = static_cast<float>(
        std::clamp(values.playheadSec / values.totalDurationSec, 0.0, 1.0));
    const float barWidth = canvas.size(kProgressW);
    const float barHeight = canvas.size(kProgressH);
    const float pivotRight = canvas.x(canvas.halfUiW * (progress * 2.0F - 1.0F));
    const float top = canvas.y(kProgressY);
    const float left = pivotRight - barWidth;
    const float clipLeft = layout.stage.x;
    const float clipRight = std::min(layout.stage.right(), pivotRight);
    if (clipRight > clipLeft) {
      const float drawLeft = std::max(left, clipLeft);
      const float drawWidth = clipRight - drawLeft;
      if (drawWidth > 0.5F) {
        const float u0 = std::clamp((drawLeft - left) / barWidth, 0.0F, 1.0F);
        const float u1 = std::clamp((clipRight - left) / barWidth, 0.0F, 1.0F);
        drawElementQuad(batch, *textures.timerLine, canvas, HudElement::Bar, transforms,
            drawLeft + drawWidth * 0.5F, top + barHeight * 0.5F, drawWidth, barHeight, 0.0F,
            colors.textR, colors.textG, colors.textB, colors.textA * alpha, u0, 0.0F, u1, 1.0F,
            projection);
      }
    }
  }

  int displayScore = values.score;
  int accuracyHundredths = static_cast<int>(std::lround(values.accuracy * 100.0));
  updateDisplayValues(values, displayScore, accuracyHundredths);

  const float textAlpha = colors.textA * alpha;
  const float bottomTextYOffset = kBottomTextYFixAt1080 * layout.stage.height / 1080.0F;

  // Song title: bottom-left, width-fitted (not ellipsized) to its 650-unit box.
  if (!values.songName.empty()) {
    const float maxWidth = canvas.size(kSongW);
    float sizePx = canvas.size(kBottomTextSize);
    const float measured = measureLegacy(text, values.songName, sizePx);
    if (measured > maxWidth && measured > 0.0F) sizePx *= maxWidth / measured;
    if (auto* glyphs = text.get(values.songName, sizePx, false); glyphs != nullptr) {
      const float left = canvas.x(kSongXOffset - canvas.halfUiW);
      const float bottom = canvas.y(kSongY) - bottomTextYOffset;
      // The reference anchors the caption's bitmap so the font's descent line sits on
      // the bottom edge, which puts the baseline one descent above it.
      const float baseline = bottom - glyphs->descentPx;
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = left + std::floor(glyphs->inkLeft);
      placed.inkTop = baseline + glyphs->inkTop;
      drawPlacedText(batch, canvas, HudElement::Name, transforms, placed, colors.textR, colors.textG,
          colors.textB, textAlpha, projection);
    }
  }

  // Difficulty: bottom-right, ellipsized to its 650-unit box.
  if (!values.levelName.empty()) {
    const float maxWidth = canvas.size(kLevelW);
    const float sizePx = canvas.size(kBottomTextSize);
    const std::string label = ellipsizeLegacy(text, values.levelName, maxWidth, sizePx);
    if (auto* glyphs = text.get(label, sizePx, false); glyphs != nullptr) {
      const float right = canvas.x(canvas.halfUiW + kLevelXOffset);
      const float bottom = canvas.y(kLevelY) - bottomTextYOffset;
      const float contentWidth = std::ceil(measureLegacy(text, label, sizePx));
      const float baseline = bottom - glyphs->descentPx;
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = right - contentWidth + std::floor(glyphs->inkLeft);
      placed.inkTop = baseline + glyphs->inkTop;
      drawPlacedText(batch, canvas, HudElement::Level, transforms, placed, colors.textR,
          colors.textG, colors.textB, textAlpha, projection);
    }
  }

  // Score: seven digits, right-aligned to the score rect's right edge.
  {
    const std::string label = formatScore(displayScore);
    const float sizePx = canvas.size(kScoreTextSize);
    if (auto* glyphs = text.get(label, sizePx, false); glyphs != nullptr) {
      const float rectRight = canvas.x(canvas.halfUiW + kScoreXOffset) +
          canvas.size(kScoreW) * 0.5F;
      const float rectCenterY = canvas.y(kScoreY);
      const float contentWidth = std::ceil(text.measure(label, sizePx));
      const float contentHeight = std::ceil(glyphs->descentPx + glyphs->ascentPx);
      const float boxTop = rectCenterY - contentHeight * 0.5F;
      const float baseline = boxTop + glyphs->ascentPx;
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = rectRight - contentWidth + std::floor(glyphs->inkLeft);
      placed.inkTop = baseline + glyphs->inkTop;
      drawPlacedText(batch, canvas, HudElement::Score, transforms, placed, colors.scoreR,
          colors.scoreG, colors.scoreB, textAlpha, projection);
    }
  }

  // Realtime accuracy readout, one combo line below the score.
  if (values.showAccuracy) {
    const std::string label = formatAccuracyHundredths(accuracyHundredths);
    const float sizePx = canvas.size(kAccTextSize);
    if (auto* glyphs = text.get(label, sizePx, false); glyphs != nullptr) {
      const float rectRight = canvas.x(canvas.halfUiW + kScoreXOffset) +
          canvas.size(kScoreW) * 0.5F;
      const float rectCenterY = canvas.y(kAccY);
      const float contentWidth = std::ceil(text.measure(label, sizePx));
      const float contentHeight = std::ceil(glyphs->descentPx + glyphs->ascentPx);
      const float boxTop = rectCenterY - contentHeight * 0.5F;
      const float baseline = boxTop + glyphs->ascentPx;
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = rectRight - contentWidth + std::floor(glyphs->inkLeft);
      placed.inkTop = baseline + glyphs->inkTop;
      drawPlacedText(batch, canvas, HudElement::Score, transforms, placed, colors.textR,
          colors.textG, colors.textB, textAlpha * comboAlpha, projection);
    }
  }

  // Combo number and label, both centred, only once the combo reaches three.
  if (values.combo >= 3 && values.showCombo) {
    const std::string label =
        transforms.element[static_cast<std::size_t>(HudElement::ComboLabel)].text.empty()
        ? values.comboLabel
        : transforms.element[static_cast<std::size_t>(HudElement::ComboLabel)].text;
    const std::string number = std::to_string(values.combo);

    const float numberSize = canvas.size(kComboNumberTextSize);
    if (auto* glyphs = text.get(number, numberSize, false); glyphs != nullptr) {
      const float inkWidth = std::ceil(glyphs->inkRight) - std::floor(glyphs->inkLeft);
      const float contentWidth = std::ceil(std::max(inkWidth, text.measure(number, numberSize)));
      const float contentHeight = std::ceil(glyphs->descentPx + glyphs->ascentPx);
      const float centerX = canvas.x(0.0F);
      const float centerY = canvas.y(kComboY);
      const float boxTop = centerY - contentHeight * 0.5F;
      const float baseline = boxTop + glyphs->ascentPx;
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = centerX - contentWidth * 0.5F;
      placed.inkTop = baseline + glyphs->inkTop;
      drawPlacedText(batch, canvas, HudElement::ComboNumber, transforms, placed, colors.textR,
          colors.textG, colors.textB, textAlpha, projection);
    }

    const float labelSize = canvas.size(kComboLabelTextSize);
    if (auto* glyphs = text.get(label, labelSize, false); glyphs != nullptr) {
      const float inkWidth = std::ceil(glyphs->inkRight) - std::floor(glyphs->inkLeft);
      const float contentWidth = std::ceil(std::max(inkWidth, measureLegacy(text, label, labelSize)));
      const float contentHeight = std::ceil(glyphs->inkBottom) - std::floor(glyphs->inkTop);
      const float centerX = canvas.x(0.0F);
      const float centerY = canvas.y(kComboTextY);
      PlacedText placed;
      placed.glyphs = glyphs;
      placed.inkLeft = centerX - contentWidth * 0.5F;
      placed.inkTop = centerY - contentHeight * 0.5F;
      drawPlacedText(batch, canvas, HudElement::ComboLabel, transforms, placed, colors.textR,
          colors.textG, colors.textB, textAlpha, projection);
    }
  }
}

} // namespace rechigros::render::scene
