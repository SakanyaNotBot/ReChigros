#pragma once

#include "rechigros/render/gl/Texture.hpp"

namespace rechigros::render::gl {

// Offscreen colour target. A normal target exposes its colour attachment as a Texture
// so later passes can sample it. A multisampled target uses a renderbuffer instead and
// must be resolved into a normal target before it is sampled.
class Framebuffer final {
public:
  Framebuffer() = default;
  ~Framebuffer();
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;
  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer&& other) noexcept;

  // Allocates (or reallocates) the colour texture. Reusing the object keeps the
  // texture identity stable across resizes.
  bool resize(int width, int height) noexcept;
  // Allocates a multisampled colour renderbuffer. The requested sample count is
  // clamped to GL_MAX_SAMPLES; fewer than two samples reports failure.
  bool resizeMultisample(int width, int height, int samples) noexcept;
  // Resolves the multisampled colour attachment into `target`. Both targets must have
  // the same size, and `target` must be a normal texture-backed target.
  bool resolveTo(Framebuffer& target) const noexcept;
  void reset() noexcept;

  void bind() const noexcept;
  static void bindDefault(int viewportWidth, int viewportHeight) noexcept;

  [[nodiscard]] unsigned int fbo() const noexcept { return fbo_; }
  [[nodiscard]] Texture& color() noexcept { return color_; }
  [[nodiscard]] const Texture& color() const noexcept { return color_; }
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] int samples() const noexcept { return samples_; }
  [[nodiscard]] bool multisampled() const noexcept { return samples_ > 0; }
  [[nodiscard]] bool valid() const noexcept {
    return fbo_ != 0 && (samples_ > 0 ? colorRenderbuffer_ != 0 : color_.valid());
  }

private:
  unsigned int fbo_{};
  Texture color_;
  unsigned int colorRenderbuffer_{};
  int width_{};
  int height_{};
  int samples_{};
};

} // namespace rechigros::render::gl
