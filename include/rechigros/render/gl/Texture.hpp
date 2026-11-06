#pragma once

#include "rechigros/render/gl/Gl.hpp"

#include <cstdint>

namespace rechigros::render::gl {

// Owning handle for a 2D texture. The first texture created on a context becomes
// the shared 1x1 white texel used by untextured geometry.
class Texture final {
public:
  Texture() = default;
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;

  // Uploads 8-bit RGBA pixels. Rows are stored top-down in the source data.
  bool uploadRgba(int width, int height, const std::uint8_t* pixels) noexcept;
  // Uploads premultiplied gray/alpha pairs and replicates the gray channel into RGB.
  bool uploadDualChannel(int width, int height, const std::uint8_t* pixels) noexcept;
  bool allocateStorage(int width, int height, bool dualChannel) noexcept;
  void updateRows(int firstRow, int rowCount, const std::uint8_t* pixels, bool dualChannel) noexcept;
  // Allocates an empty RGBA image, used for render targets.
  bool allocateEmpty(int width, int height) noexcept;
  // Builds the mip chain of an uploaded image and enables trilinear minification.
  // Render targets and sub-rectangle atlases must not call this.
  void generateMipmaps() noexcept;
  // Replaces the whole level-0 image of an existing texture (same dimensions allowed).
  void updateRgba(int width, int height, const std::uint8_t* pixels) noexcept;
  void setFilter(bool linear) noexcept;
  void setWrapRepeat(bool repeatS, bool repeatT) noexcept;
  // Tiles by mirroring at the edge instead of repeating. The displaced zone noise maps
  // are authored for mirrored tiling, so their seams do not line up.
  void setWrapMirrored(bool mirrorS, bool mirrorT) noexcept;

  [[nodiscard]] unsigned int id() const noexcept { return id_; }
  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] bool valid() const noexcept { return id_ != 0; }
  // Width divided by height, or 1 for an empty texture.
  [[nodiscard]] float aspect() const noexcept;

  void reset() noexcept;

private:
  unsigned int id_{};
  int width_{};
  int height_{};
  bool linear_{true};
  bool mipmapped_{};
};

// Shared 1x1 opaque white texture, created on first use.
Texture& whiteTexture() noexcept;

} // namespace rechigros::render::gl
