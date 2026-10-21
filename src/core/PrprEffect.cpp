#include "rechigros/game/PrprEffect.hpp"

#include "rechigros/game/EventUtils.hpp"

#include <algorithm>

namespace rechigros::game {

std::vector<float> PrprVariable::evaluate(double chartTimeSec) const {
  if (events.empty()) return values;
  // Animated tracks hold one scalar value per key; the value is the event's end value
  // so a track that has finished settles on its last key.
  const double value = EventUtils::getEventVal(chartTimeSec, events);
  return {static_cast<float>(value)};
}

} // namespace rechigros::game
