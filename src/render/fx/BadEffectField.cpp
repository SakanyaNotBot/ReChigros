#include "rechigros/render/fx/BadEffectField.hpp"

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>

namespace rechigros::render::fx {

float BadEffectField::alphaAt(double elapsedSec) noexcept {
  const double progress = elapsedSec / LIFETIME_SEC;
  if (progress <= 0.0) return 1.0F;
  if (progress >= 1.0) return 0.0F;
  // Hermite keys: alpha 1 with zero outgoing slope, then alpha 0 with incoming slope
  // -6.6625743 over 25/60 seconds.
  const double squared = progress * progress;
  const double cubic = squared * progress;
  return static_cast<float>(1.0 - 0.223927433333333 * squared
      - 0.776072566666667 * cubic);
}

void BadEffectField::clear() noexcept { effects_.clear(); }

void BadEffectField::spawn(double visualTimeSec, float x, float y, float width, float height,
    float rotationDeg, float u0, float v0, float u1, float v1, const gl::Texture* texture) {
  if (texture == nullptr || !texture->valid()) return;
  if (!(width > 0.0F) || !(height > 0.0F)) return;

  // Keep the newest copies; drop the oldest when the pool is full.
  if (static_cast<int>(effects_.size()) >= MAX_EFFECTS) {
    effects_.erase(effects_.begin());
  }
  Effect effect;
  effect.timeSec = visualTimeSec;
  effect.x = x;
  effect.y = y;
  effect.width = width;
  effect.height = height;
  effect.rotationDeg = rotationDeg;
  effect.u0 = u0;
  effect.v0 = v0;
  effect.u1 = u1;
  effect.v1 = v1;
  effect.texture = texture;
  effects_.push_back(effect);
}

void BadEffectField::draw(batch::SpriteBatch& out, const math::Mat4& projection,
    double visualTimeSec, const gl::Texture& fallback, bool mirrorU) {
  if (effects_.empty()) return;

  // Drop the copies whose fade has finished. They are appended in time order, so the
  // expired ones form a prefix.
  const auto firstLive = std::find_if(effects_.begin(), effects_.end(),
      [visualTimeSec](const Effect& effect) {
        return visualTimeSec - effect.timeSec <= LIFETIME_SEC;
      });
  effects_.erase(effects_.begin(), firstLive);
  if (effects_.empty()) return;

  const gl::Texture* current = nullptr;
  for (const auto& effect : effects_) {
    const float alpha = alphaAt(visualTimeSec - effect.timeSec);
    if (!(alpha > 0.0F)) continue;
    if (current != effect.texture) {
      if (current != nullptr) out.flush(projection, *current, nullptr);
      current = effect.texture;
    }
    out.addQuad(effect.x, effect.y, effect.width, effect.height, effect.rotationDeg, TINT_RED,
        TINT_GREEN, TINT_BLUE, alpha, mirrorU ? effect.u1 : effect.u0, effect.v0,
        mirrorU ? effect.u0 : effect.u1, effect.v1);
  }
  if (current != nullptr) out.flush(projection, *current, nullptr);
  (void)fallback;
}

} // namespace rechigros::render::fx
