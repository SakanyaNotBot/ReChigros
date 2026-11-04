#include "rechigros/game/BpmTimeline.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <stdexcept>

namespace rechigros::game {
namespace {
// Index of the segment holding `value`, where a segment covers [values[i], values[i+1]).
// Mirrors the reference's binary search exactly, including its clamp to the last segment
// for a value past the end of the table.
std::size_t findSegment(const std::vector<double>& values, double value) noexcept {
  const auto last = static_cast<long long>(values.size()) - 2;
  long long low = 0;
  long long high = last;
  while (low <= high) {
    const auto middle = (low + high) / 2;
    const auto lower = values[static_cast<std::size_t>(middle)];
    const auto upper = values[static_cast<std::size_t>(middle) + 1];
    if (value < lower) {
      high = middle - 1;
    } else if (value >= upper) {
      low = middle + 1;
    } else {
      return static_cast<std::size_t>(middle);
    }
  }
  if (low < 0) low = 0;
  if (low > last) low = last;
  return static_cast<std::size_t>(low);
}
} // namespace

BpmTimeline::BpmTimeline(std::vector<double> beats, std::vector<double> bpms,
                         std::vector<double> seconds)
    : beats_(std::move(beats)), bpms_(std::move(bpms)), seconds_(std::move(seconds)) {
  // Only the shape is validated. The values are authored chart data, so an unsorted or
  // non-positive table is carried through exactly as it appears in the chart.
  if (beats_.empty() || beats_.size() != bpms_.size() || beats_.size() != seconds_.size()) {
    throw std::invalid_argument("BPM timeline arrays must have equal non-zero lengths");
  }
}

double BpmTimeline::beatToSec(double beat) const noexcept {
  if (beat <= beats_.front()) return seconds_.front();
  const auto last = beats_.size() - 1;
  if (beat >= beats_[last]) {
    return seconds_[last] + (beat - beats_[last]) * (PGR_BEAT / bpms_[last]);
  }
  const auto index = findSegment(beats_, beat);
  return seconds_[index] + (beat - beats_[index]) * (PGR_BEAT / bpms_[index]);
}

double BpmTimeline::secToBeat(double second) const noexcept {
  if (second <= seconds_.front()) return beats_.front();
  const auto last = seconds_.size() - 1;
  if (second >= seconds_[last]) {
    return beats_[last] + (second - seconds_[last]) * (bpms_[last] / PGR_BEAT);
  }
  const auto index = findSegment(seconds_, second);
  return beats_[index] + (second - seconds_[index]) * (bpms_[index] / PGR_BEAT);
}
} // namespace rechigros::game
