#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"

#include <filesystem>

namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The pulsing grid square one level effect draws behind the play field. The object is a
// square centred on the screen, not a full-frame quad: it is authored as a 2000-unit
// square on a canvas that scales uniformly with the screen.
class DsGridPass final {
public:
  DsGridPass() = default;
  ~DsGridPass() = default;
  DsGridPass(const DsGridPass&) = delete;
  DsGridPass& operator=(const DsGridPass&) = delete;

  bool prepare(const std::filesystem::path& assetRoot);
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Side length in pixels of the authored square. `width` and `height` are the screen.
  [[nodiscard]] static float squareSidePixels(int width, int height) noexcept;

  // Draws the square. `opacity` fades the whole object; `frozen` stops the pulse.
  void draw(gl::Texture& gridTexture, float chartTimeSec, float opacity, bool frozen,
      int screenWidth, int screenHeight);

private:
  FullscreenPass pass_;
  bool ready_{};
};

} // namespace rechigros::render::fx
