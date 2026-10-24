#pragma once

#include <array>
#include <random>

namespace rechigros::game {
struct Note;

// Pre-rolled debris burst for one note. The four particles are generated once during chart
// preparation rather than at hit time, so a burst is identical across frames and replays.
struct ClickEffectItem {
  Note* note{};
  double timeSec{};
  int numOfParts{4};
  std::array<float, 4> effectRotateDeg{};
  std::array<float, 4> effectSpeed{};
  // Spawn distance from the note centre, in prefab units.
  std::array<float, 4> effectR0{};
  std::array<float, 4> effectDirX{};
  std::array<float, 4> effectDirY{};
  bool positionCached{};
  float cachedScreenX{}, cachedScreenY{}, cachedLineRotDeg{};
  bool animStartCached{};
  float animStartSec{};

  ClickEffectItem(Note* source, double second, std::mt19937& random, int parts = 4);
};
}
