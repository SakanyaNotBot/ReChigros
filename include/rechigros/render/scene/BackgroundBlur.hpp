#pragma once

#include <filesystem>

namespace rechigros::render::gl { class Framebuffer; class Texture; }
namespace rechigros::render::fx { class FullscreenPass; }

namespace rechigros::render::scene {

// The play field does not draw the raw artwork: the reference renders a heavily blurred,
// downscaled copy of the background illustration once per viewport size and reuses it for
// the stage and for the fixed side panels. This class owns that low-resolution blur chain.
class BackgroundBlur final {
public:
  BackgroundBlur();
  ~BackgroundBlur();
  BackgroundBlur(const BackgroundBlur&) = delete;
  BackgroundBlur& operator=(const BackgroundBlur&) = delete;

  // Compiles the helper passes. Safe to call again; shaders are built once.
  bool prepare();
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept;

  // Rebuilds the blurred artwork if the viewport size or the source texture changed.
  // Returns the blurred texture, or nullptr when the artwork is missing or the GL
  // resources could not be allocated (the caller falls back to its flat/dark path).
  const gl::Texture* ensure(int viewWidth, int viewHeight, const gl::Texture* artwork);

  // Drops the cached result (e.g. after a viewport resize or a context loss).
  void invalidate() noexcept;

  [[nodiscard]] const gl::Texture* texture() const noexcept;

private:
  bool rebuild(int viewWidth, int viewHeight, const gl::Texture& artwork);

  // One shared pass object per operation keeps the shader programs compact.
  class Impl;
  Impl* impl_{};
};

} // namespace rechigros::render::scene
