#include "rechigros/render/scene/PauseBackdrop.hpp"

#include "rechigros/render/scene/Transition.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rechigros::render::scene {
namespace {

// Opacity keys of the pause backdrop fade-in, one per frame of its quarter second.
constexpr float kBackdropAlphaTime[] = {
    0.0F, 0.016666668F, 0.033333335F, 0.05F, 0.06666667F, 0.083333336F, 0.1F,
    0.11666667F, 0.13333334F, 0.15F, 0.16666667F, 0.18333334F, 0.2F, 0.21666667F,
    0.23333333F, 0.25F};
constexpr float kBackdropAlphaValue[] = {
    0.0F, 0.0834299F, 0.22434197F, 0.46590355F, 0.6635525F, 0.7731407F,
    0.84202385F, 0.8893877F, 0.92346704F, 0.94848245F, 0.9668845F, 0.9802265F,
    0.9895567F, 0.9956194F, 0.99896187F, 1.0F};
constexpr float kBackdropAlphaInSlope[] = {
    0.0F, 5.005794F, 8.454725F, 14.493691F, 11.8589325F, 6.5752926F, 4.1329875F,
    2.8418329F, 2.044758F, 1.500925F, 1.1041225F, 0.80052143F, 0.55981284F,
    0.36376122F, 0.200547F, 0.06228805F};
constexpr float kBackdropAlphaOutSlope[] = {
    5.005794F, 8.454723F, 14.493695F, 11.858936F, 6.5752926F, 4.1329875F,
    2.8418329F, 2.044758F, 1.500925F, 1.1041225F, 0.80052143F, 0.55981284F,
    0.36376122F, 0.200547F, 0.06228805F, 0.0F};

} // namespace

float evaluateHermiteCurve(const float* times, const float* values, const float* inSlopes,
    const float* outSlopes, int count, float timeSec) noexcept {
  if (times == nullptr || values == nullptr || inSlopes == nullptr || outSlopes == nullptr) {
    return 0.0F;
  }
  if (count <= 0) return 0.0F;
  if (timeSec <= times[0]) return values[0];
  if (timeSec >= times[count - 1]) return values[count - 1];

  int segment = 0;
  for (int index = 0; index + 1 < count; ++index) {
    if (times[index + 1] > timeSec) {
      segment = index;
      break;
    }
    segment = index;
  }

  const float span = times[segment + 1] - times[segment];
  if (span <= 1.0e-9F) return values[segment + 1];
  const float progress = (timeSec - times[segment]) / span;
  const float t2 = progress * progress;
  const float t3 = t2 * progress;

  // Cubic Hermite basis with the supplied tangents scaled by the segment length.
  const float h00 = 2.0F * t3 - 3.0F * t2 + 1.0F;
  const float h10 = t3 - 2.0F * t2 + progress;
  const float h01 = -2.0F * t3 + 3.0F * t2;
  const float h11 = t3 - t2;
  return h00 * values[segment] + h10 * span * inSlopes[segment] +
      h01 * values[segment + 1] + h11 * span * outSlopes[segment];
}

float pauseBackdropIntroAlpha(float elapsedSec) noexcept {
  constexpr int count = static_cast<int>(sizeof(kBackdropAlphaTime) / sizeof(float));
  return evaluateHermiteCurve(kBackdropAlphaTime, kBackdropAlphaValue, kBackdropAlphaInSlope,
      kBackdropAlphaOutSlope, count, elapsedSec);
}

float resumeBackdropAlpha(float elapsedSec) noexcept {
  const float t = elapsedSec;
  if (!std::isfinite(t)) return 1.0F;
  if (t <= RESUME_BACKDROP_ALPHA_FADE_START_SEC) return 1.0F;
  if (t >= RESUME_BACKDROP_ALPHA_FADE_END_SEC) return 0.0F;
  const float progress = (t - RESUME_BACKDROP_ALPHA_FADE_START_SEC) /
      (RESUME_BACKDROP_ALPHA_FADE_END_SEC - RESUME_BACKDROP_ALPHA_FADE_START_SEC);
  return 1.0F - smoothStep01(progress);
}

float resumeBackdropBrightness(float elapsedSec) noexcept {
  const float progress = elapsedSec / RESUME_BACKDROP_BRIGHTNESS_DURATION_SEC;
  return PAUSE_BACKDROP_BRIGHTNESS + (1.0F - PAUSE_BACKDROP_BRIGHTNESS) *
      smoothStep01(progress);
}

float resumeBackdropFlatten(float elapsedSec) noexcept {
  const float progress = elapsedSec / RESUME_BACKDROP_FLATTEN_DURATION_SEC;
  return PAUSE_BACKDROP_FLATTEN + (0.0F - PAUSE_BACKDROP_FLATTEN) * smoothStep01(progress);
}

} // namespace rechigros::render::scene
