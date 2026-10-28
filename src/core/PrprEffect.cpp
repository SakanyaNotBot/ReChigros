#include "rechigros/game/PrprEffect.hpp"

#include "rechigros/game/EventUtils.hpp"

#include <algorithm>

namespace rechigros::game {

std::vector<float> PrprVariable::evaluate(double chartTimeSec) const {
  if (!vectorEvents.empty()) {
    const auto* selected = &vectorEvents.front();
    for (const auto& event : vectorEvents) {
      if (chartTimeSec < event.startTime) break;
      selected = &event;
    }
    if (chartTimeSec < selected->startTime) return values;
    const double duration = selected->endTime - selected->startTime;
    const double progress = duration <= 1e-9 ? 1.0
        : std::clamp((chartTimeSec - selected->startTime) / duration, 0.0, 1.0);
    const double eased = EventUtils::applyEasing(progress, *selected);
    std::vector<float> result(static_cast<std::size_t>(selected->count));
    for (int index = 0; index < selected->count; ++index) {
      result[static_cast<std::size_t>(index)] = static_cast<float>(
          selected->from[static_cast<std::size_t>(index)] +
          (selected->to[static_cast<std::size_t>(index)] -
           selected->from[static_cast<std::size_t>(index)]) * eased);
    }
    return result;
  }
  if (events.empty()) return values;
  const double value = EventUtils::getEventVal(chartTimeSec, events);
  return {static_cast<float>(value)};
}

} // namespace rechigros::game
