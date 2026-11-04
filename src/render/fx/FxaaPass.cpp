#include "rechigros/render/fx/FxaaPass.hpp"

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <memory>

namespace rechigros::render::fx {
namespace {
// The reference FXAA shader, written for the shared full-screen vertex stage.
constexpr const char* kFxaaFragmentSource = R"(
uniform sampler2D uTexture;
uniform vec2 uInvResolution;
in vec2 uv;
out vec4 fragmentColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
  vec3 rgbNW = texture(uTexture, uv + vec2(-1.0, -1.0) * uInvResolution).rgb;
  vec3 rgbNE = texture(uTexture, uv + vec2( 1.0, -1.0) * uInvResolution).rgb;
  vec3 rgbSW = texture(uTexture, uv + vec2(-1.0,  1.0) * uInvResolution).rgb;
  vec3 rgbSE = texture(uTexture, uv + vec2( 1.0,  1.0) * uInvResolution).rgb;
  vec4 texM = texture(uTexture, uv);
  vec3 rgbM = texM.rgb;
  float lumaNW = luma(rgbNW);
  float lumaNE = luma(rgbNE);
  float lumaSW = luma(rgbSW);
  float lumaSE = luma(rgbSE);
  float lumaM = luma(rgbM);
  float rangeMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
  float rangeMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
  float range = rangeMax - rangeMin;
  if (range < max(0.0312, rangeMax * 0.125)) {
    fragmentColor = texM;
    return;
  }
  vec2 dir;
  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
  dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
  float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = min(vec2(8.0), max(vec2(-8.0), dir * rcpDirMin)) * uInvResolution;
  vec4 rgbA = 0.5 * (texture(uTexture, uv + dir * (1.0 / 3.0 - 0.5)) +
                     texture(uTexture, uv + dir * (2.0 / 3.0 - 0.5)));
  vec4 rgbB = rgbA * 0.5 + 0.25 * (texture(uTexture, uv + dir * -0.5) +
                                   texture(uTexture, uv + dir *  0.5));
  float lumaB = luma(rgbB.rgb);
  fragmentColor = (lumaB < rangeMin || lumaB > rangeMax) ? rgbA : rgbB;
}
)";
} // namespace

FxaaPass& FxaaPass::instance() {
  static FxaaPass pass;
  return pass;
}

FxaaPass::FxaaPass() : pass_(new FullscreenPass()) {}

FxaaPass::~FxaaPass() {
  delete pass_;
  pass_ = nullptr;
}

bool FxaaPass::ensure() noexcept {
  if (ready_) return true;
  ready_ = pass_->build(kFxaaFragmentSource);
  return ready_;
}

void FxaaPass::release() noexcept {
  pass_->release();
  ready_ = false;
}

void FxaaPass::draw(gl::Texture& texture, int sourceWidth, int sourceHeight) {
  if (!ensure() || !texture.valid() || !pass_->valid()) return;
  pass_->program().use();
  pass_->program().set2f("uInvResolution",
      1.0F / static_cast<float>(sourceWidth > 0 ? sourceWidth : 1),
      1.0F / static_cast<float>(sourceHeight > 0 ? sourceHeight : 1));
  pass_->draw(texture, "uTexture", 0);
  glUseProgram(0);
}

} // namespace rechigros::render::fx
