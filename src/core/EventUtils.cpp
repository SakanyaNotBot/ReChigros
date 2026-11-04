#include "rechigros/game/EventUtils.hpp"

#include "rechigros/game/Easing.hpp"
#include "rechigros/game/JudgeLine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace rechigros::game::EventUtils {
namespace {
constexpr double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();

bool hasBezierLut(const EasingSpec& event) { return event.bezierLut.size() >= 2; }
bool hasBezierPoints(const EasingSpec& event) { return event.bezierPoints.size() >= 4; }

bool containsEventTime(double time, double start, double end, bool lastEvent) {
  if (!std::isfinite(end)) end = start;
  if (lastEvent) return time <= end;
  return time < end;
}

bool eventStarted(double time, double start) { return !std::isfinite(start) || time >= start; }

bool laterLineEventStarted(double time, const std::vector<LineEvent>& events, std::size_t index) {
  return index + 1 < events.size() && eventStarted(time, events[index + 1].startTime);
}

bool laterMoveEventStarted(double time, const std::vector<MoveEvent>& events, std::size_t index) {
  return index + 1 < events.size() && eventStarted(time, events[index + 1].startTime);
}

bool laterColorEventStarted(double time, const std::vector<ColorEvent>& events, std::size_t index) {
  return index + 1 < events.size() && eventStarted(time, events[index + 1].startTime);
}

bool laterTextEventStarted(double time, const std::vector<TextEvent>& events, std::size_t index) {
  return index + 1 < events.size() && eventStarted(time, events[index + 1].startTime);
}

bool laterGifEventStarted(double time, const std::vector<GifEvent>& events, std::size_t index) {
  return index + 1 < events.size() && eventStarted(time, events[index + 1].startTime);
}

// Returns the index of the last event whose start time has been reached, or -1.
int findEvent(double time, const std::vector<LineEvent>& events) {
  int found = -1;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto start = events[i].startTime;
    if (std::isfinite(start) && time < start) break;
    found = static_cast<int>(i);
  }
  return found;
}

int findEvent(double time, const std::vector<SpeedEvent>& events) {
  int found = -1;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto start = events[i].startTime;
    if (std::isfinite(start) && time < start) break;
    found = static_cast<int>(i);
  }
  return found;
}

double applyEasingImpl(double progress, const EasingSpec& event) noexcept;

double evaluateLineEvent(double time, const LineEvent& event, double last) noexcept {
  const double duration = event.endTime - event.startTime;
  const double progress = duration <= 1e-9 ? 1.0 : (time - event.startTime) / duration;
  if (event.trajectory) {
    const auto value = event.trajectory->sample(progress);
    if (event.trajectoryAxis == 1) return value.y * event.trajectoryScale;
    if (event.trajectoryAxis == 2) return value.rotation * event.trajectoryScale;
    return value.x * event.trajectoryScale;
  }
  const double start = std::isfinite(event.start) ? event.start : last;
  const double end = std::isfinite(event.end) ? event.end : start;
  return start + (end - start) * applyEasingImpl(progress, event);
}

double applyEasingImpl(double progress, const EasingSpec& event) noexcept {
  if (!std::isfinite(progress)) return 0.0;
  auto p = Easing::clamp01(progress);
  if (event.bezier && (hasBezierLut(event) || hasBezierPoints(event))) {
    if (hasBezierPoints(event)) return Easing::cubicBezierYForX(p, event.bezierPoints[0],
        event.bezierPoints[1], event.bezierPoints[2], event.bezierPoints[3]);
    return Easing::lookupBezierLut(event.bezierLut, p);
  }
  const auto tweenId = Easing::rpeEasingToTweenId(event.type);
  auto left = std::isfinite(event.left) ? static_cast<double>(event.left) : 0.0;
  auto right = std::isfinite(event.right) ? static_cast<double>(event.right) : 1.0;
  left = Easing::clamp01(left);
  right = Easing::clamp01(right);
  if (tweenId <= 2 || (std::abs(left) < 1e-5 && std::abs(right - 1.0) < 1e-5)) {
    return Easing::applyTween(tweenId, p);
  }
  if (std::abs(left - right) < 1e-12) return p;
  return Easing::clampedTween(tweenId, p, left, right);
}

double applyRawRpeEasingImpl(double progress, const EasingSpec& event) noexcept {
  if (!std::isfinite(progress)) return 0.0;
  const auto p = Easing::clamp01(progress);
  if (event.bezier && (hasBezierLut(event) || hasBezierPoints(event))) {
    if (hasBezierPoints(event)) return Easing::cubicBezierYForX(p, event.bezierPoints[0],
        event.bezierPoints[1], event.bezierPoints[2], event.bezierPoints[3]);
    return Easing::lookupBezierLut(event.bezierLut, p);
  }
  const auto tweenId = Easing::rpeRawEasingToTweenId(event.type);
  auto left = std::isfinite(event.left) ? static_cast<double>(event.left) : 0.0;
  auto right = std::isfinite(event.right) ? static_cast<double>(event.right) : 1.0;
  left = Easing::clamp01(left);
  right = Easing::clamp01(right);
  if (tweenId <= 2 || (std::abs(left) < 1e-5 && std::abs(right - 1.0) < 1e-5)) {
    return Easing::applyTween(tweenId, p);
  }
  if (std::abs(left - right) < 1e-12) return p;
  return Easing::clampedTween(tweenId, p, left, right);
}

double integrateBezierEasing(SpeedEvent& event, double progress) {
  auto p = Easing::clamp01(progress);
  if (p <= 0.0) return 0.0;
  if (event.bezierLut.size() < 2 && hasBezierPoints(event)) {
    event.bezierLut = Easing::buildBezierLut(event.bezierPoints[0], event.bezierPoints[1],
        event.bezierPoints[2], event.bezierPoints[3]);
  }
  if (event.bezierLut.size() < 2) return p * p * 0.5;

  const auto n = static_cast<double>(event.bezierLut.size());
  const double step = 1.0 / (n - 1);
  const double position = p * (n - 1);
  auto full = static_cast<int>(std::floor(position));
  if (full >= static_cast<int>(n) - 1) full = static_cast<int>(n) - 1;
  double sum = 0.0;
  for (int i = 0; i < full; ++i) {
    sum += (static_cast<double>(event.bezierLut[static_cast<std::size_t>(i)])
        + static_cast<double>(event.bezierLut[static_cast<std::size_t>(i) + 1])) * 0.5 * step;
  }
  if (full < static_cast<int>(n) - 1) {
    const double fraction = position - full;
    if (fraction > 1e-12) {
      const double a = event.bezierLut[static_cast<std::size_t>(full)];
      const double b = a + (static_cast<double>(event.bezierLut[static_cast<std::size_t>(full) + 1]) - a) * fraction;
      sum += (a + b) * 0.5 * (fraction * step);
    }
  }
  return sum;
}

double computeFpInEvent(SpeedEvent& event, double time) {
  const double start = event.startTime;
  double end = event.endTime;
  if (!std::isfinite(end)) end = start;
  const double duration = end - start;
  double elapsed = time - start;
  if (elapsed < 0.0) elapsed = 0.0;

  if (duration <= 1e-9) return event.floorPosition;

  auto p = elapsed / duration;
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;

  const double v0 = event.value;
  const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;

  if (event.type == 0 || std::abs(v0 - v1) < 1e-6) return event.floorPosition + v0 * elapsed;

  if (event.speedK == 0.0 && event.speedB == 0.0) {
    const double acceleration = (v1 - v0) / duration;
    return event.floorPosition + v0 * elapsed + 0.5 * acceleration * elapsed * elapsed;
  }

  double integral;
  if (event.isBezier) {
    integral = integrateBezierEasing(event, p);
  } else if (event.isClamped) {
    integral = Easing::intClampedTween(event.tweenId, p, static_cast<double>(event.left),
        static_cast<double>(event.right));
  } else {
    integral = Easing::applyTweenInt(event.tweenId, p);
  }

  const double height = (integral * event.speedK + event.speedB * p) * duration;
  const double result = event.floorPosition + height;
  if (!std::isfinite(result)) {
    const double acceleration = (v1 - v0) / duration;
    return event.floorPosition + v0 * elapsed + 0.5 * acceleration * elapsed * elapsed;
  }
  return result;
}

double computeFpForCumulative(SpeedEvent& event, double time) {
  const double start = event.startTime;
  double end = event.endTime;
  if (!std::isfinite(end)) end = start;
  if (std::isfinite(start) && time < start) return event.floorPosition + event.value * (time - start);
  if (time <= end) return computeFpInEvent(event, time);
  const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;
  const double fpEnd = computeFpInEvent(event, end);
  double elapsed = time - end;
  if (elapsed < 0.0) elapsed = 0.0;
  return fpEnd + v1 * elapsed;
}

std::string replaceAll(const std::string& source, const std::string& from, const std::string& to) {
  if (from.empty()) return source;
  std::string result;
  std::size_t position = 0;
  while (true) {
    const auto found = source.find(from, position);
    if (found == std::string::npos) {
      result.append(source, position, std::string::npos);
      break;
    }
    result.append(source, position, found - position);
    result.append(to);
    position = found + from.size();
  }
  return result;
}

double parseDoubleOrNaN(const std::string& text) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed]))) ++consumed;
    if (consumed != text.size()) return NAN_VALUE;
    return value;
  } catch (...) {
    return NAN_VALUE;
  }
}

std::string formatThreeDecimals(double value) {
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  return buffer;
}

std::string formatInteger(double value) { return std::to_string(static_cast<long long>(value)); }
} // namespace

double lerp(double a, double b, double progress) noexcept { return a + (b - a) * progress; }

double applyEasing(double progress, const EasingSpec& event) noexcept {
  return applyEasingImpl(progress, event);
}
double applyEasing(double progress, const LineEvent& event) noexcept { return applyEasingImpl(progress, event); }
double applyEasing(double progress, const MoveEvent& event) noexcept { return applyEasingImpl(progress, event); }
double applyEasing(double progress, const ColorEvent& event) noexcept { return applyEasingImpl(progress, event); }

double getEventVal(double time, const std::vector<LineEvent>& events) noexcept {
  if (events.empty()) return 0.0;
  double last = 0.0;
  bool hasLast = false;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterLineEventStarted(time, events, i)) {
      const double b = std::isfinite(event.end) ? event.end : event.start;
      if (std::isfinite(b)) { last = b; hasLast = true; }
      continue;
    }
    if (containsEventTime(time, start, end, i == events.size() - 1)) {
      return evaluateLineEvent(time, event, last);
    }
    const double b = std::isfinite(event.end) ? event.end : event.start;
    if (std::isfinite(b)) { last = b; hasLast = true; }
  }
  return hasLast ? last : 0.0;
}

double getEventVal(double time, const std::vector<LineEvent>& events, EventCursor& cursor) noexcept {
  if (events.empty()) return 0.0;
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;

  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }

  double last = 0.0;
  bool hasLast = false;
  if (index > 0) {
    const auto& previous = events[static_cast<std::size_t>(index) - 1];
    const double b = std::isfinite(previous.end) ? previous.end : previous.start;
    if (std::isfinite(b)) { last = b; hasLast = true; }
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterLineEventStarted(time, events, static_cast<std::size_t>(i))) {
      const double b = std::isfinite(event.end) ? event.end : event.start;
      if (std::isfinite(b)) { last = b; hasLast = true; }
      continue;
    }
    if (containsEventTime(time, start, end, i == size - 1)) {
      return evaluateLineEvent(time, event, last);
    }
    const double b = std::isfinite(event.end) ? event.end : event.start;
    if (std::isfinite(b)) { last = b; hasLast = true; }
  }
  return hasLast ? last : 0.0;
}

double getMoveEventVal1(double time, const std::vector<MoveEvent>& events) noexcept {
  if (events.empty()) return NAN_VALUE;
  double last = NAN_VALUE;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterMoveEventStarted(time, events, i)) {
      const double b = std::isfinite(event.end) ? event.end : event.start;
      if (std::isfinite(b)) last = b;
      continue;
    }
    if (containsEventTime(time, start, end, i == events.size() - 1)) {
      const double a = std::isfinite(event.start) ? event.start : last;
      const double b = std::isfinite(event.end) ? event.end : a;
      const double duration = end - start;
      if (duration <= 1e-9) return b;
      return lerp(a, b, applyEasingImpl((time - start) / duration, event));
    }
    const double b = std::isfinite(event.end) ? event.end : event.start;
    if (std::isfinite(b)) last = b;
  }
  return last;
}

double getMoveEventVal1(double time, const std::vector<MoveEvent>& events, EventCursor& cursor) noexcept {
  if (events.empty()) return NAN_VALUE;
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;
  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }
  double last = NAN_VALUE;
  if (index > 0) {
    const auto& previous = events[static_cast<std::size_t>(index) - 1];
    const double b = std::isfinite(previous.end) ? previous.end : previous.start;
    if (std::isfinite(b)) last = b;
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterMoveEventStarted(time, events, static_cast<std::size_t>(i))) {
      const double b = std::isfinite(event.end) ? event.end : event.start;
      if (std::isfinite(b)) last = b;
      continue;
    }
    if (containsEventTime(time, start, end, i == size - 1)) {
      const double a = std::isfinite(event.start) ? event.start : last;
      const double b = std::isfinite(event.end) ? event.end : a;
      const double duration = end - start;
      if (duration <= 1e-9) return b;
      return lerp(a, b, applyEasingImpl((time - start) / duration, event));
    }
    const double b = std::isfinite(event.end) ? event.end : event.start;
    if (std::isfinite(b)) last = b;
  }
  return last;
}

double getMoveEventVal2(double time, const std::vector<MoveEvent>& events) noexcept {
  if (events.empty()) return NAN_VALUE;
  double last = NAN_VALUE;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterMoveEventStarted(time, events, i)) {
      const double b = std::isfinite(event.end2) ? event.end2 : event.start2;
      if (std::isfinite(b)) last = b;
      continue;
    }
    if (containsEventTime(time, start, end, i == events.size() - 1)) {
      const double a = std::isfinite(event.start2) ? event.start2 : last;
      const double b = std::isfinite(event.end2) ? event.end2 : a;
      const double duration = end - start;
      if (duration <= 1e-9) return b;
      return lerp(a, b, applyEasingImpl((time - start) / duration, event));
    }
    const double b = std::isfinite(event.end2) ? event.end2 : event.start2;
    if (std::isfinite(b)) last = b;
  }
  return last;
}

double getMoveEventVal2(double time, const std::vector<MoveEvent>& events, EventCursor& cursor) noexcept {
  if (events.empty()) return NAN_VALUE;
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;
  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }
  double last = NAN_VALUE;
  if (index > 0) {
    const auto& previous = events[static_cast<std::size_t>(index) - 1];
    const double b = std::isfinite(previous.end2) ? previous.end2 : previous.start2;
    if (std::isfinite(b)) last = b;
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterMoveEventStarted(time, events, static_cast<std::size_t>(i))) {
      const double b = std::isfinite(event.end2) ? event.end2 : event.start2;
      if (std::isfinite(b)) last = b;
      continue;
    }
    if (containsEventTime(time, start, end, i == size - 1)) {
      const double a = std::isfinite(event.start2) ? event.start2 : last;
      const double b = std::isfinite(event.end2) ? event.end2 : a;
      const double duration = end - start;
      if (duration <= 1e-9) return b;
      return lerp(a, b, applyEasingImpl((time - start) / duration, event));
    }
    const double b = std::isfinite(event.end2) ? event.end2 : event.start2;
    if (std::isfinite(b)) last = b;
  }
  return last;
}

bool getColorVal(double time, const std::vector<ColorEvent>& events, EventCursor& cursor,
    std::array<float, 3>& out) noexcept {
  if (events.empty()) return false;
  if (time < events.front().startTime) {
    out = {events.front().startR, events.front().startG, events.front().startB};
    return true;
  }
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;
  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }

  bool hasLast = false;
  float lastR = 0.0F;
  float lastG = 0.0F;
  float lastB = 0.0F;
  if (index > 0) {
    const auto& previous = events[static_cast<std::size_t>(index) - 1];
    lastR = previous.endR;
    lastG = previous.endG;
    lastB = previous.endB;
    hasLast = true;
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterColorEventStarted(time, events, static_cast<std::size_t>(i))) {
      lastR = event.endR; lastG = event.endG; lastB = event.endB;
      hasLast = true;
      continue;
    }
    if (containsEventTime(time, start, end, i == size - 1)) {
      const double duration = end - start;
      if (duration <= 1e-9) {
        out[0] = event.endR; out[1] = event.endG; out[2] = event.endB;
      } else {
        const double p = applyEasingImpl((time - start) / duration, event);
        out[0] = static_cast<float>(lerp(event.startR, event.endR, p));
        out[1] = static_cast<float>(lerp(event.startG, event.endG, p));
        out[2] = static_cast<float>(lerp(event.startB, event.endB, p));
      }
      return true;
    }
    lastR = event.endR; lastG = event.endG; lastB = event.endB;
    hasLast = true;
  }
  if (!hasLast) return false;
  out[0] = lastR; out[1] = lastG; out[2] = lastB;
  return true;
}

bool getColorVal(double time, const std::vector<ColorEvent>& events, std::array<float, 3>& out) noexcept {
  if (events.empty()) return false;
  if (time < events.front().startTime) {
    out = {events.front().startR, events.front().startG, events.front().startB};
    return true;
  }
  bool hasLast = false;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterColorEventStarted(time, events, i)) {
      out[0] = event.endR; out[1] = event.endG; out[2] = event.endB;
      hasLast = true;
      continue;
    }
    if (containsEventTime(time, start, end, i == events.size() - 1)) {
      const double duration = end - start;
      if (duration <= 1e-9) {
        out[0] = event.endR; out[1] = event.endG; out[2] = event.endB;
      } else {
        const double p = applyEasingImpl((time - start) / duration, event);
        out[0] = static_cast<float>(lerp(event.startR, event.endR, p));
        out[1] = static_cast<float>(lerp(event.startG, event.endG, p));
        out[2] = static_cast<float>(lerp(event.startB, event.endB, p));
      }
      return true;
    }
    out[0] = event.endR; out[1] = event.endG; out[2] = event.endB;
    hasLast = true;
  }
  return hasLast;
}

bool getTextVal(double time, const std::vector<TextEvent>& events, EventCursor& cursor,
    std::string& out) {
  if (events.empty()) return false;
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;
  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }

  bool hasLast = false;
  std::string last;
  if (index > 0) {
    const auto& previous = events[static_cast<std::size_t>(index) - 1];
    last = previous.hasEndText ? previous.endText : previous.text;
    hasLast = true;
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterTextEventStarted(time, events, static_cast<std::size_t>(i))) {
      last = event.hasEndText ? event.endText : event.text;
      hasLast = true;
      continue;
    }
    if (containsEventTime(time, start, end, i == size - 1)) {
      const std::string& startText = event.text;
      const std::string& endText = event.hasEndText ? event.endText : event.text;
      if (!event.hasEndText || endText == startText) {
        out = stripStaticTextMarkers(startText);
        return true;
      }
      const double duration = end - start;
      if (duration <= 1e-9) {
        out = stripStaticTextMarkers(endText);
        return true;
      }
      out = calculateTextValue(startText, endText, applyEasingImpl((time - start) / duration, event));
      return true;
    }
    last = event.hasEndText ? event.endText : event.text;
    hasLast = true;
  }
  if (!hasLast) return false;
  out = stripStaticTextMarkers(last);
  return true;
}

bool getTextVal(double time, const std::vector<TextEvent>& events, std::string& out) {
  if (events.empty()) return false;
  bool hasLast = false;
  std::string last;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto& event = events[i];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterTextEventStarted(time, events, i)) {
      last = event.hasEndText ? event.endText : event.text;
      hasLast = true;
      continue;
    }
    if (containsEventTime(time, start, end, i == events.size() - 1)) {
      const std::string& startText = event.text;
      const std::string& endText = event.hasEndText ? event.endText : event.text;
      if (!event.hasEndText || endText == startText) {
        out = stripStaticTextMarkers(startText);
        return true;
      }
      const double duration = end - start;
      if (duration <= 1e-9) {
        out = stripStaticTextMarkers(endText);
        return true;
      }
      out = calculateTextValue(startText, endText, applyEasingImpl((time - start) / duration, event));
      return true;
    }
    last = event.hasEndText ? event.endText : event.text;
    hasLast = true;
  }
  if (!hasLast) return false;
  out = stripStaticTextMarkers(last);
  return true;
}

float getGifVal(double time, const std::vector<GifEvent>& events, EventCursor& cursor) noexcept {
  if (events.empty()) return std::numeric_limits<float>::quiet_NaN();
  const auto size = static_cast<int>(events.size());
  int index = cursor.index;
  if (index < 0 || index >= size) index = 0;
  while (index < size - 1) {
    if (eventStarted(time, events[static_cast<std::size_t>(index) + 1].startTime)) ++index;
    else break;
  }
  while (index > 0) {
    const auto start = events[static_cast<std::size_t>(index)].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }
  for (int i = index; i < size; ++i) {
    const auto& event = events[static_cast<std::size_t>(i)];
    const double start = event.startTime;
    if (std::isfinite(start) && time < start) break;
    double end = event.endTime;
    if (!std::isfinite(end)) end = start;
    if (laterGifEventStarted(time, events, static_cast<std::size_t>(i))) continue;
    if (containsEventTime(time, start, end, i == size - 1)) {
      const double duration = end - start;
      if (duration <= 1e-9) return static_cast<float>(event.end);
      return static_cast<float>(lerp(event.start, event.end,
          applyEasingImpl((time - start) / duration, event)));
    }
  }
  return std::numeric_limits<float>::quiet_NaN();
}

std::string stripStaticTextMarkers(const std::string& text) {
  if (text.find("%P%") == std::string::npos && text.find("%T%") == std::string::npos) return text;
  if (text.find("%P%") != std::string::npos) {
    const std::string numeric = replaceAll(text, "%P%", "");
    const double value = parseDoubleOrNaN(numeric);
    if (std::isfinite(value)) {
      if (std::rint(value) == value) return formatInteger(value);
      return formatThreeDecimals(value);
    }
    return numeric;
  }
  return replaceAll(text, "%T%", "");
}

std::vector<std::string> utf8Characters(const std::string& text) {
  std::vector<std::string> result;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 1;
    if ((lead & 0xE0U) == 0xC0U) length = 2;
    else if ((lead & 0xF0U) == 0xE0U) length = 3;
    else if ((lead & 0xF8U) == 0xF0U) length = 4;
    length = std::min(length, text.size() - index);
    result.push_back(text.substr(index, length));
    index += length;
  }
  return result;
}

std::string joinCharacters(const std::vector<std::string>& characters, std::size_t begin,
    std::size_t end) {
  std::string result;
  for (auto index = begin; index < end && index < characters.size(); ++index) {
    result += characters[index];
  }
  return result;
}

std::string calculateTextValue(const std::string& start, const std::string& end, double progress) {
  if (start.find("%P%") != std::string::npos && end.find("%P%") != std::string::npos) {
    const double startNumeric = parseDoubleOrNaN(replaceAll(start, "%P%", ""));
    const double endNumeric = parseDoubleOrNaN(replaceAll(end, "%P%", ""));
    if (std::isfinite(startNumeric) && std::isfinite(endNumeric)) {
      const double value = startNumeric + (endNumeric - startNumeric) * progress;
      if (std::rint(startNumeric) == startNumeric && std::rint(endNumeric) == endNumeric) {
        return formatInteger(std::trunc(value));
      }
      return formatThreeDecimals(value);
    }
  }
  if (start.find("%T%") != std::string::npos && end.find("%T%") != std::string::npos) {
    const auto startBase = utf8Characters(replaceAll(start, "%T%", ""));
    const auto endBase = utf8Characters(replaceAll(end, "%T%", ""));
    if (!startBase.empty() || !endBase.empty()) {
      const auto maxLength = std::max(startBase.size(), endBase.size());
      const auto between = static_cast<std::size_t>(std::clamp(
          std::floor(static_cast<double>(maxLength) * progress), 0.0,
          static_cast<double>(maxLength)));
      std::string result;
      for (std::size_t index = 0; index < maxLength; ++index) {
        if (index < between) result += index < endBase.size() ? endBase[index] : " ";
        else result += index < startBase.size() ? startBase[index] : " ";
      }
      return result;
    }
  }
  const auto startsWith = [](const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
  };
  const auto startCharacters = utf8Characters(start);
  const auto endCharacters = utf8Characters(end);
  if (startsWith(start, end)) {
    const auto remaining = startCharacters.size() - endCharacters.size();
    const auto keep = static_cast<std::size_t>(std::clamp(
        std::floor(static_cast<double>(remaining) * (1.0 - progress)), 0.0,
        static_cast<double>(remaining)));
    return end + joinCharacters(startCharacters, endCharacters.size(), endCharacters.size() + keep);
  }
  if (startsWith(end, start)) {
    const auto remaining = endCharacters.size() - startCharacters.size();
    const auto reveal = static_cast<std::size_t>(std::clamp(
        std::floor(static_cast<double>(remaining) * progress), 0.0,
        static_cast<double>(remaining)));
    return start + joinCharacters(endCharacters, startCharacters.size(),
        startCharacters.size() + reveal);
  }
  return progress >= 1.0 ? end : start;
}

double speedAt(const std::vector<SpeedEvent>& events, double time) noexcept {
  if (events.empty()) return 1.0;
  const auto found = findEvent(time, events);
  if (found < 0) {
    if (time < events.front().startTime) return events.front().value;
    const SpeedEvent* previous = nullptr;
    for (const auto& event : events) {
      if (std::isfinite(event.startTime) && time < event.startTime) break;
      previous = &event;
    }
    if (previous) return std::isfinite(previous->endValue) ? previous->endValue : previous->value;
    return 1.0;
  }
  const auto& event = events[static_cast<std::size_t>(found)];
  const double duration = event.endTime - event.startTime;
  if (duration <= 1e-9) return std::isfinite(event.endValue) ? event.endValue : event.value;
  auto p = (time - event.startTime) / duration;
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  const double v0 = event.value;
  const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;
  if (event.type == 0) return v0;
  const bool bezier = event.bezier && (hasBezierLut(event) || hasBezierPoints(event));
  if (event.type <= 1 && !bezier) return v0 + (v1 - v0) * p;
  return v0 + (v1 - v0) * applyRawRpeEasingImpl(p, event);
}

void initSpeedEvents(std::vector<SpeedEvent>& events) {
  std::stable_sort(events.begin(), events.end(), [](const SpeedEvent& a, const SpeedEvent& b) {
    return a.startTime < b.startTime;
  });
  double floorPosition = 0.0;
  for (std::size_t i = 0; i < events.size(); ++i) {
    auto& event = events[i];
    event.floorPosition = floorPosition;
    double duration = event.endTime - event.startTime;
    if (!std::isfinite(duration) || duration < 0.0) duration = 0.0;
    const double v0 = event.value;
    const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;

    event.tweenId = Easing::rpeRawEasingToTweenId(event.type);
    event.isClamped = (event.left != 0.0F || event.right != 1.0F) && (event.left < event.right);
    event.isBezier = event.bezier && (hasBezierLut(event) || hasBezierPoints(event));

    if (duration <= 1e-9) {
      event.speedK = 0.0;
      event.speedB = v0;
      event.totalHeight = 0.0;
    } else if (event.type == 0 || std::abs(v0 - v1) < 1e-6) {
      event.speedK = 0.0;
      event.speedB = v0;
      event.totalHeight = v0;
    } else if (event.type <= 1 && !event.isBezier) {
      // A linear ramp has a closed-form integral that stays valid across zero. Expressing
      // it as a normalised easing curve needs the mean speed as a divisor, which is zero
      // for a symmetric ramp, so the ramp is integrated directly instead.
      event.speedK = v1 - v0;
      event.speedB = v0;
      event.totalHeight = (v0 + v1) * 0.5;
    } else {
      event.speedK = v1 - v0;
      event.speedB = v0;
      double integralEnd;
      if (event.isBezier) {
        integralEnd = integrateBezierEasing(event, 1.0);
      } else if (event.isClamped) {
        integralEnd = Easing::intClampedTween(event.tweenId, 1.0, static_cast<double>(event.left),
            static_cast<double>(event.right));
      } else {
        integralEnd = Easing::applyTweenInt(event.tweenId, 1.0);
      }
      const double total = integralEnd * event.speedK + event.speedB;
      if (!std::isfinite(total)) {
        // An unusable curve falls back to the straight-line average; with both coefficients
        // cleared the per-instant integral uses the quadratic form of that same ramp.
        event.speedK = 0.0;
        event.speedB = 0.0;
        event.totalHeight = (v0 + v1) * 0.5;
      } else {
        event.totalHeight = total;
      }
    }

    // The reference walks forward to the next event and uses its start time as the
    // integration endpoint, falling back to this event's end time when absent or
    // non-finite. The container has no null slots, so the first successor decides.
    double nextStart = event.endTime;
    if (i + 1 < events.size() && std::isfinite(events[i + 1].startTime)) {
      nextStart = events[i + 1].startTime;
    }
    const double nextFloor = computeFpForCumulative(event, nextStart);
    floorPosition = std::isfinite(nextFloor) ? nextFloor : event.floorPosition + event.totalHeight * duration;
  }
}

double getFloorPosition(double time, const std::vector<SpeedEvent>& events) noexcept {
  if (events.empty()) return 0.0;
  // The cumulative helpers need a mutable reference for lazy bezier LUT building, which
  // initSpeedEvents has already completed, so a const_cast is safe here.
  auto& mutableEvents = const_cast<std::vector<SpeedEvent>&>(events);

  int low = 0;
  int high = static_cast<int>(events.size()) - 1;
  int index = -1;
  while (low <= high) {
    const int middle = (low + high) / 2;
    const auto start = events[static_cast<std::size_t>(middle)].startTime;
    if (!std::isfinite(start) || time >= start) {
      index = middle;
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }

  if (index < 0) {
    const SpeedEvent* last = nullptr;
    for (const auto& event : events) {
      const double start = event.startTime;
      if (std::isfinite(start) && time < start) break;
      last = &event;
      double end = event.endTime;
      if (!std::isfinite(end)) end = start;
      if (time <= end) return computeFpInEvent(mutableEvents[static_cast<std::size_t>(&event - events.data())], time);
    }
    if (last) {
      const auto lastIndex = static_cast<std::size_t>(last - events.data());
      double end = last->endTime;
      if (!std::isfinite(end)) end = last->startTime;
      const double v1 = std::isfinite(last->endValue) ? last->endValue : last->value;
      const double fpEnd = last->floorPosition + last->totalHeight * (end - last->startTime);
      double elapsed = time - end;
      if (elapsed < 0.0) elapsed = 0.0;
      (void)lastIndex;
      return fpEnd + v1 * elapsed;
    }
    const auto& first = events.front();
    if (std::isfinite(first.startTime)) return first.floorPosition + first.value * (time - first.startTime);
    return 0.0;
  }

  auto& event = mutableEvents[static_cast<std::size_t>(index)];
  const double start = event.startTime;
  if (std::isfinite(start) && time < start) return event.floorPosition + event.value * (time - start);

  double end = event.endTime;
  if (!std::isfinite(end)) end = start;
  if (time <= end) return computeFpInEvent(event, time);

  const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;
  const double fpEnd = event.floorPosition + event.totalHeight * (end - start);
  double elapsed = time - end;
  if (elapsed < 0.0) elapsed = 0.0;
  return fpEnd + v1 * elapsed;
}

double getFloorPosition(double time, const std::vector<SpeedEvent>& events,
    const std::vector<std::vector<SpeedEvent>>& layers) noexcept {
  if (!layers.empty()) {
    double sum = 0.0;
    bool any = false;
    for (const auto& layer : layers) {
      if (layer.empty()) continue;
      sum += getFloorPosition(time, layer);
      any = true;
    }
    if (any) return sum;
  }
  return getFloorPosition(time, events);
}

double getFloorPosition(double time, const JudgeLine& line) noexcept {
  return getFloorPosition(time, line.speedEvents, line.speedEventLayers);
}

double floorPositionMonotonic(double time, const std::vector<SpeedEvent>& events, int& index) noexcept {
  if (events.empty()) return 0.0;
  auto& mutableEvents = const_cast<std::vector<SpeedEvent>&>(events);
  const auto size = static_cast<int>(events.size());
  int i = index;
  if (i < 0) i = 0;
  if (i >= size) i = size - 1;

  while (i > 0) {
    const auto start = events[static_cast<std::size_t>(i)].startTime;
    if (std::isfinite(start) && time < start) --i;
    else break;
  }
  while (i + 1 < size) {
    const auto start = events[static_cast<std::size_t>(i) + 1].startTime;
    if (!std::isfinite(start) || time >= start) ++i;
    else break;
  }
  index = i;

  auto& event = mutableEvents[static_cast<std::size_t>(i)];
  const double start = event.startTime;
  if (std::isfinite(start) && time < start) return event.floorPosition + event.value * (time - start);

  double end = event.endTime;
  if (!std::isfinite(end)) end = start;
  const double v1 = std::isfinite(event.endValue) ? event.endValue : event.value;
  if (time <= end) return computeFpInEvent(event, time);

  double duration = end - start;
  if (duration < 0.0) duration = 0.0;
  const double fpEnd = event.floorPosition + event.totalHeight * duration;
  double elapsed = time - end;
  if (elapsed < 0.0) elapsed = 0.0;
  return fpEnd + v1 * elapsed;
}

double floorPositionMonotonic(double time, const std::vector<SpeedEvent>& events,
    const std::vector<std::vector<SpeedEvent>>& layers, int& index,
    std::vector<int>& layerIndices) noexcept {
  if (!layers.empty()) {
    if (layerIndices.size() < layers.size()) layerIndices.resize(layers.size(), 0);
    double sum = 0.0;
    bool any = false;
    for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
      const auto& layer = layers[layerIndex];
      if (layer.empty()) continue;
      sum += floorPositionMonotonic(time, layer, layerIndices[layerIndex]);
      any = true;
    }
    if (any) return sum;
  }
  return floorPositionMonotonic(time, events, index);
}

} // namespace rechigros::game::EventUtils
