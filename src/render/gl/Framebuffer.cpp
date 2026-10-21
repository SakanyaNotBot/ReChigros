#include "rechigros/render/gl/Framebuffer.hpp"

#include <utility>

namespace rechigros::render::gl {

Framebuffer::~Framebuffer() { reset(); }

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo_(other.fbo_), color_(std::move(other.color_)), width_(other.width_),
      height_(other.height_) {
  other.fbo_ = 0;
  other.width_ = 0;
  other.height_ = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
  if (this != &other) {
    reset();
    fbo_ = other.fbo_;
    color_ = std::move(other.color_);
    width_ = other.width_;
    height_ = other.height_;
    other.fbo_ = 0;
    other.width_ = 0;
    other.height_ = 0;
  }
  return *this;
}

void Framebuffer::reset() noexcept {
  if (fbo_ != 0) glDeleteFramebuffers(1, &fbo_);
  fbo_ = 0;
  color_.reset();
  width_ = 0;
  height_ = 0;
}

bool Framebuffer::resize(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return false;
  if (valid() && width == width_ && height == height_) return true;
  if (fbo_ == 0) glGenFramebuffers(1, &fbo_);
  if (fbo_ == 0) return false;
  if (!color_.allocateEmpty(width, height)) return false;

  unsigned int previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&previous));
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_.id(), 0);
  const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    reset();
    return false;
  }
  width_ = width;
  height_ = height;
  return true;
}

void Framebuffer::bind() const noexcept {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, width_, height_);
}

void Framebuffer::bindDefault(int viewportWidth, int viewportHeight) noexcept {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, viewportWidth, viewportHeight);
}

} // namespace rechigros::render::gl
