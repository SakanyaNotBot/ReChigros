#include "rechigros/render/scene/RevealCamera.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::scene {
namespace {

float revealScale(bool active, float scale) noexcept {
  if (!active) return 1.0F;
  return std::clamp(std::isfinite(scale) ? scale : 1.0F, 0.30F, 1.0F);
}

float smoothStep(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  return value * value * (3.0F - 2.0F * value);
}

} // namespace

RevealCameraState resolveRevealCamera(bool enabled, bool inIntro, bool inOutro,
    double chartTimeSec, double playheadSec, double totalDurationSec) noexcept {
  RevealCameraState state;
  if (!enabled || inIntro || inOutro || chartTimeSec < 0.0) return state;

  constexpr float transitionSec = 1.0F;
  constexpr float targetScale = 0.30F;
  constexpr float hiddenAlpha = 0.20F;
  const float total = static_cast<float>(totalDurationSec);
  const float playhead = static_cast<float>(playheadSec);
  const bool preOutro = total > 0.0F && playhead >= std::max(0.0F, total - transitionSec);
  state.active = true;
  state.hiddenObjects = !preOutro;
  if (preOutro) {
    const float start = std::max(0.0F, total - transitionSec);
    state.scale = targetScale + (1.0F - targetScale) * smoothStep((playhead - start) / transitionSec);
  } else {
    const float settled = smoothStep(playhead / transitionSec);
    state.scale = 1.0F + (targetScale - 1.0F) * settled;
    state.hiddenFloorAlpha = hiddenAlpha * settled;
  }
  state.scale = revealScale(true, state.scale);
  return state;
}

math::Rect revealStageRect(const StageLayout& layout, bool active, float scale) noexcept {
  if (!active || scale >= 0.999F) return layout.stage;
  const float factor = revealScale(active, scale);
  const float width = layout.stage.width * factor;
  const float height = layout.stage.height * factor;
  return {layout.centerX() - width * 0.5F, layout.centerY() - height * 0.5F, width, height};
}

math::Rect revealViewportWorldBounds(const StageLayout& layout, bool active, float scale) noexcept {
  if (!active) return layout.stage;
  const float factor = revealScale(active, scale);
  const float centerX = layout.centerX();
  const float centerY = layout.centerY();
  const float left = centerX + (0.0F - centerX) / factor;
  const float top = centerY + (0.0F - centerY) / factor;
  const float right = centerX + (layout.viewWidth - centerX) / factor;
  const float bottom = centerY + (layout.viewHeight - centerY) / factor;
  return {left, top, right - left, bottom - top};
}

float normalizeLineAlpha(float alphaRaw) noexcept {
  if (!std::isfinite(alphaRaw)) return 1.0F;
  if (alphaRaw <= 0.0F) return 0.0F;
  if (alphaRaw > 1.5F) return std::clamp(alphaRaw / 255.0F, 0.0F, 1.0F);
  return std::clamp(alphaRaw, 0.0F, 1.0F);
}

float revealLineAlpha(float alphaRaw, bool hideLine, bool revealHiddenObjects,
    float hiddenFloorAlpha) noexcept {
  float alpha = normalizeLineAlpha(alphaRaw);
  if (revealHiddenObjects && hiddenFloorAlpha > 0.001F &&
      (hideLine || alpha <= 0.0F || alphaRaw < 0.0F)) {
    alpha = std::max(alpha, hiddenFloorAlpha);
  }
  return alpha;
}

} // namespace rechigros::render::scene
