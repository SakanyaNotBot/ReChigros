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
  // Allocates an empty RGBA image, used for render targets.
  bool allocateEmpty(int width, int height) noexcept;
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
};

// Shared 1x1 opaque white texture, created on first use.
Texture& whiteTexture() noexcept;

} // namespace rechigros::render::gl
