#pragma once

namespace rechigros::app {

// Corrects the drawing clock against the audio stream's reported position.
//
// The audio backend reports where playback is only once per callback, so drawing straight
// from that value makes the play field advance in steps with jitter in between. This keeps a
// running average of the error between a predicted position and the reported one, and
// subtracts that average: the prediction is smooth by construction and the average absorbs a
// constant offset between the two clocks.
//
// Two properties keep it safe. The output is clamped to a small band around the reported
// position, so the drawing clock can never drift visibly away from the judged one; and a
// seek, restart or resume is detected as a jump and snaps the clock instead of averaging
// across the discontinuity, which would smear the jump over the whole window.
class SmoothingClock final {
public:
  // Frames in the averaging window. Short enough to follow a real drift, long enough to
  // absorb the reporting interval.
  static constexpr int kHistorySize = 60;
  // The drawing clock stays within this distance of the reported position.
  static constexpr double kMaxDeviationSec = 0.010;
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
  double history_[kHistorySize]{};
  int index_{};
  int count_{};
  double sum_{};
  double referencePlayheadSec_{};
  double referenceTimeSec_{};
  double lastReportedSec_{};
  double value_{};
  bool valid_{};
};

} // namespace rechigros::app
