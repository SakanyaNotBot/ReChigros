#pragma once

#include "rechigros/render/math/Mat4.hpp"

#include <vector>

namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The fading copy of a note head that appears when a note is judged bad.
//
// A bad note leaves a copy behind at the position and rotation it had when it was judged,
// tinted dark red, and fades out over 25 frames at 60 Hz. The native ClickBad prefab is
// authored at local scale 0.18 against the 0.22 note prefab, and the judge does not copy
// the note's scale, so on the 16:9 reference stage the copy is 9/11 of the note size.
// Misses do not produce one: only a bad judgement does.
class BadEffectField final {
public:
  // How long one copy lives, in visual seconds.
  static constexpr double LIFETIME_SEC = 25.0 / 60.0;
  // Tint of the fading copy.
  static constexpr float TINT_RED = 0.4245283F;
  static constexpr float TINT_GREEN = 0.26232645F;
  static constexpr float TINT_BLUE = 0.26232645F;
  // Upper bound on live copies; the oldest are dropped first.
  static constexpr int MAX_EFFECTS = 256;

  // Alpha of a copy after `elapsedSec`, clamped to its lifetime.
  [[nodiscard]] static float alphaAt(double elapsedSec) noexcept;

  void clear() noexcept;
  // Records one copy. `visualTimeSec` is the clock the fade runs on.
  void spawn(double visualTimeSec, float x, float y, float width, float height,
      float rotationDeg, float u0, float v0, float u1, float v1, const gl::Texture* texture);

  // Drops expired copies, then draws the rest, flushing once per texture change.
  // `mirrorU` swaps the horizontal UVs so a mirrored play field keeps the note artwork
  // the right way round.
  void draw(batch::SpriteBatch& out, const math::Mat4& projection, double visualTimeSec,
      const gl::Texture& fallback, bool mirrorU = false);

  [[nodiscard]] int size() const noexcept { return static_cast<int>(effects_.size()); }

private:
  struct Effect {
    double timeSec{};
    float x{};
    float y{};
    float width{};
    float height{};
    float rotationDeg{};
    float u0{};
    float v0{};
    float u1{1.0F};
    float v1{1.0F};
    const gl::Texture* texture{};
  };

  std::vector<Effect> effects_;
};

} // namespace rechigros::render::fx
