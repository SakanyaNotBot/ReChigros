#include "rechigros/render/fx/KaleidoBackdropPass.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/fx/PrprChain.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <string>

namespace rechigros::render::fx {

bool KaleidoBackdropPass::prepare(const std::filesystem::path& assetRoot) {
  if (ready_) return true;
  const auto bytes = platform::AssetManager::readBinary(assetRoot,
      "prpr_shaders/official_kaleido.glsl");
  if (bytes.empty()) return false;
  std::string source(bytes.begin(), bytes.end());
  ready_ = pass_.build(preparePresetSource(source).c_str());
  if (ready_) {
    pass_.program().use();
    pass_.program().set2f("UVScale", 1.0F, 1.0F);
  }
  return ready_;
}

void KaleidoBackdropPass::release() noexcept {
  pass_.release();
  ready_ = false;
}

void KaleidoBackdropPass::draw(gl::Texture& normalTexture, gl::Texture& colourTexture,
    float chartTimeSec, float mix, float alpha, int viewportX, int viewportY, int viewportWidth,
    int viewportHeight) {
  if (!ready_ || !normalTexture.valid() || !colourTexture.valid()) return;
  if (!(alpha > 0.001F)) return;

  int viewport[4]{};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const bool blendWasEnabled = glIsEnabled(GL_BLEND) != GL_FALSE;

  glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
  glEnable(GL_BLEND);
  // The object composites premultiplied; the fragment already scales by the vertex alpha.
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  auto& shader = pass_.program();
  shader.use();
  shader.set2f("UVScale", 1.0F, 1.0F);
  shader.set1f("time", chartTimeSec);
  shader.set1f("colorMixStrength", mix);
  // The object's own colour is black; only its alpha matters here.
  shader.set4f("vertexColor", 0.0F, 0.0F, 0.0F, alpha);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, normalTexture.id());
  shader.set1i("normalTex", 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, colourTexture.id());
  shader.set1i("transMap", 1);
  glActiveTexture(GL_TEXTURE0);
  (void)FullscreenQuad::instance().ensure();
  FullscreenQuad::instance().draw();

  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  if (!blendWasEnabled) glDisable(GL_BLEND);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
}

} // namespace rechigros::render::fx
