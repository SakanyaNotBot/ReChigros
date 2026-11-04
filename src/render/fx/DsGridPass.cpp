#include "rechigros/render/fx/DsGridPass.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace rechigros::render::fx {
namespace {

// The authored square, in canvas units.
constexpr float kSquareUnits = 2000.0F;
// Canvas reference resolution and the width/height match weight of its scaler.
constexpr double kReferenceWidth = 1920.0;
constexpr double kReferenceHeight = 1080.0;
constexpr double kMatchWidthOrHeight = 0.8;
// Phase offset of the engine clock the reference capture was timed against.
constexpr float kTimeOffsetSec = 0.285F;
// Texture tiling that produces the grid cells.
constexpr float kTileU = 10.0F;
constexpr float kTileV = 10.0F;

} // namespace

float DsGridPass::squareSidePixels(int width, int height) noexcept {
  const double screenWidth = std::max(1, width);
  const double screenHeight = std::max(1, height);
  // A scaler that matches width and height logarithmically gives one uniform factor, so
  // the authored square stays square on every aspect ratio.
  const double logWidth = std::log(screenWidth / kReferenceWidth) / std::log(2.0);
  const double logHeight = std::log(screenHeight / kReferenceHeight) / std::log(2.0);
  const double logScale = logWidth + (logHeight - logWidth) * kMatchWidthOrHeight;
  const auto scale = static_cast<float>(std::pow(2.0, logScale));
  return kSquareUnits * scale;
}

bool DsGridPass::prepare(const std::filesystem::path& assetRoot) {
  if (ready_) return true;
  const auto bytes = platform::AssetManager::readBinary(assetRoot,
      "prpr_shaders/official_ds_grid.glsl");
  if (bytes.empty()) return false;
  std::string source(bytes.begin(), bytes.end());
  ready_ = pass_.build(preparePresetSource(source).c_str());
  if (ready_) {
    pass_.program().use();
    pass_.program().set2f("UVScale", 1.0F, 1.0F);
  }
  return ready_;
}

void DsGridPass::release() noexcept {
  pass_.release();
  ready_ = false;
}

void DsGridPass::draw(gl::Texture& gridTexture, float chartTimeSec, float opacity, bool frozen,
    int screenWidth, int screenHeight) {
  if (!ready_ || !gridTexture.valid()) return;
  if (!(opacity > 0.001F)) return;

  // The square only covers part of the frame, so the pass runs through a viewport matching
  // the square rather than the whole framebuffer.
  int viewport[4]{};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const bool blendWasEnabled = glIsEnabled(GL_BLEND) != GL_FALSE;

  const float side = squareSidePixels(screenWidth, screenHeight);
  const int sidePixels = std::max(1, static_cast<int>(std::lround(side)));
  const int originX = static_cast<int>(std::lround((static_cast<float>(screenWidth) - side) * 0.5F));
  const int originY = static_cast<int>(std::lround((static_cast<float>(screenHeight) - side) * 0.5F));
  glViewport(originX, originY, sidePixels, sidePixels);

  glEnable(GL_BLEND);
  // The shipped pass is additive alpha-weighted: a glow, not a normal overlay.
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  auto& shader = pass_.program();
  shader.use();
  shader.set2f("UVScale", 1.0F, 1.0F);
  shader.set1f("time", chartTimeSec + kTimeOffsetSec);
  shader.set2f("texST", kTileU, kTileV);
  shader.set3f("gridColor", 0.8066038F, 0.990657F, 1.0F);
  shader.set1f("gridBrightness", 0.57F);
  shader.set3f("bloomColor", 0.514151F, 0.5749958F, 1.0F);
  shader.set1f("bloomBrightness", 0.59F);
  // The pulse stops at the level's freeze moment.
  shader.set1f("scaleSpeed", frozen ? 0.0F : 1.0F);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, gridTexture.id());
  shader.set1i("gridTex", 0);
  // The whole object fades through a colour uniform; only the alpha carries the track.
  shader.set4f("vertexColor", 1.0F, 1.0F, 1.0F, opacity);
  (void)FullscreenQuad::instance().ensure();
  FullscreenQuad::instance().draw();

  // Restore the pass state the rest of the frame expects.
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  if (!blendWasEnabled) glDisable(GL_BLEND);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

} // namespace rechigros::render::fx
