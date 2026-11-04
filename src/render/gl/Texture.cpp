#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace rechigros::render::gl {

bool contextAvailable() noexcept {
#ifdef RECHIGROS_GLES
  // The ES entry points are linked directly, so they are always callable.
  return true;
#else
  return glad_glGenTextures != nullptr && glad_glBindTexture != nullptr;
#endif
}

namespace {
// Binds `id` to texture unit 0. Every upload and parameter change goes through here.
// A cached "currently bound" value cannot be used: the effect passes bind textures
// directly, so a cached id would silently disagree with the real binding and uploads
// would land in an unrelated texture.
void bindUnit0(unsigned int id) noexcept {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, id);
}
} // namespace

Texture::~Texture() { reset(); }

Texture::Texture(Texture&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_), linear_(other.linear_) {
  other.id_ = 0;
  other.width_ = 0;
  other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
  if (this != &other) {
    reset();
    id_ = other.id_;
    width_ = other.width_;
    height_ = other.height_;
    linear_ = other.linear_;
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
  }
  return *this;
}

void Texture::reset() noexcept {
  // Without a context there is nothing to delete; clearing the handle is enough.
  if (id_ != 0 && !contextAvailable()) {
    id_ = 0;
    width_ = 0;
    height_ = 0;
    return;
  }
  if (id_ != 0) glDeleteTextures(1, &id_);
  id_ = 0;
  width_ = 0;
  height_ = 0;
}

float Texture::aspect() const noexcept {
  if (width_ <= 0 || height_ <= 0) return 1.0F;
  return static_cast<float>(width_) / static_cast<float>(height_);
}

void Texture::setFilter(bool linear) noexcept {
  if (!contextAvailable()) return;
  linear_ = linear;
  if (id_ == 0) return;
  bindUnit0(id_);
  const GLint mode = linear ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode);
}

void Texture::setWrapRepeat(bool repeatS, bool repeatT) noexcept {
  if (!contextAvailable()) return;
  if (id_ == 0) return;
  bindUnit0(id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatS ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

void Texture::setWrapMirrored(bool mirrorS, bool mirrorT) noexcept {
  if (!contextAvailable()) return;
  if (id_ == 0) return;
  bindUnit0(id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
      mirrorS ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
      mirrorT ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
}

bool Texture::uploadRgba(int width, int height, const std::uint8_t* pixels) noexcept {
  if (width <= 0 || height <= 0) return false;
  if (!contextAvailable()) return false;
  if (id_ == 0) glGenTextures(1, &id_);
  if (id_ == 0) return false;
  width_ = width;
  height_ = height;
  bindUnit0(id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return true;
}

bool Texture::uploadDualChannel(int width, int height, const std::uint8_t* pixels) noexcept {
  if (width <= 0 || height <= 0 || pixels == nullptr || !contextAvailable()) return false;
  if (id_ == 0) glGenTextures(1, &id_);
  if (id_ == 0) return false;
  width_ = width;
  height_ = height;
  bindUnit0(id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return true;
}

bool Texture::allocateStorage(int width, int height, bool dualChannel) noexcept {
  if (width <= 0 || height <= 0 || !contextAvailable()) return false;
  if (id_ == 0) glGenTextures(1, &id_);
  if (id_ == 0) return false;
  width_ = width;
  height_ = height;
  bindUnit0(id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  if (dualChannel) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, dualChannel ? 2 : 4);
  glTexImage2D(GL_TEXTURE_2D, 0, dualChannel ? GL_RG8 : GL_RGBA, width, height, 0,
      dualChannel ? GL_RG : GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return true;
}

void Texture::updateRows(int firstRow, int rowCount, const std::uint8_t* pixels,
    bool dualChannel) noexcept {
  if (id_ == 0 || pixels == nullptr || firstRow < 0 || rowCount <= 0
      || firstRow + rowCount > height_ || !contextAvailable()) {
    return;
  }
  bindUnit0(id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, dualChannel ? 2 : 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, firstRow, width_, rowCount,
      dualChannel ? GL_RG : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

bool Texture::allocateEmpty(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return false;
  if (!contextAvailable()) return false;
  if (id_ == 0) glGenTextures(1, &id_);
  if (id_ == 0) return false;
  width_ = width;
  height_ = height;
  bindUnit0(id_);
  // Render targets are fully sampled by later passes, so their initial content must be
  // defined. Passing a null data pointer leaves the image undefined: drivers differ, and
  // a target sampled before its first write shows up as random bright pixels.
  const auto rowBytes = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> zeroed(rowBytes * static_cast<std::size_t>(height), 0U);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_ ? GL_LINEAR : GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
      zeroed.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  return true;
}

void Texture::updateRgba(int width, int height, const std::uint8_t* pixels) noexcept {
  if (!contextAvailable()) return;
  if (id_ == 0 || width != width_ || height != height_) {
    uploadRgba(width, height, pixels);
    return;
  }
  bindUnit0(id_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

Texture& whiteTexture() noexcept {
  static Texture white;
  if (!white.valid()) {
    const std::uint8_t pixel[4] = {255, 255, 255, 255};
    white.uploadRgba(1, 1, pixel);
    white.setFilter(false);
  }
  return white;
}

} // namespace rechigros::render::gl
