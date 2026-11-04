#include "rechigros/game/OfficialBlock.hpp"

#include "rechigros/game/OfficialEase.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::game {
namespace {
constexpr float MIN_SCALE = 1e-4F;

float clamp01(float value) noexcept {
  if (value < 0.0F) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}

// A ~zero denominator yields 1, matching the reference's guard.
float safeDiv(float numerator, float denominator) noexcept {
  if (std::abs(denominator) > 1e-4F * std::abs(denominator) + 1e-38F) return numerator / denominator;
  return 1.0F;
}

float easedProgress(float now, float currentTime, float nextTime, int easeType) noexcept {
  const auto span = nextTime - currentTime;
  if (std::abs(span) < 1e-6F) return 1.0F;
  return static_cast<float>(OfficialEase::evaluate(easeType, (now - currentTime) / span));
}

// Index of the last event whose time is <= t, or -1 when t precedes every event.
template <typename Event>
int findEventIndex(const std::vector<Event>& events, float time) noexcept {
  int index = -1;
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (events[i].time <= time) index = static_cast<int>(i);
    else break;
  }
  return index;
}
} // namespace

void OfficialBlock::rotateAround(std::array<float, 2>& out, float cx, float cy, float anchorPctX,
    float anchorPctY, float deltaDeg, float screenW, float screenH) const noexcept {
  const auto ax = (anchorPctX - 0.5F) * screenW;
  const auto ay = (anchorPctY - 0.5F) * screenH;
  const auto radians = static_cast<double>(deltaDeg) * 3.14159265358979323846 / 180.0;
  const auto cosine = static_cast<float>(std::cos(radians));
  const auto sine = static_cast<float>(std::sin(radians));
  const auto dx = cx - ax;
  const auto dy = cy - ay;
  out[0] = ax + dx * cosine - dy * sine;
  out[1] = ay + dx * sine + dy * cosine;
}

std::array<float, 2> OfficialBlock::evalMove(int index, float screenW, float screenH) const noexcept {
  const auto& current = moveEvents[static_cast<std::size_t>(index)];
  float x{};
  float y{};
  if (index >= static_cast<int>(moveEvents.size()) - 1) {
    x = current.posX;
    y = current.posY;
  } else {
    const auto& next = moveEvents[static_cast<std::size_t>(index) + 1];
    const auto px = clamp01(easedProgress(timeNow_, current.time, next.time, current.easeTypeX));
    const auto py = clamp01(easedProgress(timeNow_, current.time, next.time, current.easeTypeY));
    x = current.posX + (next.posX - current.posX) * px;
    y = current.posY + (next.posY - current.posY) * py;
  }
  return {(x - 0.5F) * screenW, (y - 0.5F) * screenH};
}

void OfficialBlock::update(float t, float stageX, float stageY, float screenW, float screenH,
    float deltaTimeSec) {
  timeNow_ = t;
  visible = t >= appearTime && t < disappearTime;
  active = t >= enableTime && t < disableTime;

  // The disabled-fade coroutine starts on the invisible -> visible edge while inactive.
  if (visible && !wasVisible_ && !active) {
    showStarted = true;
    showElapsedSec_ = 0.0F;
  } else if (!visible) {
    showStarted = false;
    showElapsedSec_ = 0.0F;
  } else if (showStarted && showElapsedSec_ < DISABLED_SHOW_DURATION) {
    showElapsedSec_ += deltaTimeSec;
  }
  wasVisible_ = visible;

  // Geometry is resolved in a y-up, centre-origin world space and converted to y-down
  // screen pixels only at the end, so the CCW-positive rotation keeps its sign.
  const auto w0x = (bottomLeftX - 0.5F) * screenW;
  const auto w0y = (bottomLeftY - 0.5F) * screenH;
  const auto w1x = (topRightX - 0.5F) * screenW;
  const auto w1y = (topRightY - 0.5F) * screenH;
  auto cx = (w0x + w1x) * 0.5F;
  auto cy = (w0y + w1y) * 0.5F;
  const auto cx0 = cx;
  const auto cy0 = cy;
  auto sx = w1x - w0x;
  auto sy = w1y - w0y;
  float angle{};

  const auto scaleCount = static_cast<int>(scaleEvents.size());
  if (scaleCount > 0) {
    const auto index = findEventIndex(scaleEvents, t);
    if (index >= 0) {
      for (int i = 0; i < index; ++i) {
        const auto& current = scaleEvents[static_cast<std::size_t>(i)];
        const auto& next = scaleEvents[static_cast<std::size_t>(i) + 1];
        const auto rx = safeDiv(next.scaleX, current.scaleX);
        const auto ry = safeDiv(next.scaleY, current.scaleY);
        const auto ax = (current.anchorX - 0.5F) * screenW;
        const auto ay = (current.anchorY - 0.5F) * screenH;
        cx = ax + (cx - ax) * rx;
        cy = ay + (cy - ay) * ry;
      }
      const auto& current = scaleEvents[static_cast<std::size_t>(index)];
      float innerScaleX{};
      float innerScaleY{};
      if (index < scaleCount - 1) {
        const auto& next = scaleEvents[static_cast<std::size_t>(index) + 1];
        const auto px = clamp01(easedProgress(t, current.time, next.time, current.easeTypeX));
        const auto py = clamp01(easedProgress(t, current.time, next.time, current.easeTypeY));
        innerScaleX = current.scaleX + (next.scaleX - current.scaleX) * px;
        innerScaleY = current.scaleY + (next.scaleY - current.scaleY) * py;
        const auto rx = safeDiv(innerScaleX, current.scaleX);
        const auto ry = safeDiv(innerScaleY, current.scaleY);
        const auto ax = (current.anchorX - 0.5F) * screenW;
        const auto ay = (current.anchorY - 0.5F) * screenH;
        cx = ax + (cx - ax) * rx;
        cy = ay + (cy - ay) * ry;
      } else {
        innerScaleX = current.scaleX;
        innerScaleY = current.scaleY;
      }
      sx = innerScaleX * sx;
      sy = innerScaleY * sy;
    }
  }

  const auto rotateCount = static_cast<int>(rotateEvents.size());
  if (rotateCount > 0) {
    const auto index = findEventIndex(rotateEvents, t);
    if (index >= 0) {
      std::array<float, 2> moved{};
      for (int i = 0; i < index; ++i) {
        const auto& current = rotateEvents[static_cast<std::size_t>(i)];
        const auto& next = rotateEvents[static_cast<std::size_t>(i) + 1];
        rotateAround(moved, cx, cy, current.anchorX, current.anchorY, next.rotation - current.rotation,
            screenW, screenH);
        cx = moved[0];
        cy = moved[1];
      }
      const auto& current = rotateEvents[static_cast<std::size_t>(index)];
      if (index < rotateCount - 1) {
        const auto& next = rotateEvents[static_cast<std::size_t>(index) + 1];
        const auto p = clamp01(easedProgress(t, current.time, next.time, current.easeType));
        const auto interpolated = current.rotation + (next.rotation - current.rotation) * p;
        rotateAround(moved, cx, cy, current.anchorX, current.anchorY, interpolated - current.rotation,
            screenW, screenH);
        cx = moved[0];
        cy = moved[1];
        angle = interpolated;
      } else {
        angle = current.rotation;
      }
    }
  }

  if (!moveEvents.empty()) {
    const auto index = findEventIndex(moveEvents, t);
    if (index >= 0) {
      const auto target = evalMove(index, screenW, screenH);
      cx += target[0] - cx0;
      cy += target[1] - cy0;
    }
  }

  // World (stage-local, y up) -> screen pixels (y down).
  centerX = stageX + screenW * 0.5F + cx;
  centerY = stageY + screenH * 0.5F - cy;
  halfW = std::abs(sx) * 0.5F;
  halfH = std::abs(sy) * 0.5F;
  // The official angle is CCW in y-up world space; the same visual rotation on a y-down
  // screen is produced by the negated matrix angle used by corners()/toLocal().
  rotationDeg = -angle;
  const auto radians = static_cast<double>(rotationDeg) * 3.14159265358979323846 / 180.0;
  cosRot_ = static_cast<float>(std::cos(radians));
  sinRot_ = static_cast<float>(std::sin(radians));
}

int OfficialBlock::phase(float t) const noexcept {
  if (active) return 2;
  if (t < enableTime && t >= enableTime - READY_LEAD_DURATION && t < disappearTime && t >= appearTime) {
    return 1;
  }
  return 0;
}

float OfficialBlock::showProgress() const noexcept {
  if (!showStarted) return 0.0F;
  return clamp01(showElapsedSec_ / DISABLED_SHOW_DURATION);
}

float OfficialBlock::showAlpha() const noexcept {
  const auto progress = showProgress();
  return isSubtract ? 0.1F * progress : progress;
}

void OfficialBlock::toLocal(float x, float y, std::array<float, 2>& out) const noexcept {
  const auto dx = x - centerX;
  const auto dy = y - centerY;
  const auto lx = dx * cosRot_ + dy * sinRot_;
  const auto ly = -dx * sinRot_ + dy * cosRot_;
  out[0] = halfW > 1e-6F ? lx / (2.0F * halfW) : 0.0F;
  out[1] = halfH > 1e-6F ? ly / (2.0F * halfH) : 0.0F;
}

bool OfficialBlock::containsOriginal(float x, float y) const noexcept {
  if (halfW < MIN_SCALE || halfH < MIN_SCALE) return false;
  std::array<float, 2> local{};
  toLocal(x, y, local);
  return std::abs(local[0]) <= 0.5F && std::abs(local[1]) <= 0.5F;
}

bool OfficialBlock::containsInset(float x, float y, float screenHeight) const noexcept {
  if (halfW < MIN_SCALE || halfH < MIN_SCALE) return false;
  const auto insetPx = TOUCH_INSET_SCREEN_HEIGHT_RATIO * std::max(screenHeight, 0.0F);
  const auto insetX = std::min(insetPx / (2.0F * halfW), MAX_TOUCH_INSET_LOCAL);
  const auto insetY = std::min(insetPx / (2.0F * halfH), MAX_TOUCH_INSET_LOCAL);
  const auto sign = isSubtract ? 1.0F : -1.0F;
  const auto halfX = 0.5F + insetX * sign;
  const auto halfY = 0.5F + insetY * sign;
  std::array<float, 2> local{};
  toLocal(x, y, local);
  return std::abs(local[0]) <= halfX && std::abs(local[1]) <= halfY;
}

bool OfficialBlock::tryGetBlockingBlock(const std::vector<OfficialBlock>& blocks, float x, float y,
    float screenHeight) {
  bool sawOriginalNormal = false;
  int insideOriginalSubtract = 0;
  int insideSubtract = 0;
  const OfficialBlock* firstNormal = nullptr;
  const OfficialBlock* firstSubtract = nullptr;

  for (const auto& block : blocks) {
    if (!block.active || !block.visible) continue;
    if (block.containsOriginal(x, y)) {
      if (block.isSubtract) ++insideOriginalSubtract;
      else sawOriginalNormal = true;
    }
    if (block.containsInset(x, y, screenHeight)) {
      if (block.isSubtract) {
        ++insideSubtract;
        if (!firstSubtract) firstSubtract = &block;
      } else if (!firstNormal) {
        firstNormal = &block;
      }
    }
  }

  // Parity: a subtract hole cancels the enclosing normal block.
  if (((sawOriginalNormal ? 1 : 0) ^ (insideOriginalSubtract & 1)) == 0) return false;
  const auto normalActive = firstNormal != nullptr;
  if ((insideSubtract & 1) == (normalActive ? 1 : 0)) return false;
  return normalActive || firstSubtract != nullptr;
}

void OfficialBlock::resetRuntime() noexcept {
  visible = false;
  active = false;
  wasVisible_ = false;
  showStarted = false;
  showElapsedSec_ = 0.0F;
}

void OfficialBlock::syncToChartTime(float t) {
  visible = t >= appearTime && t < disappearTime;
  active = t >= enableTime && t < disableTime;
  wasVisible_ = visible;
  if (visible) {
    showStarted = true;
    auto elapsed = t - appearTime;
    if (elapsed < 0.0F) elapsed = 0.0F;
    if (elapsed > DISABLED_SHOW_DURATION) elapsed = DISABLED_SHOW_DURATION;
    showElapsedSec_ = elapsed;
  } else {
    showStarted = false;
    showElapsedSec_ = 0.0F;
  }
}

void OfficialBlock::corners(std::array<float, 8>& out) const noexcept {
  constexpr float signX[4] = {-1.0F, -1.0F, 1.0F, 1.0F};
  constexpr float signY[4] = {-1.0F, 1.0F, -1.0F, 1.0F};
  for (int i = 0; i < 4; ++i) {
    const auto lx = signX[i] * halfW;
    const auto ly = signY[i] * halfH;
    out[static_cast<std::size_t>(i) * 2] = centerX + lx * cosRot_ - ly * sinRot_;
    out[static_cast<std::size_t>(i) * 2 + 1] = centerY + lx * sinRot_ + ly * cosRot_;
  }
}
} // namespace rechigros::game
