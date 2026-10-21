#pragma once

#include "rechigros/game/Event.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {

// A storyboard effect variable. Either a constant vector or an animated track that is
// interpolated with the effect's own easing.
struct PrprVariable {
  // Constant values; used when `events` is empty.
  std::vector<float> values{0.0F};
  // Animated track. Empty means the variable is constant.
  std::vector<LineEvent> events;

  [[nodiscard]] bool animated() const noexcept { return !events.empty(); }

  // Evaluates the track at `chartTimeSec`, or returns the constant.
  [[nodiscard]] std::vector<float> evaluate(double chartTimeSec) const;
};

struct PrprEffect {
  double startTime{};
  double endTime{};
  std::string shader;
  std::unordered_map<std::string, PrprVariable> variables;
  bool global{false};
};

struct PrprVideo {
  std::string path;
  double startTime{};
  double endTime{};
  float alpha{1.0F};
  bool loop{false};
};
} // namespace rechigros::game
