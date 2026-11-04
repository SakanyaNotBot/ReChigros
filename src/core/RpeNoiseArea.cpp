#include "rechigros/game/RpeNoiseArea.hpp"

#include "rechigros/game/EventUtils.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::game {
namespace {

constexpr double EPSILON = 1e-9;
constexpr double PI = 3.14159265358979323846;

RpeNoisePoint apply(const RpeNoiseMatrix& matrix, RpeNoisePoint point) {
  return {matrix.a * point.x + matrix.c * point.y + matrix.x,
      matrix.b * point.x + matrix.d * point.y + matrix.y};
}

RpeNoiseMatrix compose(const RpeNoiseMatrix& after, const RpeNoiseMatrix& before) {
  const auto origin = apply(after, {before.x, before.y});
  return {after.a * before.a + after.c * before.b,
      after.b * before.a + after.d * before.b,
      after.a * before.c + after.c * before.d,
      after.b * before.c + after.d * before.d, origin.x, origin.y};
}

enum class Transform { Rotate, ScaleX, ScaleY };

RpeNoiseMatrix relative(Transform type, double start, double value, RpeNoisePoint anchor) {
  if (type == Transform::Rotate) {
    const double angle = (value - start) * PI / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return {cosine, -sine, sine, cosine,
        anchor.x * (1.0 - cosine) - anchor.y * sine,
        anchor.y * (1.0 - cosine) + anchor.x * sine};
  }
  double scale = start == 0.0 ? 1.0 : value / start;
  if (!std::isfinite(scale)) scale = 1.0;
  return type == Transform::ScaleX
      ? RpeNoiseMatrix{scale, 0.0, 0.0, 1.0, anchor.x * (1.0 - scale), 0.0}
      : RpeNoiseMatrix{1.0, 0.0, 0.0, scale, 0.0, anchor.y * (1.0 - scale)};
}

RpeNoiseRuntime::Prefixes prefixes(const RpeNoiseTrack& track, Transform type,
    RpeNoisePoint center) {
  RpeNoiseRuntime::Prefixes result;
  auto matrix = RpeNoiseMatrix{};
  const RpeNoiseEvent* previous = nullptr;
  for (const auto& event : track.events) {
    const double baseline = event.hasTransformStart ? event.transformStart : event.start;
    if (previous != nullptr) {
      const double endpoint = previous->hasTransformEnd ? previous->transformEnd : previous->end;
      const auto anchor = previous->hasAnchor
          ? RpeNoisePoint{previous->anchorX, previous->anchorY} : center;
      matrix = compose(relative(type, previous->hasTransformStart
          ? previous->transformStart : previous->start, endpoint, anchor), matrix);
      matrix = compose(relative(type, endpoint, baseline, anchor), matrix);
    }
    result[&event] = matrix;
    previous = &event;
  }
  return result;
}

struct TrackValue {
  double value{};
  const RpeNoiseEvent* event{};
};

TrackValue trackValue(const RpeNoiseTrack& track, double seconds, double fallback) {
  const RpeNoiseEvent* selected = nullptr;
  for (const auto& event : track.events) {
    if (seconds + EPSILON < event.startTime) break;
    selected = &event;
  }
  if (selected == nullptr) return {fallback, nullptr};
  const double duration = selected->endTime - selected->startTime;
  const double progress = duration <= EPSILON ? 1.0
      : std::clamp((seconds - selected->startTime) / duration, 0.0, 1.0);
  const double value = selected->start +
      (selected->end - selected->start) * EventUtils::applyEasing(progress, *selected);
  return {std::isfinite(value) ? value : fallback, selected};
}

RpeNoisePoint anchorOf(const RpeNoiseEvent* event, RpeNoisePoint center) {
  return event != nullptr && event->hasAnchor
      ? RpeNoisePoint{event->anchorX, event->anchorY} : center;
}

} // namespace

RpeNoiseRuntime::RpeNoiseRuntime(const RpeNoiseArea& area) : area_(&area) {
  center_ = {(area.left + area.right) * 0.5, (area.bottom + area.top) * 0.5};
  for (const auto& interval : area.intervals) activations_.push_back(interval);
  allIntervalsActive_ = std::all_of(activations_.begin(), activations_.end(),
      [](const auto& interval) { return interval.end > interval.start; });
  if (!allIntervalsActive_) {
    std::erase_if(activations_, [](const auto& interval) { return interval.end <= interval.start; });
  }
  rotatePrefix_ = prefixes(area.rotate, Transform::Rotate, center_);
  scaleXPrefix_ = prefixes(area.scaleX, Transform::ScaleX, center_);
  scaleYPrefix_ = prefixes(area.scaleY, Transform::ScaleY, center_);
}

RpeNoiseSample RpeNoiseRuntime::sample(double seconds) const noexcept {
  RpeNoiseSample sample;
  const RpeNoiseInterval* active = nullptr;
  for (const auto& interval : activations_) {
    if (seconds + EPSILON < interval.start) break;
    active = &interval;
  }
  const RpeNoiseInterval* next = nullptr;
  if (allIntervalsActive_) {
    for (const auto& interval : activations_) {
      if (seconds + EPSILON < interval.start) {
        next = &interval;
        break;
      }
    }
  } else {
    for (const auto& interval : area_->intervals) {
      if (seconds + EPSILON < interval.start) {
        next = &interval;
        break;
      }
    }
  }
  if (active != nullptr && seconds + EPSILON < active->end) sample.state = RpeNoiseState::Active;
  else if (next != nullptr && next->readyFlash > 0.0 &&
      seconds + EPSILON >= next->start - next->readyFlash) sample.state = RpeNoiseState::Ready;
  if (seconds + EPSILON < area_->appear) sample.state = RpeNoiseState::HiddenBefore;
  if (seconds + EPSILON >= area_->disappear) sample.state = RpeNoiseState::HiddenAfter;
  double phase = 0.0;
  if (next != nullptr && next->readyFlash > 0.0) {
    phase = std::clamp((seconds - next->start + next->readyFlash) / next->readyFlash, 0.0, 1.0);
  }
  if (sample.state == RpeNoiseState::Active) sample.activationMix = 1.0;
  else if (sample.state == RpeNoiseState::Ready) {
    sample.activationMix = (1.0 + std::cos(phase * PI * 6.0)) * 0.5;
  }
  sample.alpha = 0.4 + (0.667 - 0.4) * sample.activationMix;

  const auto moveX = trackValue(area_->moveX, seconds, center_.x);
  const auto moveY = trackValue(area_->moveY, seconds, center_.y);
  const auto rotation = trackValue(area_->rotate, seconds, 0.0);
  const auto scaleX = trackValue(area_->scaleX, seconds, 1.0);
  const auto scaleY = trackValue(area_->scaleY, seconds, 1.0);
  sample.rotation = rotation.value * PI / 180.0;
  sample.scaleX = scaleX.value;
  sample.scaleY = scaleY.value;
  const auto accumulate = [&](const RpeNoiseEvent* event, double value, const Prefixes& history,
      Transform type, RpeNoisePoint point) {
    if (event == nullptr) return point;
    const auto found = history.find(event);
    if (found != history.end()) point = apply(found->second, point);
    const double start = event->hasTransformStart ? event->transformStart : event->start;
    return apply(relative(type, start, value, anchorOf(event, center_)), point);
  };
  const auto transformedCenter = accumulate(rotation.event, rotation.value, rotatePrefix_,
      Transform::Rotate, accumulate(scaleY.event, scaleY.value, scaleYPrefix_, Transform::ScaleY,
          accumulate(scaleX.event, scaleX.value, scaleXPrefix_, Transform::ScaleX, center_)));
  const double moveXValue = moveX.value - center_.x;
  const double moveYValue = moveY.value - center_.y;
  const double cosine = std::cos(sample.rotation);
  const double sine = std::sin(sample.rotation);
  const auto transform = [&](RpeNoisePoint point) {
    const double x = scaleX.value * (point.x - center_.x);
    const double y = scaleY.value * (point.y - center_.y);
    return RpeNoisePoint{transformedCenter.x + cosine * x + sine * y + moveXValue,
        transformedCenter.y - sine * x + cosine * y + moveYValue};
  };
  sample.points = {transform({area_->left, area_->bottom}), transform({area_->right, area_->bottom}),
      transform({area_->right, area_->top}), transform({area_->left, area_->top})};
  sample.center = transform(center_);
  return sample;
}

} // namespace rechigros::game
