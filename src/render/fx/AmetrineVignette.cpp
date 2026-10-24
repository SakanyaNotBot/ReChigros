#include "rechigros/render/fx/AmetrineVignette.hpp"

#include "rechigros/render/gl/Gl.hpp"

#include <algorithm>

namespace rechigros::render::fx {
namespace {
// A soft ring with no texture input: the fragment only needs the quad's own uv and the
// darkness ramp. The colour is straight (not premultiplied) because this pass runs with
// the classic alpha blend below, not the frame's premultiplied one.
const char* kFragmentSource = R"(
uniform float darkness;
in vec2 uv;
out vec4 fragmentColor;
void main() {
  float d = length(uv - vec2(0.5)) - 0.24;
  float v = clamp(d / 0.55, 0.0, 1.0);
  v = v * v * (3.0 - 2.0 * v);
  fragmentColor = vec4(0.0, 0.0, 0.0, v * darkness);
}
)";
} // namespace

AmetrineVignette::~AmetrineVignette() { release(); }

bool AmetrineVignette::prepare() noexcept {
  if (ready_) return true;
  if (failed_) return false;
  if (!pass_.build(kFragmentSource)) {
    failed_ = true;
    return false;
  }
  ready_ = true;
  return true;
}

void AmetrineVignette::release() noexcept {
  pass_.release();
  ready_ = false;
}

float AmetrineVignette::hudDimFactor(float darkness) noexcept {
  return 1.0F - HUD_DIM * std::clamp(darkness, 0.0F, 1.0F);
}

void AmetrineVignette::draw(float darkness) {
  const float strength = std::clamp(darkness, 0.0F, 1.0F);
  if (!(strength > 0.001F) || !prepare()) return;

  // The ring is a straight-alpha black quad: it darkens the frame by its own coverage.
  // The promoted blend function and the premultiplied default are restored before
  // returning, because the HUD and the exit fade draw with those.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  auto& shader = pass_.program();
  shader.use();
  shader.set1f("darkness", strength);
  FullscreenQuad::instance().draw();
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

} // namespace rechigros::render::fx
