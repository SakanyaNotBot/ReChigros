#pragma once

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/LineFrame.hpp"

#include <array>
#include <limits>
#include <vector>

namespace rechigros::game { class Chart; struct Note; }

namespace rechigros::render::scene {

// Head/tail/body slices of a hold sprite sheet, expressed in normalised V. The
// sheet order is tail (top), body (middle), head (bottom).
struct HoldUv {
  float headV0{0.8F};
  float headV1{1.0F};
  float bodyV0{0.0F};
  float bodyV1{1.0F};
  float tailV0{0.0F};
  float tailV1{0.2F};
  float tailHeightPx{16.0F};
  float headHeightPx{16.0F};
};

// Splits a vertically stacked hold sheet using the atlas slice sizes in pixels.
[[nodiscard]] HoldUv buildHoldUv(int textureHeightPx, int tailSlicePx, int headSlicePx) noexcept;

// Everything the note passes need that is constant across the passes of one frame.
struct NotePassParams {
  double chartTimeSec{};
  double chartGlobalBeat{std::numeric_limits<double>::quiet_NaN()};
  float stageWidth{};
  float stageHeight{};
  // Width of one note at unit scale, in pixels.
  float noteWidthBase{};
  float scrollSpeed{1.0F};
  bool officialFormat{false};
  bool revealHiddenObjects{false};
  float revealHiddenFloorAlpha{};
  float frameGlobalAlpha{1.0F};
  // Multi-press highlight setting: when off, notes flagged for the highlight variant are
  // drawn with the normal sheet and the normal width scale.
  bool multiPressHighlight{true};
  // Age past which an unjudged note stops being drawn.
  double missFadeWindow{0.18};
  math::Rect cullBounds{};
  bool flipU{false};
  // Per-type, per-variant width correction. A highlight texture is a different canvas
  // size, so the drawn width is scaled by its width ratio to keep the note the same size.
  // [type][0] is the normal variant, [type][1] the highlight one.
  std::array<std::array<float, 2>, 5> noteWidthScale{{{{1.0F, 1.0F}}, {{1.0F, 1.0F}},
      {{1.0F, 1.0F}}, {{1.0F, 1.0F}}, {{1.0F, 1.0F}}}};

  // Correction factor for one note, from its type and highlight variant.
  [[nodiscard]] float widthScaleFor(int noteType, bool highlight) const noexcept {
    if (noteType < 0 || noteType >= 5) return 1.0F;
    const float value = noteWidthScale[static_cast<std::size_t>(noteType)]
        [highlight ? 1U : 0U];
    return (std::isfinite(value) && value > 0.0F) ? value : 1.0F;
  }
};

// World-space pose of one note head at the frame's chart time, using the same control-
// curve math as the note passes. Hit effects that appear where a note was when it was
// hit resolve their position through this.
struct NoteHeadPose {
  bool valid{false};
  float x{};
  float y{};
  float rotationDeg{};
};
[[nodiscard]] NoteHeadPose noteHeadPose(const game::Note& note, const LineFrame& frame,
    const NotePassParams& params);

// Writes every note-head sprite of one texture bucket into the batch. Notes whose
// type differs from `noteType`, or whose highlight variant differs from
// `highlightVariant`, are skipped.
void buildHeadBatch(batch::SpriteBatch& out, const std::vector<LineFrame>& frames,
    const std::vector<int>& drawOrder, const game::Chart& chart, int noteType,
    bool highlightVariant, float textureWidth, float textureHeight, const NotePassParams& params);

// Writes every hold sprite (head, body, tail) of one texture bucket into the batch.
void buildHoldBatch(batch::SpriteBatch& out, const std::vector<LineFrame>& frames,
    const std::vector<int>& drawOrder, const game::Chart& chart, bool highlightVariant,
    float textureWidth, float textureHeight, const HoldUv& uv, bool holdRepeat, bool holdKeepHead,
    bool holdCompact, float bodyTextureWidth, float bodyTextureHeight,
    const NotePassParams& params);

// Width correction for a highlight note sheet. The highlight sheet is a different canvas
// size, so the drawn width is scaled by the ratio of the two widths to keep the note the
// same size as its normal variant. Returns 1 when either sheet is unusable.
[[nodiscard]] float highlightWidthScale(int normalWidth, int highlightWidth) noexcept;

// Whether a hold has reached the judge line and therefore collapses onto it. RPE documents
// switch at the exact start instant, so a head drawn at its own start time is still the
// travelling head; the official format keeps the older inclusive boundary.
[[nodiscard]] bool holdHasStarted(double noteStartSec, double chartTimeSec,
    bool officialFormat) noexcept;

// The line's cover rule hides a note once it has passed the line. A hold compares the part
// of it that is furthest along -- its tail, not its head -- and does so in floor space, so
// a negative note speed does not flip the verdict. Every other note keeps the drawn-distance
// test, which `headFp` carries.
[[nodiscard]] bool noteCoveredByLine(const game::Note& note, const LineFrame& frame,
    double chartTimeSec, bool officialFormat, float headFp) noexcept;

// Centre of the hold's end cap relative to the hold's tail, along the head-to-tail
// direction. The cap is always drawn clear of the body, so a tail that sits behind the
// head puts it on that side instead. A compact skin centres the cap on the tail.
[[nodiscard]] float holdTailCapOffset(float headFp, float tailFp, float tailHeightPx,
    bool compact) noexcept;

// Lowest head-sprite centre the hold pass draws, relative to the judge line. The official
// format pins a head that has fallen behind the line onto the line edge; the RPE format
// draws it where the note actually is, so there is no floor at all.
[[nodiscard]] float holdHeadClampMin(float headHeightPx, bool compact, bool clampToLine) noexcept;

// Ascending, stable draw order of line indices by z-order.
void buildLineDrawOrder(const game::Chart& chart, std::vector<int>& out);

} // namespace rechigros::render::scene
