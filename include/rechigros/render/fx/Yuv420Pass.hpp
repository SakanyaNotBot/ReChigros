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

  [[nodiscard]] std::size_t frameBytes() const noexcept {
    return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3U / 2U;
  }

private:
  // Renders one plane. `plane` selects luma or which colour difference the shader writes.
  void renderPlane(int index, gl::Texture& source, int planeSelector) noexcept;

  gl::ShaderProgram lumaProgram_;
  gl::ShaderProgram chromaProgram_;
  // One single-channel target per plane, each with its own framebuffer.
  unsigned int planeTextures_[3]{};
  unsigned int planeFbos_[3]{};
  int width_{};
  int height_{};
  bool ready_{};
};

} // namespace rechigros::render::fx