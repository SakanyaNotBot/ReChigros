#pragma once

#include "rechigros/game/Event.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {

struct RpeNoiseEvent : LineEvent {
  bool hasAnchor{};
  double anchorX{};
  double anchorY{};
  bool hasTransformStart{};
  bool hasTransformEnd{};
  double transformStart{};
  double transformEnd{};
  bool transformStep{};
};

struct RpeNoiseTrack {
  std::vector<RpeNoiseEvent> events;
};

struct RpeNoiseInterval {
  double start{};
  double end{};
  double readyFlash{0.5};
};

struct RpeNoiseArea {
  std::string name;
  std::string layerId{"default"};
  double left{};
  double bottom{};
  double right{};
  double top{};
  double appear{};
  double disappear{};
  bool invert{};
  std::vector<RpeNoiseInterval> intervals;
  RpeNoiseTrack moveX, moveY, rotate, scaleX, scaleY;
};

struct RpeNoiseLayer {
  std::string id{"default"};
  std::string name;
  std::string skin;
  std::string channel{"normal"};
  std::unordered_map<std::string, std::vector<float>> parameters;
};

struct RpeNoisePoint {
  double x{};
  double y{};
};

struct RpeNoiseMatrix {
  double a{1.0}, b{}, c{}, d{1.0}, x{}, y{};
};

enum class RpeNoiseState { Disabled, Ready, Active, HiddenBefore, HiddenAfter };

struct RpeNoiseSample {
  RpeNoiseState state{RpeNoiseState::Disabled};
  double alpha{0.4};
  double activationMix{};
  double rotation{};
  double scaleX{1.0};
  double scaleY{1.0};
  std::array<RpeNoisePoint, 4> points{};
  RpeNoisePoint center;
};

class RpeNoiseRuntime final {
public:
  using Prefixes = std::unordered_map<const RpeNoiseEvent*, RpeNoiseMatrix>;
  explicit RpeNoiseRuntime(const RpeNoiseArea& area);
  [[nodiscard]] RpeNoiseSample sample(double seconds) const noexcept;

private:
  const RpeNoiseArea* area_;
  RpeNoisePoint center_;
  std::vector<RpeNoiseInterval> activations_;
  bool allIntervalsActive_{};
  Prefixes rotatePrefix_, scaleXPrefix_, scaleYPrefix_;
};

[[nodiscard]] bool rpeNoiseContains(const RpeNoiseArea& area, const RpeNoiseSample& sample,
    RpeNoisePoint point, double inset = 0.0, double aspect = 1.5) noexcept;
[[nodiscard]] bool rpeNoiseGroupCovered(const std::vector<RpeNoiseArea>& areas,
    const std::string& layerId, double seconds, RpeNoisePoint point, RpeNoiseState state,
    bool singleInversion, double inset = 0.0, double aspect = 1.5) noexcept;
[[nodiscard]] bool rpeNoiseTouchBlocked(const std::vector<RpeNoiseArea>& areas, double seconds,
    RpeNoisePoint point, double screenHeight, bool singleInversion, double aspect = 1.5) noexcept;

} // namespace rechigros::game
