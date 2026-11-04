#pragma once

#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

namespace rechigros::render::scene {

struct RevealCameraState {
  bool active{false};
  float scale{1.0F};
  bool hiddenObjects{false};
  float hiddenFloorAlpha{};
};

[[nodiscard]] RevealCameraState resolveRevealCamera(bool enabled, bool inIntro, bool inOutro,
    double chartTimeSec, double playheadSec, double totalDurationSec) noexcept;

// The inner rectangle is used only by the backdrop masks and stage-scoped effects. Chart
// geometry stays in the original stage and is scaled by StageLayout::stageProjection().
[[nodiscard]] math::Rect revealStageRect(const StageLayout& layout, bool active,
    float scale) noexcept;

// Converts the visible viewport back into unscaled chart coordinates. Non-reveal frames keep
// the ordinary stage bounds.
[[nodiscard]] math::Rect revealViewportWorldBounds(const StageLayout& layout, bool active,
    float scale) noexcept;

[[nodiscard]] float normalizeLineAlpha(float alphaRaw) noexcept;
[[nodiscard]] float revealLineAlpha(float alphaRaw, bool hideLine, bool revealHiddenObjects,
    float hiddenFloorAlpha) noexcept;

} // namespace rechigros::render::scene
