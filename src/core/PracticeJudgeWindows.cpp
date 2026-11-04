#include "rechigros/game/PracticeJudgeWindows.hpp"

#include <algorithm>

namespace rechigros::game {
namespace {

double clampValue(double value, double low, double high) noexcept {
  return value < low ? low : (value > high ? high : value);
}

} // namespace

void PracticeJudgeWindows::setPerfectMs(double value) noexcept {
  // The perfect window may not come within the minimum gap of the good window.
  const double ceiling = std::min(PERFECT_MAX_MS, goodMs_ - MIN_GAP_MS);
  perfectMs_ = clampValue(value, PERFECT_MIN_MS, ceiling);
  dirty_ = true;
}

void PracticeJudgeWindows::setGoodMs(double value) noexcept {
  // The good window may not come within the minimum gap of the perfect window.
  const double floorValue = std::max(GOOD_MIN_MS, perfectMs_ + MIN_GAP_MS);
  goodMs_ = clampValue(value, floorValue, GOOD_MAX_MS);
  dirty_ = true;
}

void PracticeJudgeWindows::reset() noexcept {
  perfectMs_ = 80.0;
  goodMs_ = 180.0;
  dirty_ = false;
}

} // namespace rechigros::game
