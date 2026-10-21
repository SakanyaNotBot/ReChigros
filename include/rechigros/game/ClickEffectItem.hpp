#pragma once

#include <array>
#include <random>

namespace rechigros::game {
struct Note;

struct ClickEffectItem {
  Note* note{};
  double timeSec{};
  int numOfParts{4};
  std::array<float, 4> effectRotateDeg{};
  std::array<float, 4> effectRBase{};
  std::array<float, 4> effectDirX{};
  std::array<float, 4> effectDirY{};
  bool positionCached{};
  float cachedScreenX{}, cachedScreenY{}, cachedLineRotDeg{};
  bool animStartCached{};
  float animStartSec{};

  ClickEffectItem(Note* source, double second, std::mt19937& random, int parts = 4);
};
}

