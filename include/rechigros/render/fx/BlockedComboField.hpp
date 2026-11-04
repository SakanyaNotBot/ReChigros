#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rechigros::render::math { struct Mat4; }
namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The failed-challenge combo readout. When a level's challenge has failed, the ordinary combo
// number and label are replaced by a field of hollow blocks: the text is rasterised into a
// coarse mask, and every surviving mask cell is drawn at a position displaced by a scrolling
// noise field, so the digits dissolve into a blocky grid that drifts with the music.
//
// The mask covers one string. The field owns no GPU resources beyond the atlas the caller
// passes in, so it stays cheap to keep one per readout.
class BlockedComboField final {
public:
  // Builds the mask for one string at the given pixel size. The mask is rebuilt only when
  // the text or the size changes, so this can be called every frame. The asset root supplies
  // the mask font and the displacement map on first use.
  void prepare(const std::filesystem::path& assetRoot, const std::string& text,
      float pixelSizePx);
  [[nodiscard]] bool valid() const noexcept { return width_ > 0 && height_ > 0; }
  // Mask dimensions in cells, and the number of cells that carry a glyph. A mask with zero
  // covered cells would draw nothing, so the probe checks that count directly.
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] int coveredCells() const noexcept;

  // Draws the pattern centred at (centerX, centerY) in screen pixels. `alpha` scales the
  // whole field; `visualTimeSec` drives the scroll and the per-cell flicker. The quads are
  // accumulated into `batch` and flushed against `projection` and `whiteTexture`.
  void draw(batch::SpriteBatch& batch, const math::Mat4& projection,
      const gl::Texture& whiteTexture, float centerX, float centerY, float alpha,
      double visualTimeSec, float screenWidth, float screenHeight) const;

  // Cell size of the mask in screen pixels. The official block mask renders at screen/8.
  static constexpr int CELL = 8;
  // Margin around the text, in cells, that the displacement and glow may occupy.
  static constexpr int PADDING_CELLS = 3;

private:
  std::vector<std::uint8_t> mask_;
  int width_{};
  int height_{};
  float contentWidth_{};
  float contentHeight_{};
  std::string sourceText_;
  float sourceSize_{};
};

} // namespace rechigros::render::fx
