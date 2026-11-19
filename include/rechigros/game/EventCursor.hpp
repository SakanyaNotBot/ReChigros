#pragma once

#include <limits>

namespace rechigros::game {

// A cursor belongs to one event list and reuses its position for finite,
// nondecreasing query times. Reset it when the list changes.
struct EventCursor {
  int index{-1};
  double lastTime{std::numeric_limits<double>::quiet_NaN()};
  void reset() noexcept {
    index = -1;
    lastTime = std::numeric_limits<double>::quiet_NaN();
  }
};

} // namespace rechigros::game
