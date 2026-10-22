#include "rechigros/render/scene/BackgroundBlur.hpp"

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <memory>

namespace rechigros::render::scene {
namespace {
// Downscale + cover crop: the artwork is sampled so that the result covers the requested
// play-field rectangle while preserving the artwork aspect ratio.
constexpr const char* kDownsampleSource = R"(
uniform sampler2D uTexture;
uniform vec2 uScale;
uniform vec2 uOffset;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec2 coord = uOffset + uv * uScale;
  fragmentColor = texture(uTexture, coord);
}
)";

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

constexpr int kBlurIterations = 8;
constexpr float kBlurOffsetPixels = 2.0F;
} // namespace

class BackgroundBlur::Impl {
public:
  fx::FullscreenPass downsample;
  fx::FullscreenPass blur;
  gl::Framebuffer downsampled;
  gl::Framebuffer ping;
  gl::Framebuffer pong;
  const gl::Texture* source{};
  int width{};
  int height{};
  bool built{};
};

BackgroundBlur::BackgroundBlur() : impl_(new Impl()) {}

BackgroundBlur::~BackgroundBlur() {
  delete impl_;
  impl_ = nullptr;
}

bool BackgroundBlur::prepare() {
  if (impl_->downsample.valid() && impl_->blur.valid()) return true;
  const bool downsampleReady = impl_->downsample.build(kDownsampleSource);
  const bool blurReady = impl_->blur.build(kBlurSource);
  return downsampleReady && blurReady;
}

void BackgroundBlur::release() noexcept {
  impl_->downsample.release();
  impl_->blur.release();
  impl_->downsampled.reset();
  impl_->ping.reset();
  impl_->pong.reset();
  impl_->source = nullptr;
  impl_->built = false;
}

bool BackgroundBlur::ready() const noexcept {
  return impl_->downsample.valid() && impl_->blur.valid();
}

void BackgroundBlur::invalidate() noexcept {
  impl_->built = false;
  impl_->source = nullptr;
}

const gl::Texture* BackgroundBlur::texture() const noexcept {
  return impl_->built ? &impl_->downsampled.color() : nullptr;
}

const gl::Texture* BackgroundBlur::ensure(int viewWidth, int viewHeight,
    const gl::Texture* artwork) {
  if (artwork == nullptr || !artwork->valid() || viewWidth <= 0 || viewHeight <= 0) {
    return nullptr;
  }
  if (!ready() && !prepare()) return nullptr;
  if (impl_->built && impl_->source == artwork && impl_->width == viewWidth &&
      impl_->height == viewHeight) {
    return &impl_->downsampled.color();
  }
  impl_->source = artwork;
  impl_->width = viewWidth;
  impl_->height = viewHeight;
  impl_->built = rebuild(viewWidth, viewHeight, *artwork);
  return impl_->built ? &impl_->downsampled.color() : nullptr;
}

bool BackgroundBlur::rebuild(int viewWidth, int viewHeight, const gl::Texture& artwork) {
  // The reference downsamples the whole viewport before blurring.
  const int targetWidth = std::max(128, viewWidth / 8);
  const int targetHeight = std::max(128, viewHeight / 8);
  if (!impl_->downsampled.resize(targetWidth, targetHeight) ||
      !impl_->ping.resize(targetWidth, targetHeight) ||
      !impl_->pong.resize(targetWidth, targetHeight)) {
    return false;
  }
  if (!fx::FullscreenQuad::instance().ensure()) return false;

  int previousFbo = 0;
  int previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const bool blendWasEnabled = glIsEnabled(GL_BLEND);
  const bool scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);

  // Cover crop of the artwork into the downsample target. The fullscreen quad produces
  // render-target textures whose zero-v row is the artwork's top row, so they can be
  // sampled with the ordinary top-left UV convention later on.
  const float sourceAspect = static_cast<float>(artwork.width()) /
      std::max(1.0F, static_cast<float>(artwork.height()));
  const float targetAspect = static_cast<float>(viewWidth) /
      std::max(1.0F, static_cast<float>(viewHeight));
  float scaleX = 1.0F;
  float scaleY = 1.0F;
  float offsetX = 0.0F;
  float offsetY = 0.0F;
  if (sourceAspect > targetAspect) {
    const float visible = targetAspect / sourceAspect;
    scaleX = visible;
    offsetX = (1.0F - visible) * 0.5F;
  } else if (sourceAspect < targetAspect) {
    const float visible = sourceAspect / targetAspect;
    scaleY = visible;
    offsetY = (1.0F - visible) * 0.5F;
  }

  impl_->downsampled.bind();
  glViewport(0, 0, targetWidth, targetHeight);
  impl_->downsample.program().use();
  impl_->downsample.program().set1i("uTexture", 0);
  impl_->downsample.program().set2f("uScale", scaleX, scaleY);
  impl_->downsample.program().set2f("uOffset", offsetX, offsetY);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, artwork.id());
  fx::FullscreenQuad::instance().draw();

  const float offsetXTexel = kBlurOffsetPixels / static_cast<float>(targetWidth);
  const float offsetYTexel = kBlurOffsetPixels / static_cast<float>(targetHeight);
  gl::Texture* current = &impl_->downsampled.color();
  for (int iteration = 0; iteration < kBlurIterations; ++iteration) {
    impl_->ping.bind();
    glViewport(0, 0, targetWidth, targetHeight);
    impl_->blur.program().use();
    impl_->blur.program().set1i("uTexture", 0);
    impl_->blur.program().set2f("uTexelOffset", offsetXTexel, 0.0F);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current->id());
    fx::FullscreenQuad::instance().draw();

    impl_->pong.bind();
    glViewport(0, 0, targetWidth, targetHeight);
    impl_->blur.program().use();
    impl_->blur.program().set1i("uTexture", 0);
    impl_->blur.program().set2f("uTexelOffset", 0.0F, offsetYTexel);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, impl_->ping.color().id());
    fx::FullscreenQuad::instance().draw();
    current = &impl_->pong.color();
  }

  // The last vertical pass wrote into the ping target; run one final horizontal pass so
  // the result lives in the downsampled target that is exposed to callers.
  impl_->downsampled.bind();
  glViewport(0, 0, targetWidth, targetHeight);
  impl_->blur.program().use();
  impl_->blur.program().set1i("uTexture", 0);
  impl_->blur.program().set2f("uTexelOffset", offsetXTexel, 0.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, current->id());
  fx::FullscreenQuad::instance().draw();

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
      previousViewport[3]);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  if (blendWasEnabled) glEnable(GL_BLEND);
  if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
  return impl_->downsampled.valid();
}

} // namespace rechigros::render::scene
