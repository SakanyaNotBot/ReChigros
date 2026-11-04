#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiWidgets.hpp"

#include <algorithm>
#include <filesystem>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

constexpr float kSkewSlope = 0.2679492F; // tan(15 degrees)
constexpr float kDesignWidth = 960.0F;
constexpr float kDesignHeight = 540.0F;

// The settlement animation follows the reference's official timing: one linear 3.5 s pass that
// drives the panel slides and the staged reveals.
constexpr double kSettlementSeconds = 3.5;
// The reference enters the settlement with its blurred cover alone. The outro capture measures
// the cover fading in over ~0.55 s, holding an empty backdrop for ~1.85 s, and only then showing
// the settlement content; the board/card animation still starts when the content appears.
constexpr float kBackdropFadeSeconds = 0.55F;
constexpr float kBackdropHoldSeconds = 1.85F;
constexpr float kContentStartSeconds = kBackdropFadeSeconds + kBackdropHoldSeconds;
constexpr float kSlideUnits = 1200.0F; // design canvas widths, as in the reference
constexpr float kBackgroundDim = 0.4F;
// The corner layer margins in the reference are plain density pixels while the plates scale with
// the canvas, so the margin in design units is smaller than its density value.
constexpr float kReferenceDensity = 1.75F;

// Glyph sizes: the labels live inside the scaled 960 x 540 canvas, so their pixel size is the
// layout value multiplied by the canvas scale. ui::TextStyle sizes are density units, hence the
// conversion.
constexpr float kCanvasTextScale = 1.0F / ui::kDensityRatio;

// Reference layout sizes, in canvas design units. The glyph sizes are calibrated so the rendered
// ink matches the reference capture: the reference build draws this page's text noticeably
// smaller than the reconstructed layout numbers suggest, and not by a constant factor.
constexpr float kScoreTextSize = 19.5F;
constexpr float kComboValueSize = 10.0F;
constexpr float kComboLabelSize = 4.9F;
constexpr float kStdValueSize = 10.4F;
constexpr float kStdLabelSize = 4.9F;
constexpr float kAccValueSize = 10.5F;
constexpr float kAccLabelSize = 4.65F;
constexpr float kCountValueSize = 11.5F;
constexpr float kCountLabelSize = 5.5F;
constexpr float kEarlyValueSize = 6.3F;
constexpr float kEarlyLabelSize = 6.3F;
constexpr float kSongTextSize = 14.0F;
constexpr float kDifficultyTextSize = 8.8F;
constexpr float kModeTagTextSize = 11.5F;

constexpr std::uint32_t kPanelFill = 0x80000000U;
constexpr std::uint32_t kMenuButtonFill = 0x9A222A35U;
constexpr std::uint32_t kMenuButtonStroke = 0x96FFFFFFU;

// ---------------------------------------------------------------------------
// animation curves (the reference's settlement easings)
// ---------------------------------------------------------------------------

float allEase(float x) {
  if (x <= 0.0F) return 0.0F;
  if (x >= 1.0F) return 1.0F;
  const float k = 1.0F - x;
  return 1.0F - std::pow(k, 10.0F);
}

float delayedSquare(float x, float k) {
  x = (x - k) * (1.0F / (1.0F - k)) * 5.0F;
  if (x <= 0.0F) return 0.0F;
  if (x >= 1.0F) return 1.0F;
  return x * x;
}

float levelSizeEase(float x) {
  const float t = 3.0F * std::max(x - 1.0F / 6.0F, 0.0F);
  return std::max(0.5F - t * t * t * t, 0.0F) + 1.0F;
}

float buttonEase(float x) {
  if (x <= 0.0F) return 0.0F;
  if (x >= 1.0F) return 1.0F;
  const float k = 1.0F - x;
  return 1.0F - k * k * k;
}

struct Settlement {
  float progress{1.0F};
  float cardDx{};
  float upperDx{};
  float middleDx{};
  float lowerDx{};
  float scoreAlpha{1.0F};
  float gradeAlpha{1.0F};
  float gradeScale{1.0F};
  float middleAlpha{1.0F};
  float lowerAlpha{1.0F};
  float tagsAlpha{1.0F};
  float retryDx{};
  float cornerDx{};
};

Settlement settlementAt(float p) {
  Settlement s;
  s.progress = p;
  s.cardDx = kSlideUnits * (1.0F - allEase(p));
  s.upperDx = kSlideUnits * (1.0F - allEase(p - 0.015F));
  s.middleDx = kSlideUnits * (1.0F - allEase(p - 0.035F));
  s.lowerDx = kSlideUnits * (1.0F - allEase(p - 0.055F));
  s.scoreAlpha = delayedSquare(p, 0.125F);
  s.gradeScale = levelSizeEase(p);
  s.gradeAlpha = delayedSquare(p, 0.25F);
  s.middleAlpha = delayedSquare(p - 0.02F, 0.25F);
  s.lowerAlpha = delayedSquare(p - 0.04F, 0.25F);
  s.tagsAlpha = delayedSquare(p - 0.03F, 0.1875F);
  const float button = buttonEase(p * 4.5F - 0.95F);
  s.retryDx = -102.0F * (1.0F - button);
  s.cornerDx = 272.0F * (1.0F - button);
  return s;
}

// State of one visit to the settlement: when its entrance started, and whether the judgement
// distribution overlay is open. Both are per-visit, so the page can be entered again cleanly.
struct ResultPageState {
  double lastDrawSec{-1.0};
  double animationStartSec{-1.0};
  bool histogramVisible{};
};

ResultPageState& resultPageState() {
  static ResultPageState state;
  return state;
}

// ---------------------------------------------------------------------------
// small drawing helpers
// ---------------------------------------------------------------------------

ImU32 whiteAlpha(float alpha) {
  const int value = static_cast<int>(std::lround(255.0F * std::clamp(alpha, 0.0F, 1.0F)));
  return IM_COL32(255, 255, 255, value);
}

ui::TextStyle boardText(
    float designSize, ImU32 color, ui::HAlign align, ui::VAlign valign, bool bold, bool fitWidth) {
  ui::TextStyle style;
  style.sizeDp = designSize * kCanvasTextScale;
  style.bold = bold;
  style.color = color;
  style.align = align;
  style.valign = valign;
  style.fitWidth = fitWidth;
  return style;
}

ui::Box shifted(const ui::Box& box, float dx, float dy = 0.0F) {
  return ui::Box{box.x0 + dx, box.y0 + dy, box.x1 + dx, box.y1 + dy};
}

// Draws an image cropped into a skewed panel. The visible region of a rectangular image clipped
// by the panel is the panel quad itself, and the texture coordinate of every vertex is linear in
// its horizontal position, so one quad reproduces the clip exactly.
void drawPanelImage(ImDrawList* drawList, const ui::Canvas& canvas, const ui::Box& box,
    std::uint64_t texture, int textureWidth, int textureHeight) {
  if (drawList == nullptr || texture == 0 || textureWidth <= 0 || textureHeight <= 0) return;
  const ui::PxRect rect = ui::toPixels(canvas, box);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 0.0F || height <= 0.0F) return;
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  const float textureAspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
  const float boxAspect = width / height;
  if (textureAspect > boxAspect) {
    const float fraction = boxAspect / textureAspect;
    u0 = (1.0F - fraction) * 0.5F;
    u1 = 1.0F - u0;
  } else if (textureAspect < boxAspect) {
    const float fraction = textureAspect / boxAspect;
    v0 = (1.0F - fraction) * 0.5F;
    v1 = 1.0F - v0;
  }
  const float skew = std::min(kSkewSlope * height, width * 0.42F);
  const auto uAt = [&](float x) { return u0 + (x - rect.min.x) / width * (u1 - u0); };
  const ImVec2 p0(rect.min.x + skew, rect.min.y);
  const ImVec2 p1(rect.max.x, rect.min.y);
  const ImVec2 p2(rect.max.x - skew, rect.max.y);
  const ImVec2 p3(rect.min.x, rect.max.y);
  drawList->AddImageQuad(static_cast<ImTextureID>(texture), p0, p1, p2, p3,
      ImVec2(uAt(p0.x), v0), ImVec2(u1, v0), ImVec2(uAt(p2.x), v1), ImVec2(u0, v1));
}

// Vertical scrim of the illustration panel: clear at the top, mostly black at the bottom, where
// the title row sits.
void drawPanelScrim(ImDrawList* drawList, const ui::Canvas& canvas, const ui::Box& box) {
  const ui::PxRect rect = ui::toPixels(canvas, box);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 0.0F || height <= 0.0F) return;
  const float skew = std::min(kSkewSlope * height, width * 0.42F);
  // Stops: 0 at 0, 0 at 0.25, 0x40 at 0.5, 0x80 at 0.75, 0xCC at 1.
  const float stops[5] = {0.0F, 0.0F, 0.25F, 0.5F, 0.8F};
  const float positions[5] = {0.0F, 0.25F, 0.5F, 0.75F, 1.0F};
  const auto alphaAt = [&](float t) {
    for (int index = 1; index < 5; ++index) {
      if (t <= positions[index]) {
        const float span = positions[index] - positions[index - 1];
        const float k = span <= 0.0F ? 0.0F : (t - positions[index - 1]) / span;
        return stops[index - 1] + (stops[index] - stops[index - 1]) * k;
      }
    }
    return stops[4];
  };
  constexpr int kSteps = 48;
  for (int step = 0; step < kSteps; ++step) {
    const float t0 = static_cast<float>(step) / static_cast<float>(kSteps);
    const float t1 = static_cast<float>(step + 1) / static_cast<float>(kSteps);
    const float y0 = rect.min.y + height * t0;
    const float y1 = rect.min.y + height * t1;
    const ImVec2 points[4] = {
        ImVec2(rect.min.x + skew * (1.0F - t0), y0),
        ImVec2(rect.max.x - skew * t0, y0),
        ImVec2(rect.max.x - skew * t1, y1),
        ImVec2(rect.min.x + skew * (1.0F - t1), y1),
    };
    const int alpha = static_cast<int>(std::lround(alphaAt((t0 + t1) * 0.5F) * 255.0F));
    if (alpha <= 0) continue;
    drawList->AddConvexPolyFilled(points, 4, IM_COL32(0, 0, 0, alpha));
  }
}

// The bundled corner glyphs are dark masks that the reference white-tints at runtime. Immediate
// mode blending cannot recolour a texture, so the same shapes are drawn as white vector glyphs.
void drawTintedGlyph(ImDrawList* drawList, const ui::PxRect& rect, int shape, ImU32 color) {
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 1.0F || height <= 1.0F) return;
  const ImVec2 center((rect.min.x + rect.max.x) * 0.5F, (rect.min.y + rect.max.y) * 0.5F);
  float radius = std::min(width, height) * 0.5F;
  if (shape == 0) radius *= 0.78F; // restart ring is drawn smaller than its plate padding
  if (shape == 1) radius *= 0.88F; // continue triangle
  switch (shape) {
    case 0: { // restart: a ring with an arrow head, opened on the right
      const float thickness = std::max(1.0F, radius * 0.22F);
      drawList->PathArcTo(center, radius - thickness * 0.5F, 1.05F, 5.60F, 40);
      drawList->PathStroke(color, 0, thickness);
      const ImVec2 tip(center.x + radius * 0.90F, center.y - radius * 0.55F);
      drawList->AddTriangleFilled(ImVec2(tip.x - radius * 0.34F, tip.y - radius * 0.18F),
          ImVec2(tip.x + radius * 0.20F, tip.y + radius * 0.30F),
          ImVec2(tip.x - radius * 0.10F, tip.y + radius * 0.66F), color);
      break;
    }
    case 1: { // continue: a right pointing triangle
      drawList->AddTriangleFilled(ImVec2(center.x - radius * 0.54F, center.y - radius * 0.80F),
          ImVec2(center.x + radius * 0.86F, center.y),
          ImVec2(center.x - radius * 0.54F, center.y + radius * 0.80F), color);
      break;
    }
    case 2: { // replay: a ring with an arrow head on the left
      const float thickness = std::max(1.0F, radius * 0.20F);
      drawList->PathArcTo(center, radius - thickness * 0.5F, -0.60F, 3.95F, 40);
      drawList->PathStroke(color, 0, thickness);
      const ImVec2 tip(center.x - radius * 0.78F, center.y + radius * 0.28F);
      drawList->AddTriangleFilled(ImVec2(tip.x + radius * 0.10F, tip.y - radius * 0.50F),
          ImVec2(tip.x + radius * 0.38F, tip.y + radius * 0.18F),
          ImVec2(tip.x - radius * 0.22F, tip.y + radius * 0.24F), color);
      break;
    }
    default: { // save: a tray with a downward arrow
      const float thickness = std::max(1.0F, radius * 0.16F);
      const float left = rect.min.x + width * 0.14F;
      const float right = rect.max.x - width * 0.14F;
      const float top = rect.min.y + height * 0.14F;
      const float bottom = rect.max.y - height * 0.12F;
      drawList->AddLine(ImVec2(left, top), ImVec2(left, bottom), color, thickness);
      drawList->AddLine(ImVec2(right, top), ImVec2(right, bottom), color, thickness);
      drawList->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), color, thickness);
      drawList->AddLine(ImVec2(left, top), ImVec2(left + width * 0.24F, top), color, thickness);
      drawList->AddLine(ImVec2(right - width * 0.24F, top), ImVec2(right, top), color, thickness);
      drawList->AddLine(ImVec2(center.x, rect.min.y + height * 0.02F),
          ImVec2(center.x, rect.min.y + height * 0.50F), color, thickness);
      drawList->AddTriangleFilled(ImVec2(center.x - width * 0.22F, rect.min.y + height * 0.40F),
          ImVec2(center.x + width * 0.22F, rect.min.y + height * 0.40F),
          ImVec2(center.x, rect.min.y + height * 0.68F), color);
      break;
    }
  }
}

std::string formatMultiplier(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
  return buffer;
}

// The library normalises the difficulty label with an en space between the prefix and "Lv.";
// the reference settlement renders that separator as a plain space.
std::string plainSpaces(const std::string& value) {
  static const char* const kSpaces[] = {"\xE2\x80\x82", "\xE2\x80\x89", "\xE2\x80\x8A",
      "\xC2\xA0", "\xE3\x80\x80"};
  std::string result = value;
  for (const char* space : kSpaces) {
    const std::string from = space;
    std::size_t position = 0;
    while ((position = result.find(from, position)) != std::string::npos) {
      result.replace(position, from.size(), " ");
      ++position;
    }
  }
  return result;
}

std::uint64_t resultCover(AppContext& context, int maxSize) {
  const auto record = context.library.find(context.play.chartId);
  if (record) return coverTexture(context, *record, maxSize);
  if (!context.play.bgFile.empty()) return context.textures.textureFor(context.play.bgFile, maxSize);
  return 0;
}

// Path of the artwork to blur for the backdrop; empty when the run has no artwork.
std::filesystem::path resultCoverPath(AppContext& context) {
  const auto record = context.library.find(context.play.chartId);
  if (record) return record->bgFile;
  return context.play.bgFile;
}

// The settlement shows the chart artwork as a heavily blurred backdrop. The backdrop texture
// is pre-blurred by the texture cache with the reference blur (third-size copy, three box
// passes of radius ~5% of the artwork width, bilinear upscale on draw), so it is a single
// filtered image instead of a stack of shifted copies.
void drawBlurredBackdrop(AppContext& context, ImDrawList* drawList, ImVec2 viewport) {
  const std::filesystem::path path = resultCoverPath(context);
  if (path.empty()) return;
  const std::uint64_t texture = context.textures.blurredBackdropFor(path);
  if (texture == 0) return;
  const int textureWidth = static_cast<int>(context.textures.width(texture));
  const int textureHeight = static_cast<int>(context.textures.height(texture));
  if (textureWidth <= 0 || textureHeight <= 0) return;

  const float screenAspect = viewport.x / std::max(1.0F, viewport.y);
  // The blurred texture keeps the artwork aspect; centre-crop it like the reference's
  // background view.
  const float referenceAspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  if (referenceAspect > screenAspect) {
    const float fraction = screenAspect / referenceAspect;
    u0 = (1.0F - fraction) * 0.5F;
    u1 = 1.0F - u0;
  } else if (referenceAspect < screenAspect) {
    const float fraction = referenceAspect / screenAspect;
    v0 = (1.0F - fraction) * 0.5F;
    v1 = 1.0F - v0;
  }
  drawList->AddImage(static_cast<ImTextureID>(texture), ImVec2(0.0F, 0.0F), viewport,
      ImVec2(u0, v0), ImVec2(u1, v1));
}

// ---------------------------------------------------------------------------
// replay actions
// ---------------------------------------------------------------------------

void saveReplay(AppContext& context) {
  if (!context.lastRunHasReplay) {
    notify(context, "本次游玩没有可保存的回放");
    return;
  }
  if (!context.savedReplayId.empty()) {
    notify(context, "回放已保存");
    return;
  }
  const std::string id = context.replays.save(context.game.recorded);
  if (id.empty()) {
    notify(context, "回放保存失败");
    return;
  }
  context.savedReplayId = id;
  notify(context, "回放已保存");
}

void playReplay(AppContext& context) {
  if (!context.lastRunHasReplay) {
    notify(context, "本次游玩没有可播放的回放");
    return;
  }
  if (context.savedReplayId.empty()) saveReplay(context);
  if (context.savedReplayId.empty()) return;
  startSavedReplay(context, context.savedReplayId);
}

// ---------------------------------------------------------------------------
// judgement distribution overlay
// ---------------------------------------------------------------------------

// Judgement histogram: bars over the illustration window, revealed from the reference's sliding
// overlay. Only reachable when the judgement-detail setting is on.
void drawJudgementHistogram(AppContext& context, ImDrawList* drawList, const ui::Canvas& canvas,
    const ui::Box& box, bool challenge, float alpha, float offsetX) {
  const ui::PxRect rect = ui::toPixels(canvas, box);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  if (width <= 1.0F || height <= 1.0F || alpha <= 0.01F) return;
  const float skew = std::min(kSkewSlope * height, width * 0.42F);
  const float inset = std::min(skew, width * 0.4F) + offsetX;
  const float left = rect.min.x + inset;
  const float right = rect.max.x - inset;
  const auto dx = [&](float value) { return (value - canvas.origin.x) / canvas.scale; };
  const auto dy = [&](float value) { return (value - canvas.origin.y) / canvas.scale; };

  // Darkened backdrop over the artwork, then a vertical gradient like the reference view.
  ImVec2 backdrop[4] = {
      ImVec2(rect.min.x + skew, rect.min.y),
      ImVec2(rect.max.x, rect.min.y),
      ImVec2(rect.max.x - skew, rect.max.y),
      ImVec2(rect.min.x, rect.max.y),
  };
  const int baseAlpha = static_cast<int>(std::lround(216.0F * alpha));
  drawList->AddConvexPolyFilled(backdrop, 4, IM_COL32(0, 0, 0, baseAlpha));

  ImGui::PushClipRect(ImVec2(left, rect.min.y), ImVec2(right, rect.max.y), true);
  const float padX = std::min(canvas.units(18.0F), (right - left) * 0.06F);
  const float graphLeft = left + padX;
  const float graphRight = right - padX;
  const float titleBaseline = rect.min.y + canvas.units(31.0F);
  ui::TextStyle title = boardText(16.0F, whiteAlpha(alpha), ui::HAlign::Left, ui::VAlign::Top,
      false, true);
  ui::drawLabel(drawList, canvas,
      ui::Box{dx(graphLeft), dy(titleBaseline - canvas.units(16.0F)), dx(graphRight),
          dy(titleBaseline + canvas.units(4.0F))},
      "判定分布", title, context.nowSec);
  drawList->AddRectFilled(ImVec2(graphLeft, titleBaseline + canvas.units(6.0F)),
      ImVec2(graphLeft + std::min(canvas.units(48.0F), canvas.units(0.0F) + (graphRight - graphLeft)
                                                                          * 0.2F),
          titleBaseline + canvas.units(7.5F)),
      whiteAlpha(0.5F * alpha));

  const auto& bins = context.lastResult.judgementOffsetBins;
  const int count = static_cast<int>(bins.size());
  const int rangeMs = challenge ? game::PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                                : game::PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS;
  float graphTop = rect.min.y + canvas.units(56.0F);
  float baseline = rect.max.y - canvas.units(36.0F);
  if (baseline - graphTop < canvas.units(42.0F)) {
    graphTop = rect.min.y + canvas.units(44.0F);
    baseline = rect.max.y - canvas.units(31.0F);
  }
  const float graphHeight = std::max(canvas.units(20.0F), baseline - graphTop);
  int maximum = 0;
  int peak = -1;
  for (int index = 0; index < count; ++index) {
    if (bins[static_cast<std::size_t>(index)] > maximum) {
      maximum = bins[static_cast<std::size_t>(index)];
      peak = index;
    }
  }
  const float slot = (graphRight - graphLeft) / static_cast<float>(std::max(1, count));
  const float barWidth =
      std::max(canvas.units(3.5F), std::min(slot * 0.58F, canvas.units(14.0F)));
  const float minBar = std::max(canvas.units(4.0F), graphHeight * 0.04F);
  const float maxBar =
      graphHeight < canvas.units(86.0F) ? graphHeight : std::max(canvas.units(20.0F),
                                                                graphHeight * 0.9F);
  const float radius = barWidth * 0.45F;
  for (int index = 0; index < count; ++index) {
    const float centerX = graphLeft + slot * (static_cast<float>(index) + 0.5F);
    const int value = bins[static_cast<std::size_t>(index)];
    float barHeight = 0.0F;
    int barAlpha = 0;
    if (maximum > 0 && value > 0) {
      barHeight = minBar + static_cast<float>(value) / static_cast<float>(maximum)
          * (maxBar - minBar);
      barAlpha = 224;
    } else {
      barHeight = std::max(canvas.units(2.5F), minBar * 0.55F);
      barAlpha = 54;
    }
    const ImU32 color = index == count / 2
        ? IM_COL32(255, 255, 255, static_cast<int>(std::lround(barAlpha * alpha)))
        : IM_COL32(201, 211, 230, static_cast<int>(std::lround(barAlpha * alpha)));
    drawList->AddRectFilled(ImVec2(centerX - barWidth * 0.5F, baseline - barHeight),
        ImVec2(centerX + barWidth * 0.5F, baseline), color, radius);
  }
  // Peak tag above the tallest bar.
  if (maximum > 0 && peak >= 0) {
    const int binWidthMs = std::max(1, (rangeMs * 2) / std::max(1, count - 1));
    const int centerMs = -rangeMs + peak * binWidthMs;
    char tag[32];
    std::snprintf(tag, sizeof(tag), centerMs > 0 ? "+%dms" : "%dms", centerMs);
    const float tagWidth = ui::labelWidth(canvas, tag, 11.0F * kCanvasTextScale, true)
            * canvas.scale
        + canvas.units(16.0F);
    const float tagHeight = canvas.units(23.0F);
    const float centerX = graphLeft + slot * (static_cast<float>(peak) + 0.5F);
    const float barHeight =
        minBar + static_cast<float>(bins[static_cast<std::size_t>(peak)])
            / static_cast<float>(maximum) * (maxBar - minBar);
    const float tagLeft = std::clamp(centerX - tagWidth * 0.5F, left + canvas.units(8.0F),
        right - canvas.units(8.0F) - tagWidth);
    const float tagTop =
        std::max(rect.min.y + canvas.units(9.0F), baseline - barHeight - tagHeight
                - canvas.units(8.0F));
    drawList->AddRectFilled(ImVec2(tagLeft, tagTop),
        ImVec2(tagLeft + tagWidth, tagTop + tagHeight), IM_COL32(255, 255, 255,
                                                         static_cast<int>(std::lround(242.0F * alpha))),
        canvas.units(8.0F));
    drawList->AddTriangleFilled(ImVec2(centerX - canvas.units(5.0F), tagTop + tagHeight),
        ImVec2(centerX + canvas.units(5.0F), tagTop + tagHeight),
        ImVec2(centerX, tagTop + tagHeight + canvas.units(5.0F)),
        IM_COL32(255, 255, 255, static_cast<int>(std::lround(242.0F * alpha))));
    ui::TextStyle tagText = boardText(11.0F, IM_COL32(16, 21, 27, static_cast<int>(
                                                            std::lround(255.0F * alpha))),
        ui::HAlign::Center, ui::VAlign::Middle, true, true);
    ui::drawLabel(drawList, canvas,
        ui::Box{dx(tagLeft), dy(tagTop), dx(tagLeft + tagWidth), dy(tagTop + tagHeight)},
        tag, tagText, context.nowSec);
  }
  // Axis labels.
  const int step = std::max(1, rangeMs / 3);
  const int binWidthMs = std::max(1, (rangeMs * 2) / std::max(1, count - 1));
  for (int index = -3; index <= 3; ++index) {
    const int value = index * step;
    const int binIndex =
        std::clamp(static_cast<int>(std::lround((value + rangeMs) / static_cast<float>(binWidthMs))),
            0, count - 1);
    char label[16];
    std::snprintf(label, sizeof(label), value > 0 ? "+%d" : "%d", value);
    const float centerX = graphLeft + slot * (static_cast<float>(binIndex) + 0.5F);
    ui::TextStyle style = boardText(11.5F, IM_COL32(255, 255, 255,
                                                static_cast<int>(std::lround(179.0F * alpha))),
        ui::HAlign::Center, ui::VAlign::Top, false, true);
    ui::drawLabel(drawList, canvas,
        ui::Box{dx(centerX - canvas.units(24.0F)), dy(baseline + canvas.units(10.0F)),
            dx(centerX + canvas.units(24.0F)), dy(baseline + canvas.units(26.0F))},
        label, style, context.nowSec);
  }
  ImGui::PopClipRect();
}

// ---------------------------------------------------------------------------
// mode tags
// ---------------------------------------------------------------------------

struct ModeTag {
  std::string label;
};

std::vector<ModeTag> modeTags(const AppContext& context) {
  std::vector<ModeTag> tags;
  const bool replayPlayback = context.play.replayMode || context.lastRunWasReplay;
  if (replayPlayback) tags.push_back({"回放"});
  // A settlement reached by replay playback describes the run that was RECORDED: the modes and
  // multipliers are display-only history and must not follow the viewer's current settings.
  // Normal runs read the live setup they were played with.
  bool mirror = context.play.mirrorX;
  bool challenge = context.play.challenge;
  bool autoplay = context.play.autoplay;
  bool practice = context.play.practice;
  float musicSpeed = context.play.musicSpeed;
  float scrollSpeed = context.play.scrollSpeed;
  const ReplayData& playback = context.game.playback;
  const bool hasRecordedMeta = replayPlayback
      && (!playback.meta.chartId.empty() || playback.meta.timestampMs != 0
          || !playback.judgements.empty() || !playback.entries.empty());
  if (hasRecordedMeta) {
    mirror = playback.meta.mirrorX;
    challenge = playback.meta.challenge;
    autoplay = playback.meta.autoplay;
    practice = playback.meta.practice;
    musicSpeed = playback.meta.musicSpeed > 0.0F ? playback.meta.musicSpeed : 1.0F;
    scrollSpeed = playback.meta.scrollSpeed > 0.0F ? playback.meta.scrollSpeed : 1.0F;
  }
  if (mirror) tags.push_back({"镜像"});
  if (challenge) tags.push_back({"挑战"});
  if (std::abs(musicSpeed - 1.0F) > 0.001F) {
    tags.push_back({"倍速 " + formatMultiplier(musicSpeed) + "x"});
  }
  if (std::abs(scrollSpeed - 1.0F) > 0.001F) {
    tags.push_back({"流速 " + formatMultiplier(scrollSpeed) + "x"});
  }
  if (autoplay) tags.push_back({"AUTOPLAY"});
  if (practice) tags.push_back({"练习"});
  return tags;
}

} // namespace

bool resultHistogramVisible() { return resultPageState().histogramVisible; }

void drawResultScreen(AppContext& context) {
  const ImVec2 viewport = ImVec2(viewportSize().width, viewportSize().height);
  // The settlement is authored on a 960 x 540 canvas scaled uniformly into the window and
  // centred, so the design units below are the reference layout's own coordinates.
  const ui::DesignCanvas design = ui::designCanvas(viewport, kDesignWidth, kDesignHeight);
  ui::Canvas canvas;
  canvas.scale = design.scale;
  canvas.origin = design.origin;
  canvas.viewport = viewport;
  const game::PlayResult& result = context.lastResult;

  ui::beginFullscreenWindow("##result");
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // The settlement plays when the screen is entered from gameplay. A capture run opens the
  // screen directly at application start and is expected to show the settled state. The
  // distribution overlay belongs to one visit: a new settlement starts with it closed, and
  // leaving the page drops it.
  ResultPageState& page = resultPageState();
  if (page.lastDrawSec < 0.0 || context.nowSec - page.lastDrawSec > 0.5) {
    page.animationStartSec = context.nowSec <= 0.1 ? -1.0 : context.nowSec;
    page.histogramVisible = false;
  }
  page.lastDrawSec = context.nowSec;
  const bool staged = page.animationStartSec > 0.0;
  const float sinceStart = staged
      ? std::max(0.0F, static_cast<float>(context.nowSec - page.animationStartSec))
      : static_cast<float>(kSettlementSeconds);
  // The backdrop crossfades in first and holds on its own, exactly like the reference outro.
  const float backdropFade = staged
      ? ui::interpolate(ui::Interp::DecelerateQuad,
            std::clamp(sinceStart / kBackdropFadeSeconds, 0.0F, 1.0F))
      : 1.0F;
  const bool contentVisible = !staged || sinceStart >= kContentStartSeconds;
  // The reference settlement animators start with the result page itself and run while only the
  // backdrop is on screen, so the content is already settled when the hold ends and it appears.
  const float progress = staged
      ? std::clamp(sinceStart / static_cast<float>(kSettlementSeconds), 0.0F, 1.0F)
      : 1.0F;
  const Settlement settlement = settlementAt(progress);

  // ---- background: the chart illustration, blurred and dimmed ----
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport, IM_COL32(0, 0, 0, 255));
  drawBlurredBackdrop(context, drawList, viewport);
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport,
      IM_COL32(0, 0, 0, static_cast<int>(std::lround(255.0F * kBackgroundDim))));
  if (backdropFade < 1.0F) {
    drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport,
        IM_COL32(0, 0, 0, static_cast<int>(std::lround(255.0F * (1.0F - backdropFade)))));
  }
  // Until the hold ends only the blurred cover is on screen, as in the reference. The content
  // below is still laid out and its controls are still submitted: the settlement actions are
  // bound when the page is created, so the entrance is never a dead window. While the cover
  // holds, the content is clipped out of the frame instead of the layout being skipped, which
  // is what keeps the same hit areas live from the first frame.
  const bool contentHidden = !contentVisible;
  if (contentHidden) {
    drawList->PushClipRect(ImVec2(0.0F, 0.0F), ImVec2(0.0F, 0.0F), false);
  }

  // Everything below is authored in the 960 x 540 design space.
  ui::Canvas content = canvas;

  const auto box = [](float x0, float y0, float w, float h) {
    return ui::Box{x0, y0, x0 + w, y0 + h};
  };
  const auto px = [&](float x, float y) { return content.point(x, y); };

  // ---- illustration panel ----
  const ui::Box cardBox = box(53.0F, 114.0F, 500.0F, 312.0F);
  const ui::Box cardPixels = shifted(cardBox, settlement.cardDx);
  {
    ui::PanelStyle style =
        ui::glassPanel(ui::argb(0xFF101318U), ui::argb(0x00000000U));
    style.strokeWidthDp = 0.0F;
    style.skewDegrees = 15.0F;
    ui::drawPanelShape(drawList, content, cardPixels, style);
    const std::uint64_t illustration = resultCover(context, 2048);
    if (illustration != 0) {
      drawPanelImage(drawList, content, cardPixels, illustration,
          static_cast<int>(context.textures.width(illustration)),
          static_cast<int>(context.textures.height(illustration)));
    }
    drawPanelScrim(drawList, content, cardPixels);
    // Song name and difficulty sit in the clear band above the judgement board.
    char nameBuffer[192];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%.64s", context.play.songName.c_str());
    ui::TextStyle song = boardText(kSongTextSize, IM_COL32(255, 255, 255, 255), ui::HAlign::Left,
        ui::VAlign::Bottom, false, false);
    song.marquee = true;
    ui::drawLabel(drawList, content,
        shifted(box(79.5F, 360.6F, 282.0F, 40.0F), settlement.cardDx), nameBuffer, song,
        context.nowSec);
    ui::TextStyle difficulty = boardText(kDifficultyTextSize, IM_COL32(255, 255, 255, 255),
        ui::HAlign::Right, ui::VAlign::Bottom, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(363.0F, 377.0F, 101.0F, 26.0F), settlement.cardDx),
        plainSpaces(context.play.difficulty),
        difficulty, context.nowSec);
  }

  // ---- upper board: score, rule and grade ----
  {
    const ui::Box board = shifted(box(526.0F, 114.0F, 389.0F, 156.0F), settlement.upperDx);
    ui::PanelStyle style =
        ui::glassPanel(ui::argb(kPanelFill), ui::argb(0x00000000U));
    style.strokeWidthDp = 0.0F;
    ui::drawPanelShape(drawList, content, board, style);

    ui::TextStyle score = boardText(kScoreTextSize,
        whiteAlpha(settlement.scoreAlpha), ui::HAlign::Left, ui::VAlign::Bottom, false, true);
    ui::drawLabel(drawList, content, shifted(box(561.0F, 150.7F, 220.0F, 58.0F), settlement.upperDx),
        formatScore(result.score), score, context.nowSec);

    const ui::PxRect rule = ui::toPixels(content,
        shifted(box(562.0F, 242.0F, 150.0F, 2.0F), settlement.upperDx));
    drawList->AddRectFilled(rule.min, rule.max,
        IM_COL32(255, 255, 255, static_cast<int>(std::lround(140.0F * settlement.scoreAlpha))));

    const int gradeIndex = gradeIndexForScore(result.score, result.isFullCombo());
    const ImVec2 gradeCenter =
        px(823.6F + settlement.upperDx, 190.05F);
    drawGradeImage(context, drawList, gradeCenter, content.units(106.5F * settlement.gradeScale),
        gradeIndex, settlement.gradeAlpha);
  }

  // ---- middle board: max combo, standard deviation, accuracy ----
  {
    const ui::Box board = shifted(box(505.0F, 292.0F, 362.0F, 64.0F), settlement.middleDx);
    ui::PanelStyle style =
        ui::glassPanel(ui::argb(kPanelFill), ui::argb(0x00000000U));
    style.strokeWidthDp = 0.0F;
    ui::drawPanelShape(drawList, content, board, style);
    const float alpha = settlement.middleAlpha;
    const ImU32 valueColor = whiteAlpha(alpha);
    const ImU32 labelColor = whiteAlpha(0.8F * alpha);

    const ImU32 comboColor = valueColor;
    ui::TextStyle combo = boardText(kComboValueSize, comboColor, ui::HAlign::Left,
        ui::VAlign::Bottom, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(530.5F, 288.2F, 104.0F, 30.0F), settlement.middleDx),
        std::to_string(result.maxCombo), combo, context.nowSec);
    ui::TextStyle comboLabel = boardText(kComboLabelSize, labelColor, ui::HAlign::Left,
        ui::VAlign::Middle, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(531.0F, 334.0F, 104.0F, 15.0F), settlement.middleDx), "MAX COMBO", comboLabel,
        context.nowSec);

    char deviation[48];
    std::snprintf(deviation, sizeof(deviation), "%.3f ms", result.stdDevMs);
    ui::TextStyle std = boardText(kStdValueSize, valueColor, ui::HAlign::Center,
        ui::VAlign::Bottom, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(627.5F, 288.75F, 124.0F, 30.0F), settlement.middleDx), deviation, std,
        context.nowSec);
    ui::TextStyle stdLabel = boardText(kStdLabelSize, labelColor, ui::HAlign::Center,
        ui::VAlign::Middle, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(627.0F, 334.0F, 124.0F, 15.0F), settlement.middleDx), "STD DEVIATION",
        stdLabel, context.nowSec);

    ui::TextStyle accuracy = boardText(kAccValueSize, valueColor, ui::HAlign::Right,
        ui::VAlign::Bottom, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(737.0F, 288.25F, 104.5F, 30.0F), settlement.middleDx),
        formatAccuracy(result.accuracy), accuracy, context.nowSec);
    ui::TextStyle accuracyLabel = boardText(kAccLabelSize, labelColor, ui::HAlign::Right,
        ui::VAlign::Middle, false, true);
    ui::drawLabel(drawList, content,
        shifted(box(737.0F, 334.0F, 104.0F, 15.0F), settlement.middleDx), "ACCURACY",
        accuracyLabel, context.nowSec);
  }

  // ---- lower board: judgement counts, early/late ----
  {
    const ui::Box board = shifted(box(485.0F, 372.0F, 362.0F, 64.0F), settlement.lowerDx);
    ui::PanelStyle style =
        ui::glassPanel(ui::argb(kPanelFill), ui::argb(0x00000000U));
    style.strokeWidthDp = 0.0F;
    ui::drawPanelShape(drawList, content, board, style);
    const float alpha = settlement.lowerAlpha;
    const ImU32 valueColor = whiteAlpha(alpha);
    const ImU32 labelColor = whiteAlpha(0.8F * alpha);

    struct CountEntry {
      const char* label;
      int value;
      float x;
      float w;
    };
    const CountEntry entries[] = {
        {"PERFECT", result.perfect, 495.0F, 72.0F},
        {"GOOD", result.goodEarly + result.goodLate, 571.0F, 56.0F},
        {"BAD", result.bad, 625.0F, 50.0F},
        {"MISS", result.miss, 676.0F, 50.0F},
    };
    for (const CountEntry& entry : entries) {
      ui::TextStyle value = boardText(kCountValueSize, valueColor, ui::HAlign::Center,
          ui::VAlign::Bottom, false, true);
      ui::drawLabel(drawList, content,
          shifted(box(entry.x, 371.9F, entry.w, 30.0F), settlement.lowerDx),
          std::to_string(entry.value), value, context.nowSec);
      ui::TextStyle label = boardText(kCountLabelSize, labelColor, ui::HAlign::Center,
          ui::VAlign::Middle, false, true);
      ui::drawLabel(drawList, content,
          shifted(box(entry.x, 414.0F, entry.w, 16.0F), settlement.lowerDx), entry.label, label,
          context.nowSec);
    }
    const ui::PxRect divider = ui::toPixels(content,
        shifted(box(737.0F, 386.0F, 1.0F, 36.0F), settlement.lowerDx));
    drawList->AddRectFilled(divider.min, divider.max, whiteAlpha(0.35F * alpha));

    struct EarlyLate {
      const char* label;
      int value;
      float labelY;
      float valueY;
      float valueX;
    };
    const EarlyLate rows[] = {
        {"EARLY", result.goodEarly, 386.0F, 385.0F, 789.0F},
        {"LATE", result.goodLate, 405.0F, 404.0F, 785.0F},
    };
    for (const EarlyLate& row : rows) {
      ui::TextStyle label = boardText(kEarlyLabelSize, labelColor, ui::HAlign::Left,
          ui::VAlign::Middle, false, true);
      ui::drawLabel(drawList, content,
          shifted(box(747.0F, row.labelY, 46.0F, 16.0F), settlement.lowerDx), row.label, label,
          context.nowSec);
      ui::TextStyle value = boardText(kEarlyValueSize, valueColor, ui::HAlign::Right,
          ui::VAlign::Middle, false, true);
      ui::drawLabel(drawList, content,
          shifted(box(row.valueX, row.valueY, 36.0F, 17.0F), settlement.lowerDx),
          std::to_string(row.value), value, context.nowSec);
    }
  }

  // ---- mode tags along the top-right ----
  {
    const std::vector<ModeTag> tags = modeTags(context);
    if (!tags.empty()) {
      const float containerRight = 915.0F;
      const float containerTop = 34.0F;
      const float height = 32.0F;
      const float gap = 9.5F;
      std::vector<float> widths(tags.size());
      float rowWidth = 0.0F;
      for (std::size_t index = 0; index < tags.size(); ++index) {
        const float textWidth =
            ui::labelWidth(content, tags[index].label, kModeTagTextSize * kCanvasTextScale, false);
        // The reference tags are tighter around CJK labels than around Latin ones; the bundled
        // typeface sets the two scripts with different side bearings, so their padding differs.
        const bool latinOnly = std::all_of(tags[index].label.begin(), tags[index].label.end(),
            [](unsigned char value) { return value < 0x80U; });
        const float padding = latinOnly ? 50.0F : 33.5F;
        widths[index] = textWidth + padding + height * kSkewSlope;
        rowWidth += widths[index];
        if (index > 0) rowWidth += gap;
      }
      float x = containerRight - rowWidth;
      for (std::size_t index = 0; index < tags.size(); ++index) {
        const float y = containerTop;
        const ui::Box chip = box(x, y, widths[index], height);
        ui::PanelStyle style = ui::glassPanel(ui::argb(kPanelFill), ui::argb(0x00000000U));
        style.strokeWidthDp = 0.0F;
        style.accentEdge = 1;
        style.accent = whiteAlpha(settlement.tagsAlpha);
        style.accentWidthDp = 2.5F * kCanvasTextScale;
        style.fill = ui::fade(ui::argb(kPanelFill), settlement.tagsAlpha);
        ui::drawPanelShape(drawList, content, chip, style);
        ui::TextStyle text = boardText(kModeTagTextSize, whiteAlpha(settlement.tagsAlpha),
            ui::HAlign::Center, ui::VAlign::Middle, false, true);
        ui::drawLabel(drawList, content, chip, tags[index].label, text, context.nowSec);
        x += widths[index] + gap;
      }
    }
  }

  // ---- corner controls ----
  const float cornerMargin = 14.0F * kReferenceDensity / std::max(0.01F, content.scale);
  const ui::PanelStyle cornerBase = ui::glassPanel(ui::argb(kMenuButtonFill),
      ui::argb(kMenuButtonStroke));
  ui::PanelStyle retryStyle = cornerBase;
  retryStyle.strokeWidthDp = kCanvasTextScale;
  retryStyle.straightLeft = true;
  retryStyle.accentEdge = 2;
  retryStyle.accent = whiteAlpha(1.0F);
  retryStyle.accentWidthDp = 2.0F * kCanvasTextScale;
  ui::PanelStyle exitStyle = cornerBase;
  exitStyle.strokeWidthDp = kCanvasTextScale;
  exitStyle.straightRight = true;
  exitStyle.accentEdge = 1;
  exitStyle.accent = whiteAlpha(1.0F);
  exitStyle.accentWidthDp = 2.0F * kCanvasTextScale;

  const ui::Box retryBox = box(0.0F, cornerMargin + settlement.retryDx, 102.0F, 78.0F);
  const ui::Box exitBox = box(kDesignWidth - 102.0F + settlement.cornerDx,
      kDesignHeight - cornerMargin - 78.0F, 102.0F, 78.0F);
  if (!context.play.replayMode) {
    const ui::Interaction retry =
        ui::panelInteraction("##result_retry", content, retryBox, retryStyle);
    ui::drawPanelShape(drawList, content, retryBox, retryStyle);
    const ui::PxRect iconRect = ui::toPixels(content, shifted(box(36.0F, 27.0F, 25.0F, 25.0F),
        settlement.retryDx, cornerMargin));
    drawTintedGlyph(drawList, iconRect, 0, whiteAlpha(1.0F));
    if (retry.pressed) {
      const PlaySetup setup = context.play;
      if (!setup.chartId.empty()) {
        startChartWithLoading(context, setup.practice);
      } else {
        context.play = setup;
        context.screen = Screen::Game;
        gameEnterWithLoading(context);
      }
    }
  }
  const bool exitPressed =
      ui::panelInteraction("##result_exit", content, exitBox, exitStyle).pressed;
  ui::drawPanelShape(drawList, content, exitBox, exitStyle);
  {
    const ui::PxRect iconRect = ui::toPixels(content, shifted(box(
        kDesignWidth - 102.0F + 41.0F, kDesignHeight - cornerMargin - 78.0F + 27.0F, 25.0F, 25.0F),
        settlement.cornerDx));
    drawTintedGlyph(drawList, iconRect, 1, whiteAlpha(1.0F));
  }

  // Replay actions: the two small chips left of the exit plate when the run was recorded.
  if (context.lastRunHasReplay && !context.play.replayMode) {
    ui::PanelStyle chip = cornerBase;
    chip.strokeWidthDp = kCanvasTextScale;
    const float chipsRight = kDesignWidth - 106.0F;
    const float chipsY = kDesignHeight - cornerMargin - 78.0F + 26.0F;
    const ui::Box playBox = box(chipsRight - 146.0F + settlement.cornerDx, chipsY, 70.0F, 40.0F);
    const ui::Box saveBox =
        box(chipsRight - 70.0F + settlement.cornerDx, chipsY, 70.0F, 40.0F);
    const bool playPressed =
        ui::panelInteraction("##result_play_replay", content, playBox, chip).pressed;
    ui::drawPanelShape(drawList, content, playBox, chip);
    drawTintedGlyph(drawList,
        ui::toPixels(content, box(playBox.x0 + 24.0F, playBox.y0 + 9.0F, 22.0F, 22.0F)), 2,
        whiteAlpha(1.0F));
    const bool savePressed =
        ui::panelInteraction("##result_save_replay", content, saveBox, chip).pressed;
    ui::drawPanelShape(drawList, content, saveBox, chip);
    drawTintedGlyph(drawList,
        ui::toPixels(content, box(saveBox.x0 + 24.0F, saveBox.y0 + 9.0F, 22.0F, 22.0F)), 3,
        whiteAlpha(1.0F));
    if (playPressed) playReplay(context);
    if (savePressed) saveReplay(context);
  }

  // ---- judgement distribution overlay ----
  // The overlay belongs to the settlement content, so it is offered as soon as that content is
  // on screen instead of only after the slide has finished. Every target below is a complete
  // press of its own plate, so the release that ends an entrance gesture cannot toggle it.
  if (contentVisible && context.settings.judgementDetailDistribution) {
    const ui::Box histogramBox = shifted(cardBox, settlement.cardDx);
    ui::PanelStyle hitStyle = ui::glassPanel(ui::argb(0x00000000U), ui::argb(0x00000000U));
    hitStyle.strokeWidthDp = 0.0F;
    if (page.histogramVisible) {
      ui::Canvas plain = canvas;
      drawJudgementHistogram(context, drawList, plain, histogramBox, context.play.challenge, 1.0F,
          0.0F);
      ImGui::SetCursorScreenPos(content.point(histogramBox.x0, histogramBox.y0));
      if (ImGui::InvisibleButton("##result_histogram",
              ImVec2(content.units(histogramBox.width()), content.units(histogramBox.height())))) {
        page.histogramVisible = false;
      }
    } else {
      // Tapping the illustration or any of the three boards reveals the overlay.
      const ui::Box targets[] = {histogramBox,
          shifted(box(526.0F, 114.0F, 389.0F, 156.0F), settlement.upperDx),
          shifted(box(505.0F, 292.0F, 362.0F, 64.0F), settlement.middleDx),
          shifted(box(485.0F, 372.0F, 362.0F, 64.0F), settlement.lowerDx)};
      for (std::size_t index = 0; index < std::size(targets); ++index) {
        const std::string id = "##result_histogram_toggle" + std::to_string(index);
        if (ui::panelInteraction(id.c_str(), content, targets[index], hitStyle).pressed) {
          page.histogramVisible = true;
          break;
        }
      }
    }
  }


  // The settlement content keeps its own staged reveals; the backdrop crossfade above already
  // handled the entrance, so no extra full-screen fade is drawn here.

  if (contentHidden) drawList->PopClipRect();

  ui::endFullscreenWindow();

  // Leave straight to the chart's detail page; the settlement page already knows which chart
  // it belongs to, and rendering the list first is what made it flash in between.
  if (exitPressed) gameQuitToMenu(context);
}

} // namespace rechigros::app
