#include "rechigros/render/fx/BlockCompositePass.hpp"

#include "rechigros/platform/AssetManager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace rechigros::render::fx {
namespace {

const char* const kPrepassShader = "official_shaders/block_prepass.glsl";
const char* const kCompositeShader = "official_shaders/block_composite.glsl";

// Material values the shipped shader reads from its uniform arrays. The rows follow the
// order the shader declares them in: compose transform and strength, then the active and
// disabled looks, then the spark and shine controls.
constexpr float kMatParams[8][4] = {
    {0.8F, 0.667F, 0.667F, 0.8F},
    {5.69F, 0.2F, 2.39F, 0.411F},
    {1.5F, 0.15F, 6.0F, 37.9F},
    {0.12F, 0.0F, 0.5F, 0.47F},
    {0.41F, 0.03F, 60.0F, 1.0F},
    {0.48F, 1.0F, 0.11F, 0.63F},
    {0.34F, 9.3F, 0.0F, 0.0F},
    {8.0F, 0.0F, 0.0F, 0.0F},
};

constexpr float kMatColors[7][4] = {
    {1.0F, 0.3301886F, 0.3301886F, 1.0F},
    {0.7132075F, 0.235493F, 0.235493F, 1.0F},
    {1.0F, 0.1792452F, 0.1792452F, 1.0F},
    {1.0F, 0.2849057F, 0.2849057F, 1.0F},
    {0.497F, 0.138F, 0.138F, 0.4F},
    {1.0F, 0.0F, 0.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 1.0F},
};

constexpr float kMatExtra[5][4] = {
    {2.13F, 1.02F, 2.59F, 0.1F},
    {0.8F, 0.3F, 1.5F, 0.15F},
    {3.0F, 1.2F, 0.5F, 0.2F},
    {0.3F, 2.29F, 0.55F, 0.3F},
    {2.9F, 0.08F, 1.5F, 1.46F},
};

[[nodiscard]] std::string readShader(const std::filesystem::path& assetRoot, const char* relative) {
  const auto bytes = platform::AssetManager::readBinary(assetRoot, relative);
  return std::string(bytes.begin(), bytes.end());
}

void bindTexture(int unit, const char* sampler, const gl::Texture* texture,
    const gl::ShaderProgram& program) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texture != nullptr ? texture->id() : 0);
  program.set1i(sampler, unit);
}

} // namespace

float BlockCompositePass::glowRingWeight(int pass) noexcept {
  if (pass < 0 || pass >= GLOW_PASSES) return 0.0F;
  // The chain weights each ring by its remaining reach over the sum of the reach values,
  // so the innermost ring carries the most weight.
  float sum = 0.0F;
  for (int ring = 1; ring <= GLOW_PASSES; ++ring) {
    sum += static_cast<float>(ring);
  }
  if (!(sum > 0.0F)) return 0.0F;
  return static_cast<float>(GLOW_PASSES - pass) / sum;
}

float BlockCompositePass::gate(float value) noexcept {
  if (value >= GATE_HIGH) return -1.0F;
  if (value >= GATE_LOW) return 1.0F;
  return 0.0F;
}

bool BlockCompositePass::prepare(
    const std::filesystem::path& assetRoot, gl::Texture* displace, gl::Texture* spark) noexcept {
  release();
  displaceMap_ = displace;
  sparkMap_ = spark;
  const std::string prepass = readShader(assetRoot, kPrepassShader);
  const std::string composite = readShader(assetRoot, kCompositeShader);
  if (prepass.empty() || composite.empty()) return false;
  // The sources are already written against the legacy ES spelling; the program rewrites
  // them for the active context.
  if (!prepass_.build(prepass.c_str())) return false;
  if (!composite_.build(composite.c_str())) return false;
  ready_ = true;
  return true;
}

void BlockCompositePass::release() noexcept {
  ready_ = false;
  prepass_.release();
  composite_.release();
  for (auto& target : maskFbo_) target.reset();
  compose_.reset();
  for (auto& target : ping_) target.reset();
  effect_.reset();
  width_ = 0;
  height_ = 0;
}

void BlockCompositePass::resize(int width, int height) noexcept {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  for (auto& target : maskFbo_) {
    target.resize(width_, height_);
    target.color().setFilter(false);
  }
  compose_.resize(std::max(1, width_ / COMPOSE_DIVISOR), std::max(1, height_ / COMPOSE_DIVISOR));
  compose_.color().setFilter(false);
  for (auto& target : ping_) {
    target.resize(std::max(1, width_ / EFFECT_DIVISOR), std::max(1, height_ / EFFECT_DIVISOR));
    target.color().setFilter(true);
  }
  effect_.resize(std::max(1, width_ / EFFECT_DIVISOR), std::max(1, height_ / EFFECT_DIVISOR));
  effect_.color().setFilter(true);
}

void BlockCompositePass::clearMasks() noexcept {
  for (auto& target : maskFbo_) {
    if (!target.valid()) continue;
    target.bind();
    glViewport(0, 0, target.width(), target.height());
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

void BlockCompositePass::uploadMaterials() const {
  auto& program = const_cast<gl::ShaderProgram&>(composite_.program());
  program.use();
  glUniform4fv(program.uniform("matParams[0]"), 8, &kMatParams[0][0]);
  glUniform4fv(program.uniform("matColors[0]"), 7, &kMatColors[0][0]);
  glUniform4fv(program.uniform("matExtra[0]"), 5, &kMatExtra[0][0]);
}

void BlockCompositePass::runPrepasses(float chartTimeSec) {
  if (!prepass_.valid() || !compose_.valid() || !effect_.valid()) return;
  auto& program = prepass_.program();
  program.use();
  program.set1f("time", chartTimeSec / TIME_DIVISOR);
  program.set2f("displaceDirection", DISPLACE_DIRECTION, DISPLACE_DIRECTION);
  program.set4f("composeParams", kMatExtra[0][0], kMatExtra[0][1], kMatExtra[0][2],
      kMatExtra[0][3]);
  program.set2f("dilateTexel", 1.0F / static_cast<float>(std::max(1, effect_.width())),
      1.0F / static_cast<float>(std::max(1, effect_.height())));

  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  // Coverage of both zone layers, displaced for the visible ones and plain for the
  // warning silhouette.
  compose_.bind();
  glViewport(0, 0, compose_.width(), compose_.height());
  program.set1i("passMode", 0);
  bindTexture(0, "displaceMap", displaceMap_, program);
  bindTexture(1, "maskA", &maskFbo_[0].color(), program);
  bindTexture(2, "maskB", &maskFbo_[1].color(), program);
  bindTexture(3, "composeRT", &compose_.color(), program);
  FullscreenQuad::instance().draw();

  // One dilation step produces the outline of the covered area.
  effect_.bind();
  glViewport(0, 0, effect_.width(), effect_.height());
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  program.set1i("passMode", 2);
  FullscreenQuad::instance().draw();

  // The glow accumulates weighted rings outside the covered area. The chain ping-pongs
  // between two targets and the last step writes only the accumulated channel into the
  // effect target, so the outline survives.
  const int last = GLOW_PASSES - 1;
  for (int pass = 0; pass < GLOW_PASSES; ++pass) {
    gl::Framebuffer& source = pass == 0 ? compose_ : ping_[(pass % 2) == 1 ? 0 : 1];
    gl::Framebuffer& destination = pass < last ? ping_[pass % 2 == 0 ? 0 : 1] : effect_;
    destination.bind();
    glViewport(0, 0, destination.width(), destination.height());
    program.set1i("passMode", 3);
    program.set1f("passWeight", glowRingWeight(pass));
    program.set1f("glowFirstPass", pass == 0 ? 1.0F : 0.0F);
    bindTexture(4, "pingTex", &source.color(), program);
    bindTexture(3, "composeRT", &compose_.color(), program);
    if (pass == last) glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    FullscreenQuad::instance().draw();
    if (pass == last) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  glEnable(GL_BLEND);
}

void BlockCompositePass::draw(gl::Texture& scene, float chartTimeSec) {
  if (!ready_ || !composite_.valid() || !scene.valid()) return;
  if (!compose_.valid() || !effect_.valid()) return;
  runPrepasses(chartTimeSec);

  auto& program = composite_.program();
  program.use();
  uploadMaterials();
  program.set1f("time", chartTimeSec / TIME_DIVISOR);
  program.set1f("timeY", chartTimeSec);
  program.set2f("screenSize", static_cast<float>(width_), static_cast<float>(height_));
  program.set4f("stageRect", 0.0F, 0.0F, static_cast<float>(width_),
      static_cast<float>(height_));
  program.set2f("displaceDirection", DISPLACE_DIRECTION, DISPLACE_DIRECTION);
  program.set1i("touchCount", 0);
  program.set1f("touchShineValue", 0.0F);

  bindTexture(0, "screenTexture", &scene, program);
  bindTexture(1, "displaceMap", displaceMap_, program);
  bindTexture(2, "sparkMap", sparkMap_, program);
  bindTexture(3, "composeRT", &compose_.color(), program);
  bindTexture(4, "effectRT", &effect_.color(), program);
  bindTexture(5, "maskA", &maskFbo_[0].color(), program);
  bindTexture(6, "maskB", &maskFbo_[1].color(), program);
  FullscreenQuad::instance().draw();
}

} // namespace rechigros::render::fx
