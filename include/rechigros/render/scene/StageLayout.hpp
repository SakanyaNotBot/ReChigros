#pragma once

#include "rechigros/render/math/Mat4.hpp"

namespace rechigros::render::scene {

// Letterboxed play field inside the window, plus the mirrored screen space used
// by geometry authored against it.
struct StageLayout {
  math::Rect stage{};
  // Ratio the target aspect is clamped to: never wider than the viewport.
  float aspect{16.0F / 9.0F};
  float viewWidth{1.0F};
  float viewHeight{1.0F};
  // Horizontal bands kept for the fixed side artwork when the window is wider
  // than the play field.
  bool hasSideBlanks{false};
  bool fullscreen{false};

  [[nodiscard]] float centerX() const noexcept { return stage.x + stage.width * 0.5F; }
  [[nodiscard]] float centerY() const noexcept { return stage.y + stage.height * 0.5F; }
  [[nodiscard]] math::Mat4 projection() const noexcept {
    return math::Mat4::orthoTopLeft(viewWidth, viewHeight);
  }
};

// Fit a stage of `targetAspect` into the viewport. Non-positive aspect ratios
// and the `fullscreenStage` flag make the stage cover the whole viewport.
[[nodiscard]] StageLayout computeStageLayout(float viewWidth, float viewHeight, float targetAspect,
    bool fullscreenStage) noexcept;

// Scale factor applied to UI elements laid out on a 1080-unit tall canvas.
[[nodiscard]] float hudUnitScale(const StageLayout& layout) noexcept;

} // namespace rechigros::render::scene
