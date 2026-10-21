#include "rechigros/render/fx/PauseBackdropPass.hpp"

#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/scene/PauseBackdrop.hpp"

#include <algorithm>

namespace rechigros::render::fx {
namespace {

// Separable box blur: one axis per pass, so two passes per iteration.
constexpr const char* kBlurSource = R"(
uniform sampler2D uTexture;
uniform vec2 uTexelOffset;
uniform vec4 uUVScale;
in vec2 uv;
void main() {
  vec2 step = uTexelOffset;
  vec4 sum = texture2D(uTexture, uv) * 0.2270270270;
  sum += texture2D(uTexture, uv + vec2(step.x, 0.0)) * 0.1945945946;
  sum += texture2D(uTexture, uv - vec2(step.x, 0.0)) * 0.1945945946;
  sum += texture2D(uTexture, uv + vec2(0.0, step.y)) * 0.1216216216;
  sum += texture2D(uTexture, uv - vec2(0.0, step.y)) * 0.1216216216;
  sum += texture2D(uTexture, uv + step) * 0.0540540541;
  sum += texture2D(uTexture, uv - step) * 0.0540540541;
  gl_FragColor = sum;
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
void main() {
  vec3 rgb = texture2D(uTexture, uv).rgb;
  float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
  rgb = mix(vec3(luma), rgb, uVibrancy);
  rgb = mix(rgb, vec3(0.5), uFlatten);
  rgb = clamp(rgb * uBrightness, 0.0, 1.0);
  gl_FragColor = vec4(rgb * uAlpha, uAlpha);
}
)";

} // namespace

bool PauseBackdropPass::prepare(const std::filesystem::path& assetRoot) {
  if (ready_) return true;
  assetRoot_ = assetRoot;
  const bool blurOk = blur_.build(kBlurSource);
  const bool gradeOk = grade_.build(kGradeSource);
  if (blurOk) {
    blur_.program().use();
    blur_.program().set2f("UVScale", 1.0F, 1.0F);
  }
  if (gradeOk) {
    grade_.program().use();
    grade_.program().set2f("UVScale", 1.0F, 1.0F);
  }
  ready_ = blurOk && gradeOk;
  return ready_;
}

void PauseBackdropPass::release() noexcept {
  blur_.release();
  grade_.release();
  ready_ = false;
}

void PauseBackdropPass::draw(gl::Texture& source, gl::Framebuffer& blurScratch,
    gl::Framebuffer& blurTemp, unsigned int outputFbo, float alpha, float vibrancy,
    float brightness, float flatten) {
  if (!(alpha > 0.001F) || !source.valid() || !ready_) return;
  if (!blurScratch.valid() || !blurTemp.valid()) return;

  // Blur the frozen scene. Each iteration is one horizontal and one vertical pass; the
  // offsets shrink with the resolution so the kernel keeps a constant visual width.
  gl::Texture* current = &source;
  glDisable(GL_BLEND);
  // The blur radius is authored against a 1080-tall reference and scales with the target,
  // clamped so extreme resolutions stay in range.
  const float scale = std::clamp(
      static_cast<float>(source.height()) / 1080.0F, 0.5F, 2.0F);
  const float radius = 4.0F * scale;
  const float texelX = radius / static_cast<float>(source.width());
  const float texelY = radius / static_cast<float>(source.height());
  for (int iteration = 0; iteration < scene::PAUSE_SCENE_BLUR_ITERATIONS; ++iteration) {
    blurScratch.bind();
    blur_.program().use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current->id());
    blur_.program().set1i("uTexture", 0);
    blur_.program().set2f("UVScale", 1.0F, 1.0F);
    blur_.program().set2f("uTexelOffset", texelX, 0.0F);
    FullscreenQuad::instance().ensure();
    FullscreenQuad::instance().draw();

    blurTemp.bind();
    blur_.program().use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, blurScratch.color().id());
    blur_.program().set1i("uTexture", 0);
    blur_.program().set2f("UVScale", 1.0F, 1.0F);
    blur_.program().set2f("uTexelOffset", 0.0F, texelY);
    FullscreenQuad::instance().draw();
    current = &blurTemp.color();
  }

  // Composite the graded backdrop over the presented frame. The blur passes left their
  // own target bound, so the output is bound explicitly here.
  glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  grade_.program().use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, current->id());
  grade_.program().set1i("uTexture", 0);
  grade_.program().set2f("UVScale", 1.0F, 1.0F);
  grade_.program().set1f("uVibrancy", std::clamp(vibrancy, -2.0F, 2.0F));
  grade_.program().set1f("uBrightness", std::clamp(brightness, 0.0F, 2.0F));
  grade_.program().set1f("uFlatten", std::clamp(flatten, -1.0F, 1.0F));
  grade_.program().set1f("uAlpha", std::clamp(alpha, 0.0F, 1.0F));
  FullscreenQuad::instance().draw();
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

} // namespace rechigros::render::fx
