#pragma once

#include <vector>

namespace rechigros::game {
class BpmTimeline final {
public:
  BpmTimeline(std::vector<double> beats, std::vector<double> bpms, std::vector<double> seconds);
  [[nodiscard]] std::size_t size() const noexcept { return beats_.size(); }
  [[nodiscard]] double beatToSec(double beat) const noexcept;
  [[nodiscard]] double secToBeat(double second) const noexcept;
private:
  std::vector<double> beats_;
  std::vector<double> bpms_;
  std::vector<double> seconds_;
};
}

