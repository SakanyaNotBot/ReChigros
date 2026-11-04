#pragma once

namespace rechigros::render::gl {
class Texture;
}

namespace rechigros::render::fx {
class FullscreenPass;
}

namespace rechigros::render::fx {

// Full-screen FXAA resolve: takes the full-resolution antialias target and writes the
// filtered frame into the currently bound target. The shader follows the reference FXAA
// implementation (luma range test, direction estimate, two weighted candidates), written
// in the same GLSL flavour as the other full-screen passes.
class FxaaPass final {
public:
  FxaaPass();
  ~FxaaPass();
  FxaaPass(const FxaaPass&) = delete;
  FxaaPass& operator=(const FxaaPass&) = delete;

  static FxaaPass& instance();
  bool ensure() noexcept;
  void release() noexcept;

  // Draws `texture` over the current target. `sourceWidth`/`sourceHeight` feed the
  // inverse-resolution uniform.
  void draw(gl::Texture& texture, int sourceWidth, int sourceHeight);

private:
  FullscreenPass* pass_{};
  bool ready_{};
};

} // namespace rechigros::render::fx
