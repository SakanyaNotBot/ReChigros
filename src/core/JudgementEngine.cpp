#include "rechigros/game/JudgementEngine.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rechigros::game {
namespace {
constexpr double NORMAL_PERFECT = 0.080;
constexpr double NORMAL_GOOD = 0.180;
constexpr double NORMAL_BAD = 0.220;
constexpr double CHALLENGE_PERFECT = 0.040;
constexpr double CHALLENGE_GOOD = 0.090;
constexpr double CHALLENGE_BAD = 0.140;
constexpr double DRAG_MISS_THRESHOLD = 0.100;
constexpr float FLICK_BASE = 0.06F;
constexpr float FLICK_REFERENCE_DPI = 380.0F;
constexpr float FLICK_TIME_SCALE = 60.0F;
constexpr float FLICK_NEW_MULTIPLIER = 5.0F;

// Round-half-up of a float, matching the reference's Math.round(float) call.
int roundFloatToInt(float value) {
  if (!std::isfinite(value)) return 0;
  return static_cast<int>(std::floor(static_cast<double>(value) + 0.5));
}
} // namespace

JudgementEngine::FlickTracker::FlickTracker(float x, float y) noexcept
    : nowPositionX_(x), nowPositionY_(y) {
  historyX_.fill(x);
  historyY_.fill(y);
}

void JudgementEngine::FlickTracker::move(float x, float y) noexcept {
  nowPositionX_ = x;
  nowPositionY_ = y;
}

void JudgementEngine::FlickTracker::settleAtCurrentPosition() noexcept {
  lastMoveX_ = lastMoveY_ = nowMoveX_ = nowMoveY_ = 0.0F;
  historyX_.fill(nowPositionX_);
  historyY_.fill(nowPositionY_);
  stopped_ = true;
  flicked_ = newFlick_ = false;
}

void JudgementEngine::FlickTracker::advanceFrame(float frameDelta, float judgeSpeed) noexcept {
  const auto delta = std::isfinite(frameDelta) && frameDelta > 1e-6F ? frameDelta : 1.0F / FLICK_TIME_SCALE;
  const auto threshold = std::isfinite(judgeSpeed) && judgeSpeed > 0.0F ? judgeSpeed : 0.0F;

  lastMoveX_ = nowMoveX_;
  lastMoveY_ = nowMoveY_;
  historyX_[2] = historyX_[1];
  historyY_[2] = historyY_[1];
  historyX_[1] = historyX_[0];
  historyY_[1] = historyY_[0];
  historyX_[0] = nowPositionX_;
  historyY_[0] = nowPositionY_;
  nowMoveX_ = historyX_[0] - historyX_[2];
  nowMoveY_ = historyY_[0] - historyY_[2];

  float projected{};
  const auto lastLengthSquared = lastMoveX_ * lastMoveX_ + lastMoveY_ * lastMoveY_;
  if (lastLengthSquared > 0.01F) {
    const auto length = std::sqrt(lastLengthSquared);
    projected = (nowMoveX_ * lastMoveX_ + nowMoveY_ * lastMoveY_) / length;
  }

  if (projected / FLICK_TIME_SCALE / delta < threshold || stopped_) {
    const auto raw = std::sqrt(nowMoveX_ * nowMoveX_ + nowMoveY_ * nowMoveY_) / FLICK_TIME_SCALE / delta;
    newFlick_ = raw >= threshold * FLICK_NEW_MULTIPLIER;
    stopped_ = !newFlick_;
    if (stopped_) flicked_ = false;
  }
  if (!newFlick_) flicked_ = false;
}

JudgementEngine::JudgementEngine(int total, bool challenge) noexcept { reset(total, challenge); }

void JudgementEngine::reset(int total, bool challenge) noexcept {
  totalNotes_ = std::max(0, total);
  challengeMode_ = challenge;
  combo_ = maxCombo_ = goodEarly_ = goodLate_ = timingCount_ = 0;
  counts_.fill(0);
  bins_.fill(0);
  apStill_ = fcStill_ = true;
  timingMeanMs_ = timingM2Ms_ = 0.0;
  challengeTimeSum_ = 0.0;
  visualTimeSec_ = 0.0;
  indicator_.reset();
}

JudgementEngine::Windows JudgementEngine::windows(double challengeTimeSum) const noexcept {
  if (!challengeMode_) return Windows{NORMAL_PERFECT, NORMAL_GOOD, NORMAL_BAD};
  return Windows{CHALLENGE_PERFECT + challengeTimeSum, CHALLENGE_GOOD + challengeTimeSum,
      CHALLENGE_BAD + challengeTimeSum};
}

double JudgementEngine::judgeWindowSec(int noteType) const noexcept {
  const auto range = windows();
  switch (noteType) {
    case NOTE_DRAG:
    case NOTE_FLICK:
      return range.bad;
    case NOTE_HOLD:
      return range.good;
    default:
      return range.bad;
  }
}

JudgementEngine::Judgement JudgementEngine::calculate(int noteType, double difference,
    double challengeTimeSum) const noexcept {
  const auto range = windows(challengeTimeSum);
  // Practice mode may narrow or widen the tap and hold windows; the drag, flick and bad
  // windows always come from the chart.
  const bool tapOrHold = noteType == NOTE_TAP || noteType == NOTE_HOLD;
  const double perfect = tapOrHold && practiceOverrideActive()
      ? practiceWindows_->perfectSec()
      : range.perfect;
  const double good = tapOrHold && practiceOverrideActive()
      ? practiceWindows_->goodSec()
      : range.good;
  if (difference < perfect) return Judgement::Perfect;
  if (noteType == NOTE_DRAG || noteType == NOTE_FLICK) return Judgement::Miss;
  if (difference < good) return Judgement::Good;
  if (noteType == NOTE_HOLD) return Judgement::Miss;
  if (difference <= range.bad) return Judgement::Bad;
  return Judgement::Miss;
}

bool JudgementEngine::commit(Note& note, Judgement judgement, double diffSec, double chartTime) noexcept {
  if (note.judgeResult >= 0) return false;

  note.judgeResult = static_cast<int>(judgement);
  note.judgeDiffSec = diffSec;
  note.judgeTimeSec = chartTime;
  note.preJudge = false;
  note.holdActive = false;
  note.holdPreJudge = false;
  note.holdUpTimeSec = std::numeric_limits<double>::infinity();
  const bool hit = isHit(judgement);
  note.clicked = hit;
  note.holdFxActive = note.type == NOTE_HOLD && hit && note.holdFxActive && chartTime < note.holdEndTime;

  if (judgement == Judgement::Perfect) {
    ++counts_[0];
    ++combo_;
  } else if (judgement == Judgement::Good) {
    ++counts_[1];
    ++combo_;
    if (diffSec > 0.0) ++goodEarly_;
    else ++goodLate_;
    apStill_ = false;
  } else if (judgement == Judgement::Bad) {
    ++counts_[2];
    combo_ = 0;
    apStill_ = false;
    fcStill_ = false;
  } else {
    ++counts_[3];
    combo_ = 0;
    apStill_ = false;
    fcStill_ = false;
  }

  if (hit) {
    const auto milliseconds = diffSec * 1000.0;
    ++timingCount_;
    const auto delta = milliseconds - timingMeanMs_;
    timingMeanMs_ += delta / static_cast<double>(timingCount_);
    const auto delta2 = milliseconds - timingMeanMs_;
    timingM2Ms_ += delta * delta2;
  }

  if (combo_ > maxCombo_) maxCombo_ = combo_;

  recordOffset(note, judgement, diffSec, false);
  recordIndicator(note, judgement, diffSec, false);
  return true;
}

void JudgementEngine::recordOffset(Note& note, Judgement judgement, double diffSec,
    bool forceHoldHead) noexcept {
  if (!judgementDetailDistribution_) return;
  if (autoplay_ && !replayPlayback_) return;
  if (judgement == Judgement::Miss) return;
  if (note.type != NOTE_TAP && note.type != NOTE_HOLD) return;
  if (!forceHoldHead && note.type == NOTE_HOLD) return;
  if (forceHoldHead && note.judgementDetailOffsetRecorded) return;

  const auto displayMs = -diffSec * 1000.0;
  if (!std::isfinite(displayMs)) return;

  const auto rangeMs = judgementDetailRangeMs(challengeMode_);
  const auto binWidthMs = judgementDetailBinWidthMs(rangeMs);
  const auto raw = roundFloatToInt(static_cast<float>(
      (displayMs + static_cast<double>(rangeMs)) / static_cast<double>(binWidthMs)));
  const auto lastIndex = static_cast<int>(bins_.size()) - 1;
  const auto index = std::clamp(raw, 0, lastIndex);
  if (bins_[static_cast<std::size_t>(index)] < std::numeric_limits<int>::max()) {
    ++bins_[static_cast<std::size_t>(index)];
  }
  if (forceHoldHead) note.judgementDetailOffsetRecorded = true;
}

void JudgementEngine::recordIndicator(Note& note, Judgement judgement, double diffSec,
    bool forceHoldHead) noexcept {
  if (!hitOffsetIndicatorEnabled_) return;
  if (autoplay_ && !replayPlayback_) return;
  if (judgement == Judgement::Miss) return;
  const auto indicatorJudgement = static_cast<HitOffsetIndicator::Judgement>(judgement);
  if (!forceHoldHead
      && !HitOffsetIndicator::shouldShowCommitMark(note.type, note.offsetIndicatorShown, indicatorJudgement)) {
    return;
  }
  if (forceHoldHead && note.offsetIndicatorShown) return;

  indicator_.addHit(-diffSec, indicatorJudgement, visualTimeSec_, windows().bad);
  if (note.type == NOTE_HOLD || forceHoldHead) note.offsetIndicatorShown = true;
}

double JudgementEngine::stdDevMs() const noexcept {
  if (timingCount_ <= 0) return 0.0;
  return std::sqrt(std::max(0.0, timingM2Ms_ / static_cast<double>(timingCount_)));
}

int JudgementEngine::score() const noexcept {
  if (totalNotes_ <= 0) return 0;
  const auto ratio = std::clamp((counts_[0] + 0.65 * counts_[1]) / static_cast<double>(totalNotes_),
      0.0, 1.0);
  auto value = challengeMode_
      ? ratio * 1'000'000.0
      : ratio * 900'000.0 + static_cast<double>(maxCombo_) * 100'000.0 / static_cast<double>(totalNotes_);
  if (!std::isfinite(value)) return 0;
  value = std::clamp(value, 0.0, 1'000'000.0);
  return static_cast<int>(std::floor(value + 0.5));
}

double JudgementEngine::accuracy() const noexcept {
  if (totalNotes_ <= 0) return 0.0;
  return std::clamp((counts_[0] + 0.65 * counts_[1]) / static_cast<double>(totalNotes_), 0.0, 1.0) * 100.0;
}

double JudgementEngine::realtimeAccuracy() const noexcept {
  const auto judged = counts_[0] + counts_[1] + counts_[2] + counts_[3];
  if (judged <= 0) return 100.0;
  return std::clamp((counts_[0] + 0.65 * counts_[1]) / static_cast<double>(judged), 0.0, 1.0) * 100.0;
}

PlayResult JudgementEngine::result() const noexcept {
  PlayResult value;
  value.score = score();
  value.accuracy = accuracy();
  value.maxCombo = maxCombo_;
  value.totalNotes = totalNotes_;
  value.perfect = counts_[0];
  value.good = counts_[1];
  value.bad = counts_[2];
  value.miss = counts_[3];
  value.goodEarly = goodEarly_;
  value.goodLate = goodLate_;
  value.stdDevMs = stdDevMs();
  value.judgementOffsetBins = bins_;
  return value;
}

double JudgementEngine::spatialJudgeWindow(double perfectWindow, double badWindow,
    float touchPosition) noexcept {
  if (touchPosition <= 0.9F) return badWindow;
  if (touchPosition < 1.9F) return badWindow - (touchPosition - 0.9F) * perfectWindow * 0.5;
  return 0.0;
}

bool JudgementEngine::checkNoteCandidateReplaces(bool hasCurrent, int currentType, double currentDelta,
    double currentAbsDelta, int candidateType, double candidateDelta, double,
    double currentDistance, double candidateDistance) noexcept {
  if (!hasCurrent) return true;
  const auto currentTapHold = currentType == NOTE_TAP || currentType == NOTE_HOLD;
  const auto candidateTapHold = candidateType == NOTE_TAP || candidateType == NOTE_HOLD;
  if (candidateDelta >= currentAbsDelta + 0.01) return false;
  if (currentTapHold && candidateTapHold && std::abs(candidateDelta - currentDelta) <= 0.01
      && candidateDistance < currentDistance) {
    return true;
  }
  return currentType == NOTE_DRAG || currentType == NOTE_FLICK;
}

bool JudgementEngine::checkFlickCandidateReplaces(bool hasCurrent, double currentDelta,
    double currentAbsDelta, double candidateDelta, double currentDistance,
    double candidateDistance) noexcept {
  if (!hasCurrent) return true;
  if (candidateDelta >= currentAbsDelta + 0.01) return false;
  return std::abs(candidateDelta - currentDelta) <= 0.01 && candidateDistance < currentDistance;
}

double JudgementEngine::lateMissLimitSec(int noteType, double perfectWindow, double goodWindow,
    double badWindow) noexcept {
  (void)badWindow;
  switch (noteType) {
    case NOTE_DRAG: return DRAG_MISS_THRESHOLD;
    case NOTE_FLICK: return perfectWindow * 1.75;
    case NOTE_HOLD: return goodWindow;
    default: return goodWindow;
  }
}

double JudgementEngine::lateMissLimitSec(int noteType, const Windows& range) noexcept {
  return lateMissLimitSec(noteType, range.perfect, range.good, range.bad);
}

float JudgementEngine::judgePositionScalePx(float screenHeight, float stageHeight,
    float stageWidth) noexcept {
  auto scale = screenHeight * 0.1F;
  if (!std::isfinite(scale) || scale <= 1e-6F) scale = stageHeight * 0.1F;
  if (!std::isfinite(scale) || scale <= 1e-6F) scale = PGR_WIDTH * stageWidth;
  return std::isfinite(scale) && scale > 1e-6F ? scale : 1.0F;
}

float JudgementEngine::flickJudgeSpeed(float dpi) noexcept {
  const auto safe = std::isfinite(dpi) && dpi > 0.0F ? dpi : FLICK_REFERENCE_DPI;
  return FLICK_BASE * safe / FLICK_REFERENCE_DPI;
}

int JudgementEngine::judgementDetailRangeMs(bool challenge) noexcept {
  return challenge ? PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                   : PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS;
}

int JudgementEngine::judgementDetailBinWidthMs(int rangeMs) noexcept {
  return std::max(1, (rangeMs * 2) / std::max(1, PlayResult::JUDGEMENT_DETAIL_BIN_COUNT - 1));
}
} // namespace rechigros::game
