#include "rechigros/render/scene/PauseHudBlur.hpp"

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"

#include <algorithm>
#include <memory>

namespace rechigros::render::scene {
namespace {
// Separable five-tap blur; the offsets shrink with the target resolution so the kernel
// keeps the same visual width at every viewport size.
constexpr const char* kBlurSource = R"(
uniform sampler2D uTexture;
uniform vec2 uTexelOffset;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec2 step = uTexelOffset;
  vec4 sum = texture(uTexture, uv) * 0.2270270270;
  sum += texture(uTexture, uv + vec2(step.x, 0.0)) * 0.1945945946;
  sum += texture(uTexture, uv - vec2(step.x, 0.0)) * 0.1945945946;
  sum += texture(uTexture, uv + vec2(0.0, step.y)) * 0.1216216216;
  sum += texture(uTexture, uv - vec2(0.0, step.y)) * 0.1216216216;
  sum += texture(uTexture, uv + step) * 0.0540540541;
  sum += texture(uTexture, uv - step) * 0.0540540541;
  fragmentColor = sum;
}
)";

// Premultiplied composite of the blurred HUD with a brightness scale.
constexpr const char* kCompositeSource = R"(
uniform sampler2D uTexture;
uniform float uBrightness;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  fragmentColor = texture(uTexture, uv) * uBrightness;
}
)";
} // namespace

class PauseHudBlur::Impl {
public:
  fx::FullscreenPass blur;
  fx::FullscreenPass composite;
  gl::Framebuffer hud;
  gl::Framebuffer ping;
  gl::Framebuffer pong;
  int width{};
  int height{};
};

PauseHudBlur::PauseHudBlur() : impl_(new Impl()) {}

PauseHudBlur::~PauseHudBlur() {
  delete impl_;
  impl_ = nullptr;
}

bool PauseHudBlur::prepare() {
  if (impl_->blur.valid() && impl_->composite.valid()) return true;
  const bool blurReady = impl_->blur.build(kBlurSource);
  const bool compositeReady = impl_->composite.build(kCompositeSource);
  return blurReady && compositeReady;
}

void PauseHudBlur::release() noexcept {
  impl_->blur.release();
  impl_->composite.release();
  impl_->hud.reset();
  impl_->ping.reset();
  impl_->pong.reset();
  impl_->width = 0;
  impl_->height = 0;
}

bool PauseHudBlur::ready() const noexcept {
  return impl_->blur.valid() && impl_->composite.valid();
}

int PauseHudBlur::width() const noexcept {
  return impl_->width;
}

int PauseHudBlur::height() const noexcept {
  return impl_->height;
}

const gl::Texture* PauseHudBlur::texture() const noexcept {
  return impl_->ping.valid() ? &impl_->ping.color() : nullptr;
}

bool PauseHudBlur::begin(int viewWidth, int viewHeight) {
  if (viewWidth <= 0 || viewHeight <= 0) return false;
  if (!ready() && !prepare()) return false;
  const int divisor = std::max(1, PAUSE_HUD_SOFT_DIVISOR);
  impl_->width = std::max(4, viewWidth / divisor);
  impl_->height = std::max(4, viewHeight / divisor);
  if (!impl_->hud.resize(impl_->width, impl_->height) ||
      !impl_->ping.resize(impl_->width, impl_->height) ||
      !impl_->pong.resize(impl_->width, impl_->height)) {
    return false;
  }
  if (!fx::FullscreenQuad::instance().ensure()) return false;
  impl_->hud.bind();
  glViewport(0, 0, impl_->width, impl_->height);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  return true;
}

void PauseHudBlur::composite(float brightness) {
  if (impl_->width <= 0 || impl_->height <= 0) return;
  int previousFbo = 0;
  int previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const bool blendWasEnabled = glIsEnabled(GL_BLEND);
  const bool scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);

  const float offsetX = PAUSE_HUD_SOFT_OFFSET_PX / static_cast<float>(impl_->width);
  const float offsetY = PAUSE_HUD_SOFT_OFFSET_PX / static_cast<float>(impl_->height);
  glDisable(GL_BLEND);
  glActiveTexture(GL_TEXTURE0);
  gl::Texture* source = &impl_->hud.color();
  for (int iteration = 0; iteration < PAUSE_HUD_SOFT_ITERATIONS; ++iteration) {
    impl_->ping.bind();
    glViewport(0, 0, impl_->width, impl_->height);
    impl_->blur.program().use();
    impl_->blur.program().set1i("uTexture", 0);
    impl_->blur.program().set2f("uTexelOffset", offsetX, 0.0F);
    glBindTexture(GL_TEXTURE_2D, source->id());
    fx::FullscreenQuad::instance().draw();
    impl_->pong.bind();
    glViewport(0, 0, impl_->width, impl_->height);
    impl_->blur.program().use();
    impl_->blur.program().set1i("uTexture", 0);
    impl_->blur.program().set2f("uTexelOffset", 0.0F, offsetY);
    glBindTexture(GL_TEXTURE_2D, impl_->ping.color().id());
    fx::FullscreenQuad::instance().draw();
    source = &impl_->pong.color();
  }
  // The last vertical pass wrote into the pong target; one final horizontal pass leaves the
  // composited result in the ping target exposed by texture().
  impl_->ping.bind();
  glViewport(0, 0, impl_->width, impl_->height);
  impl_->blur.program().use();
  impl_->blur.program().set1i("uTexture", 0);
  impl_->blur.program().set2f("uTexelOffset", offsetX, 0.0F);
  glBindTexture(GL_TEXTURE_2D, source->id());
  fx::FullscreenQuad::instance().draw();

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
      previousViewport[3]);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  impl_->composite.program().use();
  impl_->composite.program().set1i("uTexture", 0);
  impl_->composite.program().set1f("uBrightness", brightness);
  glBindTexture(GL_TEXTURE_2D, impl_->ping.color().id());
  fx::FullscreenQuad::instance().draw();

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  if (!blendWasEnabled) glDisable(GL_BLEND);
  if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
}

} // namespace rechigros::render::scene
