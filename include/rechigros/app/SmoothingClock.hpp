#pragma once

namespace rechigros::app {

// Keeps the drawing clock locked to the audio stream without copying its reporting steps.
//
// The audio backend reports its position once per callback, and a callback can be late or
// arrive in a burst, so drawing straight from the report makes the play field freeze and jump.
// This advances the clock at the playback rate on every frame and applies a bounded correction
// towards the report: a late report slows the clock, an early one speeds it up, and neither can
// move it backwards. Judgement keeps using the audio position itself, so the correction never
// moves a hit window.
//
// A seek, a restart or the jump out of a pause is a different case: it is detected as a
// discontinuity and snaps the clock, because easing across it would smear the jump over a
// second of play.
class SmoothingClock final {
public:
  // Largest fraction of a frame's advance that a correction may add or remove. Keeping half
  // means a stalled report makes the clock coast at half speed instead of freezing outright.
  static constexpr double kCorrectionShare = 0.5;
  // Response of the correction to the measured error. Half converges within a few frames while
  // leaving the motion smooth.
  static constexpr double kCorrectionGain = 0.5;
  // A backwards jump larger than this is a seek or a restart.
  static constexpr double kJumpBackwardSec = 0.1;
  // Any jump larger than this in either direction is a discontinuity.
  static constexpr double kBigJumpSec = 0.5;

  // Starts tracking from `reportedSec`. Call after a load, a seek or a resume.
  void reset(double reportedSec, double nowSec) noexcept;

  // Advances the clock. `reportedSec` is the audio stream's position, `nowSec` a monotonic
  // wall-clock reading and `speed` the music playback speed. Returns the position to draw.
  [[nodiscard]] double advance(double reportedSec, double nowSec, double speed) noexcept;

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] double value() const noexcept { return value_; }

private:
  double lastReportedSec_{};
  double lastAdvanceSec_{};
  double value_{};
  bool valid_{};
};

} // namespace rechigros::app
