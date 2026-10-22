#include "rechigros/render/fx/PauseBackdropPass.hpp"

#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"

#include <algorithm>

namespace rechigros::render::fx {
namespace {

// The reference scalable-blur kernel: the centre tap plus eight neighbours at the full
// radius, normalised to 1.
constexpr const char* kBlurSource = R"(
uniform sampler2D uTexture;
uniform vec2 uTexelOffset;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec2 step = uTexelOffset;
  vec4 sum = texture(uTexture, uv) * 0.5;
  sum += texture(uTexture, uv + vec2( step.x, 0.0)) * 0.0625;
  sum += texture(uTexture, uv + vec2(-step.x, 0.0)) * 0.0625;
  sum += texture(uTexture, uv + vec2(0.0,  step.y)) * 0.0625;
  sum += texture(uTexture, uv + vec2(0.0, -step.y)) * 0.0625;
  sum += texture(uTexture, uv + step) * 0.0625;
  sum += texture(uTexture, uv + vec2(-step.x,  step.y)) * 0.0625;
  sum += texture(uTexture, uv + vec2( step.x, -step.y)) * 0.0625;
  sum += texture(uTexture, uv - step) * 0.0625;
  fragmentColor = sum;
}
)";

// The pause grade: vibrancy pulls colour towards its luma, flatten pulls towards neutral
// grey, then brightness scales and the whole quad is composited at `uAlpha`.
constexpr const char* kGradeSource = R"(
uniform sampler2D uTexture;
uniform float uVibrancy;
uniform float uBrightness;
uniform float uFlatten;
uniform float uAlpha;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  vec3 rgb = texture(uTexture, uv).rgb;
  float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
  rgb = mix(vec3(luma), rgb, uVibrancy);
  rgb = mix(rgb, vec3(0.5), uFlatten);
  rgb = clamp(rgb * uBrightness, 0.0, 1.0);
  fragmentColor = vec4(rgb * uAlpha, uAlpha);
}
)";

// Reference constants for the pause blur radius.
constexpr float kBlurRadius = 4.0F;
constexpr float kBlurReferenceResolution = 1080.0F;
constexpr float kBlurScaleMin = 0.5F;
constexpr float kBlurScaleMax = 2.0F;

} // namespace

bool PauseBackdropPass::prepare(const std::filesystem::path& assetRoot) {
  if (ready_) return true;
  assetRoot_ = assetRoot;
  const bool blurOk = blur_.build(kBlurSource);
  const bool gradeOk = grade_.build(kGradeSource);
  ready_ = blurOk && gradeOk;
  return ready_;
}

void PauseBackdropPass::release() noexcept {
  blur_.release();
  grade_.release();
  for (auto& target : pyramid_) target.reset();
  output_.reset();
  viewWidth_ = 0;
  viewHeight_ = 0;
  ready_ = false;
}

void PauseBackdropPass::ensureTargets(int viewWidth, int viewHeight) {
  if (viewWidth == viewWidth_ && viewHeight == viewHeight_ && output_.valid()) return;
  viewWidth_ = viewWidth;
  viewHeight_ = viewHeight;
  for (int depth = 1; depth <= kMaxDepth; ++depth) {
    const int divisor = 1 << depth;
    pyramid_[depth].resize(std::max(1, viewWidth / divisor), std::max(1, viewHeight / divisor));
  }
  output_.resize(viewWidth, viewHeight);
}

void PauseBackdropPass::blurPass(const gl::Texture& source, gl::Framebuffer& destination,
    float radiusPx) {
  // The offset is authored in full-view pixels and stays constant over the pyramid, exactly
  // like the reference pass.
  const float baseWidth = static_cast<float>(std::max(1, viewWidth_));
  const float baseHeight = static_cast<float>(std::max(1, viewHeight_));
  const float texelX = radiusPx / baseWidth;
  const float texelY = radiusPx / baseHeight;
  destination.bind();
  glDisable(GL_BLEND);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  blur_.program().use();
  blur_.program().set1i("uTexture", 0);
  blur_.program().set2f("uTexelOffset", texelX, texelY);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, source.id());
  FullscreenQuad::instance().ensure();
  FullscreenQuad::instance().draw();
}

void PauseBackdropPass::draw(gl::Texture& source, unsigned int outputFbo, float alpha,
    float vibrancy, float brightness, float flatten) {
  if (!(alpha > 0.001F) || !source.valid() || !ready_) return;
  const int width = source.width();
  const int height = source.height();
  if (width <= 0 || height <= 0) return;
  ensureTargets(width, height);
  if (!output_.valid()) return;

  float scale = static_cast<float>(std::max(width, height)) / kBlurReferenceResolution;
  scale = std::clamp(scale, kBlurScaleMin, kBlurScaleMax);
  const float radiusPx = kBlurRadius * scale;

  // Down the pyramid: the full-view scene becomes progressively smaller blur targets.
  gl::Texture* current = &source;
  for (int depth = 1; depth <= kMaxDepth; ++depth) {
    blurPass(*current, pyramid_[depth], radiusPx);
    current = &pyramid_[depth].color();
  }
  // Back up: each pass writes into the next-larger target, which upsamples with bilinear
  // filtering while blurring again.
  for (int depth = kMaxDepth - 1; depth >= 1; --depth) {
    blurPass(*current, pyramid_[depth], radiusPx);
    current = &pyramid_[depth].color();
  }
  // Final pass to the full-view output.
  blurPass(*current, output_, radiusPx);

  // Composite the graded backdrop over the presented frame.
  glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
  glViewport(0, 0, width, height);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  grade_.program().use();
  grade_.program().set1i("uTexture", 0);
  grade_.program().set1f("uVibrancy", std::clamp(vibrancy, -2.0F, 2.0F));
  grade_.program().set1f("uBrightness", std::clamp(brightness, 0.0F, 2.0F));
  grade_.program().set1f("uFlatten", std::clamp(flatten, -1.0F, 1.0F));
  grade_.program().set1f("uAlpha", std::clamp(alpha, 0.0F, 1.0F));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, output_.color().id());
  FullscreenQuad::instance().draw();
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

} // namespace rechigros::render::fx
