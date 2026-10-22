#include "rechigros/render/batch/SpriteBatch.hpp"

#include "rechigros/render/gl/ShaderProgram.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/math/Mat4.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>

namespace rechigros::render::batch {
namespace {
constexpr const char* VERTEX_SOURCE = R"(
uniform mat4 uProjection;
in vec2 aCorner;
in vec4 aCenterSize;
in vec2 aRotation;
in vec4 aUvRect;
in vec4 aColor;
in float aTextureSelect;
out vec2 vUv;
out vec4 vColor;
out float vTextureSelect;
void main() {
  vec2 scaled = aCorner * aCenterSize.zw;
  vec2 position = aCenterSize.xy + vec2(
      scaled.x * aRotation.x - scaled.y * aRotation.y,
      scaled.x * aRotation.y + scaled.y * aRotation.x);
  gl_Position = uProjection * vec4(position, 0.0, 1.0);
  vUv = mix(aUvRect.xy, aUvRect.zw, aCorner + vec2(0.5));
  vColor = aColor;
  vTextureSelect = aTextureSelect;
}
)";

constexpr const char* FRAGMENT_SOURCE = R"(
uniform sampler2D uTexture;
uniform sampler2D uTextureBody;
uniform int uHasBody;
in vec2 vUv;
in vec4 vColor;
in float vTextureSelect;
out vec4 fragmentColor;
void main() {
  vec4 texel;
  if (vTextureSelect >= 0.5 && uHasBody == 1) {
    texel = texture(uTextureBody, vUv);
  } else {
    texel = texture(uTexture, vUv);
  }
  // Premultiplied output: geometry is composited over the frame, not darkened by it.
  fragmentColor.a = texel.a * vColor.a;
  fragmentColor.rgb = texel.rgb * vColor.rgb * vColor.a;
}
)";

constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;
constexpr float kQuadCorners[8] = {
    -0.5F, -0.5F,
    0.5F, -0.5F,
    -0.5F, 0.5F,
    0.5F, 0.5F};
} // namespace

SpriteBatch::SpriteBatch() = default;

SpriteBatch::~SpriteBatch() { release(); }

bool SpriteBatch::ensureProgram() noexcept {
  if (program_ != nullptr) return program_->valid();
  program_ = new gl::ShaderProgram();
  if (!program_->build(VERTEX_SOURCE, FRAGMENT_SOURCE)) {
    // Without the program every batch draw becomes a no-op, which presents as a missing
    // picture rather than as a shader failure, so the diagnostic is printed here.
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render] sprite batch shader failed: %s",
        program_->log().c_str());
    delete program_;
    program_ = nullptr;
    return false;
  }
  return true;
}

bool SpriteBatch::ensureBuffers() noexcept {
  if (vao_ != 0 && instanceVbo_ != 0) return true;
  if (!ensureProgram()) return false;
  if (buffer_ == nullptr) buffer_ = new SpriteInstance[MAX_BATCH_SPRITES];

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &quadVbo_);
  glGenBuffers(1, &instanceVbo_);
  if (vao_ == 0 || quadVbo_ == 0 || instanceVbo_ == 0) return false;

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadCorners), kQuadCorners, GL_STATIC_DRAW);

  const int corner = program_->attribute("aCorner");
  if (corner >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(corner));
    glVertexAttribPointer(static_cast<GLuint>(corner), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
#ifdef RECHIGROS_GLES
    glVertexAttribDivisor(static_cast<GLuint>(corner), 0U);
#else
    glVertexAttribDivisor(static_cast<GLuint>(corner), 0U);
#endif
  }

  glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
  const auto stride = static_cast<GLsizei>(sizeof(SpriteInstance));
  const auto offset = [](std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
  };
  const int centerSize = program_->attribute("aCenterSize");
  if (centerSize >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(centerSize));
    glVertexAttribPointer(static_cast<GLuint>(centerSize), 4, GL_FLOAT, GL_FALSE, stride, offset(0));
    glVertexAttribDivisor(static_cast<GLuint>(centerSize), 1U);
  }
  const int rotation = program_->attribute("aRotation");
  if (rotation >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(rotation));
    glVertexAttribPointer(static_cast<GLuint>(rotation), 2, GL_FLOAT, GL_FALSE, stride,
        offset(4 * sizeof(float)));
    glVertexAttribDivisor(static_cast<GLuint>(rotation), 1U);
  }
  const int uvRect = program_->attribute("aUvRect");
  if (uvRect >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(uvRect));
    glVertexAttribPointer(static_cast<GLuint>(uvRect), 4, GL_FLOAT, GL_FALSE, stride,
        offset(6 * sizeof(float)));
    glVertexAttribDivisor(static_cast<GLuint>(uvRect), 1U);
  }
  const int color = program_->attribute("aColor");
  if (color >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(color));
    glVertexAttribPointer(static_cast<GLuint>(color), 4, GL_FLOAT, GL_FALSE, stride,
        offset(10 * sizeof(float)));
    glVertexAttribDivisor(static_cast<GLuint>(color), 1U);
  }
  const int textureSelect = program_->attribute("aTextureSelect");
  if (textureSelect >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(textureSelect));
    glVertexAttribPointer(static_cast<GLuint>(textureSelect), 1, GL_FLOAT, GL_FALSE, stride,
        offset(14 * sizeof(float)));
    glVertexAttribDivisor(static_cast<GLuint>(textureSelect), 1U);
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

void SpriteBatch::release() noexcept {
  if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
  if (quadVbo_ != 0) glDeleteBuffers(1, &quadVbo_);
  if (instanceVbo_ != 0) glDeleteBuffers(1, &instanceVbo_);
  vao_ = 0;
  quadVbo_ = 0;
  instanceVbo_ = 0;
  delete[] buffer_;
  buffer_ = nullptr;
  delete program_;
  program_ = nullptr;
  count_ = 0;
}

void SpriteBatch::begin() noexcept {
  count_ = 0;
  clipEnabled_ = false;
  // Buffers are allocated lazily, but a batch must never hand out writes into a
  // null instance array, so allocFailure latches and addQuad* become no-ops.
  allocFailure_ = !ensureBuffers();
}

bool SpriteBatch::addQuadSelect(float centerX, float centerY, float width, float height,
    float rotationDeg, float red, float green, float blue, float alpha,
    float u0, float v0, float u1, float v1, float textureSelect) {
  if (count_ >= MAX_BATCH_SPRITES) return false;
  if (allocFailure_ || buffer_ == nullptr) return true;
  (void)clipRect_;
  if (width == 0.0F || height == 0.0F || alpha <= 0.0F) return true;
  SpriteInstance& instance = buffer_[count_++];
  instance.centerX = centerX;
  instance.centerY = centerY;
  instance.width = width;
  instance.height = height;
  if (rotationDeg == 0.0F) {
    instance.rotationCos = 1.0F;
    instance.rotationSin = 0.0F;
  } else {
    const float radians = rotationDeg * kDegToRad;
    instance.rotationCos = std::cos(radians);
    instance.rotationSin = std::sin(radians);
  }
  instance.u0 = u0;
  instance.v0 = v0;
  instance.u1 = u1;
  instance.v1 = v1;
  instance.red = red;
  instance.green = green;
  instance.blue = blue;
  instance.alpha = alpha;
  instance.textureSelect = textureSelect;
  return true;
}

bool SpriteBatch::addQuad(float centerX, float centerY, float width, float height,
    float rotationDeg, float red, float green, float blue, float alpha,
    float u0, float v0, float u1, float v1) {
  return addQuadSelect(centerX, centerY, width, height, rotationDeg, red, green, blue, alpha,
      u0, v0, u1, v1, 0.0F);
}

bool SpriteBatch::addQuadAnchored(float x, float y, float width, float height, float rotationDeg,
    float red, float green, float blue, float alpha,
    float anchorX, float anchorY,
    float u0, float v0, float u1, float v1) {
  if (!std::isfinite(anchorX)) anchorX = 0.5F;
  if (!std::isfinite(anchorY)) anchorY = 0.5F;
  const float dx = (anchorX - 0.5F) * width;
  const float dy = (anchorY - 0.5F) * height;
  const float radians = rotationDeg * kDegToRad;
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  const float ox = dx * c - dy * s;
  const float oy = dx * s + dy * c;
  return addQuad(x - ox, y - oy, width, height, rotationDeg, red, green, blue, alpha,
      u0, v0, u1, v1);
}

bool SpriteBatch::addQuadTopLeft(float x, float y, float width, float height,
    float red, float green, float blue, float alpha,
    float u0, float v0, float u1, float v1) {
  return addQuad(x + width * 0.5F, y + height * 0.5F, width, height, 0.0F,
      red, green, blue, alpha, u0, v0, u1, v1);
}

bool SpriteBatch::addLine(float x0, float y0, float x1, float y1, float width,
    float red, float green, float blue, float alpha) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.0001F) return true;
  const float degrees = std::atan2(dy, dx) * (180.0F / 3.14159265358979323846F);
  return addQuad((x0 + x1) * 0.5F, (y0 + y1) * 0.5F, length, width, degrees,
      red, green, blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
}

void SpriteBatch::setClip(bool enabled, const math::Rect& rect) noexcept {
  clipEnabled_ = enabled;
  clipLeft_ = rect.left();
  clipTop_ = rect.top();
  clipRight_ = rect.right();
  clipBottom_ = rect.bottom();
}

void SpriteBatch::discard() noexcept { count_ = 0; }

void SpriteBatch::flush(const math::Mat4& projection, const gl::Texture& texture,
    const gl::Texture* bodyTexture) {
  if (count_ == 0) return;
  if (!ensureBuffers()) {
    count_ = 0;
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
  const auto bytes = static_cast<GLsizeiptr>(count_ * static_cast<int>(sizeof(SpriteInstance)));
  glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  program_->use();
  program_->setMat4("uProjection", projection.data());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture.valid() ? texture.id() : gl::whiteTexture().id());
  program_->set1i("uTexture", 0);
  glActiveTexture(GL_TEXTURE1);
  const unsigned int bodyId =
      bodyTexture != nullptr && bodyTexture->valid() ? bodyTexture->id() : 0U;
  glBindTexture(GL_TEXTURE_2D, bodyId != 0 ? bodyId : (texture.valid() ? texture.id() : 0U));
  program_->set1i("uTextureBody", 1);
  program_->set1i("uHasBody", bodyId != 0 ? 1 : 0);
  glActiveTexture(GL_TEXTURE0);

  glBindVertexArray(vao_);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count_);
  glBindVertexArray(0);
  count_ = 0;
}

} // namespace rechigros::render::batch
