#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"

namespace rechigros::render::fx {

// The Ametrine failure vignette: a soft dark ring that closes in from the corners while
// the challenge is being lost.
//
// The pass writes straight over the finished frame as an alpha blend and samples no
// texture, so it needs no input and no render target. The HUD is dimmed by the same ramp
// because the reference scales the whole HUD canvas with it.
class AmetrineVignette final {
public:
  // Radius of the untouched centre of the screen, in normalised coordinates.
  static constexpr float RADIUS = 0.24F;
  // Width of the fade from the centre to full darkness.
  static constexpr float SMOOTHNESS = 0.55F;
  // Share of the HUD opacity the vignette removes at full strength.
  static constexpr float HUD_DIM = 0.32F;

  AmetrineVignette() = default;
  ~AmetrineVignette();
  AmetrineVignette(const AmetrineVignette&) = delete;
  AmetrineVignette& operator=(const AmetrineVignette&) = delete;

  // Compiles the program. Safe to call repeatedly; a build failure latches off so a
  // driver without the required precision is not retried every frame.
  [[nodiscard]] bool prepare() noexcept;
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Draws the vignette over whatever target is bound. `darkness` is clamped to 0..1, and
  // a value at or below the draw threshold leaves the target untouched.
  void draw(float darkness);

  // Opacity multiplier the HUD takes from the same ramp.
  [[nodiscard]] static float hudDimFactor(float darkness) noexcept;

private:
  FullscreenPass pass_;
  bool ready_{};
  bool failed_{};
};

} // namespace rechigros::render::fx
