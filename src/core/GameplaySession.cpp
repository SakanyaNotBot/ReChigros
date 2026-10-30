#include "rechigros/game/GameplaySession.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace rechigros::game {
namespace {
constexpr double DRAG_MISS_THRESHOLD = 0.100;
constexpr double HOLD_TAIL_EARLY_SETTLE = 0.220;
constexpr int HOLD_SAFE_FRAME_INIT = 2;
constexpr float JUDGE_DISTANCE_Y_SCALE = 2.2F;
constexpr double PI = 3.14159265358979323846;

// Notes further ahead than this are never candidates.
constexpr double HORIZON_SEC = 1.5;

// Spatial radii in the reference's positionX space.
constexpr float RADIUS_DRAG_PRIME = 2.1F;
constexpr float RADIUS_FLICK_PRIME = 2.1F;
constexpr float RADIUS_HOLD_BODY = 1.9F;
constexpr float RADIUS_SPATIAL_LIMIT = 1.9F;
} // namespace

void GameplaySession::setChart(Chart* chart, bool challengeMode) {
  chart_ = chart;
  challengeMode_ = challengeMode;
  judgeCursor_ = 0;
  autoNoteIndex_ = 0;
  challengeTimeSum_ = 0.0;
  frameTimeIndex_ = frameTimeCount_ = 0;
  challengeTimingStarted_ = false;
  frameTimes_.fill(0.0);
  soundEvents_.clear();
  judgementEvents_.clear();
  cancelTouches();
  if (chart_) chart_->resetRuntime();
  blocks_ = chart_ ? chart_->blockAreaList : std::vector<OfficialBlock>{};
  for (auto& block : blocks_) block.resetRuntime();
  blockedTouchIds_.clear();
  blockedTouches_.clear();
  blockedTouchPulseSec_ = -1.0;
  // A restarted or seeked run starts a new recording, and a replay restarts from its first
  // entry. The recording mode itself survives, because it belongs to the session settings.
  replayCursor_ = 0;
  recordedHoldPressNotes_.clear();
  holdFxNotes_.clear();
  activeHolds_.clear();
  holdBodyBursts_.clear();
  holdHeadBursts_.clear();
  if (replayRecording_) recordedReplay_.clear();
  judgement_.reset(chart_ ? static_cast<int>(chart_->allNotesSorted.size()) : 0, challengeMode);
  judgement_.setAutoplay(autoplay_);
  judgement_.setReplayPlayback(replayPlayback_);
  if (autoplay_) judgement_.setHitOffsetIndicatorEnabled(false);
}

void GameplaySession::setViewport(Viewport viewport) noexcept {
  viewport.width = std::max(1.0F, viewport.width);
  viewport.height = std::max(1.0F, viewport.height);
  if (!std::isfinite(viewport.targetAspect) || viewport.targetAspect <= 0.0F) {
    viewport.targetAspect = 16.0F / 9.0F;
  }
  if (!std::isfinite(viewport.scrollSpeed)) viewport.scrollSpeed = 1.0F;
  if (!std::isfinite(viewport.keyScale) || viewport.keyScale <= 0.0F) viewport.keyScale = 1.0F;
  viewport_ = viewport;
}

void GameplaySession::setMusicSpeed(double value) noexcept {
  musicSpeed_ = std::isfinite(value) && value > 0.0 ? value : 1.0;
}

void GameplaySession::setAutoplay(bool value) noexcept {
  autoplay_ = value;
  judgeCursor_ = 0;
  autoNoteIndex_ = 0;
  judgement_.setAutoplay(value);
  if (value) judgement_.setHitOffsetIndicatorEnabled(false);
}

GameplaySession::Stage GameplaySession::stage() const noexcept {
  Stage result;
  result.aspect = viewport_.targetAspect;
  result.width = viewport_.width;
  result.height = result.width / result.aspect;
  if (result.height > viewport_.height) {
    result.height = viewport_.height;
    result.width = result.height * result.aspect;
  }
  result.left = (viewport_.width - result.width) * 0.5F;
  result.top = (viewport_.height - result.height) * 0.5F;
  return result;
}

float GameplaySession::fingerWorldScale() const noexcept {
  auto scale = viewport_.height * 0.1F;
  if (!std::isfinite(scale) || scale <= 1e-6F) scale = stage().height * 0.1F;
  return std::isfinite(scale) && scale > 1e-6F ? scale : 1.0F;
}

void GameplaySession::touchDown(std::int64_t id, float x, float y) {
  const auto scale = fingerWorldScale();
  touches_[id] = std::make_unique<Touch>(x, y, x / scale, (viewport_.height - y) / scale);
}

void GameplaySession::touchMove(std::int64_t id, float x, float y) noexcept {
  const auto found = touches_.find(id);
  if (found == touches_.end()) return;
  found->second->x = x;
  found->second->y = y;
  const auto scale = fingerWorldScale();
  found->second->flick.move(x / scale, (viewport_.height - y) / scale);
}

void GameplaySession::touchUp(std::int64_t id) noexcept {
  touches_.erase(id);
  blockedTouchIds_.erase(id);
  std::erase_if(blockedTouches_, [id](const BlockedTouch& touch) { return touch.id == id; });
  if (blockedTouches_.empty()) blockedTouchPulseSec_ = -1.0;
}

void GameplaySession::cancelTouches() noexcept {
  touches_.clear();
  blockedTouchIds_.clear();
  blockedTouches_.clear();
  blockedTouchPulseSec_ = -1.0;
}

void GameplaySession::settleTouches() noexcept {
  for (auto& [id, touch] : touches_) {
    (void)id;
    touch->started = false;
    touch->flick.settleAtCurrentPosition();
  }
}

NoteGeometry GameplaySession::geometryFor(const Note& note, const JudgeLine::State& state,
    const Stage& view) const noexcept {
  auto* line = note.master;
  if (!line) return {};
  return resolveNoteGeometry(note, *line, state.inclineSinr, state.floorPosition, line->bpm,
      viewport_.scrollSpeed, view.width, view.height);
}

float GameplaySession::effectiveJudgeArea(const Note& note, double chartTime) const noexcept {
  const auto view = stage();
  auto area = sanitizeJudgeArea(note.judgeArea);
  auto* line = note.master;
  if (!line || line->noteJudgeAreaControls.empty()) return area;
  const auto state = line->fillState(chartTime, view.aspect);
  const auto geometry = geometryFor(note, state, view);
  const auto control = line->calcNoteControl(geometry.controlHeight, JudgeLine::NOTE_CTRL_JUDGE_AREA, 1.0F);
  if (std::isfinite(control) && control > 0.0F) area *= control;
  return sanitizeJudgeArea(area);
}

float GameplaySession::touchPosition(const Note& note, float x, float y, double chartTime) const noexcept {
  auto* line = note.master;
  if (!line) return std::numeric_limits<float>::max();
  const auto view = stage();
  const auto state = line->fillState(chartTime, view.aspect);
  const auto lineX = view.left + state.xNorm * view.width;
  const auto lineY = view.top + state.yNorm * view.height;
  const auto radians = state.rotateDeg * PI / 180.0;
  const auto cosine = static_cast<float>(std::cos(radians));
  const auto sine = static_cast<float>(std::sin(radians));
  const auto alongPixels = (x - lineX) * cosine + (y - lineY) * sine;

  // Incline and X control only shape non-hold notes; a hold keeps its raw offset.
  const auto geometry = geometryFor(note, state, view);
  const auto notePixels = note.isHold
      ? static_cast<float>(note.positionX * PGR_WIDTH * view.width)
      : geometry.alongLineOffsetPx;

  const auto scale = JudgementEngine::judgePositionScalePx(viewport_.height, view.height, view.width);
  return std::abs(alongPixels / scale - notePixels / scale) / effectiveJudgeArea(note, chartTime);
}

float GameplaySession::judgeDistance(const Note& note, float x, float y, double chartTime) const noexcept {
  return judgeDistanceFromPosition(note, x, y, chartTime,
      touchPosition(note, x, y, chartTime));
}

float GameplaySession::judgeDistanceFromPosition(const Note& note, float x, float y,
    double chartTime, float position) const noexcept {
  auto* line = note.master;
  if (!line) return std::numeric_limits<float>::max();
  const auto view = stage();
  const auto state = line->fillState(chartTime, view.aspect);
  const auto lineX = view.left + state.xNorm * view.width;
  const auto lineY = view.top + state.yNorm * view.height;
  const auto radians = state.rotateDeg * PI / 180.0;
  const auto sine = static_cast<float>(std::sin(radians));
  const auto cosine = static_cast<float>(std::cos(radians));
  if (!std::isfinite(position)) return std::numeric_limits<float>::max();
  const auto dx = x - lineX;
  const auto dy = y - lineY;
  const auto scale = JudgementEngine::judgePositionScalePx(viewport_.height, view.height, view.width);
  const auto perpendicular = (-dx * sine + dy * cosine) / scale;
  return position + std::abs(perpendicular / JUDGE_DISTANCE_Y_SCALE);
}

void GameplaySession::activateHold(Note& note, bool perfect, double chartTime, double diff,
    bool recordPress) noexcept {
  note.holdActive = true;
  note.holdPerfect = perfect;
  note.holdPreJudge = false;
  note.holdFxActive = true;
  note.holdUpTimeSec = std::numeric_limits<double>::infinity();
  note.holdDiffSec = diff;
  note.safeFrame = HOLD_SAFE_FRAME_INIT;
  note.clicked = true;
  note.holdBroken = false;
  // The body burst timer restarts with the head, and the hold joins the small set the
  // per-frame burst work walks instead of scanning the whole chart.
  note.holdFxTimerSec = 0.0;
  if (std::find(holdFxNotes_.begin(), holdFxNotes_.end(), &note) == holdFxNotes_.end()) {
    holdFxNotes_.push_back(&note);
  }
  const auto activePosition = std::lower_bound(activeHolds_.begin(), activeHolds_.end(), &note,
      [](const Note* first, const Note* second) { return first->sortedIndex < second->sortedIndex; });
  if (activePosition == activeHolds_.end() || *activePosition != &note) {
    activeHolds_.insert(activePosition, &note);
  }
  const auto judgement = perfect ? JudgementEngine::Judgement::Perfect
                                 : JudgementEngine::Judgement::Good;
  judgement_.recordOffset(note, judgement, diff, true);
  judgement_.recordIndicator(note, judgement, diff, true);
  soundEvents_.push_back({NOTE_TAP, note.hitSound});
  // The reference spawns the head burst at the moment and place the head was hit; the
  // renderer resolves that position from the line state of the judged frame.
  holdHeadBursts_.push_back({note.sortedIndex, chartTime, perfect});
  if (recordPress) recordHoldPress(note, perfect, diff);
}

bool GameplaySession::commitNote(Note& note, JudgementEngine::Judgement judgement,
    double diff, double time, bool emitSound) noexcept {
  const auto committed = judgement_.commit(note, judgement, diff, time);
  if (committed) {
    judgementEvents_.push_back({note.sortedIndex, static_cast<int>(judgement)});
    if (emitSound && note.type != NOTE_HOLD && JudgementEngine::isHit(judgement)) {
      soundEvents_.push_back({note.type, note.hitSound});
    }
    recordJudgement(note, judgement, diff, time);
  }
  return committed;
}

void GameplaySession::recordJudgement(const Note& note, JudgementEngine::Judgement judgement,
    double diffSec, double chartTimeSec) {
  if (!replayRecording_ || note.sortedIndex < 0) return;
  ReplayJudgement entry;
  switch (note.type) {
    case NOTE_TAP: entry.type = ReplayJudgement::Type::Tap; break;
    case NOTE_DRAG: entry.type = ReplayJudgement::Type::Drag; break;
    case NOTE_HOLD: entry.type = ReplayJudgement::Type::HoldRelease; break;
    case NOTE_FLICK: entry.type = ReplayJudgement::Type::Flick; break;
    default: entry.type = ReplayJudgement::Type::Tap; break;
  }
  entry.noteIndex = note.sortedIndex;
  entry.judgement = static_cast<int>(judgement);
  // A hold release and a miss are timed at the frame they happened; every other judgement
  // is timed at the moment the note was actually hit, which is the note time minus the
  // signed timing delta.
  const bool atFrameTime = entry.type == ReplayJudgement::Type::HoldRelease
      || judgement == JudgementEngine::Judgement::Miss;
  entry.timeSec = atFrameTime
      ? chartTimeSec
      : JudgementEngine::chartTimeForJudgementDiff(note, diffSec, chartTimeSec);
  recordedReplay_.push_back(entry);
}

void GameplaySession::recordHoldPress(const Note& note, bool perfect, double diffSec) {
  if (!replayRecording_ || note.sortedIndex < 0) return;
  if (!recordedHoldPressNotes_.insert(note.sortedIndex).second) return;
  ReplayJudgement entry;
  entry.type = ReplayJudgement::Type::HoldPress;
  entry.noteIndex = note.sortedIndex;
  entry.judgement = perfect ? static_cast<int>(JudgementEngine::Judgement::Perfect)
                            : static_cast<int>(JudgementEngine::Judgement::Good);
  entry.timeSec = JudgementEngine::chartTimeForJudgementDiff(note, diffSec, note.sect);
  recordedReplay_.push_back(entry);
}

void GameplaySession::recordMissingHoldPresses() {
  if (!replayRecording_ || chart_ == nullptr) return;
  for (auto* note : activeHolds_) {
    if (note->type != NOTE_HOLD || !note->holdActive) continue;
    recordHoldPress(*note, note->holdPerfect, note->holdDiffSec);
  }
}

bool GameplaySession::touchBlocked(std::int64_t id) const noexcept {
  return blockedTouchIds_.find(id) != blockedTouchIds_.end();
}

std::vector<std::array<float, 2>> GameplaySession::blockedTouchPositions() const {
  std::vector<std::array<float, 2>> positions;
  positions.reserve(blockedTouches_.size());
  for (const auto& touch : blockedTouches_) positions.push_back({touch.x, touch.y});
  return positions;
}

void GameplaySession::setBlockTouchObserver(BlockTouchObserver observer) {
  blockTouchObserver_ = std::move(observer);
  // A new observer has missed every edge so far, so the next frame states the current one.
  blockTouchRepublish_ = blockTouchObserver_ != nullptr;
  publishedBlockTouch_ = false;
}

void GameplaySession::publishBlockTouchState(double time) {
  (void)time;
  const bool touching = !blockedTouches_.empty();
  if (!blockTouchRepublish_ && touching == publishedBlockTouch_) return;
  blockTouchRepublish_ = false;
  publishedBlockTouch_ = touching;
  if (blockTouchObserver_) blockTouchObserver_(touching);
}

void GameplaySession::updateBlocks(double time, float frameDeltaSec) {
  blockedTouches_.clear();
  // Zone state only feeds the per-finger touch block latch, so with no zone, or with no finger
  // down, nothing can be swallowed and no latch can survive; the renderer keeps its own zone
  // runtime for drawing, so skipping the pass here changes no drawn pixel either.
  if (blocks_.empty() || touches_.empty()) {
    blockedTouchIds_.clear();
    blockedTouchPulseSec_ = -1.0;
    publishBlockTouchState(time);
    return;
  }
  const auto view = stage();
  // Zones whose window is closed are reset, exactly as the reference does on the same cull:
  // a chapter chart can author more than ten thousand of them while only a handful are live
  // at any moment, and an entry that is out of its window has no runtime state worth keeping.
  for (auto& block : blocks_) {
    if (time < static_cast<double>(block.appearTime)
        || time >= static_cast<double>(block.disappearTime)) {
      block.resetRuntime();
      continue;
    }
    block.update(static_cast<float>(time), view.left, view.top, view.width, view.height,
        frameDeltaSec);
  }
  // Re-test only the fingers that are not latched yet: once a zone has swallowed a finger the
  // reference keeps it swallowed for the rest of the press, so dragging out does not restore
  // judging. The position list, in contrast, is rebuilt every frame from the latched ids, which
  // is why the pulse follows a finger that has already left the zone.
  for (auto& [id, touch] : touches_) {
    if (touch == nullptr) continue;
    if (blockedTouchIds_.find(id) == blockedTouchIds_.end()) {
      if (!OfficialBlock::tryGetBlockingBlock(blocks_, touch->x, touch->y, view.height)) continue;
      blockedTouchIds_.insert(id);
    }
    blockedTouches_.push_back({id, touch->x, touch->y});
  }
  std::erase_if(blockedTouchIds_, [this](std::int64_t id) {
    return touches_.find(id) == touches_.end();
  });
  // The list order follows the touch map, which is not stable across frames; sorting keeps the
  // renderer's output reproducible for a given set of fingers.
  std::sort(blockedTouches_.begin(), blockedTouches_.end(),
      [](const BlockedTouch& first, const BlockedTouch& second) { return first.id < second.id; });
  // The pulse expands from the last frame a finger was confirmed inside a zone. It is stamped
  // every such frame rather than only on entry, because the dwell itself is what the feedback
  // shows; releasing every finger clears it instead of leaving a stale timestamp behind.
  if (blockedTouches_.empty()) {
    blockedTouchPulseSec_ = -1.0;
  } else {
    blockedTouchPulseSec_ = time;
  }
  publishBlockTouchState(time);
}

void GameplaySession::updateChallengeTiming(float delta) noexcept {
  if (!challengeMode_) return;
  // The first frame only establishes the reference instant: there is no previous frame to
  // measure against, so it contributes no sample to the average.
  if (!challengeTimingStarted_) {
    challengeTimingStarted_ = true;
    return;
  }
  if (!std::isfinite(delta) || delta <= 0.0F) delta = 1.0F / 60.0F;
  frameTimes_[frameTimeIndex_] = static_cast<double>(delta);
  frameTimeIndex_ = (frameTimeIndex_ + 1) % frameTimes_.size();
  frameTimeCount_ = std::min(frameTimeCount_ + 1, frameTimes_.size());
  double sum{};
  for (std::size_t i = 0; i < frameTimeCount_; ++i) sum += frameTimes_[i];
  if (frameTimeCount_ > 0) challengeTimeSum_ = sum / static_cast<double>(frameTimeCount_) * 0.5;
}

void GameplaySession::update(double time, float frameDelta) {
  if (!chart_) return;
  // The challenge window tracks recent frame durations to absorb jitter in a human press.
  // Autoplay and replay do not judge from input, so they leave the window at its base value.
  const bool manualJudging = !replayPlayback_ && !autoplay_;
  if (manualJudging) updateChallengeTiming(frameDelta);
  updateBlocks(time, frameDelta);
  judgement_.setChallengeTimeSum(challengeTimeSum_);
  judgement_.setVisualTime(time);

  const auto flickSpeed = JudgementEngine::flickJudgeSpeed(viewport_.dpi);
  for (auto& [id, touch] : touches_) {
    (void)id;
    touch->flick.advanceFrame(frameDelta, flickSpeed);
  }

  if (replayPlayback_) {
    // A replay re-applies its recorded judgements; it never synthesises touches, so the
    // viewer's window, speed and input path cannot change the outcome.
    applyReplayEntries(time);
  } else if (autoplay_) {
    auto& notes = chart_->allNotesSorted;
    while (autoNoteIndex_ < notes.size() && notes[autoNoteIndex_]->sect <= time) {
      auto& note = *notes[autoNoteIndex_];
      if (note.judgeResult < 0) {
        if (note.type == NOTE_HOLD) {
          // The hold head is hit at its own note time, not at the frame that noticed it, so
          // the head burst sits where the head is drawn and the recorded delta is zero.
          activateHold(note, true, note.sect, 0.0, false);
        } else {
          commitNote(note, JudgementEngine::Judgement::Perfect, time - note.sect, time);
        }
      }
      ++autoNoteIndex_;
    }
    if (replayRecording_) recordMissingHoldPresses();
  } else {
    const auto view = stage();
    judgeNewTouches(time, view);
    judgeDrags(time, view);
    judgeFlicks(time, view);
    judgeTapHold(time, view);
    if (replayRecording_) recordMissingHoldPresses();
  }

  // Hold body feedback and tail settlement run on every judgement source, so a replay
  // shows the same hold feedback a live run did.
  updateHoldBodyBursts(time, frameDelta);
  settleHoldTails(time);
  detectMisses(time);
}

void GameplaySession::setReplayRecording(bool enabled) noexcept {
  replayRecording_ = enabled;
  if (!enabled) {
    recordedReplay_.clear();
    recordedHoldPressNotes_.clear();
  }
}

void GameplaySession::clearRecordedReplay() noexcept {
  recordedReplay_.clear();
  recordedHoldPressNotes_.clear();
}

void GameplaySession::setReplayPlayback(std::vector<ReplayJudgement> entries) {
  replayEntries_ = std::move(entries);
  std::stable_sort(replayEntries_.begin(), replayEntries_.end(),
      [](const ReplayJudgement& first, const ReplayJudgement& second) {
        return first.timeSec < second.timeSec;
      });
  replayPlayback_ = !replayEntries_.empty();
  replayCursor_ = 0;
  judgement_.setReplayPlayback(replayPlayback_);
}

void GameplaySession::clearReplayPlayback() noexcept {
  replayEntries_.clear();
  replayPlayback_ = false;
  replayCursor_ = 0;
  judgement_.setReplayPlayback(false);
}

std::vector<HoldBodyBurst> GameplaySession::takeHoldBodyBursts() {
  std::vector<HoldBodyBurst> bursts;
  bursts.swap(holdBodyBursts_);
  return bursts;
}

std::vector<HoldHeadBurst> GameplaySession::takeHoldHeadBursts() {
  std::vector<HoldHeadBurst> bursts;
  bursts.swap(holdHeadBursts_);
  return bursts;
}

void GameplaySession::applyReplayEntries(double time) {
  auto& notes = chart_->allNotesSorted;
  while (replayCursor_ < replayEntries_.size()) {
    const auto& entry = replayEntries_[replayCursor_];
    if (!(entry.timeSec <= time)) break;
    if (entry.noteIndex >= 0 && entry.noteIndex < static_cast<int>(notes.size())) {
      Note& note = *notes[static_cast<std::size_t>(entry.noteIndex)];
      if (note.judgeResult < 0) {
        const auto judgement = static_cast<JudgementEngine::Judgement>(
            std::clamp(entry.judgement, 0, 3));
        if (note.type == NOTE_HOLD) {
          if (entry.type == ReplayJudgement::Type::HoldPress) {
            if (!note.holdActive && JudgementEngine::isHit(judgement)) {
              // The recorded press time is the effect time, exactly like a manual head hit.
              activateHold(note, judgement == JudgementEngine::Judgement::Perfect,
                  entry.timeSec, note.sect - entry.timeSec, false);
            }
          } else if (JudgementEngine::isHit(judgement)) {
            // A good hold release only arms the tail; the official settlement window then
            // commits it with the head's judgement.
            note.holdPreJudge = true;
          } else {
            note.holdActive = false;
            note.holdPreJudge = false;
            commitNote(note, judgement, note.sect - entry.timeSec, entry.timeSec);
          }
        } else {
          commitNote(note, judgement, note.sect - entry.timeSec, entry.timeSec);
        }
      }
    }
    ++replayCursor_;
  }
}

double GameplaySession::holdBodyIntervalSec(const Note& note) noexcept {
  const double bpm = note.master != nullptr && note.master->bpm > 0.0 ? note.master->bpm : 120.0;
  return 0.5 * 60.0 / bpm;
}

double GameplaySession::realFrameDeltaSec(float frameDeltaSec) const noexcept {
  // The chart clock advances at `musicSpeed` times real time, so the real time a frame took
  // is the chart delta divided by that speed. A non-positive or absurdly large result means
  // the clock stalled or jumped, and the nominal frame duration is used instead.
  double realDelta = static_cast<double>(frameDeltaSec) / std::max(0.001, musicSpeed_);
  if (!(realDelta > 0.0) || realDelta > 0.1) realDelta = 1.0 / 60.0;
  return realDelta;
}

void GameplaySession::updateHoldBodyBursts(double time, float frameDeltaSec) {
  if (holdFxNotes_.empty()) return;
  const double realDelta = realFrameDeltaSec(frameDeltaSec);
  std::size_t retained = 0;
  for (std::size_t index = 0; index < holdFxNotes_.size(); ++index) {
    Note* note = holdFxNotes_[index];
    if (note == nullptr || !note->holdFxActive
        || note->judgeResult == static_cast<int>(JudgementEngine::Judgement::Miss)
        || note->holdBroken || time >= note->holdEndTime) {
      if (note != nullptr) note->holdFxActive = false;
      continue;
    }
    if (time <= note->sect) {
      note->holdFxTimerSec = 0.0;
      holdFxNotes_[retained++] = note;
      continue;
    }
    const double interval = holdBodyIntervalSec(*note);
    if (!std::isfinite(interval) || interval <= 0.0) {
      note->holdFxActive = false;
      continue;
    }
    note->holdFxTimerSec += realDelta;
    if (note->holdFxTimerSec >= interval - 1.0e-5) {
      holdBodyBursts_.push_back({note->sortedIndex, time, note->holdPerfect});
      note->holdFxTimerSec = 0.0;
      constexpr std::size_t kMaxPendingHoldBursts = 64;
      if (holdBodyBursts_.size() > kMaxPendingHoldBursts) {
        holdBodyBursts_.erase(holdBodyBursts_.begin(),
            holdBodyBursts_.begin()
                + static_cast<std::ptrdiff_t>(holdBodyBursts_.size() - kMaxPendingHoldBursts));
      }
    }
    holdFxNotes_[retained++] = note;
  }
  holdFxNotes_.resize(retained);
}

void GameplaySession::judgeNewTouches(double time, const Stage& view) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  const auto horizon = time + HORIZON_SEC;
  (void)view;

  for (auto& [id, touch] : touches_) {
    // A finger that went down inside a block zone never judges anything: the reference takes
    // it out of the judge list for the whole press. Without this the zone behaves like an
    // ordinary part of the play field, which is exactly the wrong behaviour the zones exist
    // to prevent.
    if (blockedTouchIds_.find(id) != blockedTouchIds_.end()) continue;
    if (!touch->started) continue;

    Note* best{};
    double bestDelta{};
    auto bestAbsolute = std::numeric_limits<double>::max();
    auto bestDistance = std::numeric_limits<float>::max();

    for (std::size_t i = judgeCursor_; i < notes.size(); ++i) {
      auto& note = *notes[i];
      if (note.judgeResult >= 0 || note.isJudged || note.holdActive) continue;
      if (note.sect > horizon) break;
      if (note.type != NOTE_TAP && note.type != NOTE_HOLD && note.type != NOTE_DRAG
          && note.type != NOTE_FLICK) {
        continue;
      }
      const auto delta = note.sect - time;
      // The scan window is asymmetric: GOOD window early, BAD window late.
      if (delta <= -range.good) continue;
      if (delta >= range.bad) continue;

      const auto position = touchPosition(note, touch->x, touch->y, time);
      if (position >= RADIUS_SPATIAL_LIMIT) continue;
      // Practice window overrides shrink or widen the spatial falloff for taps and holds,
      // exactly like the classification that follows.
      const bool tapOrHold = note.type == NOTE_TAP || note.type == NOTE_HOLD;
      const double perfectWindow =
          tapOrHold ? judgement_.tapPerfectWindow() : range.perfect;
      if (delta > JudgementEngine::spatialJudgeWindow(perfectWindow, range.bad, position)) {
        continue;
      }
      const auto absolute = std::abs(delta);
      auto distance = judgeDistanceFromPosition(note, touch->x, touch->y, time, position);
      if (!std::isfinite(distance)) distance = position;
      if (JudgementEngine::checkNoteCandidateReplaces(best != nullptr, best ? best->type : 0,
          bestDelta, bestAbsolute, note.type, delta, absolute, bestDistance, distance)) {
        best = &note;
        bestDelta = delta;
        bestAbsolute = absolute;
        bestDistance = distance;
      }
    }
    if (best && best->type != NOTE_FLICK) best->isJudged = true;
    touch->started = false;
  }
}

void GameplaySession::judgeDrags(double time, const Stage& view) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  const auto horizon = time + HORIZON_SEC;
  (void)view;

  for (std::size_t i = judgeCursor_; i < notes.size(); ++i) {
    auto& note = *notes[i];
    if (note.judgeResult >= 0) continue;
    if (note.sect > horizon) break;
    if (note.type != NOTE_DRAG || note.holdActive) continue;
    const auto delta = note.sect - time;
    if (delta > range.bad) continue;

    note.preJudge = JudgementEngine::dragLocalJudged(note.preJudge || note.dragPrimed, note.isJudged);
    if (!note.preJudge && std::abs(delta) <= DRAG_MISS_THRESHOLD) {
      for (const auto& [id, touch] : touches_) {
        if (blockedTouchIds_.find(id) != blockedTouchIds_.end()) continue;
        if (touchPosition(note, touch->x, touch->y, time) < RADIUS_DRAG_PRIME) {
          note.preJudge = true;
          note.dragPrimed = true;
          break;
        }
      }
    }
    if (note.preJudge && delta < 0.005) {
      note.preJudge = false;
      commitNote(note, JudgementEngine::Judgement::Perfect, 0.0, time);
      continue;
    }
    if (!note.preJudge && delta < -DRAG_MISS_THRESHOLD) {
      commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
    }
  }
}

void GameplaySession::judgeFlicks(double time, const Stage& view) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  const auto horizon = time + HORIZON_SEC;
  const auto flickMissLimit = range.perfect * 1.75;
  (void)view;

  for (std::size_t i = judgeCursor_; i < notes.size(); ++i) {
    auto& note = *notes[i];
    if (note.judgeResult >= 0) continue;
    if (note.sect > horizon) break;
    if (note.type != NOTE_FLICK || note.holdActive) continue;
    const auto delta = note.sect - time;

    // A primed flick confirms before the late-miss branch is considered.
    if (note.preJudge && delta < 0.005) {
      note.preJudge = false;
      commitNote(note, JudgementEngine::Judgement::Perfect, 0.0, time);
      continue;
    }
    if (delta < -flickMissLimit) {
      commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
      continue;
    }
    if (delta >= flickMissLimit || delta <= -flickMissLimit) continue;

    if (!note.preJudge) {
      for (auto& [id, touch] : touches_) {
        if (blockedTouchIds_.find(id) != blockedTouchIds_.end()) continue;
        if (!touch->flick.newFlick() || touch->flick.flicked()) continue;
        if (touchPosition(note, touch->x, touch->y, time) >= RADIUS_FLICK_PRIME) continue;

        auto distance = judgeDistance(note, touch->x, touch->y, time);
        if (!std::isfinite(distance)) distance = touchPosition(note, touch->x, touch->y, time);
        Note* selected = &note;
        auto selectedDelta = delta;
        auto selectedAbsolute = std::abs(delta);
        bool nearSelected = false;

        // A flick can snap to a neighbouring flick within the same 0.01s cluster when
        // that neighbour is the better spatial match.
        for (auto* near : note.nearNotes) {
          if (!near || near->judgeResult >= 0 || near->preJudge) continue;
          const auto nearDelta = near->sect - time;
          if (nearDelta >= flickMissLimit) break;
          const auto nearPosition = touchPosition(*near, touch->x, touch->y, time);
          if (nearPosition >= RADIUS_FLICK_PRIME) continue;
          auto nearDistance = judgeDistance(*near, touch->x, touch->y, time);
          if (!std::isfinite(nearDistance)) nearDistance = nearPosition;
          if (JudgementEngine::checkFlickCandidateReplaces(true, selectedDelta, selectedAbsolute,
              nearDelta, distance, nearDistance)) {
            distance = nearDistance;
            selected = near;
            selectedDelta = nearDelta;
            selectedAbsolute = std::abs(nearDelta);
            nearSelected = true;
          }
        }

        selected->preJudge = true;
        touch->flick.consumeFlick();
        if (!nearSelected) break;
      }
    }
    if (note.preJudge && delta < 0.005) {
      note.preJudge = false;
      commitNote(note, JudgementEngine::Judgement::Perfect, 0.0, time);
    }
  }
}

void GameplaySession::judgeTapHold(double time, const Stage& view) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  const auto horizon = time + HORIZON_SEC;
  (void)view;

  for (std::size_t i = judgeCursor_; i < notes.size(); ++i) {
    auto& note = *notes[i];
    if (note.judgeResult >= 0) continue;
    if (note.sect > horizon) break;

    // A primed drag settles on its own branch so it cannot fall into the tap path.
    if (note.dragPrimed && note.type == NOTE_DRAG) {
      const auto delta = note.sect - time;
      if (delta < 0.005) {
        commitNote(note, JudgementEngine::Judgement::Perfect, 0.0, time);
      } else if (delta < -DRAG_MISS_THRESHOLD) {
        commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
      }
      continue;
    }
    if (note.type != NOTE_TAP && note.type != NOTE_HOLD) continue;

    const auto delta = note.sect - time;
    // The settlement limits use the same practice-aware windows as the classification.
    const double perfectWindow = judgement_.tapPerfectWindow();
    const double goodWindow = judgement_.tapGoodWindow();

    if (note.type == NOTE_HOLD && note.holdActive) {
      // The body is held while any continuous touch is close enough; a small frame budget
      // absorbs momentary finger jitter before the hold actually breaks.
      note.holdBroken = true;
      for (const auto& [id, touch] : touches_) {
        // A blocked finger cannot hold a body either: the reference removes it from the
        // judge list, so a hold under a zone breaks the same way it would with no finger.
        if (blockedTouchIds_.find(id) != blockedTouchIds_.end()) continue;
        if (touchPosition(note, touch->x, touch->y, time) < RADIUS_HOLD_BODY) {
          note.holdBroken = false;
          note.safeFrame = HOLD_SAFE_FRAME_INIT;
          break;
        }
      }
      if (note.holdBroken) {
        if (note.safeFrame < 0) {
          note.holdActive = false;
          note.clicked = false;
          commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
        } else {
          --note.safeFrame;
          note.holdBroken = false;
        }
      }
      continue;
    }

    if (note.isJudged) {
      const auto absolute = std::abs(delta);
      if (absolute < perfectWindow) {
        if (note.type == NOTE_TAP) {
          commitNote(note, JudgementEngine::Judgement::Perfect, delta, time);
        } else {
          activateHold(note, true, time, delta, true);
        }
        note.statOffset = delta;
      } else if (absolute < goodWindow) {
        if (note.type == NOTE_TAP) {
          commitNote(note, JudgementEngine::Judgement::Good, delta, time);
        } else {
          activateHold(note, false, time, delta, true);
        }
        note.statOffset = delta;
      } else if (note.type != NOTE_HOLD) {
        commitNote(note, JudgementEngine::Judgement::Bad, delta, time);
        note.statOffset = delta;
      } else {
        // Hold heads have no BAD judgement: release the claim and keep waiting.
        note.isJudged = false;
      }
    }

    if (!note.isJudged && !note.holdActive && note.judgeResult < 0
        && delta < -judgement_.lateMissLimitForNote(note.type)) {
      commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
    }
  }
}

void GameplaySession::settleHoldTails(double time) {
  for (auto* note : activeHolds_) {
    if (!note->holdActive || note->judgeResult >= 0) continue;
    if (!note->clicked && time >= note->sect) note->clicked = true;
    if (time > note->holdEndTime - HOLD_TAIL_EARLY_SETTLE) note->holdPreJudge = true;
  }
  for (auto* note : activeHolds_) {
    if (!note->holdActive || !note->holdPreJudge || note->judgeResult >= 0) continue;
    if (time > note->holdEndTime - HOLD_TAIL_EARLY_SETTLE) {
      const auto judgement = note->holdPerfect ? JudgementEngine::Judgement::Perfect
                                              : JudgementEngine::Judgement::Good;
      note->holdActive = false;
      note->holdPreJudge = false;
      commitNote(*note, judgement, note->holdDiffSec, time);
    }
  }
  std::erase_if(activeHolds_, [](const Note* note) {
    return !note->holdActive || note->judgeResult >= 0;
  });
}

void GameplaySession::detectMisses(double time) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  while (judgeCursor_ < notes.size()) {
    auto& note = *notes[judgeCursor_];
    if (note.isFake) { ++judgeCursor_; continue; }
    if (note.judgeResult >= 0) { ++judgeCursor_; continue; }
    if (note.holdActive) break;
    if (time - note.sect > judgement_.lateMissLimitForNote(note.type)) {
      commitNote(note, JudgementEngine::Judgement::Miss, 0.25, time);
      ++judgeCursor_;
      continue;
    }
    break;
  }
}

std::vector<int> GameplaySession::takeSoundEvents() {
  std::vector<int> events;
  for (const auto& event : takeDetailedSoundEvents()) events.push_back(event.type);
  return events;
}

std::vector<GameplaySession::SoundEvent> GameplaySession::takeDetailedSoundEvents() {
  std::vector<SoundEvent> events;
  events.swap(soundEvents_);
  return events;
}

std::vector<JudgementEvent> GameplaySession::takeJudgementEvents() {
  std::vector<JudgementEvent> events;
  events.swap(judgementEvents_);
  return events;
}
} // namespace rechigros::game
