#include "rechigros/render/fx/Yuv420Pass.hpp"

#include "rechigros/render/gl/Texture.hpp"

#include <SDL3/SDL_log.h>

#include <cstring>
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
  // Render targets are stored bottom-up while the encoder reads the first row as the picture's
  // top, so the source is sampled upside down here. Doing it in the sampler means the bytes come
  // back in the order they are wanted, with no pass over the frame afterwards.
  vec3 rgb = texture(uTexture, vec2(uv.x, 1.0 - uv.y)).rgb;
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
  vec2 coord = vec2(uv.x, 1.0 - uv.y);
  vec2 first = (floor(coord * uSourceSize - 0.5) + 0.5) * texel;
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
  // Two target sets, one per plane: the luma plane at full size, the two chroma planes at half.
  // Writing into the other set lets the next frame render while the previous one is still being
  // transferred out, which is what makes the deferred path overlap instead of serialising.
  const int sizes[3][2] = {{width_, height_}, {width_ / 2, height_ / 2},
      {width_ / 2, height_ / 2}};
  glGenTextures(6, &planeTextures_[0][0]);
  glGenFramebuffers(6, &planeFbos_[0][0]);
  for (int slot = 0; slot < 2; ++slot) {
    for (int index = 0; index < 3; ++index) {
      if (planeTextures_[slot][index] == 0 || planeFbos_[slot][index] == 0) {
        release();
        return false;
      }
      glBindTexture(GL_TEXTURE_2D, planeTextures_[slot][index]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, sizes[index][0], sizes[index][1], 0, GL_RED,
          GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, planeFbos_[slot][index]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
          planeTextures_[slot][index], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[record] the conversion target for plane %d is incomplete", index);
        release();
        return false;
      }
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // The staging buffers are sized for their own plane; the colour planes are a quarter of luma.
  const int planeCounts[3][2] = {{width_, height_}, {width_ / 2, height_ / 2},
      {width_ / 2, height_ / 2}};
  glGenBuffers(6, &packBuffers_[0][0]);
  for (int slot = 0; slot < 2; ++slot) {
    for (int index = 0; index < 3; ++index) {
      if (packBuffers_[slot][index] == 0) {
        release();
        return false;
      }
      const auto bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(planeCounts[index][0])
          * static_cast<std::size_t>(planeCounts[index][1]));
      glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffers_[slot][index]);
      glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    }
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  ready_ = FullscreenQuad::instance().ensure();
  if (!ready_) release();
  return ready_;
}

void Yuv420Pass::release() noexcept {
  lumaProgram_.reset();
  chromaProgram_.reset();
  if (planeFbos_[0][0] != 0) glDeleteFramebuffers(6, &planeFbos_[0][0]);
  if (planeTextures_[0][0] != 0) glDeleteTextures(6, &planeTextures_[0][0]);
  if (packBuffers_[0][0] != 0) glDeleteBuffers(6, &packBuffers_[0][0]);
  for (auto& slot : planeFbos_) {
    for (unsigned int& fbo : slot) fbo = 0;
  }
  for (auto& slot : planeTextures_) {
    for (unsigned int& texture : slot) texture = 0;
  }
  for (auto& slot : packBuffers_) {
    for (unsigned int& buffer : slot) buffer = 0;
  }
  width_ = 0;
  height_ = 0;
  readSlot_ = 0;
  writeSlot_ = 0;
  pendingCount_ = 0;
  ready_ = false;
}

void Yuv420Pass::renderPlane(int slot, int index, gl::Texture& source,
    int planeSelector) noexcept {
  const bool luma = index == 0;
  glBindFramebuffer(GL_FRAMEBUFFER, planeFbos_[slot][index]);
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

bool Yuv420Pass::request(gl::Texture& source) noexcept {
  if (!ready_ || !source.valid() || pendingCount_ >= 2) return false;
  const int slot = writeSlot_;
  renderPlane(slot, 0, source, 0);
  renderPlane(slot, 1, source, 0);
  renderPlane(slot, 2, source, 1);
  const GLsizei planeHeights[3] = {height_, height_ / 2, height_ / 2};
  const GLsizei planeWidths[3] = {width_, width_ / 2, width_ / 2};
  // Start all three transfers before returning to the caller. A later collection maps the other
  // set of buffers, so the device can finish this frame while subsequent scene work is submitted.
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  for (int index = 0; index < 3; ++index) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, planeFbos_[slot][index]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffers_[slot][index]);
    glReadPixels(0, 0, planeWidths[index], planeHeights[index], GL_RED, GL_UNSIGNED_BYTE,
        nullptr);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  ++pendingCount_;
  writeSlot_ = 1 - writeSlot_;
  return true;
}

bool Yuv420Pass::takePending(std::vector<std::uint8_t>& out) noexcept {
  if (!ready_ || pendingCount_ == 0) return false;
  const GLsizei planeHeights[3] = {height_, height_ / 2, height_ / 2};
  const GLsizei planeWidths[3] = {width_, width_ / 2, width_ / 2};
  out.resize(frameBytes());
  std::size_t offset = 0;
  for (int index = 0; index < 3; ++index) {
    const auto bytes = static_cast<std::size_t>(planeWidths[index])
        * static_cast<std::size_t>(planeHeights[index]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffers_[readSlot_][index]);
    const auto* mapped = static_cast<const std::uint8_t*>(glMapBufferRange(
        GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT));
    if (mapped == nullptr) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      return false;
    }
    std::memcpy(out.data() + offset, mapped, bytes);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    offset += bytes;
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  readSlot_ = (readSlot_ + 1) % 2;
  --pendingCount_;
  return true;
}

bool Yuv420Pass::convert(gl::Texture& source, std::vector<std::uint8_t>& out) noexcept {
  if (pendingCount_ != 0 || !request(source)) return false;
  return takePending(out);
}

bool Yuv420Pass::convertAsync(gl::Texture& source, std::vector<std::uint8_t>& out,
    bool& frameReady) noexcept {
  frameReady = false;
  const bool collect = hasPending();
  if (!request(source)) return false;
  if (!collect) return true;
  frameReady = takePending(out);
  return frameReady;
}

} // namespace rechigros::render::fx