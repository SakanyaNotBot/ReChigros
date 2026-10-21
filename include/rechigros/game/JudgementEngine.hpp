#pragma once

#include "rechigros/game/HitOffsetIndicator.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/game/PlayResult.hpp"
#include "rechigros/game/PracticeJudgeWindows.hpp"

#include <array>

namespace rechigros::game {
class JudgementEngine final {
public:
  enum class Judgement : int { Perfect = 0, Good = 1, Bad = 2, Miss = 3 };
  struct Windows { double perfect{0.080}, good{0.180}, bad{0.220}; };

  class FlickTracker final {
  public:
    FlickTracker(float x, float y) noexcept;
    void move(float x, float y) noexcept;
    void settleAtCurrentPosition() noexcept;
    void advanceFrame(float frameDeltaSec, float judgeSpeed) noexcept;
    [[nodiscard]] bool newFlick() const noexcept { return newFlick_; }
    [[nodiscard]] bool flicked() const noexcept { return flicked_; }
    void setFlicked(bool value) noexcept { flicked_ = value; }
    void consumeFlick() noexcept { flicked_ = true; newFlick_ = false; }
  private:
    float lastMoveX_{}, lastMoveY_{}, nowMoveX_{}, nowMoveY_{};
    float nowPositionX_{}, nowPositionY_{};
    std::array<float,3> historyX_{}, historyY_{};
    bool stopped_{true}, flicked_{}, newFlick_{};
  };

  explicit JudgementEngine(int totalNotes = 0, bool challengeMode = false) noexcept;
  void reset(int totalNotes, bool challengeMode = false) noexcept;

  // Presentation/statistics switches. Autoplay and replay playback suppress the
  // offset statistics that a manual player would accumulate.
  void setAutoplay(bool value) noexcept { autoplay_ = value; }
  void setReplayPlayback(bool value) noexcept { replayPlayback_ = value; }
  void setHitOffsetIndicatorEnabled(bool value) noexcept { hitOffsetIndicatorEnabled_ = value; }
  void setJudgementDetailDistribution(bool value) noexcept { judgementDetailDistribution_ = value; }
  void setVisualTime(double value) noexcept { visualTimeSec_ = value; }
  void setChallengeTimeSum(double value) noexcept { challengeTimeSum_ = value; }

  [[nodiscard]] bool challengeMode() const noexcept { return challengeMode_; }
  [[nodiscard]] double visualTime() const noexcept { return visualTimeSec_; }
  [[nodiscard]] Windows windows(double challengeTimeSum) const noexcept;
  [[nodiscard]] Windows windows() const noexcept { return windows(challengeTimeSum_); }

  // Practice-mode window overrides. Set to null to judge with the chart's own windows.
  void setPracticeWindows(const PracticeJudgeWindows* windows) noexcept {
    practiceWindows_ = windows;
  }
  [[nodiscard]] bool practiceOverrideActive() const noexcept {
    return practiceWindows_ != nullptr && practiceWindows_->active(practice_, challengeMode_);
  }
  void setPracticeEnabled(bool value) noexcept { practice_ = value; }

  [[nodiscard]] Judgement calculate(int noteType, double absDiffSec, double challengeTimeSum) const noexcept;
  [[nodiscard]] Judgement calculate(int noteType, double absDiffSec) const noexcept {
    return calculate(noteType, absDiffSec, challengeTimeSum_);
  }

  // Per-type maximum |delta| accepted for hit detection.
  [[nodiscard]] double judgeWindowSec(int noteType) const noexcept;

  bool commit(Note& note, Judgement judgement, double diffSec, double chartTimeSec) noexcept;
  void recordOffset(Note& note, Judgement judgement, double diffSec, bool forceHoldHead = false) noexcept;
  void recordIndicator(Note& note, Judgement judgement, double diffSec, bool forceHoldHead = false) noexcept;

  [[nodiscard]] int score() const noexcept;
  [[nodiscard]] double accuracy() const noexcept;
  [[nodiscard]] double realtimeAccuracy() const noexcept;
  [[nodiscard]] int combo() const noexcept { return combo_; }
  [[nodiscard]] int maxCombo() const noexcept { return maxCombo_; }
  [[nodiscard]] bool allPerfectSoFar() const noexcept { return apStill_; }
  [[nodiscard]] bool fullComboSoFar() const noexcept { return fcStill_; }
  [[nodiscard]] double stdDevMs() const noexcept;
  [[nodiscard]] const std::array<int, 4>& counts() const noexcept { return counts_; }
  [[nodiscard]] const std::array<int, PlayResult::JUDGEMENT_DETAIL_BIN_COUNT>& detailBins() const noexcept {
    return bins_;
  }
  [[nodiscard]] int goodEarly() const noexcept { return goodEarly_; }
  [[nodiscard]] int goodLate() const noexcept { return goodLate_; }
  [[nodiscard]] int totalNotes() const noexcept { return totalNotes_; }
  [[nodiscard]] const HitOffsetIndicator& indicator() const noexcept { return indicator_; }
  [[nodiscard]] HitOffsetIndicator& indicator() noexcept { return indicator_; }
  [[nodiscard]] PlayResult result() const noexcept;

  static double spatialJudgeWindow(double perfectWindow, double badWindow, float touchPosition) noexcept;
  static bool checkNoteCandidateReplaces(bool hasCurrent, int currentType, double currentDeltaSec,
      double currentAbsDeltaSec, int candidateType, double candidateDeltaSec,
      double candidateAbsDeltaSec, double currentJudgeDistance, double candidateJudgeDistance) noexcept;
  static bool checkFlickCandidateReplaces(bool hasCurrent, double currentDeltaSec,
      double currentAbsDeltaSec, double candidateDeltaSec,
      double currentJudgeDistance, double candidateJudgeDistance) noexcept;
  static double lateMissLimitSec(int noteType, double perfectWindow, double goodWindow, double badWindow) noexcept;
  static double lateMissLimitSec(int noteType, const Windows& windows) noexcept;
  static bool isHit(Judgement judgement) noexcept {
    return judgement == Judgement::Perfect || judgement == Judgement::Good;
  }
  static bool dragLocalJudged(bool localJudged, bool chartNoteJudged) noexcept { return localJudged || chartNoteJudged; }
  static float judgePositionScalePx(float screenHeight, float stageHeight, float stageWidth) noexcept;
  static float flickJudgeSpeed(float dpi) noexcept;
  static int judgementDetailRangeMs(bool challengeMode) noexcept;
  static int judgementDetailBinWidthMs(int rangeMs) noexcept;

private:
  int totalNotes_{}, combo_{}, maxCombo_{}, goodEarly_{}, goodLate_{};
  std::array<int,4> counts_{};
  std::array<int,PlayResult::JUDGEMENT_DETAIL_BIN_COUNT> bins_{};
  bool challengeMode_{}, apStill_{true}, fcStill_{true};
  bool autoplay_{}, replayPlayback_{}, hitOffsetIndicatorEnabled_{true}, judgementDetailDistribution_{true};
  // Practice-mode timing override; only consulted for taps and holds.
  const PracticeJudgeWindows* practiceWindows_{};
  bool practice_{};
  int timingCount_{};
  double timingMeanMs_{}, timingM2Ms_{};
  double challengeTimeSum_{}, visualTimeSec_{};
  HitOffsetIndicator indicator_{64};
};
}
