#pragma once

namespace rechigros::render::scene {

// Pause and resume animation curves. The pause backdrop fades in over a quarter second
// along the shipped Hermite curve; the resume countdown fades it back out, brightens the
// scene and flattens it towards neutral grey.
inline constexpr float PAUSE_BACKDROP_FADE_IN_SEC = 0.25F;
inline constexpr float PAUSE_BACKDROP_VIBRANCY = 1.0F;
inline constexpr float PAUSE_BACKDROP_BRIGHTNESS = 0.45F;
inline constexpr float PAUSE_BACKDROP_FLATTEN = -0.125F;
inline constexpr float RESUME_BACKDROP_BRIGHTNESS_DURATION_SEC = 2.0F;
inline constexpr float RESUME_BACKDROP_FLATTEN_DURATION_SEC = 2.5F;
inline constexpr float RESUME_BACKDROP_ALPHA_FADE_START_SEC = 1.5F;
inline constexpr float RESUME_BACKDROP_ALPHA_FADE_END_SEC = 2.25F;
// Separable blur iterations applied to the frozen scene behind the pause menu. The
// reference backdrop is a wide, clearly readable smear rather than a light soften, so the
// repeated small-radius kernel is run enough times to reach that width.
inline constexpr int PAUSE_SCENE_BLUR_ITERATIONS = 6;

// Interpolates a uniformly spaced key table with a cubic Hermite using the supplied
// in/out slopes. `times` may be non-uniform; the search tolerates that.
[[nodiscard]] float evaluateHermiteCurve(const float* times, const float* values,
    const float* inSlopes, const float* outSlopes, int count, float timeSec) noexcept;

// Opacity of the pause backdrop as it fades in.
[[nodiscard]] float pauseBackdropIntroAlpha(float elapsedSec) noexcept;


// Opacity of the backdrop during the resume countdown; it holds, then fades out between
// the two cutoff times.
[[nodiscard]] float resumeBackdropAlpha(float elapsedSec) noexcept;

// Brightness the backdrop returns towards during the resume countdown.
[[nodiscard]] float resumeBackdropBrightness(float elapsedSec) noexcept;

// Flatten towards neutral grey during the resume countdown.
[[nodiscard]] float resumeBackdropFlatten(float elapsedSec) noexcept;

} // namespace rechigros::render::scene
