#include "rechigros/render/gl/Framebuffer.hpp"

#include <algorithm>
#include <utility>

namespace rechigros::render::gl {
namespace {
// Zero on desktop GL and EGL; the UIKit view's own object on iOS.
unsigned int g_screenFramebuffer = 0;
} // namespace

unsigned int screenFramebuffer() noexcept { return g_screenFramebuffer; }

void setScreenFramebuffer(unsigned int framebuffer) noexcept {
  g_screenFramebuffer = framebuffer;
}

void bindScreenFramebuffer() noexcept {
  glBindFramebuffer(GL_FRAMEBUFFER, g_screenFramebuffer);
}

Framebuffer::~Framebuffer() { reset(); }

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : fbo_(other.fbo_), color_(std::move(other.color_)),
      colorRenderbuffer_(other.colorRenderbuffer_), width_(other.width_),
      height_(other.height_), samples_(other.samples_) {
  other.fbo_ = 0;
  other.colorRenderbuffer_ = 0;
  other.width_ = 0;
  other.height_ = 0;
  other.samples_ = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
  if (this != &other) {
    reset();
    fbo_ = other.fbo_;
    color_ = std::move(other.color_);
    colorRenderbuffer_ = other.colorRenderbuffer_;
    width_ = other.width_;
    height_ = other.height_;
    samples_ = other.samples_;
    other.fbo_ = 0;
    other.colorRenderbuffer_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.samples_ = 0;
  }
  return *this;
}

void Framebuffer::reset() noexcept {
  if (fbo_ != 0) glDeleteFramebuffers(1, &fbo_);
  if (colorRenderbuffer_ != 0) glDeleteRenderbuffers(1, &colorRenderbuffer_);
  fbo_ = 0;
  colorRenderbuffer_ = 0;
  color_.reset();
  width_ = 0;
  height_ = 0;
  samples_ = 0;
}

bool Framebuffer::resize(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return false;
  if (samples_ == 0 && valid() && width == width_ && height == height_) return true;
  // A target switches attachment kind; the old renderbuffer and framebuffer are cheap
  // to recreate and keeping a stale attachment would make the framebuffer incomplete.
  if (samples_ != 0) reset();
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

bool Framebuffer::resizeMultisample(int width, int height, int samples) noexcept {
  if (width <= 0 || height <= 0 || samples < 2) return false;
  if (samples_ > 0 && valid() && width == width_ && height == height_) return true;
  if (samples_ == 0) color_.reset();
  if (fbo_ == 0) glGenFramebuffers(1, &fbo_);
  if (fbo_ == 0) return false;
  if (colorRenderbuffer_ == 0) glGenRenderbuffers(1, &colorRenderbuffer_);
  if (colorRenderbuffer_ == 0) {
    reset();
    return false;
  }

  GLint maxSamples = 1;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  const int effectiveSamples = std::min(samples, std::max(1, static_cast<int>(maxSamples)));
  if (effectiveSamples < 2) {
    reset();
    return false;
  }

  GLint previousFramebuffer = 0;
  GLint previousRenderbuffer = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, colorRenderbuffer_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, effectiveSamples, GL_RGBA8, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
      colorRenderbuffer_);
  const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
  glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    reset();
    return false;
  }
  width_ = width;
  height_ = height;
  samples_ = effectiveSamples;
  return true;
}

bool Framebuffer::resolveTo(Framebuffer& target) const noexcept {
  if (!valid() || samples_ <= 0 || !target.valid() || target.samples_ != 0
      || width_ != target.width_ || height_ != target.height_) {
    return false;
  }
  GLint previousRead = 0;
  GLint previousDraw = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
  const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  if (scissor) glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo_);
  glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
      GL_COLOR_BUFFER_BIT, GL_NEAREST);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
  return true;
}

void Framebuffer::bind() const noexcept {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, width_, height_);
}

void Framebuffer::bindDefault(int viewportWidth, int viewportHeight) noexcept {
  bindScreenFramebuffer();
  glViewport(0, 0, viewportWidth, viewportHeight);
}

} // namespace rechigros::render::gl
