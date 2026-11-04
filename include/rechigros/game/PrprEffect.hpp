#pragma once

#include "rechigros/game/Event.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {

// A storyboard effect variable. Either a constant vector or an animated track that is
// interpolated with the effect's own easing.
struct PrprVariableEvent : LineEvent {
  int count{1};
  std::array<float, 4> from{};
  std::array<float, 4> to{};
};

struct PrprVariable {
  // Constant values; used when `events` is empty.
  std::vector<float> values{0.0F};
  // Animated track. Empty means the variable is constant.
  std::vector<LineEvent> events;
  std::vector<PrprVariableEvent> vectorEvents;

  [[nodiscard]] bool animated() const noexcept {
    return !events.empty() || !vectorEvents.empty();
  }

  // Evaluates the track at `chartTimeSec`, or returns the constant.
  [[nodiscard]] std::vector<float> evaluate(double chartTimeSec) const;
};

struct PrprEffect {
  double startTime{};
  double endTime{};
  std::string shader;
  std::unordered_map<std::string, PrprVariable> variables;
  bool global{false};
  int order{};
  int lineIndex{-1};
  std::string sourceName;
};

// A storyboard video background. Times are absolute chart seconds; the video itself begins
// at `startTimeSec`, and the alpha/dim tracks are evaluated on that same absolute timeline
// rather than being offset from the video's own start.
struct PrprVideo {
  enum ScaleType : int { CropCenter = 0, Inside = 1, Fit = 2 };

  std::string path;
  double startTimeSec{};
  int scaleType{CropCenter};
  float alphaDefault{1.0F};
  std::vector<LineEvent> alphaEvents;
  float dimDefault{0.0F};
  std::vector<LineEvent> dimEvents;
  // Judge line the video is parented to, or -1 when it is not attached to one.
  int attachLineIndex{-1};

  // Only the three defined placements survive; anything else means crop-centre.
  [[nodiscard]] static int sanitizeScaleType(int value) noexcept {
    return value == Inside || value == Fit ? value : CropCenter;
  }
};
} // namespace rechigros::game
