#include "rechigros/app/SmoothingClock.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::app {

void SmoothingClock::reset(double reportedSec, double nowSec) noexcept {
  valid_ = true;
  value_ = reportedSec;
  lastAdvanceSec_ = nowSec;
  lastReportedSec_ = reportedSec;
}

double SmoothingClock::advance(double reportedSec, double nowSec, double speed) noexcept {
  if (!valid_) {
    reset(reportedSec, nowSec);
    return value_;
  }

  // A frame interval can be zero after a clock reset and can be long after a stall; both are
  // handled by the same bound. A non-finite interval (a broken wall clock) must not poison the
  // accumulator, so it maps to zero. A non-positive speed is not a playback state, so the
  // nominal rate is used rather than freezing the field.
  double dt = nowSec - lastAdvanceSec_;
  if (!(dt > 0.0)) dt = 0.0;
  if (dt > 0.25) dt = 0.25;
  if (std::isfinite(nowSec)) lastAdvanceSec_ = nowSec;
  const double rate = std::isfinite(speed) && speed > 0.0 ? speed : 1.0;
  const double base = dt * rate;

  if (!std::isfinite(reportedSec)) {
    // A reading that is not a number carries no information. Coasting at the playback rate
    // leaves the frame drawable; snapping to it would blank the play field.
    value_ += base;
    return value_;
  }

  const double difference = reportedSec - lastReportedSec_;
  if (difference < -kJumpBackwardSec || difference > kBigJumpSec) {
    // A seek or a restart: the prediction was derived from a position that no longer applies.
    reset(reportedSec, nowSec);
    return value_;
  }

  // First-order tracking: advance at the playback rate, then correct a bounded fraction of the
  // distance to the reported position. During a late report the correction is negative and the
  // clock coasts; it never freezes and never runs backwards, because the correction can remove
  // at most half of the frame's advance.
  const double error = reportedSec - (value_ + base);
  const double maxCorrection = base * kCorrectionShare;
  const double correction = std::clamp(error * kCorrectionGain, -maxCorrection, maxCorrection);
  value_ = std::max(value_, value_ + base + correction);
  lastReportedSec_ = reportedSec;
  return value_;
}

} // namespace rechigros::app
