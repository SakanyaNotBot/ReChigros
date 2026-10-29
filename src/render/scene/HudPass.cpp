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

} // namespace

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

namespace {

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
  HudRevealTransform reveal;

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
  cx = canvas.reveal.x(cx);
  cy = canvas.reveal.y(cy);
  w = canvas.reveal.size(w);
  h = canvas.reveal.size(h);
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

// A label's metrics: from the cached glyph strip when every character is available (the
// composing path never allocates or uploads), otherwise from a rasterised string texture.
struct LabelMetrics {
  const text::GlyphStrip* strip{};
  text::TextTexture* texture{};
  text::ComposedTextMetrics values;
  std::array<text::ComposedGlyphPlacement, text::kMaxComposedGlyphs> placements{};
  int placementCount{};
};

bool loadLabelMetrics(text::TextRenderer& text, const std::string& label, float pixelSize,
    LabelMetrics& out) {
  if (const text::GlyphStrip* strip =
          text.compose(label, pixelSize, out.values, out.placements, out.placementCount)) {
    out.strip = strip;
    return true;
  }
  out.texture = text.get(label, pixelSize, false);
  if (out.texture == nullptr) return false;
  out.values.advanceWidth = out.texture->advanceWidth;
  out.values.inkLeft = out.texture->inkLeft;
  out.values.inkTop = out.texture->inkTop;
  out.values.inkRight = out.texture->inkRight;
  out.values.inkBottom = out.texture->inkBottom;
  out.values.ascentPx = out.texture->ascentPx;
  out.values.descentPx = out.texture->descentPx;
  out.values.textureWidth = out.texture->width;
  out.values.textureHeight = out.texture->height;
  return true;
}

// Whether the strip path reproduces the element transform exactly. Rotation and mirrored
// scales are not representable glyph-by-glyph, so those fall back to the string texture.
bool elementSupportsComposedText(const HudTransforms& transforms, HudElement element) {
  const auto& transform = transforms.element[static_cast<std::size_t>(element)];
  if (!transform.valid) return true;
  return transform.rotateDeg == 0.0F && transform.scaleX > 0.0F && transform.scaleY > 0.0F;
}

// Composes a label from the glyph strip: one quad per glyph, batched into a single flush.
// The quads occupy the same pixels as the equivalent rasterised string texture.
void drawComposedLabel(batch::SpriteBatch& batch, const HudCanvas& canvas, HudElement element,
    const HudTransforms& transforms, const text::GlyphStrip& strip,
    const LabelMetrics& metrics, float inkLeft, float inkTop, float red, float green, float blue,
    float alpha, const math::Mat4& projection) {
  if (metrics.placementCount <= 0 || strip.width <= 0 || strip.height <= 0) return;
  const auto& transform = transforms.element[static_cast<std::size_t>(element)];
  float scaleX = 1.0F;
  float scaleY = 1.0F;
  float translateX = 0.0F;
  float translateY = 0.0F;
  float r = red;
  float g = green;
  float b = blue;
  float a = alpha;
  if (transform.valid) {
    scaleX = std::abs(transform.scaleX);
    scaleY = std::abs(transform.scaleY);
    translateX = transform.translateX;
    translateY = transform.translateY;
    r *= transform.red;
    g *= transform.green;
    b *= transform.blue;
    a *= transform.alpha;
  }
  const float baseLeft = inkLeft - 1.0F;
  const float baseTop = inkTop - 1.0F;
  const float wholeWidth = static_cast<float>(metrics.values.textureWidth);
  const float wholeHeight = static_cast<float>(metrics.values.textureHeight);
  const float wholeCenterX = baseLeft + wholeWidth * 0.5F;
  const float wholeCenterY = baseTop + wholeHeight * 0.5F;
  if (!(a > 0.002F) || !(scaleX > 0.0F) || !(scaleY > 0.0F)) return;
  const float textureWidth = static_cast<float>(strip.width);
  const float textureHeight = static_cast<float>(strip.height);
  batch.begin();
  for (int index = 0; index < metrics.placementCount; ++index) {
    const auto& placement = metrics.placements[static_cast<std::size_t>(index)];
    const float glyphLeft = baseLeft + static_cast<float>(placement.targetX);
    const float glyphTop = baseTop + static_cast<float>(placement.targetY);
    float width = static_cast<float>(placement.width) * scaleX;
    const float height = static_cast<float>(placement.height) * scaleY;
    float centerX = wholeCenterX
        + (glyphLeft + static_cast<float>(placement.width) * 0.5F - wholeCenterX) * scaleX
        + translateX;
    float centerY = wholeCenterY
        + (glyphTop + static_cast<float>(placement.height) * 0.5F - wholeCenterY) * scaleY
        + translateY;
    centerY = canvas.rootY + (centerY - canvas.rootY) * canvas.rootScale;
    float scaledHeight = height * canvas.rootScale;
    centerX = canvas.reveal.x(centerX);
    centerY = canvas.reveal.y(centerY);
    width = canvas.reveal.size(width);
    scaledHeight = canvas.reveal.size(scaledHeight);
    if (!(width > 0.0F) || !(scaledHeight > 0.0F)) continue;
    batch.addQuad(centerX, centerY, width, scaledHeight, 0.0F, std::clamp(r, 0.0F, 1.0F),
        std::clamp(g, 0.0F, 1.0F), std::clamp(b, 0.0F, 1.0F), std::clamp(a, 0.0F, 1.0F),
        static_cast<float>(placement.sourceX) / textureWidth,
        static_cast<float>(placement.sourceY) / textureHeight,
        static_cast<float>(placement.sourceX + placement.width) / textureWidth,
        static_cast<float>(placement.sourceY + placement.height) / textureHeight);
  }
  batch.flush(projection, strip.texture, nullptr);
}

// Draws one label at its anchored ink position (same contract as drawPlacedText).
void drawLabel(batch::SpriteBatch& batch, const HudCanvas& canvas, HudElement element,
    const HudTransforms& transforms, text::TextRenderer& text, const std::string& label,
    float pixelSize, const LabelMetrics& metrics, float inkLeft, float inkTop, float red,
    float green, float blue, float alpha, const math::Mat4& projection) {
  if (metrics.strip != nullptr && elementSupportsComposedText(transforms, element)) {
    drawComposedLabel(batch, canvas, element, transforms, *metrics.strip, metrics, inkLeft, inkTop,
        red, green, blue, alpha, projection);
    return;
  }
  if (metrics.strip == nullptr && metrics.texture != nullptr) {
    PlacedText placed;
    placed.glyphs = metrics.texture;
    placed.inkLeft = inkLeft;
    placed.inkTop = inkTop;
    drawPlacedText(batch, canvas, element, transforms, placed, red, green, blue, alpha, projection);
    return;
  }
  auto* glyphs = text.get(label, pixelSize, false);
  if (glyphs == nullptr) return;
  PlacedText placed;
  placed.glyphs = glyphs;
  placed.inkLeft = inkLeft;
  placed.inkTop = inkTop;
  drawPlacedText(batch, canvas, element, transforms, placed, red, green, blue, alpha, projection);
}

// Draws a composed label at an absolute top-left, used by the untransformed FPS readout.
void drawComposedTopLeft(batch::SpriteBatch& batch, const text::GlyphStrip& strip,
    const LabelMetrics& metrics, float left, float top, float red, float green, float blue,
    float alpha, const math::Mat4& projection) {
  if (metrics.placementCount <= 0 || strip.width <= 0 || strip.height <= 0) return;
  const float textureWidth = static_cast<float>(strip.width);
  const float textureHeight = static_cast<float>(strip.height);
  batch.begin();
  for (int index = 0; index < metrics.placementCount; ++index) {
    const auto& placement = metrics.placements[static_cast<std::size_t>(index)];
    const float width = static_cast<float>(placement.width);
    const float height = static_cast<float>(placement.height);
    batch.addQuad(left + static_cast<float>(placement.targetX) + width * 0.5F,
        top + static_cast<float>(placement.targetY) + height * 0.5F, width, height, 0.0F, red, green,
        blue, alpha, static_cast<float>(placement.sourceX) / textureWidth,
        static_cast<float>(placement.sourceY) / textureHeight,
        static_cast<float>(placement.sourceX + placement.width) / textureWidth,
        static_cast<float>(placement.sourceY + placement.height) / textureHeight);
  }
  batch.flush(projection, strip.texture, nullptr);
}


} // namespace

// HUD start/exit animation factor for the current frame.
float hudRootScaleY(float introProgress, float outroProgress) noexcept {
  if (outroProgress > 0.0F) {
    return 1.0F + (kRootScaleYStart - 1.0F) * smoothStep01(outroProgress);
  }
  const int count = static_cast<int>(sizeof(kLevelStartRootScaleY) / sizeof(float));
  return sampleCurve(kLevelStartRootScaleY, count, introProgress);
}

namespace {

float computeRootScale(float introProgress, float outroProgress) {
  return hudRootScaleY(introProgress, outroProgress);
}

} // namespace

float comboLabelOutroAlpha(float outroProgress) noexcept {
  // The label clears faster than the rest of the HUD, so its own fade window is shorter than
  // the HUD exit and it is already gone by the time the field starts blurring out.
  if (outroProgress <= 0.0F) return 1.0F;
  const float progress = std::clamp(
      outroProgress * kIntroDurationSec / kComboLabelOutroFadeSec, 0.0F, 1.0F);
  return 1.0F - smoothStep01(progress);
}

namespace {

// The reference advances the displayed values by the elapsed wall time divided by a fixed
// catch-up window, saturating at one window per frame so a long frame cannot overshoot.
constexpr double kScoreDisplayCatchUpSec = 0.050;

} // namespace

double approachDisplayValue(double current, double target, double deltaMs) noexcept {
  if (!std::isfinite(target)) return std::numeric_limits<double>::quiet_NaN();
  if (!std::isfinite(current)) return target;
  // The step is the elapsed fraction of the catch-up window, saturating at one window's
  // worth so a long frame cannot overshoot the target.
  double factor = deltaMs / (kScoreDisplayCatchUpSec * 1000.0);
  if (!std::isfinite(factor) || factor < 0.0) factor = 0.0;
  if (factor > 1.0) factor = 1.0;
  const double next = current + (target - current) * factor;
  if (std::abs(target - next) < 0.0001) return target;
  return next;
}

namespace {

// Advances the displayed score/accuracy and returns the values to draw. The state lives in
// the caller's HudValues so it is per-run: a previous run's displayed score must not carry
// over into the next one's first frame.
void updateHudDisplayValues(HudValues& values, int& scoreOut, int& accuracyHundredthsOut) {
  if (values.displayReset) {
    values.displayScore = std::numeric_limits<double>::quiet_NaN();
    values.displayAccuracy = std::numeric_limits<double>::quiet_NaN();
    values.displayLastUpdateSec = -1.0;
    values.displayReset = false;
  }
  double deltaMs = kScoreDisplayCatchUpSec * 1000.0;
  if (std::isfinite(values.visualTimeSec)) {
    if (values.displayLastUpdateSec >= 0.0 && values.visualTimeSec >= values.displayLastUpdateSec) {
      deltaMs = (values.visualTimeSec - values.displayLastUpdateSec) * 1000.0;
    }
    values.displayLastUpdateSec = values.visualTimeSec;
  }
  // The reference clamps the frame delta to one catch-up window; a paused frame or a seek
  // must not fast-forward the readout.
  if (!std::isfinite(deltaMs) || deltaMs < 0.0) deltaMs = kScoreDisplayCatchUpSec * 1000.0;
  if (deltaMs > 100.0) deltaMs = 100.0;

  const int scoreTarget = std::clamp(values.score, 0, 1000000);
  const double accuracyTarget = values.showAccuracy ? values.accuracy
                                                    : std::numeric_limits<double>::quiet_NaN();

  if (!values.scoreJumpEffect) {
    values.displayScore = static_cast<double>(scoreTarget);
    values.displayAccuracy = accuracyTarget;
  } else {
    values.displayScore = approachDisplayValue(values.displayScore,
        static_cast<double>(scoreTarget), deltaMs);
    values.displayAccuracy = approachDisplayValue(values.displayAccuracy, accuracyTarget, deltaMs);
  }

  const int displayScore = std::isfinite(values.displayScore)
      ? std::clamp(static_cast<int>(std::lround(values.displayScore)), 0, 1000000)
      : scoreTarget;
  int hundredths = 0;
  if (std::isfinite(values.displayAccuracy)) {
    hundredths = static_cast<int>(std::lround(values.displayAccuracy * 100.0));
  } else if (std::isfinite(values.accuracy)) {
    hundredths = static_cast<int>(std::lround(values.accuracy * 100.0));
  }
  scoreOut = displayScore;
  accuracyHundredthsOut = std::clamp(hundredths, 0, 10000);
}

// Judgement offset dial colours, matching the reference HUD constants.
constexpr float kOffsetGreenR = 0.42F;constexpr float kOffsetGreenG = 1.00F;
constexpr float kOffsetGreenB = 0.36F;
constexpr float kOffsetYellowR = 1.00F;
constexpr float kOffsetYellowG = 0.86F;
constexpr float kOffsetYellowB = 0.28F;
constexpr float kOffsetRedR = 1.00F;
constexpr float kOffsetRedG = 0.36F;
constexpr float kOffsetRedB = 0.31F;
constexpr float kOffsetMaxAngleDeg = 60.0F;

float offsetMarkFadeOut(float progress) {
  const float inverse = 1.0F - std::clamp(progress, 0.0F, 1.0F);
  return inverse * inverse;
}

// One reference layout step in pixels: density 1.75 on a 1080-tall viewport.
float referenceDp(const StageLayout& layout) {
  return 1.75F * std::max(0.2F, layout.viewHeight / 1080.0F);
}

// Adds one glow-stroked segment to the accumulated line batch. The three widths and the
// falloff alphas mirror the reference's three-layer stroke.
void addOffsetStroke(batch::SpriteBatch& batch, float x0, float y0, float x1, float y1,
    float width, float dp, float red, float green, float blue, float alpha) {
  if (!(alpha > 0.0F)) return;
  const float antiAlias = std::max(1.0F, dp * 0.45F);
  batch.addLine(x0, y0, x1, y1, width + antiAlias * 2.0F, red, green, blue, alpha * 0.10F);
  batch.addLine(x0, y0, x1, y1, width + antiAlias, red, green, blue, alpha * 0.18F);
  batch.addLine(x0, y0, x1, y1, width, red, green, blue, alpha * 0.88F);
}

float offsetPointX(float cx, float angleDeg, float radius) {
  const float radians = angleDeg * 3.14159265358979323846F / 180.0F;
  return cx + std::sin(radians) * radius;
}

float offsetPointY(float baseY, float angleDeg, float radius) {
  const float radians = angleDeg * 3.14159265358979323846F / 180.0F;
  return baseY - std::cos(radians) * radius;
}

void addOffsetArc(batch::SpriteBatch& batch, float cx, float baseY, float radius,
    float startAngle, float endAngle, float width, float dp, float red, float green, float blue,
    float alpha) {
  if (endAngle <= startAngle || !(alpha > 0.0F)) return;
  const int steps = std::max(3, static_cast<int>(std::ceil((endAngle - startAngle) / 2.0F)));
  float previousX = offsetPointX(cx, startAngle, radius);
  float previousY = offsetPointY(baseY, startAngle, radius);
  for (int index = 1; index <= steps; ++index) {
    const float t = static_cast<float>(index) / static_cast<float>(steps);
    const float angle = startAngle + (endAngle - startAngle) * t;
    const float x = offsetPointX(cx, angle, radius);
    const float y = offsetPointY(baseY, angle, radius);
    addOffsetStroke(batch, previousX, previousY, x, y, width, dp, red, green, blue, alpha);
    previousX = x;
    previousY = y;
  }
}

void addOffsetRadial(batch::SpriteBatch& batch, float cx, float baseY, float angleDeg,
    float innerRadius, float outerRadius, float width, float dp, float red, float green, float blue,
    float alpha) {
  addOffsetStroke(batch, offsetPointX(cx, angleDeg, innerRadius),
      offsetPointY(baseY, angleDeg, innerRadius), offsetPointX(cx, angleDeg, outerRadius),
      offsetPointY(baseY, angleDeg, outerRadius), width, dp, red, green, blue, alpha);
}

void offsetColour(int judgement, float& red, float& green, float& blue) {
  if (judgement == 1) {
    red = kOffsetYellowR;
    green = kOffsetYellowG;
    blue = kOffsetYellowB;
  } else if (judgement == 2) {
    red = kOffsetRedR;
    green = kOffsetRedG;
    blue = kOffsetRedB;
  } else {
    red = kOffsetGreenR;
    green = kOffsetGreenG;
    blue = kOffsetGreenB;
  }
}

// Draws one texture with its top-left corner at (left, top), without any HUD transform.
void drawTextureTopLeftUntransformed(batch::SpriteBatch& batch, const gl::Texture& texture,
    float left, float top, float width, float height, float red, float green, float blue,
    float alpha, const math::Mat4& projection) {
  if (!(alpha > 0.001F) || width <= 0.0F || height <= 0.0F) return;
  batch.begin();
  batch.addQuad(left + width * 0.5F, top + height * 0.5F, width, height, 0.0F,
      std::clamp(red, 0.0F, 1.0F), std::clamp(green, 0.0F, 1.0F), std::clamp(blue, 0.0F, 1.0F),
      std::clamp(alpha, 0.0F, 1.0F), 0.0F, 0.0F, 1.0F, 1.0F);
  batch.flush(projection, texture, nullptr);
}

// Centre-anchored texture draw, used by the pointer glyph.
void drawTextureCentred(batch::SpriteBatch& batch, const gl::Texture& texture, float centerX,
    float centerY, float width, float height, float rotationDeg, float red, float green, float blue,
    float alpha, const math::Mat4& projection) {
  if (!(alpha > 0.001F) || width <= 0.0F || height <= 0.0F) return;
  batch.begin();
  batch.addQuad(centerX, centerY, width, height, rotationDeg, red, green, blue, alpha, 0.0F, 0.0F,
      1.0F, 1.0F);
  batch.flush(projection, texture, nullptr);
}

// The judgement offset dial / line, ported from the reference HUD pass. The shipped
// reference has no numeric offset readout, so the dial is the only feedback it draws.
void drawHitOffsetIndicator(batch::SpriteBatch& batch, const StageLayout& layout,
    const HudHitOffset& hitOffset, float hudAlpha, const HudTextures& textures,
    const math::Mat4& projection) {
  if (hitOffset.mode == 0 || hitOffset.suppressed || !(hudAlpha > 0.001F)) return;
  const float viewWidth = layout.viewWidth;
  const float viewHeight = layout.viewHeight;
  if (viewWidth <= 0.0F || viewHeight <= 0.0F) return;

  const float dp = referenceDp(layout);
  const float alpha = hudAlpha * 0.68F;
  if (alpha <= 0.001F) return;

  const float meterWidth = std::clamp(viewWidth * 0.18F, dp * 120.0F, dp * 190.0F);
  const float radius = meterWidth * 0.5F;
  const float centerX = viewWidth * 0.5F;
  const float arcWidth = std::max(dp * 1.6F, meterWidth * 0.012F);
  constexpr float pi = 3.14159265358979323846F;
  const float maxAngleRad = kOffsetMaxAngleDeg * pi / 180.0F;
  const float endpointCos = std::cos(maxAngleRad);
  const float endpointSin = std::sin(maxAngleRad);
  const float lineHalfWidth = radius * endpointSin;
  const float arcHeight = radius * (1.0F - endpointCos);
  const float arrowRadius = radius - dp * 9.0F;

  // Pointer glyph metrics; the reference accounts for its footprint when placing the
  // dial so it never runs off the bottom edge.
  float arrowWidth = std::clamp(meterWidth * 0.095F, dp * 12.0F, dp * 17.0F);
  float arrowHeight = arrowWidth;
  const bool hasArrow = textures.hitOffsetArrow != nullptr && textures.hitOffsetArrow->valid() &&
      textures.hitOffsetArrow->width() > 0 && textures.hitOffsetArrow->height() > 0;
  if (hasArrow) {
    arrowHeight = arrowWidth * static_cast<float>(textures.hitOffsetArrow->height()) /
        std::max(1.0F, static_cast<float>(textures.hitOffsetArrow->width()));
  }

  const float perfectAngle = std::clamp(
      static_cast<float>(hitOffset.perfectWindowSec / std::max(0.001F, hitOffset.badWindowSec)) *
          kOffsetMaxAngleDeg, 0.0F, kOffsetMaxAngleDeg);
  const float goodAngle = std::clamp(
      static_cast<float>(hitOffset.goodWindowSec / std::max(0.001F, hitOffset.badWindowSec)) *
          kOffsetMaxAngleDeg, perfectAngle, kOffsetMaxAngleDeg);

  const bool lineMode = hitOffset.mode == 2;
  const bool canDraw = textures.white != nullptr && textures.white->valid();
  if (lineMode) {
    // The pointer keeps its footprint even when its texture is unavailable, so the line
    // clears the same space either way and does not shift when an asset is missing.
    const float bottomSafety = arrowHeight + dp * 5.0F;
    const float lineY = viewHeight - bottomSafety;
    const float half = lineHalfWidth;
    const float perfectX = std::clamp(
        static_cast<float>(hitOffset.perfectWindowSec /
            std::max(0.001F, hitOffset.badWindowSec)) * half, 0.0F, half);
    const float goodX = std::clamp(
        static_cast<float>(hitOffset.goodWindowSec /
            std::max(0.001F, hitOffset.badWindowSec)) * half, perfectX, half);
    if (canDraw) {
      batch.begin();
      // The line variant uses the same red/yellow/green bands as the dial, mirrored about
      // the centre.
      addOffsetStroke(batch, centerX - half, lineY, centerX - goodX, lineY, arcWidth, dp,
          kOffsetRedR, kOffsetRedG, kOffsetRedB, alpha);
      addOffsetStroke(batch, centerX - goodX, lineY, centerX - perfectX, lineY, arcWidth, dp,
          kOffsetYellowR, kOffsetYellowG, kOffsetYellowB, alpha);
      addOffsetStroke(batch, centerX - perfectX, lineY, centerX + perfectX, lineY, arcWidth, dp,
          kOffsetGreenR, kOffsetGreenG, kOffsetGreenB, alpha);
      addOffsetStroke(batch, centerX + perfectX, lineY, centerX + goodX, lineY, arcWidth, dp,
          kOffsetYellowR, kOffsetYellowG, kOffsetYellowB, alpha);
      addOffsetStroke(batch, centerX + goodX, lineY, centerX + half, lineY, arcWidth, dp,
          kOffsetRedR, kOffsetRedG, kOffsetRedB, alpha);
      addOffsetStroke(batch, centerX, lineY - dp * 4.0F, centerX, lineY + dp * 4.0F,
          std::max(dp * 0.8F, arcWidth * 0.55F), dp, 1.0F, 1.0F, 1.0F, alpha * 0.7F);
      for (int index = 0; index < hitOffset.markCount; ++index) {
        const auto& mark = hitOffset.marks[static_cast<std::size_t>(index)];
        if (!mark.active || mark.age < 0.0F || mark.age >= 1.0F) continue;
        const float markAlpha = offsetMarkFadeOut(mark.age) * hudAlpha;
        if (markAlpha <= 0.001F) continue;
        float red = 0.0F;
        float green = 0.0F;
        float blue = 0.0F;
        offsetColour(mark.judgement, red, green, blue);
        const float fraction = std::clamp(mark.angleDeg / kOffsetMaxAngleDeg, -1.0F, 1.0F);
        const float x = centerX + fraction * half;
        addOffsetStroke(batch, x, lineY - dp * 3.0F, x, lineY + dp * 4.0F, arcWidth * 2.5F, dp,
            red, green, blue, markAlpha * 0.16F);
        addOffsetStroke(batch, x, lineY - dp * 3.0F, x, lineY + dp * 4.0F, arcWidth * 1.45F, dp,
            red, green, blue, markAlpha * 0.34F);
        addOffsetStroke(batch, x, lineY - dp * 3.0F, x, lineY + dp * 4.0F, arcWidth * 0.7F, dp,
            red, green, blue, markAlpha * 0.9F);
      }
      batch.flush(projection, *textures.white, nullptr);
    }
    if (hasArrow) {
      const float fraction = std::clamp(hitOffset.pointerAngleDeg / kOffsetMaxAngleDeg, -1.0F, 1.0F);
      const float x = centerX + fraction * half;
      drawTextureCentred(batch, *textures.hitOffsetArrow, x, lineY + dp * 3.0F + arrowHeight * 0.5F,
          arrowWidth, arrowHeight, 0.0F, 1.0F, 1.0F, 1.0F, hudAlpha * 0.82F, projection);
    }
    return;
  }

  // Sector dial.
  const float arrowBelowArc = endpointCos * std::max(0.0F, radius - arrowRadius);
  const float arrowHalfBounds = 0.5F * std::sqrt(arrowWidth * arrowWidth +
      arrowHeight * arrowHeight);
  // The pointer's footprint is reserved whether or not its texture loaded, so the dial sits
  // at the same height on every device rather than jumping when an asset is missing.
  const float bottomSafety = std::max(dp * 3.0F, arrowBelowArc + arrowHalfBounds + dp * 2.0F);
  const float baseY = viewHeight - arcHeight - bottomSafety + radius;
  if (canDraw) {
    batch.begin();
    addOffsetArc(batch, centerX, baseY, radius, -kOffsetMaxAngleDeg, -goodAngle, arcWidth, dp,
        kOffsetRedR, kOffsetRedG, kOffsetRedB, alpha);
    addOffsetArc(batch, centerX, baseY, radius, -goodAngle, -perfectAngle, arcWidth, dp,
        kOffsetYellowR, kOffsetYellowG, kOffsetYellowB, alpha);
    addOffsetArc(batch, centerX, baseY, radius, -perfectAngle, perfectAngle, arcWidth, dp,
        kOffsetGreenR, kOffsetGreenG, kOffsetGreenB, alpha);
    addOffsetArc(batch, centerX, baseY, radius, perfectAngle, goodAngle, arcWidth, dp,
        kOffsetYellowR, kOffsetYellowG, kOffsetYellowB, alpha);
    addOffsetArc(batch, centerX, baseY, radius, goodAngle, kOffsetMaxAngleDeg, arcWidth, dp,
        kOffsetRedR, kOffsetRedG, kOffsetRedB, alpha);
    addOffsetRadial(batch, centerX, baseY, 0.0F, radius - dp * 6.0F, radius + dp * 6.0F,
        std::max(dp * 0.8F, meterWidth * 0.005F), dp, 1.0F, 1.0F, 1.0F, alpha * 0.7F);
    for (int index = 0; index < hitOffset.markCount; ++index) {
      const auto& mark = hitOffset.marks[static_cast<std::size_t>(index)];
      if (!mark.active || mark.age < 0.0F || mark.age >= 1.0F) continue;
      const float markAlpha = offsetMarkFadeOut(mark.age) * hudAlpha;
      if (markAlpha <= 0.001F) continue;
      float red = 0.0F;
      float green = 0.0F;
      float blue = 0.0F;
      offsetColour(mark.judgement, red, green, blue);
      addOffsetRadial(batch, centerX, baseY, mark.angleDeg, radius - dp * 3.0F, radius + dp * 4.0F,
          arcWidth * 2.5F, dp, red, green, blue, markAlpha * 0.16F);
      addOffsetRadial(batch, centerX, baseY, mark.angleDeg, radius - dp * 3.0F, radius + dp * 4.0F,
          arcWidth * 1.45F, dp, red, green, blue, markAlpha * 0.34F);
      addOffsetRadial(batch, centerX, baseY, mark.angleDeg, radius - dp * 3.0F, radius + dp * 4.0F,
          arcWidth * 0.7F, dp, red, green, blue, markAlpha * 0.9F);
    }
    batch.flush(projection, *textures.white, nullptr);
  }
  if (hasArrow) {
    // The pointer rides the inner arrow radius, not the arc itself.
    drawTextureCentred(batch, *textures.hitOffsetArrow,
        offsetPointX(centerX, hitOffset.pointerAngleDeg, arrowRadius),
        offsetPointY(baseY, hitOffset.pointerAngleDeg, arrowRadius), arrowWidth, arrowHeight,
        hitOffset.pointerAngleDeg, 1.0F, 1.0F, 1.0F, hudAlpha * 0.82F, projection);
  }
  // The shipped reference dial has no numeric readout under the arc; the raw offset text
  // was a port-only addition and is intentionally not drawn here.
}

// Reference frame-rate readout: bottom-left "N FPS" on a half-transparent plate.
void drawFpsCounter(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, float hudAlpha, const HudTextures& textures,
    const math::Mat4& projection) {
  if (!values.showFps || !(hudAlpha > 0.001F)) return;
  const float dp = referenceDp(layout);
  const float textPx = dp * 12.0F;
  const int fps = std::max(0, static_cast<int>(std::lround(values.fps)));
  const std::string label = std::to_string(fps) + " FPS";
  LabelMetrics metrics;
  if (!loadLabelMetrics(text, label, textPx, metrics)) return;
  // The composed path reports the same virtual texture size and ink border as the rasterised
  // string, so the plate and the top-left anchoring are unchanged.
  const float width = static_cast<float>(metrics.values.textureWidth);
  const float height = static_cast<float>(metrics.values.textureHeight);
  if (!(width > 0.0F) || !(height > 0.0F)) return;
  const float left = layout.stage.x + dp * 4.0F;
  const float top = std::max(layout.stage.y + dp * 2.0F,
      layout.stage.y + layout.stage.height - dp * 2.0F - height);
  if (textures.white != nullptr && textures.white->valid()) {
    batch.begin();
    batch.addQuad(left - dp * 4.0F + (width + dp * 8.0F) * 0.5F,
        top - dp * 2.0F + (height + dp * 4.0F) * 0.5F, width + dp * 8.0F, height + dp * 4.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 0.5F * hudAlpha, 0.0F, 0.0F, 1.0F, 1.0F);
    batch.flush(projection, *textures.white, nullptr);
  }
  if (metrics.strip != nullptr) {
    drawComposedTopLeft(batch, *metrics.strip, metrics, left, top, 1.0F, 1.0F, 1.0F, hudAlpha,
        projection);
  } else if (metrics.texture != nullptr) {
    drawTextureTopLeftUntransformed(batch, metrics.texture->texture, left, top, width, height, 1.0F,
        1.0F, 1.0F, hudAlpha, projection);
  }
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

HudRevealTransform hudRevealTransform(const StageLayout& layout, bool active, float scale) noexcept {
  HudRevealTransform transform;
  transform.centerX = layout.centerX();
  transform.centerY = layout.centerY();
  if (active) transform.scale = std::clamp(std::isfinite(scale) ? scale : 1.0F, 0.30F, 1.0F);
  return transform;
}

bool hudNeedsStageClip(bool revealActive, bool fullscreen) noexcept {
  return !revealActive && !fullscreen;
}

void updateHudDisplayValuesForTest(HudValues& values, int& scoreOut,
    int& accuracyHundredthsOut) {
  updateHudDisplayValues(values, scoreOut, accuracyHundredthsOut);
}

void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    HudValues& values, const HudColors& colors, const HudTextures& textures,
    const HudTransforms& transforms, const HudHitOffset& hitOffset, float alpha,
    const math::Mat4& projection) {
  if (!(alpha > 0.001F) || !text.ready()) return;

  HudCanvas canvas{layout};
  canvas.unit = hudUnit(layout);
  // The HUD canvas follows the stage's actual aspect. A degenerate stage cannot express one,
  // so it falls back to the 16:9 reference aspect rather than to a bare half-height.
  const float hudAspect = layout.stage.height > 1.0e-6F
      ? layout.stage.width / layout.stage.height
      : 16.0F / 9.0F;
  // The HUD follows the full stage aspect. On a wider viewport its edge widgets reach into the
  // side blanks, where the stage clip or the side-column overlay hides them.
  canvas.halfUiW = hudAspect * 500.0F;
  canvas.rootY = layout.centerY();
  canvas.rootScale = computeRootScale(values.introProgress, values.outroProgress);
  canvas.reveal = hudRevealTransform(layout, values.revealCameraActive, values.revealScale);
  const float comboAlpha = comboLabelOutroAlpha(values.outroProgress);

  // Pause button and its touch feedback ring share the same camera path as every other widget.
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
          centerX + canvas.size(kPauseRingOffsetXUi),
          centerY - canvas.size(kPauseRingOffsetYUi), ringSize, ringSize, 0.0F,
          1.0F, 1.0F, 1.0F, ringAlpha, 0.0F, 0.0F, 1.0F, 1.0F, projection);
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
  updateHudDisplayValues(values, displayScore, accuracyHundredths);

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
    LabelMetrics metrics;
    if (loadLabelMetrics(text, label, sizePx, metrics)) {
      const float rectRight = canvas.x(canvas.halfUiW + kScoreXOffset) +
          canvas.size(kScoreW) * 0.5F;
      const float rectCenterY = canvas.y(kScoreY);
      const float contentWidth = std::ceil(text.measure(label, sizePx));
      const float contentHeight = std::ceil(metrics.values.descentPx + metrics.values.ascentPx);
      const float boxTop = rectCenterY - contentHeight * 0.5F;
      const float baseline = boxTop + metrics.values.ascentPx;
      drawLabel(batch, canvas, HudElement::Score, transforms, text, label, sizePx, metrics,
          rectRight - contentWidth + std::floor(metrics.values.inkLeft),
          baseline + metrics.values.inkTop, colors.scoreR, colors.scoreG, colors.scoreB, textAlpha,
          projection);
    }
  }

  // Realtime accuracy readout, one combo line below the score.
  if (values.showAccuracy) {
    const std::string label = formatAccuracyHundredths(accuracyHundredths);
    const float sizePx = canvas.size(kAccTextSize);
    LabelMetrics metrics;
    if (loadLabelMetrics(text, label, sizePx, metrics)) {
      const float rectRight = canvas.x(canvas.halfUiW + kScoreXOffset) +
          canvas.size(kScoreW) * 0.5F;
      const float rectCenterY = canvas.y(kAccY);
      const float contentWidth = std::ceil(text.measure(label, sizePx));
      const float contentHeight = std::ceil(metrics.values.descentPx + metrics.values.ascentPx);
      const float boxTop = rectCenterY - contentHeight * 0.5F;
      const float baseline = boxTop + metrics.values.ascentPx;
      drawLabel(batch, canvas, HudElement::Score, transforms, text, label, sizePx, metrics,
          rectRight - contentWidth + std::floor(metrics.values.inkLeft),
          baseline + metrics.values.inkTop, colors.textR, colors.textG, colors.textB,
          textAlpha * comboAlpha, projection);
    }
  }

  // Combo number and label, both centred, only once the combo reaches three. A failed level
  // challenge replaces them with the distorted block field, which the caller draws over the
  // same anchor; the plain widgets are suppressed by `showCombo` in that state.
  if (values.combo >= 3 && values.showCombo) {
    const std::string label =
        transforms.element[static_cast<std::size_t>(HudElement::ComboLabel)].text.empty()
        ? values.comboLabel
        : transforms.element[static_cast<std::size_t>(HudElement::ComboLabel)].text;
    const std::string number = std::to_string(values.combo);

    const float numberSize = canvas.size(kComboNumberTextSize);
    LabelMetrics numberMetrics;
    if (loadLabelMetrics(text, number, numberSize, numberMetrics)) {
      const float inkWidth =
          std::ceil(numberMetrics.values.inkRight) - std::floor(numberMetrics.values.inkLeft);
      const float contentWidth = std::ceil(std::max(inkWidth, text.measure(number, numberSize)));
      const float contentHeight =
          std::ceil(numberMetrics.values.descentPx + numberMetrics.values.ascentPx);
      const float centerX = canvas.x(0.0F);
      const float centerY = canvas.y(kComboY);
      const float boxTop = centerY - contentHeight * 0.5F;
      const float baseline = boxTop + numberMetrics.values.ascentPx;
      drawLabel(batch, canvas, HudElement::ComboNumber, transforms, text, number, numberSize,
          numberMetrics, centerX - contentWidth * 0.5F,
          baseline + numberMetrics.values.inkTop, colors.textR, colors.textG, colors.textB,
          textAlpha, projection);
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
          colors.textG, colors.textB, textAlpha * comboAlpha, projection);
    }
  }

  drawHitOffsetIndicator(batch, layout, hitOffset, alpha, textures, projection);
  drawFpsCounter(batch, text, layout, values, alpha, textures, projection);
}

} // namespace rechigros::render::scene
