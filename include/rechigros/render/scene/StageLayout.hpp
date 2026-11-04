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
  // Reflects the play field about its centre for demo mode. Only geometry that
  // belongs to the field is reflected; the HUD and the side bands are not.
  bool mirror{false};
  // Uniform reveal transform applied after chart geometry has been evaluated in stage space.
  float fieldScale{1.0F};
  bool cameraActive{};
  bool cameraIncludesUi{};
  math::Mat4 cameraMatrix{};

  [[nodiscard]] float centerX() const noexcept { return stage.x + stage.width * 0.5F; }
  [[nodiscard]] float centerY() const noexcept { return stage.y + stage.height * 0.5F; }
  [[nodiscard]] math::Mat4 projection() const noexcept {
    return cameraIncludesUi ? cameraMatrix : math::Mat4::orthoTopLeft(viewWidth, viewHeight);
  }
  // Play-field geometry shares mirror, reveal and the optional chart camera. UI uses
  // projection(), which follows the same camera only when the chart enables that option.
  [[nodiscard]] math::Mat4 stageProjection() const noexcept {
    math::Mat4 transform = mirror ? math::Mat4::mirrorX(centerX()) : math::Mat4::identity();
    if (std::isfinite(fieldScale) && fieldScale > 1.0e-6F && fieldScale < 0.999999F) {
      transform = transform * math::Mat4::translation(centerX(), centerY()) *
          math::Mat4::scaling(fieldScale, fieldScale) * math::Mat4::translation(-centerX(), -centerY());
    }
    const auto base = cameraActive ? cameraMatrix : projection();
    return base * transform;
  }
};

// Fit a stage of `targetAspect` into the viewport. Non-positive aspect ratios
// and the `fullscreenStage` flag make the stage cover the whole viewport.
[[nodiscard]] StageLayout computeStageLayout(float viewWidth, float viewHeight, float targetAspect,
    bool fullscreenStage) noexcept;

// Scale factor applied to UI elements laid out on a 1080-unit tall canvas.
[[nodiscard]] float hudUnitScale(const StageLayout& layout) noexcept;

} // namespace rechigros::render::scene
