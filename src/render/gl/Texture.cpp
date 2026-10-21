#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <utility>

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
unsigned int boundTexture = 0;



void bindUnit0(unsigned int id) noexcept {
  if (boundTexture == id) return;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, id);
  boundTexture = id;
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
  if (id_ != 0) {
    if (boundTexture == id_) boundTexture = 0;
    glDeleteTextures(1, &id_);
  }
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

bool Texture::allocateEmpty(int width, int height) noexcept {
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
