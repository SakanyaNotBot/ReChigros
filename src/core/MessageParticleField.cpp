#include "rechigros/game/MessageParticleField.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::game {
namespace {

constexpr float clamp01(float value) noexcept {
  return value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
}

// startSize 0.5 world units expressed as a fraction of the stage height.
constexpr float FULL_SIZE = 0.5F / MessageParticleField::UNITS;
// Fade width of the band clip.
constexpr float HIDE_SMOOTHNESS = 0.1F;
// Velocity y magnitude times the speed modifier range, converted to uv per second.
constexpr float SPEED_MIN = 0.5F * 0.5F / MessageParticleField::UNITS;
constexpr float SPEED_MAX = 0.5F * 2.0F / MessageParticleField::UNITS;
// Constant acceleration towards the centre, and the external force field's own pull.
constexpr float FORCE_ACCEL = 0.5F / MessageParticleField::UNITS;
constexpr float FIELD_ACCEL = 5.0F / MessageParticleField::UNITS;
constexpr float FIELD_DRAG = 0.8F;
// The force field only covers a band: half width in world units, and a half height that
// scales with the level's light band.
constexpr float FIELD_HALF_WIDTH = 7.5F;
constexpr float FIELD_HALF_HEIGHT_PER_BAND = 1.5F;
// Tangents of the shipped size curve.
constexpr float SIZE_OUT_SLOPE0 = 3.6587481F;
constexpr float SIZE_IN_SLOPE1 = 0.23306048F;

} // namespace

float MessageParticleField::sizeCurve(float progress) noexcept {
  const float u = clamp01(progress);
  if (u <= 0.0F) return 0.0F;
  if (u >= 1.0F) return 1.0F;
  const float u2 = u * u;
  const float u3 = u2 * u;
  // Plain cubic Hermite through (0,0) and (1,1) with the shipped tangents; the shipped
  // keys use the unweighted mode, so the stored weights are ignored.
  const float h10 = u3 - 2.0F * u2 + u;
  const float h01 = -2.0F * u3 + 3.0F * u2;
  const float h11 = u3 - u2;
  const float value = h10 * SIZE_OUT_SLOPE0 + h01 + h11 * SIZE_IN_SLOPE1;
  return clamp01(value);
}

int MessageParticleField::textureAlpha(int red, int green) noexcept {
  const int core = std::clamp(green, 0, 255);
  const int halo = std::clamp((red * 55 + 50) / 100, 0, 255);
  return std::max(core, halo);
}

float MessageParticleField::bandAlpha(const Particle& particle, float bandWidth) noexcept {
  const float hidePosition = 0.5F + bandWidth * 0.5F;
  const float screenYUp = 1.0F - particle.y;
  float t = 0.0F;
  if (particle.top) {
    t = clamp01((screenYUp - hidePosition) / HIDE_SMOOTHNESS);
    return t * t * (3.0F - 2.0F * t);
  }
  t = clamp01((screenYUp - (1.0F - hidePosition - HIDE_SMOOTHNESS)) / HIDE_SMOOTHNESS);
  t = t * t * (3.0F - 2.0F * t);
  return 1.0F - t;
}

void MessageParticleField::reset() noexcept {
  count_ = 0;
  emitCarry_ = 0.0F;
  randomState_ = 0x9E3779B9U;
}

float MessageParticleField::nextFloat() noexcept {
  // Deterministic xorshift so the field is reproducible across runs and platforms.
  randomState_ ^= randomState_ << 13U;
  randomState_ ^= randomState_ >> 17U;
  randomState_ ^= randomState_ << 5U;
  return static_cast<float>(randomState_ & 0x00FFFFFFU) /
      static_cast<float>(0x01000000U);
}

void MessageParticleField::spawnOne() {
  int slot = count_;
  // Reuse the oldest expired particle when the pool is full.
  for (int index = 0; index < count_; ++index) {
    if (particles_[static_cast<std::size_t>(index)].age >= LIFE_SEC) {
      slot = index;
      break;
    }
  }
  if (slot >= MAX_PARTICLES) return;

  Particle particle;
  particle.top = nextFloat() < 0.5F;
  // Box shape, uniform over the 25 x 4.5 box centred six world units from the middle.
  const float worldY = BOX_NEAR + BOX_HEIGHT * nextFloat();
  particle.y = particle.top ? (0.5F - worldY / UNITS) : (0.5F + worldY / UNITS);
  particle.worldX = -BOX_HALF_WIDTH + 2.0F * BOX_HALF_WIDTH * nextFloat();
  const float speed = SPEED_MIN + (SPEED_MAX - SPEED_MIN) * nextFloat();
  particle.velocityY = particle.top ? speed : -speed;
  particle.size = FULL_SIZE;
  particle.age = 0.0F;

  particles_[static_cast<std::size_t>(slot)] = particle;
  count_ = std::max(count_, slot + 1);
}

void MessageParticleField::update(float deltaTimeSec, float bandWidth) {
  if (!(deltaTimeSec > 0.0F)) return;

  emitCarry_ += deltaTimeSec * EMIT_RATE * 2.0F;
  while (emitCarry_ >= 1.0F) {
    emitCarry_ -= 1.0F;
    spawnOne();
  }

  const float fieldHalfHeight = FIELD_HALF_HEIGHT_PER_BAND * std::max(0.0F, bandWidth);
  for (int index = 0; index < count_; ++index) {
    auto& particle = particles_[static_cast<std::size_t>(index)];
    particle.age += deltaTimeSec;
    // Positive velocity moves down the screen, so "towards the centre" is +1 for a top
    // emitter and -1 for a bottom one.
    const float towardsCentre = particle.top ? 1.0F : -1.0F;
    particle.velocityY += towardsCentre * FORCE_ACCEL * deltaTimeSec;

    const float worldY = (0.5F - particle.y) * UNITS;
    if (fieldHalfHeight > 0.0F && std::abs(worldY) < fieldHalfHeight &&
        std::abs(particle.worldX) < FIELD_HALF_WIDTH) {
      particle.velocityY += towardsCentre * FIELD_ACCEL * deltaTimeSec;
      // The drag scales with the particle's current size and its velocity.
      const float drag = FIELD_DRAG * sizeAt(particle) * UNITS * deltaTimeSec;
      particle.velocityY *= 1.0F / (1.0F + drag);
    }
    particle.y += particle.velocityY * deltaTimeSec;
  }
}

float MessageParticleField::sizeAt(const Particle& particle) const noexcept {
  return particle.size * sizeCurve(clamp01(particle.age / LIFE_SEC));
}

} // namespace rechigros::game
