#pragma once

#include <limits>
#include <string>
#include <vector>

namespace rechigros::game {
class HitOffsetIndicator final {
public:
  enum class Judgement { Perfect = 0, Good = 1, Bad = 2, Miss = 3 };
  struct Mark { bool active{}; float angleDeg{}; Judgement judgement{}; double startSec{}; double diffMs{}; };
  static constexpr float MAX_ANGLE_DEG = 60.0F;
  static constexpr float MOVE_DURATION_SEC = 0.20F;
  static constexpr float RETURN_DELAY_SEC = 0.45F;
  static constexpr float RETURN_DURATION_SEC = 0.35F;
  static constexpr float MARK_LIFE_SEC = 0.90F;

  explicit HitOffsetIndicator(std::size_t markCount = 24);
  void reset() noexcept;
  void addHit(double diffSec, Judgement judgement, double nowSec, double badWindowSec);
  void update(double nowSec) noexcept;
  [[nodiscard]] float latestTextAlpha(double nowSec) const noexcept;
  [[nodiscard]] float pointerAngleDeg() const noexcept { return pointerAngleDeg_; }
  [[nodiscard]] const std::vector<Mark>& marks() const noexcept { return marks_; }
  [[nodiscard]] const std::string& latestText() const noexcept { return latestText_; }
  static float markAge(double nowSec, const Mark& mark) noexcept;
  static float angleForDiff(double diffSec, double badWindowSec) noexcept;
  static Judgement judgementForOffset(double absDiffSec, double perfectWindowSec, double goodWindowSec, double badWindowSec) noexcept;
  static bool shouldShowCommitMark(int noteType, bool holdHeadAlreadyShown, Judgement judgement) noexcept;
  static float fadeOutAlpha(float progress) noexcept;
  static std::string formatOffsetMs(double diffSec);
private:
  void applyPointerMove(double nowSec) noexcept;
  std::vector<Mark> marks_;
  std::size_t nextMarkIndex_{};
  float pointerAngleDeg_{};
  float pointerMoveStartAngleDeg_{};
  float pointerMoveTargetAngleDeg_{};
  double pointerMoveStartSec_{};
  float pointerMoveDurationSec_{MOVE_DURATION_SEC};
  double lastHitSec_{-std::numeric_limits<double>::infinity()};
  bool returningToCenter_{};
  std::string latestText_;
  double latestTextStartSec_{-std::numeric_limits<double>::infinity()};
};
}
