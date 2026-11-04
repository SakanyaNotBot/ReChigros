#include "rechigros/game/Easing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rechigros::game::Easing {
namespace {
constexpr double PI = 3.14159265358979323846;

double sineIn(double x) { return 1.0 - std::cos(x * PI / 2.0); }
double quadIn(double x) { return x * x; }
double cubicIn(double x) { return x * x * x; }
double quartIn(double x) { return x * x * x * x; }
double quintIn(double x) { return x * x * x * x * x; }

double expoIn(double x) { return std::pow(2.0, 10.0 * (x - 1.0)); }

double circIn(double x) { return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x)); }

double backIn(double x) {
  constexpr double C1 = 1.70158;
  constexpr double C3 = C1 + 1.0;
  return (C3 * x - C1) * x * x;
}

double elasticIn(double x) {
  constexpr double C4 = (2.0 * PI) / 3.0;
  return -(std::pow(2.0, 10.0 * x - 10.0) * std::sin((x * 10.0 - 10.75) * C4));
}

double bounceOut(double x) {
  constexpr double N1 = 7.5625;
  constexpr double D1 = 2.75;
  const double xm = 1.0 - x;
  double result;
  if (xm < 1.0 / D1) {
    result = N1 * xm * xm;
  } else if (xm < 2.0 / D1) {
    const double d = xm - 1.5 / D1;
    result = N1 * d * d + 0.75;
  } else if (xm < 2.5 / D1) {
    const double d = xm - 2.25 / D1;
    result = N1 * d * d + 0.9375;
  } else {
    const double d = xm - 2.625 / D1;
    result = N1 * d * d + 0.984375;
  }
  return 1.0 - result;
}

double bounceIn(double x) { return 1.0 - bounceOut(1.0 - x); }

template <typename In>
double sideOut(In in, double x) { return 1.0 - in(1.0 - x); }

template <typename In>
double sideInOut(In in, double x) {
  const double x2 = x * 2.0;
  if (x2 < 1.0) return in(x2) / 2.0;
  return 1.0 - in(2.0 - x2) / 2.0;
}

double intSineIn(double x) { return x - std::sin(x * PI / 2.0) * (2.0 / PI); }
double intQuadIn(double x) { return x * x * x / 3.0; }
double intCubicIn(double x) { return x * x * x * x / 4.0; }
double intQuartIn(double x) { return x * x * x * x * x / 5.0; }
double intQuintIn(double x) { return x * x * x * x * x * x / 6.0; }

double intExpoIn(double x) {
  const double ln2 = std::log(2.0);
  return (std::pow(2.0, 10.0 * x - 10.0) - std::pow(2.0, -10.0)) / (10.0 * ln2);
}

double intCircIn(double x) { return x - 0.5 * (x * std::sqrt(std::max(0.0, 1.0 - x * x)) + std::asin(x)); }

double intBackIn(double x) {
  constexpr double C1 = 1.70158;
  constexpr double C3 = C1 + 1.0;
  return (C3 * x / 4.0 - C1 / 3.0) * x * x * x;
}

double elasticFAntideriv(double x) {
  constexpr double C4 = (2.0 * PI) / 3.0;
  const double a = std::log(2.0);
  const double b = C4;
  const double u = 10.0 * x - 10.0;
  const double v = (x * 10.0 - 10.75) * b;
  return -(std::pow(2.0, u) / (10.0 * (a * a + b * b))) * (a * std::sin(v) - b * std::cos(v));
}

double intElasticIn(double x) { return elasticFAntideriv(x) - elasticFAntideriv(0.0); }

double bounceHAntideriv(double u) {
  constexpr double N1 = 7.5625;
  constexpr double D1 = 2.75;
  const double end1 = 1.0 / D1;
  const double val1 = N1 / 3.0 * end1 * end1 * end1;

  const double end2 = 2.0 / D1;
  const double c2 = val1 - (N1 / 3.0 * std::pow(end1 - 1.5 / D1, 3) + 0.75 * end1);
  const double val2 = N1 / 3.0 * std::pow(end2 - 1.5 / D1, 3) + 0.75 * end2 + c2;

  const double end3 = 2.5 / D1;
  const double c3 = val2 - (N1 / 3.0 * std::pow(end2 - 2.25 / D1, 3) + 0.9375 * end2);
  const double val3 = N1 / 3.0 * std::pow(end3 - 2.25 / D1, 3) + 0.9375 * end3 + c3;

  if (u < end1) {
    return N1 / 3.0 * u * u * u;
  }
  if (u < end2) {
    return N1 / 3.0 * std::pow(u - 1.5 / D1, 3) + 0.75 * u + c2;
  }
  if (u < end3) {
    return N1 / 3.0 * std::pow(u - 2.25 / D1, 3) + 0.9375 * u + c3;
  }
  const double c4 = val3 - (N1 / 3.0 * std::pow(end3 - 2.625 / D1, 3) + 0.984375 * end3);
  return N1 / 3.0 * std::pow(u - 2.625 / D1, 3) + 0.984375 * u + c4;
}

double intBounceIn(double x) { return x - bounceHAntideriv(1.0) + bounceHAntideriv(1.0 - x); }

template <typename In>
double intSideOut(In inInt, double x) { return x + inInt(1.0 - x) - inInt(1.0); }

template <typename In>
double intSideInOut(In inInt, double x) {
  const double x2 = x * 2.0;
  if (x2 < 1.0) return inInt(x2) / 4.0;
  return x - 0.5 + inInt(2.0 - x2) / 4.0;
}

double bezierSampleX(double a1, double a2, double t) {
  const double u = 1.0 - t;
  return 3.0 * u * u * t * a1 + 3.0 * u * t * t * a2 + t * t * t;
}

} // namespace

double clamp01(double value) noexcept {
  if (!std::isfinite(value)) return 0.0;
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

double applyTween(int tweenId, double x) noexcept {
  x = clamp01(x);
  if (tweenId <= 0) return 0.0;
  if (tweenId == 1) return 1.0;
  if (tweenId == 2) return x;
  switch (tweenId) {
    case 3: return sineIn(x);
    case 4: return sideOut(sineIn, x);
    case 5: return sideInOut(sineIn, x);
    case 6: return quadIn(x);
    case 7: return sideOut(quadIn, x);
    case 8: return sideInOut(quadIn, x);
    case 9: return cubicIn(x);
    case 10: return sideOut(cubicIn, x);
    case 11: return sideInOut(cubicIn, x);
    case 12: return quartIn(x);
    case 13: return sideOut(quartIn, x);
    case 14: return sideInOut(quartIn, x);
    case 15: return quintIn(x);
    case 16: return sideOut(quintIn, x);
    case 17: return sideInOut(quintIn, x);
    case 18: return expoIn(x);
    case 19: return sideOut(expoIn, x);
    case 20: return sideInOut(expoIn, x);
    case 21: return circIn(x);
    case 22: return sideOut(circIn, x);
    case 23: return sideInOut(circIn, x);
    case 24: return backIn(x);
    case 25: return sideOut(backIn, x);
    case 26: return sideInOut(backIn, x);
    case 27: return elasticIn(x);
    case 28: return sideOut(elasticIn, x);
    case 29: return sideInOut(elasticIn, x);
    case 30: return bounceIn(x);
    case 31: return bounceOut(x);
    case 32: return sideInOut(bounceIn, x);
    default: return x;
  }
}

double applyTweenInt(int tweenId, double x) noexcept {
  x = clamp01(x);
  if (tweenId <= 0) return 0.0;
  if (tweenId == 1) return x;
  if (tweenId == 2) return x * x / 2.0;
  switch (tweenId) {
    case 3: return intSineIn(x);
    case 4: return intSideOut(intSineIn, x);
    case 5: return intSideInOut(intSineIn, x);
    case 6: return intQuadIn(x);
    case 7: return intSideOut(intQuadIn, x);
    case 8: return intSideInOut(intQuadIn, x);
    case 9: return intCubicIn(x);
    case 10: return intSideOut(intCubicIn, x);
    case 11: return intSideInOut(intCubicIn, x);
    case 12: return intQuartIn(x);
    case 13: return intSideOut(intQuartIn, x);
    case 14: return intSideInOut(intQuartIn, x);
    case 15: return intQuintIn(x);
    case 16: return intSideOut(intQuintIn, x);
    case 17: return intSideInOut(intQuintIn, x);
    case 18: return intExpoIn(x);
    case 19: return intSideOut(intExpoIn, x);
    case 20: return intSideInOut(intExpoIn, x);
    case 21: return intCircIn(x);
    case 22: return intSideOut(intCircIn, x);
    case 23: return intSideInOut(intCircIn, x);
    case 24: return intBackIn(x);
    case 25: return intSideOut(intBackIn, x);
    case 26: return intSideInOut(intBackIn, x);
    case 27: return intElasticIn(x);
    case 28: return intSideOut(intElasticIn, x);
    case 29: return intSideInOut(intElasticIn, x);
    case 30: return intBounceIn(x);
    case 31: return intSideOut(intBounceIn, x);
    case 32: return intSideInOut(intBounceIn, x);
    default: return x * x / 2.0;
  }
}

namespace {
constexpr int RPE_TWEEN_MAP[30] = {
    2, 2, 4, 3, 7, 6, 5, 8, 10, 9, 13, 12, 11, 14, 16, 15, 19, 18, 22, 21, 25, 24, 23, 26, 28, 27, 31, 30, 32, 29,
};
} // namespace

int rpeRawEasingToTweenId(int easingType) noexcept {
  auto index = std::max(1, easingType);
  if (index >= 30) index = 0;
  return RPE_TWEEN_MAP[index];
}

int rpeEasingToTweenId(int easingType) noexcept {
  auto index = easingType + 1;
  if (index < 0) index = 0;
  if (index >= 30) index = 0;
  return RPE_TWEEN_MAP[index];
}

double apply(int easingType, double progress) noexcept {
  return applyTween(rpeEasingToTweenId(easingType), progress);
}

double clampedTween(int tweenId, double progress, double left, double right) noexcept {
  progress = clamp01(progress);
  left = clamp01(left);
  right = clamp01(right);
  if (tweenId <= 2 || (std::abs(left) < 1e-5 && std::abs(right - 1.0) < 1e-5)) {
    return applyTween(tweenId, progress);
  }
  if (std::abs(left - right) < 1e-12) return progress;
  const double arg = left + (right - left) * progress;
  const double easedArg = applyTween(tweenId, arg);
  const double easedLeft = applyTween(tweenId, left);
  const double easedRight = applyTween(tweenId, right);
  const double denominator = easedRight - easedLeft;
  if (std::abs(denominator) < 1e-12) return progress;
  return (easedArg - easedLeft) / denominator;
}

double intClampedTween(int tweenId, double progress, double left, double right) noexcept {
  progress = clamp01(progress);
  left = clamp01(left);
  right = clamp01(right);
  const double easedLeft = applyTween(tweenId, left);
  const double easedRight = applyTween(tweenId, right);
  const double base = applyTweenInt(tweenId, left);
  const double denominator = easedRight - easedLeft;
  if (!std::isfinite(denominator) || std::abs(denominator) < 1e-8) return progress * progress / 2.0;
  const double arg = left + (right - left) * progress;
  const double integral = applyTweenInt(tweenId, arg) - base - easedLeft * (arg - left);
  const double scale = (right - left) * denominator;
  if (std::abs(scale) < 1e-12) return progress * progress / 2.0;
  return integral / scale;
}

double cubicBezierYForX(double x, double x1, double y1, double x2, double y2) noexcept {
  x = clamp01(x);
  if (!std::isfinite(x1)) x1 = 0.0;
  if (!std::isfinite(y1)) y1 = 0.0;
  if (!std::isfinite(x2)) x2 = 1.0;
  if (!std::isfinite(y2)) y2 = 1.0;
  if (x <= 0.0) return 0.0;
  if (x >= 1.0) return 1.0;
  if (std::abs(x1 - y1) < 1e-9 && std::abs(x2 - y2) < 1e-9) return x;

  double parameter = 0.0;
  if (x1 == 0.0 && x2 == 0.0) {
    parameter = std::cbrt(x);
  } else if (x1 == 1.0 && x2 == 1.0) {
    parameter = 1.0 - std::cbrt(1.0 - x);
  } else {
    const bool reverse = x > 0.5;
    const double target = reverse ? 1.0 - x : x;
    const double first = reverse ? 1.0 - x2 : x1;
    const double second = reverse ? 1.0 - x1 : x2;
    double low = 0.0;
    double high = 1.0;
    for (int iteration = 0; iteration < 40; ++iteration) {
      const double middle = (low + high) * 0.5;
      if (bezierSampleX(first, second, middle) < target) low = middle;
      else high = middle;
    }
    parameter = (low + high) * 0.5;
    if (reverse) parameter = 1.0 - parameter;
  }
  return parameter > 0.5
      ? 1.0 - bezierSampleX(1.0 - y2, 1.0 - y1, 1.0 - parameter)
      : bezierSampleX(y1, y2, parameter);
}

std::vector<float> buildBezierLut(const std::array<float, 4>& points, int size) {
  return buildBezierLut(points[0], points[1], points[2], points[3], size);
}

std::vector<float> buildBezierLut(float x1, float y1, float x2, float y2, int size) {
  if (size < 2) size = BEZIER_LUT_SIZE;
  std::vector<float> lut(static_cast<std::size_t>(size));
  const double step = 1.0 / (size - 1);
  for (int i = 0; i < size; ++i) {
    lut[static_cast<std::size_t>(i)] = static_cast<float>(
        cubicBezierYForX(i * step, static_cast<double>(x1), static_cast<double>(y1),
            static_cast<double>(x2), static_cast<double>(y2)));
  }
  return lut;
}

double lookupBezierLut(const std::vector<float>& lut, double progress) noexcept {
  if (lut.size() < 2) return progress;
  progress = clamp01(progress);
  const double index = progress * static_cast<double>(lut.size() - 1);
  auto low = static_cast<std::ptrdiff_t>(std::floor(index));
  auto high = low + 1;
  if (low < 0) { low = 0; high = 1; }
  if (high >= static_cast<std::ptrdiff_t>(lut.size())) {
    high = static_cast<std::ptrdiff_t>(lut.size()) - 1;
    low = high - 1;
  }
  const double fraction = index - static_cast<double>(low);
  const auto a = lut[static_cast<std::size_t>(low)];
  const auto b = lut[static_cast<std::size_t>(high)];
  return a + (b - a) * fraction;
}
} // namespace rechigros::game::Easing
