#include "rechigros/game/RpeNoiseArea.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::game {
namespace {

bool visible(const RpeNoiseSample& sample, const RpeNoiseArea& area) noexcept {
  return sample.state != RpeNoiseState::HiddenBefore &&
      sample.state != RpeNoiseState::HiddenAfter &&
      sample.scaleX != 0.0 && sample.scaleY != 0.0 &&
      area.left != area.right && area.bottom != area.top;
}

} // namespace

bool rpeNoiseContains(const RpeNoiseArea& area, const RpeNoiseSample& sample,
    RpeNoisePoint point, double inset, double aspect) noexcept {
  if (!visible(sample, area) || !std::isfinite(point.x) || !std::isfinite(point.y) ||
      !std::isfinite(inset) || !std::isfinite(aspect) || !(aspect > 0.0)) return false;
  const double halfX = std::abs(area.right - area.left) * std::abs(sample.scaleX) * 0.5;
  const double halfY = std::abs(area.top - area.bottom) * std::abs(sample.scaleY) * 0.5;
  const double cosine = std::cos(sample.rotation);
  const double sine = std::sin(sample.rotation);
  const double dx = point.x - sample.center.x;
  const double dy = point.y - sample.center.y;
  const double localX = cosine * dx + sine * dy;
  const double localY = -sine * dx + cosine * dy;
  const double yScale = 1.5 / aspect;
  const double insetX = std::min(inset / std::hypot(cosine, sine * yScale), halfX * 0.5);
  const double insetY = std::min(inset / std::hypot(sine, cosine * yScale), halfY * 0.5);
  const double sign = area.invert ? 1.0 : -1.0;
  return std::abs(localX) <= halfX + sign * insetX &&
      std::abs(localY) <= halfY + sign * insetY;
}

bool rpeNoiseGroupCovered(const std::vector<RpeNoiseArea>& areas, const std::string& layerId,
    double seconds, RpeNoisePoint point, RpeNoiseState state, bool singleInversion,
    double inset, double aspect) noexcept {
  bool normal = false;
  int inversions = 0;
  for (const auto& area : areas) {
    if (area.layerId != layerId) continue;
    const auto sample = RpeNoiseRuntime(area).sample(seconds);
    if (sample.state != state || !rpeNoiseContains(area, sample, point, inset, aspect)) continue;
    if (area.invert) ++inversions;
    else normal = true;
  }
  return normal != (singleInversion ? inversions == 1 : (inversions & 1) != 0);
}

bool rpeNoiseTouchBlocked(const std::vector<RpeNoiseArea>& areas, double seconds,
    RpeNoisePoint point, double screenHeight, bool singleInversion, double aspect) noexcept {
  const double inset = std::max(0.0, screenHeight) * 0.1;
  for (std::size_t index = 0; index < areas.size(); ++index) {
    const auto& layerId = areas[index].layerId;
    if (std::any_of(areas.begin(), areas.begin() + static_cast<std::ptrdiff_t>(index),
        [&](const auto& area) { return area.layerId == layerId; })) continue;
    if (rpeNoiseGroupCovered(areas, layerId, seconds, point, RpeNoiseState::Active,
            singleInversion, 0.0, aspect) &&
        rpeNoiseGroupCovered(areas, layerId, seconds, point, RpeNoiseState::Active,
            singleInversion, inset, aspect)) return true;
  }
  return false;
}

} // namespace rechigros::game
