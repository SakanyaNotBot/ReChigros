#include "rechigros/render/fx/FullscreenPass.hpp"

#include "rechigros/render/gl/Texture.hpp"

#include <SDL_log.h>

namespace rechigros::render::fx {
namespace {
// Clip-space quad as a triangle strip: (-1,-1), (1,-1), (-1,1), (1,1).
constexpr float kQuadPositions[8] = {
    -1.0F, -1.0F,
    1.0F, -1.0F,
    -1.0F, 1.0F,
    1.0F, 1.0F};

const char* kVertexSource = R"(
layout(location = 0) in vec2 aPosition;
out vec2 uv;
void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0);
  uv = aPosition * 0.5 + 0.5;
}
)";

const char* kBlitFragment = R"(
uniform sampler2D uTexture;
uniform vec4 uColor;
uniform float uFlipV;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec2 coord = vec2(uv.x, uFlipV > 0.5 ? 1.0 - uv.y : uv.y);
  vec4 texel = texture(uTexture, coord);
  fragmentColor = vec4(texel.rgb * uColor.rgb * uColor.a, texel.a * uColor.a);
}
)";
} // namespace

const char* fullscreenVertexSource() noexcept { return kVertexSource; }

FullscreenQuad& FullscreenQuad::instance() {
  static FullscreenQuad quad;
  return quad;
}

bool FullscreenQuad::ensure() noexcept {
  if (vao_ != 0) return true;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  if (vao_ == 0 || vbo_ == 0) return false;
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadPositions), kQuadPositions, GL_STATIC_DRAW);
  // Location 0 is the only vertex input; shaders declare it as attribute aPosition.
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

void FullscreenQuad::bind() const noexcept { glBindVertexArray(vao_); }

void FullscreenQuad::draw() const noexcept {
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

FullscreenPass::~FullscreenPass() { release(); }

bool FullscreenPass::build(const char* fragmentSource) noexcept {
  if (fragmentSource == nullptr) return false;
  if (!program_.build(kVertexSource, fragmentSource)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render] fullscreen pass shader failed: %s",
        program_.log().c_str());
    return false;
  }
  return FullscreenQuad::instance().ensure();
}

void FullscreenPass::release() noexcept { program_.reset(); }

void FullscreenPass::draw(gl::Texture& texture, const char* samplerName, int unit) {
  if (!program_.valid()) return;
  program_.use();
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, texture.valid() ? texture.id() : 0U);
  program_.set1i(samplerName, unit);
  glActiveTexture(GL_TEXTURE0);
  FullscreenQuad::instance().draw();
}

BlitPass& BlitPass::instance() {
  static BlitPass pass;
  return pass;
}

bool BlitPass::ensure() noexcept {
  if (ready_) return true;
  if (!program_.build(kVertexSource, kBlitFragment)) return false;
  ready_ = FullscreenQuad::instance().ensure();
  return ready_;
}

void BlitPass::draw(gl::Texture& texture, float red, float green, float blue, float alpha,
    bool flipV) {
  if (!ensure()) return;
  program_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture.valid() ? texture.id() : 0U);
  program_.set1i("uTexture", 0);
  program_.set4f("uColor", red, green, blue, alpha);
  program_.set1f("uFlipV", flipV ? 1.0F : 0.0F);
  FullscreenQuad::instance().draw();
}

} // namespace rechigros::render::fx
