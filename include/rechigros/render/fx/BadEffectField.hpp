#pragma once

#include "rechigros/render/math/Mat4.hpp"

#include <vector>

namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The fading copy of a note head that appears when a note is judged bad.
//
// A bad note leaves its head behind at the position and size it had when it was judged,
// tinted dark red, and fades out over half a second of visual time. Misses do not produce
// one: only a bad judgement does.
class BadEffectField final {
public:
  // How long one copy lives, in visual seconds.
  static constexpr double LIFETIME_SEC = 0.5;
  // Tint of the fading copy.
  static constexpr float TINT_RED = 0.423529F;
  static constexpr float TINT_GREEN = 0.262745F;
  static constexpr float TINT_BLUE = 0.262745F;
  // Upper bound on live copies; the oldest are dropped first.
  static constexpr int MAX_EFFECTS = 256;

  // Alpha of a copy after `elapsedSec`, clamped to its lifetime.
  [[nodiscard]] static float alphaAt(double elapsedSec) noexcept;

  void clear() noexcept;
  // Records one copy. `visualTimeSec` is the clock the fade runs on.
  void spawn(double visualTimeSec, float x, float y, float width, float height,
      float rotationDeg, float u0, float v0, float u1, float v1, const gl::Texture* texture);

  // Drops expired copies, then draws the rest, flushing once per texture change.
  void draw(batch::SpriteBatch& out, const math::Mat4& projection, double visualTimeSec,
      const gl::Texture& fallback);

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
