#pragma once

#include "rechigros/game/BpmTimeline.hpp"
#include "rechigros/game/Event.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rechigros::game {

inline const double RPE_CAMERA_DEFAULT_FOV =
    2.0 * std::atan(450.0 / 1000.0) * 180.0 / 3.14159265358979323846;
inline constexpr double RPE_CAMERA_NEAR = 1.0;
inline constexpr double RPE_CAMERA_FAR = 150000.0;

struct RpeCameraFrame {
  double x{}, y{}, z{1000.0};
  double yaw{}, pitch{}, roll{};
  double fov{RPE_CAMERA_DEFAULT_FOV};
  bool active{};
  bool followChart{};

  [[nodiscard]] std::array<std::array<double, 3>, 3> basis() const noexcept;
  [[nodiscard]] std::optional<std::array<double, 2>> planeAt(double screenX, double screenY,
      double viewWidth, double viewHeight, double viewScale) const noexcept;
};

class RpeCamera final {
public:
  [[nodiscard]] static RpeCamera parse(const std::string& globalEventsJson,
      const std::shared_ptr<BpmTimeline>& bpmTimeline);
  [[nodiscard]] RpeCameraFrame sample(double seconds) const noexcept;
  [[nodiscard]] bool available() const noexcept { return available_; }

private:
  struct Track {
    std::vector<LineEvent> events;
    double fallback{};
    double minimum{-50000.0};
    double maximum{50000.0};
  };

  std::array<Track, 7> tracks_;
  bool available_{};
  bool followChart_{};
};

} // namespace rechigros::game
