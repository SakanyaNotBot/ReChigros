#include "rechigros/game/OfficialEase.hpp"

#include "rechigros/game/Easing.hpp"

#include <cmath>

namespace rechigros::game::OfficialEase {
namespace {
constexpr int TWEEN_BY_OFFICIAL[16] = {
    2,   // 0  Linear
    3,   // 1  InSine
    4,   // 2  OutSine
    5,   // 3  InOutSine
    6,   // 4  InQuad
    7,   // 5  OutQuad
    8,   // 6  InOutQuad
    9,   // 7  InCubic
    10,  // 8  OutCubic
    11,  // 9  InOutCubic
    12,  // 10 InQuart
    13,  // 11 OutQuart
    14,  // 12 InOutQuart
    0,   // 13 Zero
    1,   // 14 One
    2,   // 15 AnimationCurve (linear fallback)
};

double sample(double a, double b, double c, double u) { return ((a * u + b) * u + c) * u; }
double derivativeX(double a, double b, double c, double u) { return (3.0 * a * u + 2.0 * b) * u + c; }

// Cubic Bezier with P0=(0,0), P3=(1,1): Newton refinement then bisection fallback.
double bezier01(double x1, double y1, double x2, double y2, double x) {
  const double cx = 3.0 * x1;
  const double bx = 3.0 * (x2 - x1) - cx;
  const double ax = 1.0 - cx - bx;
  const double cy = 3.0 * y1;
  const double by = 3.0 * (y2 - y1) - cy;
  const double ay = 1.0 - cy - by;

  double u = x;
  for (int i = 0; i < 8; ++i) {
    const double error = sample(ax, bx, cx, u) - x;
    if (std::abs(error) < 1e-6) return sample(ay, by, cy, u);
    const double slope = derivativeX(ax, bx, cx, u);
    if (std::abs(slope) < 1e-6) break;
    u -= error / slope;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
  }
  double low = 0.0;
  double high = 1.0;
  u = x;
  for (int i = 0; i < 24; ++i) {
    const double error = sample(ax, bx, cx, u) - x;
    if (std::abs(error) < 1e-6) break;
    if (error > 0.0) high = u;
    else low = u;
    u = (low + high) * 0.5;
  }
  return sample(ay, by, cy, u);
}
} // namespace

int toRpeEasing(int easeType) noexcept {
  switch (easeType) {
    case 1: return 2;
    case 2: return 1;
    case 3: return 5;
    case 4: return 4;
    case 5: return 3;
    case 6: return 6;
    case 7: return 8;
    case 8: return 7;
    case 9: return 11;
    case 10: return 10;
    case 11: return 9;
    case 12: return 12;
    default: return 0;
  }
}

double evaluate(int easeType, double t) noexcept {
  if (!std::isfinite(t)) return 0.0;
  if (t <= 0.0) return easeType == 14 ? 1.0 : 0.0;
  if (t >= 1.0) return easeType == 13 ? 0.0 : 1.0;
  auto index = easeType;
  if (index < 0 || index >= 16) index = 0;
  if (index == 13) return 0.0;
  if (index == 14) return 1.0;
  return Easing::applyTween(TWEEN_BY_OFFICIAL[index], t);
}

double evaluate(int easeType, double t, const std::array<float, 10>& key) noexcept {
  if (!std::isfinite(t)) return 0.0;
  if (t <= 0.0) return easeType == 14 ? 1.0 : 0.0;
  if (t >= 1.0) return easeType == 13 ? 0.0 : 1.0;
  if (easeType == 15 && key[6] > 0.0F) {
    return bezier01(key[6], static_cast<double>(key[7]) * key[6], 1.0 - key[8],
        1.0 - static_cast<double>(key[9]) * key[8], t);
  }
  return evaluate(easeType, t);
}
} // namespace rechigros::game::OfficialEase
