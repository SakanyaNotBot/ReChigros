#pragma once

#include <array>

namespace rechigros::game::OfficialEase {

// Official (StageScene) ease table. The shipped game bakes each curve into a 100-sample
// AnimationCurve; the analytic definitions below are equivalent to within one sample.
//
//  0 Liner   1 InSine   2 OutSine   3 InOutSine   4 InQuad   5 OutQuad   6 InOutQuad
//  7 InCubic 8 OutCubic 9 InOutCubic 10 InQuart  11 OutQuart 12 InOutQuart
// 13 Zero   14 One     15 AnimationCurve (per-key cubic, linear fallback)
enum class EaseType : int {
  Linear = 0,
  InSine = 1,
  OutSine = 2,
  InOutSine = 3,
  InQuad = 4,
  OutQuad = 5,
  InOutQuad = 6,
  InCubic = 7,
  OutCubic = 8,
  InOutCubic = 9,
  InQuart = 10,
  OutQuart = 11,
  InOutQuart = 12,
  Zero = 13,
  One = 14,
  AnimationCurve = 15,
};

// Maps an official ease type to the 0-based easing index used by Easing::apply. This is a
// lookup into the RPE table rather than an identity: the In/Out pairs are swapped relative
// to the official numbering, so a Linear segment must not be evaluated as OutSine.
int toRpeEasing(int easeType) noexcept;

// Evaluates an ease type at progress t, clamped to the unit domain.
double evaluate(int easeType, double t) noexcept;

// Evaluates an AnimationCurve key. `key` holds [.., outWeight0, outSlope0, inWeight1,
// inSlope1] at slots 6..9; the weighted Hermite is solved as a cubic Bezier with control
// points P1 = (w0, m0*w0) and P2 = (1 - w1, 1 - m1*w1).
double evaluate(int easeType, double t, const std::array<float, 10>& key) noexcept;

} // namespace rechigros::game::OfficialEase
