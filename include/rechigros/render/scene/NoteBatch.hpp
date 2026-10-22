#pragma once

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/LineFrame.hpp"

#include <array>
#include <limits>
#include <vector>

namespace rechigros::game { class Chart; }

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

// Ascending, stable draw order of line indices by z-order.
void buildLineDrawOrder(const game::Chart& chart, std::vector<int>& out);

} // namespace rechigros::render::scene
