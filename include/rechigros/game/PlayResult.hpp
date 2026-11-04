#pragma once

#include <array>

namespace rechigros::game {
struct PlayResult {
  static constexpr int JUDGEMENT_DETAIL_BIN_COUNT = 19;
  static constexpr int JUDGEMENT_DETAIL_NORMAL_RANGE_MS = 180;
  static constexpr int JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS = 90;

  int score{};
  double accuracy{};
  int maxCombo{};
  int totalNotes{};
  int perfect{}, good{}, bad{}, miss{};
  int goodEarly{}, goodLate{};
  double stdDevMs{};
  std::array<int, JUDGEMENT_DETAIL_BIN_COUNT> judgementOffsetBins{};

  [[nodiscard]] bool isFullCombo() const noexcept { return bad == 0 && miss == 0 && totalNotes > 0; }
  [[nodiscard]] bool isAllPerfect() const noexcept { return good == 0 && bad == 0 && miss == 0 && totalNotes > 0; }
};
}

