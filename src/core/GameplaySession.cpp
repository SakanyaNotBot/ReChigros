#include "rechigros/game/GameplaySession.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
  frameTimes_.fill(0.0);
  soundEvents_.clear();
  cancelTouches();
  if (chart_) chart_->resetRuntime();
  blocks_ = chart_ ? chart_->blockAreaList : std::vector<OfficialBlock>{};
  for (auto& block : blocks_) block.resetRuntime();
  blockedTouchIds_.clear();
  judgement_.reset(chart_ ? static_cast<int>(chart_->allNotesSorted.size()) : 0, challengeMode);
  judgement_.setAutoplay(autoplay_);
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
}

void GameplaySession::cancelTouches() noexcept {
  touches_.clear();
  blockedTouchIds_.clear();
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
  auto* line = note.master;
  if (!line) return std::numeric_limits<float>::max();
  const auto view = stage();
  const auto state = line->fillState(chartTime, view.aspect);
  const auto lineX = view.left + state.xNorm * view.width;
  const auto lineY = view.top + state.yNorm * view.height;
  const auto radians = state.rotateDeg * PI / 180.0;
  const auto sine = static_cast<float>(std::sin(radians));
  const auto cosine = static_cast<float>(std::cos(radians));
  const auto horizontal = touchPosition(note, x, y, chartTime);
  if (!std::isfinite(horizontal)) return std::numeric_limits<float>::max();
  const auto dx = x - lineX;
  const auto dy = y - lineY;
  const auto scale = JudgementEngine::judgePositionScalePx(viewport_.height, view.height, view.width);
  const auto perpendicular = (-dx * sine + dy * cosine) / scale;
  return horizontal + std::abs(perpendicular / JUDGE_DISTANCE_Y_SCALE);
}

void GameplaySession::activateHold(Note& note, bool perfect, double chartTime, double diff) noexcept {
  note.holdActive = true;
  note.holdPerfect = perfect;
  note.holdPreJudge = false;
  note.holdFxActive = true;
  note.holdUpTimeSec = std::numeric_limits<double>::infinity();
  note.holdDiffSec = diff;
  note.safeFrame = HOLD_SAFE_FRAME_INIT;
  note.clicked = true;
  note.holdBroken = false;
  const auto judgement = perfect ? JudgementEngine::Judgement::Perfect
                                 : JudgementEngine::Judgement::Good;
  judgement_.recordOffset(note, judgement, diff, true);
  judgement_.recordIndicator(note, judgement, diff, true);
  soundEvents_.push_back(NOTE_TAP);
  (void)chartTime;
}

bool GameplaySession::commitNote(Note& note, JudgementEngine::Judgement judgement,
    double diff, double time, bool emitSound) noexcept {
  const auto committed = judgement_.commit(note, judgement, diff, time);
  if (committed && emitSound && note.type != NOTE_HOLD && JudgementEngine::isHit(judgement)) {
    soundEvents_.push_back(note.type);
  }
  return committed;
}

bool GameplaySession::touchBlocked(std::int64_t id) const noexcept {
  return blockedTouchIds_.find(id) != blockedTouchIds_.end();
}

void GameplaySession::updateBlocks(double time, float frameDeltaSec) {
  if (blocks_.empty()) {
    blockedTouchIds_.clear();
    return;
  }
  const auto view = stage();
  for (auto& block : blocks_) {
    block.update(static_cast<float>(time), view.left, view.top, view.width, view.height,
        frameDeltaSec);
  }
  // Latch the block state per finger for the rest of its press: the reference removes a
  // blocked finger from the judge list entirely rather than re-testing each frame.
  for (auto& [id, touch] : touches_) {
    if (blockedTouchIds_.find(id) != blockedTouchIds_.end()) continue;
    if (!touch->started) continue;
    if (OfficialBlock::tryGetBlockingBlock(blocks_, touch->x, touch->y, view.height)) {
      blockedTouchIds_.insert(id);
    }
  }
  std::erase_if(blockedTouchIds_, [this](std::int64_t id) {
    return touches_.find(id) == touches_.end();
  });
}

void GameplaySession::updateChallengeTiming(float delta) noexcept {
  if (!challengeMode_) return;
  if (!std::isfinite(delta) || delta <= 0.0F) delta = 0.0F;
  frameTimes_[frameTimeIndex_] = static_cast<double>(delta);
  frameTimeIndex_ = (frameTimeIndex_ + 1) % frameTimes_.size();
  frameTimeCount_ = std::min(frameTimeCount_ + 1, frameTimes_.size());
  double sum{};
  for (std::size_t i = 0; i < frameTimeCount_; ++i) sum += frameTimes_[i];
  if (frameTimeCount_ > 0) challengeTimeSum_ = sum / static_cast<double>(frameTimeCount_) * 0.5;
}

void GameplaySession::update(double time, float frameDelta) {
  if (!chart_) return;
  updateChallengeTiming(frameDelta);
  updateBlocks(time, frameDelta);
  judgement_.setChallengeTimeSum(challengeTimeSum_);
  judgement_.setVisualTime(time);

  const auto flickSpeed = JudgementEngine::flickJudgeSpeed(viewport_.dpi);
  for (auto& [id, touch] : touches_) {
    (void)id;
    touch->flick.advanceFrame(frameDelta, flickSpeed);
  }

  if (autoplay_) {
    auto& notes = chart_->allNotesSorted;
    while (autoNoteIndex_ < notes.size() && notes[autoNoteIndex_]->sect <= time) {
      auto& note = *notes[autoNoteIndex_];
      if (note.judgeResult < 0) {
        if (note.type == NOTE_HOLD) {
          soundEvents_.push_back(NOTE_TAP);
          activateHold(note, true, time, 0.0);
        } else {
          commitNote(note, JudgementEngine::Judgement::Perfect, time - note.sect, time, false);
        }
      }
      ++autoNoteIndex_;
    }
  } else {
    const auto view = stage();
    judgeNewTouches(time, view);
    judgeDrags(time, view);
    judgeFlicks(time, view);
    judgeTapHold(time, view);
  }

  settleHoldTails(time);
  detectMisses(time);
}

void GameplaySession::judgeNewTouches(double time, const Stage& view) {
  auto& notes = chart_->allNotesSorted;
  const auto range = judgement_.windows();
  const auto horizon = time + HORIZON_SEC;
  (void)view;

  for (auto& [id, touch] : touches_) {
    (void)id;
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
      auto distance = judgeDistance(note, touch->x, touch->y, time);
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
        (void)id;
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
        (void)id;
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
        (void)id;
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
          activateHold(note, true, time, delta);
        }
        note.statOffset = delta;
      } else if (absolute < goodWindow) {
        if (note.type == NOTE_TAP) {
          commitNote(note, JudgementEngine::Judgement::Good, delta, time);
        } else {
          activateHold(note, false, time, delta);
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
  for (auto* note : chart_->allNotesSorted) {
    if (!note->holdActive || note->judgeResult >= 0) continue;
    if (!note->clicked && time >= note->sect) note->clicked = true;
    // Settlement lands slightly before the nominal tail so a release is never late.
    if (time > note->holdEndTime - HOLD_TAIL_EARLY_SETTLE) note->holdPreJudge = true;
  }
  for (auto* note : chart_->allNotesSorted) {
    if (!note->holdActive || !note->holdPreJudge || note->judgeResult >= 0) continue;
    if (time > note->holdEndTime - HOLD_TAIL_EARLY_SETTLE) {
      const auto judgement = note->holdPerfect ? JudgementEngine::Judgement::Perfect
                                              : JudgementEngine::Judgement::Good;
      note->holdActive = false;
      note->holdPreJudge = false;
      commitNote(*note, judgement, note->holdDiffSec, time);
    }
  }
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

bool GameplaySession::finished(double time) const noexcept {
  if (!chart_ || chart_->allNotesSorted.empty()) return false;
  const auto result = judgement_.result();
  return result.perfect + result.good + result.bad + result.miss >= result.totalNotes
      && time >= chart_->chartEndTimeSec;
}

std::vector<int> GameplaySession::takeSoundEvents() {
  std::vector<int> events;
  events.swap(soundEvents_);
  return events;
}
} // namespace rechigros::game
