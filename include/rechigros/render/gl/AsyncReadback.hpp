#pragma once

#include <cstdint>
#include <vector>

namespace rechigros::render::gl {

// Asynchronous framebuffer readback through pixel buffer objects.
//
// A plain glReadPixels makes the CPU wait until every pixel of the frame has been resolved, so a
// recording runs at the speed of its slowest stage instead of at the speed it renders. Reading
// into a pixel buffer object only asks the driver to start the transfer: the pixels come back
// from a buffer filled while the next frame was being drawn. Two buffers alternate so one is
// always in flight.
//
// A frame is therefore delivered one call late. The first call has nothing to deliver yet, so it
// reports that with `pending` and the caller keeps the frame it already had.
class AsyncReadback final {
public:
  AsyncReadback() = default;
  ~AsyncReadback();
  AsyncReadback(const AsyncReadback&) = delete;
  AsyncReadback& operator=(const AsyncReadback&) = delete;

  // Allocates the staging buffers. Returns false when the driver has no pixel buffer support,
  // in which case the caller falls back to a direct read.
  bool resize(int width, int height) noexcept;
  void reset() noexcept;

  [[nodiscard]] bool valid() const noexcept { return buffers_[0] != 0 && width_ > 0; }

  // Starts reading the framebuffer bound to GL_READ_FRAMEBUFFER. Does not wait for the pixels.
  void request(int width, int height) noexcept;
  // Collects the previously requested frame into `out` (top-down RGBA). Returns false when no
  // completed frame is available yet, which is the case for the first request.
  bool take(std::vector<std::uint8_t>& out) noexcept;

private:
  unsigned int buffers_[2]{};
  int width_{};
  int height_{};
  // Buffer the next request writes into, and the one holding the finished frame.
  int writeIndex_{};
  int pendingCount_{};
};

} // namespace rechigros::render::gl