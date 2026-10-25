#include "rechigros/render/fx/Yuv420Pass.hpp"

#include "rechigros/render/gl/Texture.hpp"

#include <SDL3/SDL_log.h>

#include <utility>

namespace rechigros::render::fx {
namespace {

// The encoder reads limited-range BT.709, so the shaders map into that range rather than passing
// the full 0..1 span through; using the full range is what makes a recording look washed out.
const char* kLumaFragment = R"(
uniform sampler2D uTexture;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec3 rgb = texture(uTexture, uv).rgb;
  float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  fragmentColor = vec4(16.0 / 255.0 + luma * (219.0 / 255.0), 0.0, 0.0, 1.0);
}
)";

// Chroma covers a 2x2 block of source pixels per output pixel, so the four contributing samples
// are averaged before the colour difference is taken. The selector picks which difference to write.
const char* kChromaFragment = R"(
uniform sampler2D uTexture;
uniform vec2 uSourceSize;
uniform int uPlane;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec2 texel = 1.0 / uSourceSize;
  vec2 first = (floor(uv * uSourceSize - 0.5) + 0.5) * texel;
  vec3 rgb = texture(uTexture, first).rgb;
  rgb += texture(uTexture, first + vec2(texel.x, 0.0)).rgb;
  rgb += texture(uTexture, first + vec2(0.0, texel.y)).rgb;
  rgb += texture(uTexture, first + vec2(texel.x, texel.y)).rgb;
  rgb *= 0.25;
  float blue = -0.114572 * rgb.r - 0.385428 * rgb.g + 0.5 * rgb.b;
  float red = 0.5 * rgb.r - 0.454153 * rgb.g - 0.045847 * rgb.b;
  float value = (uPlane == 0 ? blue : red) * (224.0 / 255.0) + 128.0 / 255.0;
  fragmentColor = vec4(value, 0.0, 0.0, 1.0);
}
)";

} // namespace

Yuv420Pass::~Yuv420Pass() { release(); }

bool Yuv420Pass::ensure(int width, int height) noexcept {
  // Chroma is stored at half resolution in both directions, so the size has to divide evenly.
  const int evenWidth = width & ~1;
  const int evenHeight = height & ~1;
  if (evenWidth <= 0 || evenHeight <= 0) return false;
  if (ready_ && width_ == evenWidth && height_ == evenHeight) return true;
  release();
  width_ = evenWidth;
  height_ = evenHeight;
  if (!lumaProgram_.build(fullscreenVertexSource(), kLumaFragment)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] luma conversion shader failed: %s",
        lumaProgram_.log().c_str());
    release();
    return false;
  }
  if (!chromaProgram_.build(fullscreenVertexSource(), kChromaFragment)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] chroma conversion shader failed: %s",
        chromaProgram_.log().c_str());
    release();
    return false;
  }
  // One target per plane: the luma plane at full size, the two chroma planes at half.
  const int sizes[3][2] = {{width_, height_}, {width_ / 2, height_ / 2},
      {width_ / 2, height_ / 2}};
  glGenTextures(3, planeTextures_);
  glGenFramebuffers(3, planeFbos_);
  for (int index = 0; index < 3; ++index) {
    if (planeTextures_[index] == 0 || planeFbos_[index] == 0) {
      release();
      return false;
    }
    glBindTexture(GL_TEXTURE_2D, planeTextures_[index]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, sizes[index][0], sizes[index][1], 0, GL_RED,
        GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, planeFbos_[index]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        planeTextures_[index], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
          "[record] the conversion target for plane %d is incomplete", index);
      release();
      return false;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  ready_ = FullscreenQuad::instance().ensure();
  if (!ready_) release();
  return ready_;
}

void Yuv420Pass::release() noexcept {
  lumaProgram_.reset();
  chromaProgram_.reset();
  if (planeFbos_[0] != 0) glDeleteFramebuffers(3, planeFbos_);
  if (planeTextures_[0] != 0) glDeleteTextures(3, planeTextures_);
  for (unsigned int& fbo : planeFbos_) fbo = 0;
  for (unsigned int& texture : planeTextures_) texture = 0;
  width_ = 0;
  height_ = 0;
  ready_ = false;
}

void Yuv420Pass::renderPlane(int index, gl::Texture& source, int planeSelector) noexcept {
  const bool luma = index == 0;
  glBindFramebuffer(GL_FRAMEBUFFER, planeFbos_[index]);
  glViewport(0, 0, luma ? width_ : width_ / 2, luma ? height_ : height_ / 2);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, source.id());
  if (luma) {
    lumaProgram_.use();
    lumaProgram_.set1i("uTexture", 0);
  } else {
    chromaProgram_.use();
    chromaProgram_.set1i("uTexture", 0);
    chromaProgram_.set2f("uSourceSize", static_cast<float>(width_), static_cast<float>(height_));
    chromaProgram_.set1i("uPlane", planeSelector);
  }
  FullscreenQuad::instance().draw();
}

bool Yuv420Pass::convert(gl::Texture& source, std::vector<std::uint8_t>& out) noexcept {
  if (!ready_ || !source.valid()) return false;
  renderPlane(0, source, 0);
  renderPlane(1, source, 0);
  renderPlane(2, source, 1);
  // The planes are read back one at a time and land straight in their place in the frame, so the
  // assembled buffer is the encoder's layout with no further work.
  out.resize(frameBytes());
  const GLsizei planeHeights[3] = {height_, height_ / 2, height_ / 2};
  const GLsizei planeWidths[3] = {width_, width_ / 2, width_ / 2};
  std::size_t offset = 0;
  for (int index = 0; index < 3; ++index) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, planeFbos_[index]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const auto planeWidth = static_cast<std::size_t>(planeWidths[index]);
    const auto planeHeight = static_cast<std::size_t>(planeHeights[index]);
    // Read into the plane's place in the frame, then reverse its rows in place: render targets are
    // stored bottom-up while every consumer of these bytes expects the first row to be the top one.
    std::uint8_t* plane = out.data() + offset;
    glReadPixels(0, 0, planeWidths[index], planeHeights[index], GL_RED, GL_UNSIGNED_BYTE, plane);
    for (std::size_t row = 0; row < planeHeight / 2; ++row) {
      std::uint8_t* upper = plane + row * planeWidth;
      std::uint8_t* lower = plane + (planeHeight - 1 - row) * planeWidth;
      for (std::size_t column = 0; column < planeWidth; ++column) {
        std::swap(upper[column], lower[column]);
      }
    }
    offset += planeWidth * planeHeight;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

} // namespace rechigros::render::fx