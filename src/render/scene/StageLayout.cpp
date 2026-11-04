#include "rechigros/render/scene/StageLayout.hpp"

#include <algorithm>

namespace rechigros::render::scene {

StageLayout computeStageLayout(float viewWidth, float viewHeight, float targetAspect,
    bool fullscreenStage) noexcept {
  StageLayout layout;
  layout.viewWidth = std::max(1.0F, viewWidth);
  layout.viewHeight = std::max(1.0F, viewHeight);

  float effectiveAspect = fullscreenStage ? 0.0F : targetAspect;
  if (effectiveAspect <= 0.0F) {
    // Cover the viewport: no letterboxing at all.
    layout.aspect = layout.viewWidth / layout.viewHeight;
    layout.stage = {0.0F, 0.0F, layout.viewWidth, layout.viewHeight};
    layout.fullscreen = true;
    layout.hasSideBlanks = false;
    return layout;
  }

  const float viewAspect = layout.viewWidth / layout.viewHeight;
  // Never force the play field wider than the window; this keeps 16:10 and 4:3
  // displays filled vertically instead of leaving unpainted bands.
  const float aspect = std::min(effectiveAspect, viewAspect);
  layout.aspect = aspect;

  if (viewAspect >= aspect) {
    // Window is wider than the play field: the bands sit left and right.
    const float height = layout.viewHeight;
    const float width = height * aspect;
    layout.stage = {(layout.viewWidth - width) * 0.5F, 0.0F, width, height};
  } else {
    // Window is taller than the play field: the bands sit top and bottom.
    const float width = layout.viewWidth;
    const float height = width / aspect;
    layout.stage = {0.0F, (layout.viewHeight - height) * 0.5F, width, height};
  }

  layout.hasSideBlanks = layout.stage.x > 0.5F ||
      layout.stage.x + layout.stage.width < layout.viewWidth - 0.5F;
  layout.fullscreen = !layout.hasSideBlanks && std::abs(layout.stage.y) < 0.5F &&
      std::abs(layout.stage.height - layout.viewHeight) < 0.5F;
  return layout;
}

float hudUnitScale(const StageLayout& layout) noexcept {
  // HUD coordinates are authored on a 1000-unit tall canvas.
  return layout.stage.height > 1.0e-6F ? layout.stage.height / 1000.0F : 1.0F;
}

} // namespace rechigros::render::scene
