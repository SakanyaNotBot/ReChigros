#pragma once

namespace rechigros::render::gl {
class Framebuffer;
class Texture;
} // namespace rechigros::render::gl

namespace rechigros::render::fx {
class FullscreenPass;
}

namespace rechigros::render::scene {

// The reference pause screen leaves the frozen HUD readable but soft: it is part of the
// graded, blurred backdrop rather than a sharp overlay on top of it. This helper renders
// the dimmed HUD at reduced resolution, blurs it with the same separable kernel as the
// other backdrop passes, and composites it over the graded frame.
class PauseHudBlur final {
public:
  PauseHudBlur();
  ~PauseHudBlur();
  PauseHudBlur(const PauseHudBlur&) = delete;
  PauseHudBlur& operator=(const PauseHudBlur&) = delete;

  // Compiles the helper passes. Safe to call again; shaders are built once.
  bool prepare();
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept;

  // Sizes and binds the reduced-resolution HUD target for the given output view. The
  // caller draws the HUD with a stage layout computed for width()/height() and then calls
  // composite(). Returns false when the GL resources are unavailable.
  bool begin(int viewWidth, int viewHeight);

  // Allocates the reduced-resolution targets for this view size ahead of the first pause, so
  // the frame where the player pauses does not pay for the driver-side allocation.
  void warmUp(int viewWidth, int viewHeight);

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

  // Blurs the HUD target and composites it over the currently bound output target. The
  // brightness scales the premultiplied HUD colour before blending.
  void composite(float brightness);

  // The composited texture (the last blur result), for tests and callers that need it.
  [[nodiscard]] const gl::Texture* texture() const noexcept;

private:
  class Impl;
  Impl* impl_{};
};

} // namespace rechigros::render::scene
