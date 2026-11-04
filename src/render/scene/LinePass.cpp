#include "rechigros/render/scene/LinePass.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::scene {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr double kBeatSeconds = 60.0 / 32.0;

float clamp01(float value) {
  if (value < 0.0F) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}
} // namespace

void evaluateLines(const game::Chart& chart, const StageLayout& layout, double chartTimeSec,
    float scrollSpeed, bool blockNotes, bool revealHiddenObjects, float revealScale,
    const math::Rect& worldBounds, float visibleRangePad, LinePassParams params,
    std::vector<LineFrame>& out) {
  const auto lineCount = static_cast<int>(chart.judgeLineList.size());
  out.assign(static_cast<std::size_t>(lineCount), LineFrame{});
  const float stageWidth = layout.stage.width;
  const float stageHeight = layout.stage.height;
  const float stageAspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : 16.0F / 9.0F;
  const bool reveal = revealHiddenObjects && params.revealFloorAlpha > 0.001F;

  for (int index = 0; index < lineCount; ++index) {
    auto& line = *chart.judgeLineList[static_cast<std::size_t>(index)];
    auto& frame = out[static_cast<std::size_t>(index)];
    const auto state = line.fillState(chartTimeSec, stageAspect);

    frame.valid = true;
    frame.renderable = true;
    frame.hideLine = state.hideLine;
    frame.hideNotes = state.hideNotes;
    frame.cover = line.isCover;
    frame.hasColorEvent = state.hasColorEvent;
    frame.xNorm = state.xNorm;
    frame.yNorm = state.yNorm;
    frame.x = layout.stage.x + state.xNorm * stageWidth;
    frame.y = layout.stage.y + state.yNorm * stageHeight;
    frame.rotateDeg = state.rotateDeg;
    const double radians = static_cast<double>(frame.rotateDeg) * kPi / 180.0;
    frame.cosLine = static_cast<float>(std::cos(radians));
    frame.sinLine = static_cast<float>(std::sin(radians));
    frame.alphaRaw = state.alpha;
    frame.scaleX = state.scaleX;
    frame.scaleY = state.scaleY;
    frame.colorR = state.colorR;
    frame.colorG = state.colorG;
    frame.colorB = state.colorB;
    frame.anchorX = state.anchorX;
    frame.anchorY = state.anchorY;
    frame.gifPosition = state.gifPosition;
    frame.inclineSinr = state.inclineSinr;
    frame.bpm = line.bpm > 0.0 ? line.bpm : 120.0;
    frame.floorPosition = state.floorPosition;

    // `fillState` already resolves text events onto the state, so the frame only has to
    // remember whether the line owns text events at all: such a line draws no plain quad
    // even when its current text is empty.
    frame.text = state.text;
    frame.hasTextEvents = !line.judgeLineTextEvents.empty();

    // Hidden notes stay out of the note pass unless the reveal floor is lifting them.
    frame.noteValid = !blockNotes && !line.notes.empty() && (!frame.hideNotes || reveal);
    if (frame.noteValid) {
      computeVisibleRange(frame.x, frame.y, frame.cosLine, frame.sinLine, revealScale < 0.999F,
          revealScale, stageWidth, stageHeight, scrollSpeed, worldBounds, frame.visibleMin,
          frame.visibleMax);
      frame.visiblePad = visibleRangePad;
    }
    (void)params;
  }
}

bool lineSpriteVisible(const math::Rect& stage, float centerX, float centerY, float width,
    float height, float rotationDeg) noexcept {
  if (!(width > 0.0F) || !(height > 0.0F)) return false;
  const float halfW = width * 0.5F;
  const float halfH = height * 0.5F;
  if (rotationDeg == 0.0F) {
    return !(centerX + halfW < stage.left() || centerX - halfW > stage.right() ||
             centerY + halfH < stage.top() || centerY - halfH > stage.bottom());
  }
  const double radians = static_cast<double>(rotationDeg) * kPi / 180.0;
  const auto cosAbs = static_cast<float>(std::abs(std::cos(radians)));
  const auto sinAbs = static_cast<float>(std::abs(std::sin(radians)));
  const float extentX = halfW * cosAbs + halfH * sinAbs;
  const float extentY = halfW * sinAbs + halfH * cosAbs;
  return !(centerX + extentX < stage.left() || centerX - extentX > stage.right() ||
           centerY + extentY < stage.top() || centerY - extentY > stage.bottom());
}

void drawPlainLine(batch::SpriteBatch& batch, float centerX, float centerY, float halfLength,
    float width, float rotationDeg, const LineTint& tint, float alpha) noexcept {
  if (halfLength <= 0.0F || width <= 0.0F || alpha <= 0.0F) return;
  batch.addQuad(centerX, centerY, halfLength * 2.0F, width, rotationDeg, tint.red, tint.green,
      tint.blue, alpha * tint.alpha, 0.0F, 0.0F, 1.0F, 1.0F);
}

void drawIntroLine(batch::SpriteBatch& batch, const StageLayout& layout, bool officialFormat,
    float scale, float thickness, const LineTint& tint) noexcept {
  if (scale <= 0.001F) return;
  scale = std::min(1.0F, std::max(0.0F, scale));
  // The decorative line spans a little beyond the stage so its growth is visible.
  const float decorHalf = officialFormat ? layout.stage.width * 0.72F
                                         : layout.stage.width * LINE_LENGTH_EDITOR;
  const float width =
      layout.stage.height * (officialFormat ? LINE_WIDTH_OFFICIAL : LINE_WIDTH_EDITOR) * thickness;
  drawPlainLine(batch, layout.centerX(), layout.centerY(), decorHalf * scale, width, 0.0F, tint,
      1.0F);
}

} // namespace rechigros::render::scene
