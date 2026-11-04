#include "rechigros/game/RpeCamera.hpp"

#include "rechigros/game/Easing.hpp"
#include "rechigros/game/EventUtils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace rechigros::game {
namespace {

double number(const nlohmann::json& value, double fallback = 0.0) {
  return value.is_number() ? value.get<double>() : fallback;
}

double beat(const nlohmann::json& value) {
  if (value.is_number()) return value.get<double>();
  if (!value.is_array() || value.size() < 3) return 0.0;
  const double denominator = number(value[2]);
  return denominator == 0.0 ? number(value[0])
      : number(value[0]) + number(value[1]) / denominator;
}

std::vector<LineEvent> events(const nlohmann::json& source,
    const std::shared_ptr<BpmTimeline>& timeline) {
  std::vector<LineEvent> result;
  if (!source.is_array()) return result;
  for (const auto& item : source) {
    if (!item.is_object()) continue;
    LineEvent event;
    event.startTime = timeline->beatToSec(beat(item["startTime"]) * 32.0);
    event.endTime = timeline->beatToSec(beat(item["endTime"]) * 32.0);
    if (event.endTime < event.startTime) event.endTime = event.startTime;
    event.start = number(item["start"]);
    event.end = number(item["end"], event.start);
    event.type = std::max(0, static_cast<int>(number(item["easingType"], 1.0)) - 1);
    event.left = static_cast<float>(number(item["easingLeft"]));
    event.right = static_cast<float>(number(item["easingRight"], 1.0));
    event.bezier = item["bezier"].is_boolean() ? item["bezier"].get<bool>()
        : number(item["bezier"]) != 0.0;
    event.bezierPoints = {0.0F, 0.0F, 1.0F, 1.0F};
    if (const auto points = item.find("bezierPoints");
        points != item.end() && points->is_array() && points->size() == 4) {
      for (std::size_t index = 0; index < 4; ++index) {
        event.bezierPoints[index] = static_cast<float>(number((*points)[index],
            index < 2 ? 0.0 : 1.0));
      }
    }
    if (event.bezier) {
      event.bezierLut = Easing::buildBezierLut(event.bezierPoints[0], event.bezierPoints[1],
          event.bezierPoints[2], event.bezierPoints[3]);
    }
    result.push_back(std::move(event));
  }
  std::stable_sort(result.begin(), result.end(),
      [](const auto& first, const auto& second) { return first.startTime < second.startTime; });
  return result;
}

} // namespace

std::array<std::array<double, 3>, 3> RpeCameraFrame::basis() const noexcept {
  constexpr double pi = 3.14159265358979323846;
  const double yawRadians = yaw * pi / 180.0;
  const double pitchRadians = pitch * pi / 180.0;
  const double rollRadians = roll * pi / 180.0;
  const double cy = std::cos(yawRadians), sy = std::sin(yawRadians);
  const double cp = std::cos(pitchRadians), sp = std::sin(pitchRadians);
  const double cr = std::cos(rollRadians), sr = std::sin(rollRadians);
  const std::array<double, 3> right{cy, 0.0, sy};
  const std::array<double, 3> up{-sy * sp, cp, cy * sp};
  std::array<std::array<double, 3>, 3> result;
  for (std::size_t index = 0; index < 3; ++index) {
    result[0][index] = right[index] * cr + up[index] * sr;
    result[1][index] = up[index] * cr - right[index] * sr;
  }
  result[2] = {sy * cp, sp, -cy * cp};
  return result;
}

std::optional<std::array<double, 2>> RpeCameraFrame::planeAt(double screenX, double screenY,
    double viewWidth, double viewHeight, double viewScale) const noexcept {
  if (!active || !(viewScale > 0.0)) return std::nullopt;
  constexpr double pi = 3.14159265358979323846;
  const double focal = 450.0 / std::tan(fov * pi / 360.0);
  const double dx = (screenX - viewWidth * 0.5) / (viewScale * focal);
  const double dy = -(screenY - viewHeight * 0.5) / (viewScale * focal);
  const auto axes = basis();
  std::array<double, 3> direction;
  for (std::size_t index = 0; index < 3; ++index) {
    direction[index] = axes[2][index] + dx * axes[0][index] + dy * axes[1][index];
  }
  if (std::abs(direction[2]) < 1.0e-7) return std::nullopt;
  const double distance = -z / direction[2];
  if (distance < RPE_CAMERA_NEAR || distance > RPE_CAMERA_FAR) return std::nullopt;
  return std::array<double, 2>{x + direction[0] * distance, y + direction[1] * distance};
}

RpeCamera RpeCamera::parse(const std::string& globalEventsJson,
    const std::shared_ptr<BpmTimeline>& bpmTimeline) {
  RpeCamera camera;
  if (globalEventsJson.empty() || !bpmTimeline) return camera;
  const auto root = nlohmann::json::parse(globalEventsJson, nullptr, false);
  if (!root.is_object()) return camera;
  const auto types = root.find("types");
  if (types == root.end() || !types->is_object()) return camera;
  struct Axis {
    const char* type;
    const char* column;
    double fallback;
    double minimum;
    double maximum;
  };
  static const Axis axes[] = {
      {"spatial-camera/position", "x", 0.0, -50000.0, 50000.0},
      {"spatial-camera/position", "y", 0.0, -50000.0, 50000.0},
      {"spatial-camera/position", "z", 1000.0, -50000.0, 50000.0},
      {"spatial-camera/rotation", "yaw", 0.0, -3600.0, 3600.0},
      {"spatial-camera/rotation", "pitch", 0.0, -89.0, 89.0},
      {"spatial-camera/rotation", "roll", 0.0, -3600.0, 3600.0},
      {"spatial-camera/lens", "fov", RPE_CAMERA_DEFAULT_FOV, 10.0, 150.0},
  };
  for (std::size_t index = 0; index < camera.tracks_.size(); ++index) {
    auto& track = camera.tracks_[index];
    track.fallback = axes[index].fallback;
    track.minimum = axes[index].minimum;
    track.maximum = axes[index].maximum;
    const auto type = types->find(axes[index].type);
    if (type == types->end() || !type->is_object()) continue;
    const auto columns = type->find("columns");
    if (columns == type->end() || !columns->is_object()) continue;
    const auto column = columns->find(axes[index].column);
    if (column == columns->end() || !column->is_object()) continue;
    const auto source = column->find("events");
    if (source == column->end()) continue;
    track.events = events(*source, bpmTimeline);
    camera.available_ = camera.available_ || !track.events.empty();
  }
  const auto lens = types->find("spatial-camera/lens");
  if (lens != types->end() && lens->is_object()) {
    const auto settings = lens->find("settings");
    if (settings != lens->end() && settings->is_object()) {
      camera.followChart_ = settings->value("followChart", false);
      camera.available_ = camera.available_ || camera.followChart_;
    }
  }
  return camera;
}

RpeCameraFrame RpeCamera::sample(double seconds) const noexcept {
  RpeCameraFrame frame;
  frame.active = available_;
  frame.followChart = followChart_;
  if (!available_) return frame;
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    const auto& track = tracks_[index];
    double value = track.fallback;
    if (!track.events.empty() && seconds >= track.events.front().startTime) {
      value = EventUtils::getEventVal(seconds, track.events);
      if (!std::isfinite(value)) value = track.fallback;
    }
    value = std::clamp(value, track.minimum, track.maximum);
    switch (index) {
      case 0: frame.x = value; break;
      case 1: frame.y = value; break;
      case 2: frame.z = value; break;
      case 3: frame.yaw = value; break;
      case 4: frame.pitch = value; break;
      case 5: frame.roll = value; break;
      case 6: frame.fov = value; break;
      default: break;
    }
  }
  return frame;
}

} // namespace rechigros::game
