#pragma once

#include <algorithm>
#include <array>

namespace rechigros::render::fx {

// ScoreControl.Perfect/Good call set_localScale(noteScale * 1.35), which replaces the
// prefab's 1.5 scale; 2.1758 is the note prefab width (9.89 * 0.22).
inline constexpr float kHitEffectPrefabToNoteWidth = 1.35F / 2.1758F;
inline constexpr float kHitEffectParticleSize = 0.3F * kHitEffectPrefabToNoteWidth;
inline constexpr std::array<float, 30> kHitEffectFrameWidths{{
    87.847755F, 141.84775F, 179.84775F, 199.78885F, 211.84775F,
    217.84775F, 221.84775F, 225.84775F, 227.84775F, 227.84775F,
    229.84775F, 231.84775F, 231.7013F, 233.84775F, 233.7013F,
    235.84775F, 235.7013F, 237.84775F, 237.84775F, 237.84775F,
    239.84775F, 239.84775F, 241.84775F, 241.84775F, 243.84775F,
    243.84775F, 245.84775F, 247.84775F, 247.84775F, 247.84775F,
}};

[[nodiscard]] inline float hitEffectProgress(float value) noexcept {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] inline float hitEffectFrameScale(int frame) noexcept {
  return kHitEffectPrefabToNoteWidth * kHitEffectFrameWidths[static_cast<std::size_t>(
      std::clamp(frame, 0, 29))] * 0.01F;
}

[[nodiscard]] inline float particleDragIntegral(float u) noexcept {
  u = hitEffectProgress(u);
  const float u2 = u * u;
  const float u3 = u2 * u;
  const float u4 = u3 * u;
  // The prefab curve uses weightedMode: 0, so the serialized slope is used directly.
  constexpr float tangent = 2.1999998F;
  return 2.5F * (u3 - 0.5F * u4 + tangent * (0.25F * u4 - (2.0F / 3.0F) * u3 + 0.5F * u2));
}

[[nodiscard]] inline float particleTravel(float speed, float progress) noexcept {
  constexpr int steps = 24;
  const float step = hitEffectProgress(progress) / static_cast<float>(steps);
  float sum = 0.0F;
  for (int index = 0; index <= steps; ++index) {
    const float u = static_cast<float>(index) * step;
    const float weight = (index == 0 || index == steps)
        ? 1.0F : ((index & 1) == 1 ? 4.0F : 2.0F);
    sum += weight / (1.0F / speed + particleDragIntegral(u));
  }
  return sum * step / 3.0F * 0.5F;
}

[[nodiscard]] inline float particleSize(float progress) noexcept {
  const float t = hitEffectProgress(progress);
  const float t2 = t * t;
  const float t3 = t2 * t;
  constexpr float y0 = 0.49884492F;
  constexpr float y1 = 0.6941143F;
  // Same unweighted tangent rule as the drag curve.
  constexpr float m0 = 1.6398785F;
  constexpr float m1 = -1.0415096F;
  return y0 * (2.0F * t3 - 3.0F * t2 + 1.0F) + m0 * (t3 - 2.0F * t2 + t)
      + y1 * (3.0F * t2 - 2.0F * t3) + m1 * (t3 - t2);
}

} // namespace rechigros::render::fx
