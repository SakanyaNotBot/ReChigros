#pragma once

#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/ShaderProgram.hpp"

namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// Shared vertex shader source for every fullscreen pass. Texture coordinates are
// derived from the clip-space position so passes only need the vertex buffer.
[[nodiscard]] const char* fullscreenVertexSource() noexcept;

// Owns the fullscreen quad geometry. One instance is shared by all passes.
class FullscreenQuad final {
public:
  static FullscreenQuad& instance();

  // Creates the VAO on first use and binds the shared vertex buffer.
  [[nodiscard]] bool ensure() noexcept;
  void bind() const noexcept;
  [[nodiscard]] unsigned int vao() const noexcept { return vao_; }
  void draw() const noexcept;

private:
  FullscreenQuad() = default;
  unsigned int vao_{};
  unsigned int vbo_{};
};

// Shader-driven fullscreen pass: bind a target, bind the input texture, set
// uniforms through program(), then draw().
class FullscreenPass final {
public:
  FullscreenPass() = default;
  ~FullscreenPass();
  FullscreenPass(const FullscreenPass&) = delete;
  FullscreenPass& operator=(const FullscreenPass&) = delete;

  bool build(const char* fragmentSource, bool rpeViewport = false) noexcept;
  void release() noexcept;

  [[nodiscard]] bool valid() const noexcept { return program_.valid(); }
  [[nodiscard]] gl::ShaderProgram& program() noexcept { return program_; }
  [[nodiscard]] const gl::ShaderProgram& program() const noexcept { return program_; }

  // Binds `texture` to `unit` under `samplerName` and draws the quad.
  void draw(gl::Texture& texture, const char* samplerName = "uTexture", int unit = 0);

private:
  gl::ShaderProgram program_;
};

// Copy shader for presenting a texture. Uploaded images are stored top-down while
// render targets are bottom-up, so the vertical flip is a uniform.
class BlitPass final {
public:
  static BlitPass& instance();

  bool ensure() noexcept;
  // Draws `texture` over the current target with a tint.
  void draw(gl::Texture& texture, float red, float green, float blue, float alpha, bool flipV);

private:
  BlitPass() = default;
  gl::ShaderProgram program_;
  bool ready_{};
};

} // namespace rechigros::render::fx
