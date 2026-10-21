#pragma once

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace rechigros::game {

// Common easing payload shared by every event type. An empty bezierLut means "not
// precomputed"; an empty bezierPoints means "no bezier data at all".
struct EasingSpec {
  int type{0};
  float left{0.0F};
  float right{1.0F};
  bool bezier{false};
  std::vector<float> bezierPoints;
  std::vector<float> bezierLut;
};

struct LineEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  double start{0.0};
  double end{0.0};
};

struct MoveEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  double start{0.0};
  double end{0.0};
  double start2{0.0};
  double end2{0.0};
};

struct SpeedEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  double value{0.0};
  double endValue{std::numeric_limits<double>::quiet_NaN()};

  double floorPosition{0.0};
  int tweenId{2};
  bool isClamped{false};
  bool isBezier{false};
  double totalHeight{0.0};
  double speedK{0.0};
  double speedB{0.0};
};

struct ColorEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  float startR{0.0F};
  float startG{0.0F};
  float startB{0.0F};
  float endR{0.0F};
  float endG{0.0F};
  float endB{0.0F};
};

struct TextEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  std::string text;
  std::string endText;
  bool hasEndText{false};
};

struct GifEvent : EasingSpec {
  double startTime{0.0};
  double endTime{0.0};
  double start{0.0};
  double end{0.0};
};

} // namespace rechigros::game
