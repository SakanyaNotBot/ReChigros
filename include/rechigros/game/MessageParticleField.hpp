#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rechigros::game {

// The white square-particle field one level effect adds around the play field.
//
// Two emitters sit above and below the centre and drift boxes of particles towards the
// middle. Each particle is driven by a constant force, plus an external force field that
// only reaches a band around the centre whose height follows the level's light band, plus
// a velocity- and size-proportional drag. Positions are kept in screen uv space (0..1 with
// y running down) so a renderer can place them directly.
class MessageParticleField final {
public:
  // Screen height in world units; the play field camera spans ten of them.
  static constexpr float UNITS = 10.0F;
  // Emitter rate, lifetime and the resulting spawn box.
  static constexpr float EMIT_RATE = 10.0F;
  static constexpr float LIFE_SEC = 10.0F;
  static constexpr float BOX_HALF_WIDTH = 12.5F;
  static constexpr float BOX_HEIGHT = 4.5F;
  static constexpr float BOX_NEAR = 3.75F;
  // 2 emitters x 10 per second x 10 seconds lifetime, with headroom.
  static constexpr int MAX_PARTICLES = 512;

  struct Particle {
    // World-unit x, kept in world units so the renderer can map it through the aspect.
    float worldX{};
    // Screen uv y, 0 at the top.
    float y{};
    // uv per second; positive moves down.
    float velocityY{};
    // Full-grown size as a fraction of the stage height.
    float size{};
    float age{};
    bool top{};
  };

  // Size over lifetime: a two-key cubic Hermite with the shipped tangents.
  [[nodiscard]] static float sizeCurve(float progress) noexcept;
  // Band clip used by the shipped fragment: top particles fade in above the band, bottom
  // particles below it.
  [[nodiscard]] static float bandAlpha(const Particle& particle, float bandWidth) noexcept;

  void reset() noexcept;
  // Advances by a chart-clock delta. A non-positive delta is ignored.
  void update(float deltaTimeSec, float bandWidth);

  [[nodiscard]] int count() const noexcept { return count_; }
  [[nodiscard]] const Particle& at(int index) const noexcept {
    return particles_[static_cast<std::size_t>(index)];
  }
  // Current size as a stage-height fraction.
  [[nodiscard]] float sizeAt(const Particle& particle) const noexcept;

private:
  void spawnOne();
  [[nodiscard]] float nextFloat() noexcept;

  std::array<Particle, MAX_PARTICLES> particles_{};
  int count_{};
  float emitCarry_{};
  std::uint32_t randomState_{0x9E3779B9U};
};

} // namespace rechigros::game
