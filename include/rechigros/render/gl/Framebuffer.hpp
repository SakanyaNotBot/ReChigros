#pragma once

#include "rechigros/render/gl/Texture.hpp"

namespace rechigros::render::gl {

// Offscreen colour target. The colour attachment is exposed as a Texture so it can
// be sampled directly by later passes.
class Framebuffer final {
public:
  Framebuffer() = default;
  ~Framebuffer();
  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;
  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer&& other) noexcept;

  // Allocates (or reallocates) the colour attachment. Reusing the object keeps the
  // texture identity stable across resizes.
  bool resize(int width, int height) noexcept;
  void reset() noexcept;

  void bind() const noexcept;
  static void bindDefault(int viewportWidth, int viewportHeight) noexcept;

  [[nodiscard]] unsigned int fbo() const noexcept { return fbo_; }
  [[nodiscard]] Texture& color() noexcept { return color_; }
  [[nodiscard]] const Texture& color() const noexcept { return color_; }
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] bool valid() const noexcept { return fbo_ != 0 && color_.valid(); }

private:
  unsigned int fbo_{};
  Texture color_;
  int width_{};
  int height_{};
};

} // namespace rechigros::render::gl
