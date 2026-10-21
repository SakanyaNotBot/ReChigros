#include "rechigros/game/BpmTimeline.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <stdexcept>

namespace rechigros::game {
BpmTimeline::BpmTimeline(std::vector<double> beats, std::vector<double> bpms,
                         std::vector<double> seconds)
    : beats_(std::move(beats)), bpms_(std::move(bpms)), seconds_(std::move(seconds)) {
  if (beats_.empty() || beats_.size() != bpms_.size() || beats_.size() != seconds_.size()) {
    throw std::invalid_argument("BPM timeline arrays must have equal non-zero lengths");
  }
  if (!std::is_sorted(beats_.begin(), beats_.end()) ||
      !std::is_sorted(seconds_.begin(), seconds_.end()) ||
      std::any_of(bpms_.begin(), bpms_.end(), [](double bpm) { return bpm <= 0.0; })) {
    throw std::invalid_argument("BPM timeline must be sorted and contain positive BPM values");
  }
}

double BpmTimeline::beatToSec(double beat) const noexcept {
  if (beat <= beats_.front()) return seconds_.front();
  const auto upper = std::upper_bound(beats_.begin(), beats_.end(), beat);
  const auto index = upper == beats_.end() ? beats_.size() - 1
                                           : static_cast<std::size_t>(upper - beats_.begin() - 1);
  return seconds_[index] + (beat - beats_[index]) * (PGR_BEAT / bpms_[index]);
}

double BpmTimeline::secToBeat(double second) const noexcept {
  if (second <= seconds_.front()) return beats_.front();
  const auto upper = std::upper_bound(seconds_.begin(), seconds_.end(), second);
  const auto index = upper == seconds_.end() ? seconds_.size() - 1
                                             : static_cast<std::size_t>(upper - seconds_.begin() - 1);
  return beats_[index] + (second - seconds_[index]) * (bpms_[index] / PGR_BEAT);
}
} // namespace rechigros::game

