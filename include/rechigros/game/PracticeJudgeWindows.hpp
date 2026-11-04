#pragma once

namespace rechigros::game {

// Practice-mode judgement window overrides.
//
// A player in practice mode can widen or tighten the perfect and good windows for an
// attempt. The override only applies to taps and holds, only while practice is enabled,
// only outside challenge mode, and only once a window has actually been changed. A
// practice session that never touches a window keeps the chart's normal timing.
//
// The two windows are kept in milliseconds and clamped so the perfect window never
// reaches the good window: the classification compares against them in order, so an
// inverted pair would make the good band unreachable.
class PracticeJudgeWindows final {
public:
  static constexpr double PERFECT_MIN_MS = 20.0;
  static constexpr double PERFECT_MAX_MS = 80.0;
  static constexpr double GOOD_MIN_MS = 25.0;
  static constexpr double GOOD_MAX_MS = 180.0;
  // Smallest separation kept between the two windows.
  static constexpr double MIN_GAP_MS = 5.0;

  [[nodiscard]] bool active(bool practiceEnabled, bool challengeMode) const noexcept {
    return practiceEnabled && !challengeMode && dirty_;
  }

  // Sets the perfect window, clamped to keep it below the good window.
  void setPerfectMs(double value) noexcept;
  // Sets the good window, clamped to keep it above the perfect window.
  void setGoodMs(double value) noexcept;

  [[nodiscard]] double perfectMs() const noexcept { return perfectMs_; }
  [[nodiscard]] double goodMs() const noexcept { return goodMs_; }
  [[nodiscard]] double perfectSec() const noexcept { return perfectMs_ / 1000.0; }
  [[nodiscard]] double goodSec() const noexcept { return goodMs_ / 1000.0; }

  // Restores both windows to the chart's normal values and clears the override.
  void reset() noexcept;

private:
  double perfectMs_{80.0};
  double goodMs_{180.0};
  bool dirty_{};
};

} // namespace rechigros::game
