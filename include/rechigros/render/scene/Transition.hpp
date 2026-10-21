#pragma once

namespace rechigros::render::scene {

// Official scene timing: the decorative opening line expands over 55 frames and the
// level start UI rides in over 40 frames. The exit fades the background over 40
// frames, then collapses the remaining plain lines between 1.0 s and 1.7 s.
inline constexpr float INTRO_LINE_DURATION_SEC = 55.0F / 60.0F;
inline constexpr float INTRO_HOLD_SEC = 10.0F / 60.0F;
inline constexpr float UI_INTRO_DURATION_SEC = 40.0F / 60.0F;
inline constexpr float UI_OUTRO_DURATION_SEC = 40.0F / 60.0F;
inline constexpr float OUTRO_LINE_COLLAPSE_START_SEC = 1.0F;
inline constexpr float OUTRO_LINE_COLLAPSE_DURATION_SEC = 0.7F;
// The result page paints 0.4 over the artwork, so the exit brings the background to 0.6.
inline constexpr float OUTRO_BG_TRANSMISSION = 0.6F;
// The level start opens at about 0.55x of the raw artwork.
inline constexpr float INTRO_START_BRIGHTNESS = 0.55F;
// The band outside the play field sits at a fixed 0.3 transmission.
inline constexpr float SIDE_FACTOR = 0.3F;

// Decorative opening line scale sampled from the level start curve. The table holds 55
// samples, one per frame of the 55-frame expansion.
[[nodiscard]] float introLineScale(float progress) noexcept;

// Eased progress of the level start UI, derived from the same curve family.
[[nodiscard]] float introUiEase(float progress) noexcept;

// Smoothstep in [0, 1]; the exit fade uses it so it starts and ends without a step.
[[nodiscard]] float smoothStep01(float progress) noexcept;

// Progress of the exit background fade, 0 = gameplay, 1 = result page level.
[[nodiscard]] float outroDimMix(bool inOutro, float elapsedSec) noexcept;

// Remaining length factor of the collapsing plain lines during the exit. A line draws
// nothing outside the collapse window once the exit has started.
[[nodiscard]] float outroLineCollapseScale(bool inOutro, float elapsedSec) noexcept;

// Whether the plain lines still need to be drawn at all during the exit.
[[nodiscard]] bool outroLinesVisible(bool inOutro, float elapsedSec) noexcept;

// Artwork alpha for the play field. `introDimMix` runs 0..1 with the decorative line, so
// the artwork opens at INTRO_START_BRIGHTNESS and lands on the gameplay value as the
// line reaches the stage ends.
[[nodiscard]] float stageTextureAlpha(bool introStarted, float introLineScale,
    float introDimMix, float backgroundDim, bool inOutro, float outroElapsedSec) noexcept;

// Artwork alpha for the side bands, which sit at a fixed transmission.
[[nodiscard]] float sideArtworkAlpha(bool introStarted, float introDimMix) noexcept;

// Alpha of the black overlay over the play field.
[[nodiscard]] float backgroundDimAlpha(bool introStarted, float introDimMix,
    float backgroundDim, bool inOutro, float outroElapsedSec) noexcept;

// Alpha of the black overlay over the side bands.
[[nodiscard]] float sideDimAlpha(bool introStarted, float introDimMix, bool inOutro,
    float outroElapsedSec) noexcept;

// How far the decorative line has grown relative to the point where its tip reaches the
// stage ends: 0 at the start, 1 when the dim has fully landed.
[[nodiscard]] float introDimProgress(float introLineScale, bool officialFormat) noexcept;

} // namespace rechigros::render::scene
