#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {

struct RpeTrajectoryOptions {
  std::string mode{"parametric"};
  std::string xExpression{"200*cos(2*pi*t)"};
  std::string yExpression{"200*sin(2*pi*t)"};
  std::string radiusExpression{"200"};
  std::string rotationExpression;
  bool tangentRotation{};
  std::string parameterStart{"0"};
  std::string parameterEnd{"1"};
  std::string angleStart{"0"};
  std::string angleEnd{"2*pi"};
  double trimStart{};
  double trimEnd{1.0};
  double rotation{};
  double scaleX{1.0};
  double scaleY{1.0};
  bool alignStart{};
  double startX{};
  double startY{};
  int easingX{1};
  int easingY{1};
  double seed{1.0};
  double randomness{};
  std::string shape;
  std::unordered_map<std::string, double> parameters;
};

struct RpeTrajectorySample {
  double x{};
  double y{};
  double rotation{};
};

class RpeTrajectory final {
public:
  using Variables = std::unordered_map<std::string, double>;
  explicit RpeTrajectory(const RpeTrajectoryOptions& options);
  [[nodiscard]] RpeTrajectorySample sample(double progress) const noexcept;
  [[nodiscard]] bool hasRotation() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace rechigros::game
