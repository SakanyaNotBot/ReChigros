#include "rechigros/game/ClickEffectItem.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <cmath>

namespace rechigros::game {
namespace {
constexpr double PI = 3.14159265358979323846;

// Uniform in [0,1). The generator's own range-wide draw keeps the sequence reproducible.
float sampleUnitRandom(std::mt19937& random) {
  constexpr double denominator = static_cast<double>(std::mt19937::max()) + 1.0;
  return static_cast<float>(static_cast<double>(random()) / denominator);
}
} // namespace

ClickEffectItem::ClickEffectItem(Note* source, double second, std::mt19937& random, int parts)
    : note(source), timeSec(second), numOfParts(parts) {
  const float speedSpan = HIT_FX_PARTICLE_SPEED_MAX - HIT_FX_PARTICLE_SPEED_MIN;
  for (std::size_t i = 0; i < effectRotateDeg.size(); ++i) {
    effectRotateDeg[i] = sampleUnitRandom(random) * 360.0F;
    const auto radians = static_cast<double>(effectRotateDeg[i]) * PI / 180.0;
    effectDirX[i] = static_cast<float>(std::cos(radians));
    effectDirY[i] = static_cast<float>(std::sin(radians));
    effectSpeed[i] = HIT_FX_PARTICLE_SPEED_MIN + sampleUnitRandom(random) * speedSpan;
    // The ring is thinned inwards by a random fraction of its thickness, so spawn radii run
    // from radius*(1-thickness) up to radius.
    effectR0[i] = HIT_FX_PARTICLE_SPAWN_RADIUS
        * (1.0F - HIT_FX_PARTICLE_SPAWN_THICKNESS * sampleUnitRandom(random));
  }
}
} // namespace rechigros::game
