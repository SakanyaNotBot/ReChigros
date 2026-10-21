#include "rechigros/game/ClickEffectItem.hpp"

#include <cmath>

namespace rechigros::game {
ClickEffectItem::ClickEffectItem(Note* source, double second, std::mt19937& random, int parts)
    : note(source), timeSec(second), numOfParts(parts) {
  std::uniform_real_distribution<float> rotation(0.0F, 360.0F);
  std::uniform_real_distribution<float> radius(185.0F, 265.0F);
  constexpr double radians = 3.14159265358979323846 / 180.0;
  for (std::size_t i = 0; i < effectRotateDeg.size(); ++i) {
    effectRotateDeg[i] = rotation(random);
    effectDirX[i] = static_cast<float>(std::cos(effectRotateDeg[i] * radians));
    effectDirY[i] = static_cast<float>(std::sin(effectRotateDeg[i] * radians));
    effectRBase[i] = radius(random);
  }
}
} // namespace rechigros::game
