#include "rechigros/app/SmoothingClock.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::app {

void SmoothingClock::reset(double reportedSec, double nowSec) noexcept {
  valid_ = true;
  value_ = reportedSec;
  referencePlayheadSec_ = reportedSec;
  referenceTimeSec_ = nowSec;
  lastReportedSec_ = reportedSec;
  index_ = 0;
  count_ = 0;
  sum_ = 0.0;
}

double SmoothingClock::advance(double reportedSec, double nowSec, double speed) noexcept {
  if (!valid_) {
    reset(reportedSec, nowSec);
    return value_;
  }

  const double elapsed = nowSec - referenceTimeSec_;
  const double predicted = referencePlayheadSec_ + elapsed * speed;
  const double bias = predicted - reportedSec;

  // Rolling average of the error, held in a ring buffer so the window costs no shifting.
  if (count_ < kHistorySize) {
    history_[index_] = bias;
    sum_ += bias;
    ++count_;
  } else {
    sum_ -= history_[index_];
    history_[index_] = bias;
    sum_ += bias;
  }
  index_ = (index_ + 1) % kHistorySize;
  const double averageBias = count_ == 0 ? 0.0 : sum_ / count_;

  const double difference = reportedSec - lastReportedSec_;
  const bool nonFinite = !std::isfinite(predicted) || !std::isfinite(reportedSec);
  const bool discontinuity = !nonFinite
      && (difference < -kJumpBackwardSec || std::abs(difference) > kBigJumpSec);
  if (nonFinite) {
    // A reading that is not a number carries no information. Keeping the last good position
    // is the only option that leaves the frame drawable; snapping to it would emit a
    // not-a-number and blank the play field.
    return value_;
  }
  if (discontinuity) {
    // Snapping and clearing the window is the only correct answer here: the prediction is
    // derived from a position that no longer applies.
    value_ = reportedSec;
    referencePlayheadSec_ = reportedSec;
    referenceTimeSec_ = nowSec;
    index_ = 0;
    count_ = 0;
    sum_ = 0.0;
  } else {
    value_ = std::clamp(predicted - averageBias, reportedSec - kMaxDeviationSec,
        reportedSec + kMaxDeviationSec);
    // Continue from the emitted value so the correction never accumulates.
    referencePlayheadSec_ = value_;
    referenceTimeSec_ = nowSec;
  }
  lastReportedSec_ = reportedSec;
  return value_;
}

} // namespace rechigros::app
