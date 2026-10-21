#include "rechigros/render/fx/BloomPass.hpp"

#include "rechigros/platform/AssetManager.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace rechigros::render::fx {
namespace {

// The bloom stages run at a reduced scale, so the intermediate targets never need
// to be full size.
constexpr int kMinimumTargetSize = 4;
constexpr int kPoolLimit = 24;

int scaledExtent(float rate, int extent) {
  const int scaled = static_cast<int>(rate * static_cast<float>(extent));
  return std::max(1, scaled);
}

int halve(int extent) {
  return std::max(kMinimumTargetSize, (extent + 1) >> 1);
}

} // namespace

void BloomPass::releaseTargets() noexcept {
  pool_.clear();
}

void BloomPass::release() noexcept {
  luminance_.pass.release();
  down_.pass.release();
  up_.pass.release();
  composite_.pass.release();
  luminance_.valid = false;
  down_.valid = false;
  up_.valid = false;
  composite_.valid = false;
  releaseTargets();
  ready_ = false;
}

bool BloomPass::compile(Stage& stage, const char* fileName) {
  const auto bytes = platform::AssetManager::readBinary(assetRoot_,
      "prpr_shaders/" + std::string(fileName));
  if (bytes.empty()) {
    stage.valid = false;
    return false;
  }
  std::string source(bytes.begin(), bytes.end());
  stage.valid = stage.pass.build(source.c_str());
  if (stage.valid) {
    // The fullscreen vertex stage derives uv from UVScale, which must be (1,1) or
    // every fragment samples the centre texel.
    stage.pass.program().use();
    stage.pass.program().set2f("UVScale", 1.0F, 1.0F);
  }
  return stage.valid;
}

bool BloomPass::prepare(const std::filesystem::path& assetRoot) {
  if (ready_) return true;
  assetRoot_ = assetRoot;
  const bool lumOk = compile(luminance_, "official_bloom_lum.glsl");
  const bool ok = lumOk &&
      compile(down_, "official_bloom_down.glsl") && compile(up_, "official_bloom_up.glsl") &&
      compile(composite_, "official_bloom_composite.glsl");
  ready_ = ok;
  return ready_;
}

BloomPass::Target* BloomPass::acquire(int width, int height, const Target* exclude) {
  for (auto& candidate : pool_) {
    if (candidate.get() == exclude) continue;
    if (candidate->width == width && candidate->height == height && candidate->fbo.valid()) {
      return candidate.get();
    }
  }
  if (pool_.size() >= static_cast<std::size_t>(kPoolLimit)) return nullptr;
  auto target = std::make_unique<Target>();
  if (!target->fbo.resize(width, height)) return nullptr;
  target->width = width;
  target->height = height;
  auto* raw = target.get();
  pool_.push_back(std::move(target));
  return raw;
}

void BloomPass::apply(const BloomSettings& settings, const gl::Texture& input,
    gl::Framebuffer& output, double chartTimeSec) {
  if (!input.valid() || !output.valid()) return;

  const auto blitThrough = [&]() {
    output.bind();
    glDisable(GL_BLEND);
    BlitPass::instance().ensure();
    BlitPass::instance().draw(const_cast<gl::Texture&>(input), 1.0F, 1.0F, 1.0F, 1.0F, false);
  };

  if (!ready_) {
    blitThrough();
    return;
  }

  const int outWidth = output.width();
  const int outHeight = output.height();
  // The official gate: no bloom when either the radius or the brightness is off.
  if (!(settings.radius * settings.brightness > 0.0F)) {
    blitThrough();
    return;
  }

  int iterations = std::max(1, settings.iterations);

  // Stage 0: luminance prefilter into a downscaled target.
  const int sampleWidth = scaledExtent(settings.sampleRate, outWidth);
  const int sampleHeight = scaledExtent(settings.sampleRate, outHeight);
  Target* pre = acquire(sampleWidth, sampleHeight, nullptr);
  if (pre == nullptr) {
    blitThrough();
    return;
  }
  pre->fbo.bind();
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  {
    auto& shader = luminance_.pass.program();
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.id());
    shader.set1i("screenTexture", 0);
    shader.set2f("UVScale", 1.0F, 1.0F);
    shader.set1f("bloomThreshold", settings.threshold);
    FullscreenQuad::instance().ensure();
    FullscreenQuad::instance().draw();
  }

  Target* previous = pre;
  int currentWidth = sampleWidth;
  int currentHeight = sampleHeight;

  // Down-sampling. The first step keeps the prefilter size, later steps halve.
  for (int step = 0; step < iterations; ++step) {
    const int targetWidth = (step == 0) ? currentWidth : currentWidth;
    const int targetHeight = (step == 0) ? currentHeight : currentHeight;
    Target* destination = (step == 0)
        ? acquire(targetWidth, targetHeight, previous)
        : acquire(targetWidth, targetHeight, previous);
    if (destination == nullptr) break;
    destination->fbo.bind();
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    auto& shader = down_.pass.program();
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previous->fbo.color().id());
    shader.set1i("screenTexture", 0);
    shader.set2f("UVScale", 1.0F, 1.0F);
    shader.set2f("texelSize", 1.0F / static_cast<float>(previous->width),
        1.0F / static_cast<float>(previous->height));
    shader.set1f("bloomRadius", settings.radius);
    FullscreenQuad::instance().draw();
    previous = destination;
    currentWidth = halve(currentWidth);
    currentHeight = halve(currentHeight);
  }

  // Up-sampling back towards the prefilter size.
  for (int step = 0; step < iterations - 1; ++step) {
    const int targetWidth = previous->width << 1;
    const int targetHeight = previous->height << 1;
    Target* destination = acquire(targetWidth, targetHeight, previous);
    if (destination == nullptr) break;
    destination->fbo.bind();
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    auto& shader = up_.pass.program();
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, previous->fbo.color().id());
    shader.set1i("screenTexture", 0);
    shader.set2f("UVScale", 1.0F, 1.0F);
    shader.set2f("texelSize", 1.0F / static_cast<float>(previous->width),
        1.0F / static_cast<float>(previous->height));
    shader.set1f("bloomRadius", settings.radius);
    FullscreenQuad::instance().draw();
    previous = destination;
  }

  // Composite: the blurred result is screen-blended onto the source.
  output.bind();
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  {
    auto& shader = composite_.pass.program();
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.id());
    shader.set1i("screenTexture", 0);
    shader.set2f("UVScale", 1.0F, 1.0F);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, previous->fbo.color().id());
    shader.set1i("blurTex", 1);
    glActiveTexture(GL_TEXTURE0);
    shader.set1f("bloomIntensity", settings.intensity);
    // The preset colour is white, scaled by the animated brightness.
    shader.set3f("bloomColor", settings.brightness, settings.brightness, settings.brightness);
    FullscreenQuad::instance().draw();
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  // The preset has no time-varying uniform; the parameter is kept for symmetry.
  (void)chartTimeSec;
}

} // namespace rechigros::render::fx
