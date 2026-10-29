#pragma once

#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Note.hpp"

namespace rechigros::game {

// Note placement along the judge line. Every helper reproduces the reference renderer's
// pixel-space maths exactly, including its float32 rounding points, because the drawn
// position and the judge window have to agree to within a few pixels.
//
// Three speed-selection rules exist in the reference and are NOT interchangeable:
//   * Render  - (useOfficialSpeed ? 1 : speed) * scrollSpeed     (drawn head position)
//   * Control - (isHold && useOfficialSpeed ? 1 : speed) * scrollSpeed
//               (note controls, spatial judge position, hold/body geometry)
//   * Judge   - speed * scrollSpeed                              (raw along-line offset)
// Pick the basis the caller's reference site uses; never substitute one for another.
enum class SpeedBasis { Render, Control, Judge };

float speedFor(const Note& note, double scrollSpeed, SpeedBasis basis) noexcept;
float renderSpeed(const Note& note, double scrollSpeed) noexcept;
float controlSpeed(const Note& note, double scrollSpeed) noexcept;
float judgeSpeed(const Note& note, double scrollSpeed) noexcept;

// Scales the note-to-line distance by the Y note-control value. A non-finite control
// leaves the distance untouched.
float applyYControlScale(float baseFpPx, float yControl) noexcept;

// Maps the note-to-line distance in pixels back to the RPE pixel height that the
// alpha/scale/position/Y note controls are parameterised on. `yOffset` is already
// normalised to RPE height.
float rpeControlHeightFromBasePx(float baseFpPx, float yOffset, double visualSpeed,
    float stageAspect, float stageHeight) noexcept;

// inclineValue = 1 - sin(incline) * (base * aspect + transY) * RPE_HEIGHT / (stageH * 360).
// `baseFpPx` must already include the Y control; `transYPx` excludes it.
float calcInclineValue(float inclineSin, float baseFpPx, float transYPx,
    float stageAspect, float stageHeight) noexcept;

// Combines the judge line incline with a note skew control point, both in sin space.
// Returns NaN when neither contribution is present.
float combineInclineSinr(float baseSinr, float skewDegrees) noexcept;

// Normalised RPE Y offset to stage pixels. Independent of note speed by design.
float yOffsetToTransYPx(float yOffset, double speed, float stageHeight) noexcept;

// Clamps a judge-area multiplier into the range the reference accepts.
float sanitizeJudgeArea(float area) noexcept;

// Full local geometry of one note relative to its judge line at one instant.
struct NoteGeometry {
  float baseDistancePx{};    // note-to-line distance before Y control
  float transYPx{};          // yOffset contribution, Y control not applied
  float controlHeight{};     // RPE height feeding the note control points
  float visualDistancePx{};  // baseDistancePx * Y control + transYPx
  float inclineValue{1.0F};
  float alongLineOffsetPx{}; // signed offset along the line, incline and X control applied
};

// Resolves every per-note geometry term. `stateInclineSinr` comes from
// JudgeLine::fillState and may be NaN; `line` supplies the note control point lists.
NoteGeometry resolveNoteGeometry(const Note& note, const JudgeLine& line, float stateInclineSinr,
    double lineFloorPosition, double lineBpm, double scrollSpeed,
    float stageWidth, float stageHeight);

struct HoldHeadEffectPose {
  bool valid{false};
  float x{};
  float y{};
  float rotationDeg{};
};

// Resolves a hold-head burst at its event time. A late event removes only the floor-distance
// term, retaining the lane and vertical offsets; an early event uses the full control curves.
HoldHeadEffectPose resolveHoldHeadEffectPose(const Note& note, const JudgeLine& line,
    const JudgeLine::State& state, double timeSec, double scrollSpeed,
    float stageX, float stageY, float stageWidth, float stageHeight) noexcept;

} // namespace rechigros::game
