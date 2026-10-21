#pragma once

#include <array>
#include <vector>

namespace rechigros::game::Easing {
inline constexpr int BEZIER_LUT_SIZE = 128;

double clamp01(double value) noexcept;

double applyTween(int tweenId, double progress) noexcept;
double applyTweenInt(int tweenId, double progress) noexcept;

int rpeRawEasingToTweenId(int easingType) noexcept;
int rpeEasingToTweenId(int easingType) noexcept;
double apply(int easingType, double progress) noexcept;

double clampedTween(int tweenId, double progress, double left, double right) noexcept;
double intClampedTween(int tweenId, double progress, double left, double right) noexcept;

double cubicBezierYForX(double x, double x1, double y1, double x2, double y2) noexcept;
std::vector<float> buildBezierLut(const std::array<float, 4>& points, int size = BEZIER_LUT_SIZE);
std::vector<float> buildBezierLut(float x1, float y1, float x2, float y2, int size = BEZIER_LUT_SIZE);
double lookupBezierLut(const std::vector<float>& lut, double progress) noexcept;
}
