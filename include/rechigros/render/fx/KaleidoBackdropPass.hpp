#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"

#include <filesystem>

namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The kaleidoscope backdrop one level effect layers under the play field. It runs over the
// stage rectangle only and is driven by two mirrored textures plus an animated colour mix.
class KaleidoBackdropPass final {
public:
  KaleidoBackdropPass() = default;
  ~KaleidoBackdropPass() = default;
  KaleidoBackdropPass(const KaleidoBackdropPass&) = delete;
  KaleidoBackdropPass& operator=(const KaleidoBackdropPass&) = delete;

  bool prepare(const std::filesystem::path& assetRoot);
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Draws the backdrop into `viewportX/Y/Width/Height`. `mix` is the animated colour mix,
  // `alpha` fades the object and `chartTimeSec` drives its motion.
  void draw(gl::Texture& normalTexture, gl::Texture& colourTexture, float chartTimeSec,
      float mix, float alpha, int viewportX, int viewportY, int viewportWidth,
      int viewportHeight);

private:
  FullscreenPass pass_;
  bool ready_{};
};

} // namespace rechigros::render::fx
