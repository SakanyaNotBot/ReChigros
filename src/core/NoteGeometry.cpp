#include "rechigros/game/NoteGeometry.hpp"

#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rechigros::game {
namespace {
constexpr double PI = 3.14159265358979323846;
} // namespace

float speedFor(const Note& note, double scrollSpeed, SpeedBasis basis) noexcept {
  switch (basis) {
    case SpeedBasis::Render:
      return static_cast<float>((note.useOfficialSpeed ? 1.0 : note.speed) * scrollSpeed);
    case SpeedBasis::Control:
      return static_cast<float>((note.isHold && note.useOfficialSpeed ? 1.0 : note.speed) * scrollSpeed);
    case SpeedBasis::Judge:
      return static_cast<float>(note.speed * scrollSpeed);
  }
  return static_cast<float>(note.speed * scrollSpeed);
}

float renderSpeed(const Note& note, double scrollSpeed) noexcept {
  return speedFor(note, scrollSpeed, SpeedBasis::Render);
}

float controlSpeed(const Note& note, double scrollSpeed) noexcept {
  return speedFor(note, scrollSpeed, SpeedBasis::Control);
}

float judgeSpeed(const Note& note, double scrollSpeed) noexcept {
  return speedFor(note, scrollSpeed, SpeedBasis::Judge);
}

float applyYControlScale(float baseFpPx, float yControl) noexcept {
  return std::isfinite(yControl) ? baseFpPx * yControl : baseFpPx;
}

float rpeControlHeightFromBasePx(float baseFpPx, float yOffset, double visualSpeed,
    float stageAspect, float stageHeight) noexcept {
  if (!std::isfinite(baseFpPx) || !std::isfinite(stageAspect) || !std::isfinite(stageHeight)
      || !std::isfinite(visualSpeed) || stageHeight == 0.0F || std::abs(visualSpeed) < 1e-9) {
    return std::isfinite(yOffset) ? yOffset * RPE_HEIGHT : 0.0F;
  }
  const auto yOffsetRpe = yOffset * RPE_HEIGHT;
  const auto height = yOffsetRpe + baseFpPx * stageAspect * RPE_HEIGHT
      / (stageHeight * static_cast<float>(visualSpeed));
  return std::isfinite(height) ? height : (std::isfinite(yOffset) ? yOffsetRpe : 0.0F);
}

float calcInclineValue(float inclineSin, float baseFpPx, float transYPx,
    float stageAspect, float stageHeight) noexcept {
  const auto parameter = (baseFpPx * stageAspect + transYPx) * RPE_HEIGHT / (stageHeight * 360.0F);
  auto value = 1.0F - inclineSin * parameter;
  if (!std::isfinite(value) || value == 0.0F) value = 1.0F;
  return value;
}

float combineInclineSinr(float baseSinr, float skewDegrees) noexcept {
  const bool hasBase = std::isfinite(baseSinr);
  const bool hasSkew = std::isfinite(skewDegrees) && std::abs(skewDegrees) > 1e-6F;
  if (!hasBase && !hasSkew) return std::numeric_limits<float>::quiet_NaN();
  auto radians = hasBase ? std::asin(std::clamp(baseSinr, -1.0F, 1.0F)) : 0.0;
  if (hasSkew) radians += static_cast<double>(skewDegrees) * PI / 180.0;
  return static_cast<float>(std::sin(radians));
}

float yOffsetToTransYPx(float yOffset, double, float stageHeight) noexcept {
  return yOffset * stageHeight;
}

float sanitizeJudgeArea(float area) noexcept {
  if (!std::isfinite(area) || area <= 0.0F) return 1.0F;
  return std::max(1e-4F, area);
}

NoteGeometry resolveNoteGeometry(const Note& note, const JudgeLine& line, float stateInclineSinr,
    double lineFloorPosition, double lineBpm, double scrollSpeed,
    float stageWidth, float stageHeight) {
  NoteGeometry geometry;
  if (!(stageHeight > 1e-6F)) return geometry;
  const auto stageAspect = stageWidth / stageHeight;
  const auto bpm = lineBpm > 0.0 ? lineBpm : 120.0;
  const auto speed = static_cast<double>(controlSpeed(note, scrollSpeed));

  geometry.baseDistancePx = static_cast<float>((note.floorPosition - lineFloorPosition) * PGR_HEIGHT
      * (PGR_BEAT / bpm) * stageHeight * speed);
  geometry.transYPx = yOffsetToTransYPx(note.yOffset, speed, stageHeight);
  geometry.controlHeight = rpeControlHeightFromBasePx(geometry.baseDistancePx, note.yOffset, speed,
      stageAspect, stageHeight);

  const auto yControl = line.calcNoteControl(geometry.controlHeight, JudgeLine::NOTE_CTRL_Y, 1.0F);
  const auto baseWithControl = applyYControlScale(geometry.baseDistancePx, yControl);
  geometry.visualDistancePx = baseWithControl + geometry.transYPx;

  const auto skewControl = line.calcNoteControl(geometry.controlHeight, JudgeLine::NOTE_CTRL_SKEW, 0.0F);
  const auto inclineSin = combineInclineSinr(stateInclineSinr, skewControl);
  if (std::isfinite(inclineSin)) {
    geometry.inclineValue = calcInclineValue(inclineSin, baseWithControl, geometry.transYPx,
        stageAspect, stageHeight);
  }

  auto offset = static_cast<float>(note.positionX * PGR_WIDTH * stageWidth);
  if (!note.isHold) {
    offset *= geometry.inclineValue;
    const auto xControl = line.calcNoteControl(geometry.controlHeight, JudgeLine::NOTE_CTRL_X, 1.0F);
    if (std::isfinite(xControl) && xControl != 0.0F) offset *= xControl;
  }
  geometry.alongLineOffsetPx = offset;
  return geometry;
}
} // namespace rechigros::game
