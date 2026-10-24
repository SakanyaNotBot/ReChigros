#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"

#include <filesystem>

namespace rechigros::render::fx {

// Draws the frozen scene behind the pause and resume overlays. The scene runs through the
// reference scalable-blur pyramid (progressively downscaled blur targets, then back up to
// the full view) and the result is composited through the pause backdrop grade (vibrancy,
// brightness, flatten) at the requested opacity.
class PauseBackdropPass final {
public:
  PauseBackdropPass() = default;
  ~PauseBackdropPass() = default;
  PauseBackdropPass(const PauseBackdropPass&) = delete;
  PauseBackdropPass& operator=(const PauseBackdropPass&) = delete;

  bool prepare(const std::filesystem::path& assetRoot);
  // Allocates the pyramid and output targets for this view size ahead of the first pause.
  // Driver-side allocation and texture creation on this scale costs a noticeable stall, so
  // doing it during initialisation keeps the frame where the player pauses smooth.
  bool warmUp(int viewWidth, int viewHeight);
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Blurs `source` through the scalable-blur pyramid, then composites the graded result
  // into `outputFbo`. `alpha` of zero or less returns without drawing. `vibrancy`,
  // `brightness` and `flatten` follow the shipped pause grading.
  void draw(gl::Texture& source, unsigned int outputFbo, float alpha, float vibrancy,
      float brightness, float flatten);

private:
  // One blur pass from `source` into `destination`, with the reference scalable-blur
  // kernel and the fixed full-view radius.
  void blurPass(const gl::Texture& source, gl::Framebuffer& destination, float radiusPx);
  void ensureTargets(int viewWidth, int viewHeight);

  FullscreenPass blur_;
  FullscreenPass grade_;
  // Depth 0 is unused; depths 1..PAUSE_SCENE_BLUR_ITERATIONS hold the downscaled chain.
  static constexpr int kMaxDepth = 4;
  gl::Framebuffer pyramid_[kMaxDepth + 1];
  gl::Framebuffer output_;
  int viewWidth_{};
  int viewHeight_{};
  std::filesystem::path assetRoot_;
  bool ready_{};
};

} // namespace rechigros::render::fx
