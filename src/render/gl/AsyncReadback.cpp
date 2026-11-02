#include "rechigros/render/gl/AsyncReadback.hpp"

#include "rechigros/render/gl/Gl.hpp"

#include <algorithm>

namespace rechigros::render::gl {

AsyncReadback::~AsyncReadback() { reset(); }

bool AsyncReadback::resize(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return false;
  if (width == width_ && height == height_ && buffers_[0] != 0) return true;
  reset();
  glGenBuffers(2, buffers_);
  if (buffers_[0] == 0 || buffers_[1] == 0) {
    reset();
    return false;
  }
  // The buffers are only ever filled by the driver, so their contents never need to be uploaded;
  // providing no initial data leaves the allocation to the driver.
  const auto bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(width) * height * 4U);
  for (unsigned int& buffer : buffers_) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  const GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    reset();
    return false;
  }
  width_ = width;
  height_ = height;
  writeIndex_ = 0;
  pendingCount_ = 0;
  return true;
}

void AsyncReadback::reset() noexcept {
  if (buffers_[0] != 0) glDeleteBuffers(2, buffers_);
  buffers_[0] = 0;
  buffers_[1] = 0;
  width_ = 0;
  height_ = 0;
  writeIndex_ = 0;
  pendingCount_ = 0;
}

void AsyncReadback::request(int width, int height) noexcept {
  if (!valid() || width != width_ || height != height_) return;
  // The read goes into the buffer that is not the one still being drained.
  writeIndex_ = (writeIndex_ + 1) % 2;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers_[writeIndex_]);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  pendingCount_ = std::min(pendingCount_ + 1, 2);
}

bool AsyncReadback::take(std::vector<std::uint8_t>& out) noexcept {
  // The caller requests a frame and then collects the previous one. A single pending request
  // is therefore the current frame, not a completed earlier frame; the caller must read it
  // directly instead of consuming the other, still unwritten buffer.
  if (!valid() || pendingCount_ < 2) return false;
  // Collections lag requests by one: the buffer holding the completed frame is the one the
  // previous request wrote, which is the other half of the pair.
  const int readIndex = (writeIndex_ + 1) % 2;
  glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers_[readIndex]);
  // Mapping waits for the transfer the driver started earlier. By the time the caller asks, the
  // frame it wants was requested one step ago, so the wait is for a transfer already under way.
  // The range form takes an access bitmask, which is what the mobile flavour of the API offers;
  // the single-value form of the call is desktop only.
  const auto* pixels = static_cast<const std::uint8_t*>(
      glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
          static_cast<GLsizeiptr>(
              static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U),
          GL_MAP_READ_BIT));
  if (pixels == nullptr) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    --pendingCount_;
    return false;
  }
  out.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U);
  const auto rowBytes = static_cast<std::size_t>(width_) * 4U;
  // Readback rows arrive bottom-up; every consumer wants the first row to be the top one.
  for (int row = 0; row < height_; ++row) {
    std::copy_n(pixels + static_cast<std::size_t>(height_ - 1 - row) * rowBytes, rowBytes,
        out.data() + static_cast<std::size_t>(row) * rowBytes);
  }
  glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  --pendingCount_;
  return true;
}

} // namespace rechigros::render::gl