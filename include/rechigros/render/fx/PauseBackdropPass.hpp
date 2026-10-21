#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"

#include <filesystem>

namespace rechigros::render::gl { class Framebuffer; }

namespace rechigros::render::fx {

// Draws the frozen scene behind the pause and resume overlays. The scene is copied into
// a scratch target, blurred with a separable kernel, then composited through the pause
// backdrop grade (vibrancy, brightness, flatten) at the requested opacity.
class PauseBackdropPass final {
public:
  PauseBackdropPass() = default;
  ~PauseBackdropPass() = default;
  PauseBackdropPass(const PauseBackdropPass&) = delete;
  PauseBackdropPass& operator=(const PauseBackdropPass&) = delete;

  bool prepare(const std::filesystem::path& assetRoot);
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Blurs `source` into `scratch`, then draws the graded result over the current target.
  // `alpha` of zero or less returns without drawing. `vibrancy`, `brightness` and
  // `flatten` follow the shipped pause grading.
  // `output` receives the composite; it is bound explicitly because the blur passes leave
  // their own target bound.
  void draw(gl::Texture& source, gl::Framebuffer& blurScratch, gl::Framebuffer& blurTemp,
      unsigned int outputFbo, float alpha, float vibrancy, float brightness, float flatten);

private:
  FullscreenPass blur_;
  FullscreenPass grade_;
  std::filesystem::path assetRoot_;
  bool ready_{};
};

} // namespace rechigros::render::fx
