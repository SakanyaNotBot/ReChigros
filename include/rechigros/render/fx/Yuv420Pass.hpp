#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/ShaderProgram.hpp"

#include <cstdint>
#include <vector>

namespace rechigros::render::fx {

// Converts a rendered frame to the planar colour layout a video encoder takes, on the GPU.
//
// The encoder accepts planar YUV, and converting to it on the processor costs more per frame than
// drawing the frame did. Here the conversion is a fullscreen pass per plane, and each plane lands
// in a single-channel target: one byte per sample is exactly what the layout needs, so the transfer
// afterwards moves three and a half times less data than the four-byte-per-pixel frame it came
// from.
class Yuv420Pass final {
public:
  ~Yuv420Pass();
  Yuv420Pass() = default;
  Yuv420Pass(const Yuv420Pass&) = delete;
  Yuv420Pass& operator=(const Yuv420Pass&) = delete;

  // Prepares the shaders and the plane targets for the given size. Returns false when the frame
  // cannot be converted here, and the caller keeps whatever path it had.
  bool ensure(int width, int height) noexcept;
  void release() noexcept;

  [[nodiscard]] bool valid() const noexcept { return ready_; }

  // Converts `source` into `out` in the encoder's planar layout: every luma row, then both chroma
  // planes at half size.
  bool convert(gl::Texture& source, std::vector<std::uint8_t>& out) noexcept;

  // Starts the current conversion and collects the preceding one when there is one. The first
  // frame returns with frameReady false; takePending collects the final frame after the last call.
  bool convertAsync(gl::Texture& source, std::vector<std::uint8_t>& out,
      bool& frameReady) noexcept;
  bool takePending(std::vector<std::uint8_t>& out) noexcept;
  [[nodiscard]] bool hasPending() const noexcept { return pendingCount_ != 0; }

  [[nodiscard]] std::size_t frameBytes() const noexcept {
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3U / 2U;
  }

private:
  // Renders one plane into one slot. `plane` selects luma or which colour difference the
  // shader writes; `slot` picks the target set so a transfer in flight is never overwritten.
  void renderPlane(int slot, int index, gl::Texture& source, int planeSelector) noexcept;
  bool request(gl::Texture& source) noexcept;

  gl::ShaderProgram lumaProgram_;
  gl::ShaderProgram chromaProgram_;
  // Two sets of single-channel targets, one per plane. The set being written differs from the
  // set being transferred, so rendering the next frame never waits for the previous transfer.
  unsigned int planeTextures_[2][3]{};
  unsigned int planeFbos_[2][3]{};
  // Two sets of staging buffers let one frame complete its transfer while the next is drawn.
  unsigned int packBuffers_[2][3]{};
  int width_{};
  int height_{};
  int readSlot_{};
  int writeSlot_{};
  int pendingCount_{};
  bool ready_{};
};

} // namespace rechigros::render::fx