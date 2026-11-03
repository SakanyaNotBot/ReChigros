#pragma once

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/HoldBodyBurst.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/JudgementEvent.hpp"
#include "rechigros/game/NoteGeometry.hpp"
#include "rechigros/game/ReplayLog.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rechigros::game {
class GameplaySession final {
public:
  struct Viewport {
    float width{1280.0F};
    float height{720.0F};
    float targetAspect{16.0F / 9.0F};
    float scrollSpeed{1.0F};
    float dpi{380.0F};
    float keyScale{1.0F};
  };

  GameplaySession() = default;

  void setChart(Chart* chart, bool challengeMode = false);
  void setViewport(Viewport viewport) noexcept;
  // Music playback speed. The frame delta the session receives is a chart-time delta, so
  // the real time that elapsed is that delta divided by this speed. Hold body feedback is
  // driven by real time and therefore needs it.
  void setMusicSpeed(double value) noexcept;
  [[nodiscard]] double musicSpeed() const noexcept { return musicSpeed_; }
  void setAutoplay(bool value) noexcept;

  void touchDown(std::int64_t id, float x, float y, bool started = true);
  void touchMove(std::int64_t id, float x, float y) noexcept;
  void touchUp(std::int64_t id) noexcept;
  void cancelTouches() noexcept;
  void settleTouches() noexcept;
  void update(double chartTimeSec, float frameDeltaSec);

  [[nodiscard]] Chart* chart() const noexcept { return chart_; }
  [[nodiscard]] const JudgementEngine& judgement() const noexcept { return judgement_; }
  [[nodiscard]] JudgementEngine& judgement() noexcept { return judgement_; }
  [[nodiscard]] bool autoplay() const noexcept { return autoplay_; }
  struct SoundEvent {
    int type{};
    std::string hitSound;
  };
  std::vector<int> takeSoundEvents();
  std::vector<SoundEvent> takeDetailedSoundEvents();
  std::vector<JudgementEvent> takeJudgementEvents();

  [[nodiscard]] float touchPosition(const Note& note, float x, float y, double chartTimeSec) const noexcept;
  [[nodiscard]] float judgeDistance(const Note& note, float x, float y, double chartTimeSec) const noexcept;
  // Perpendicular component only: the caller already knows the along-line falloff, so this
  // avoids evaluating the note geometry a second time in the touch scan.
  [[nodiscard]] float judgeDistanceFromPosition(const Note& note, float x, float y,
      double chartTimeSec, float position) const noexcept;
  [[nodiscard]] float effectiveJudgeArea(const Note& note, double chartTimeSec) const noexcept;

  // --- replay model -----------------------------------------------------------
  // Recording appends one entry per judgement plus a press entry when a hold head
  // activates. Playback re-applies the recorded judgements in chart-time order instead of
  // synthesising touches, so a replay is judged the same way on any device and the modes
  // recorded in its metadata stay display-only.
  void setReplayRecording(bool enabled) noexcept;
  [[nodiscard]] bool replayRecording() const noexcept { return replayRecording_; }
  [[nodiscard]] const std::vector<ReplayJudgement>& recordedReplay() const noexcept {
    return recordedReplay_;
  }
  void clearRecordedReplay() noexcept;

  // Entries must be sorted by time; an empty list turns playback off.
  void setReplayPlayback(std::vector<ReplayJudgement> entries);
  void clearReplayPlayback() noexcept;
  [[nodiscard]] bool replayPlayback() const noexcept { return replayPlayback_; }

  // One hold-body burst produced during the last update. The caller spawns the hit effect
  // for the note; the index addresses Chart::allNotesSorted.
  std::vector<HoldBodyBurst> takeHoldBodyBursts();

  // One hold-head burst produced during the last update. The renderer resolves its
  // position from the judged frame and draws the effect there.
  std::vector<HoldHeadBurst> takeHoldHeadBursts();

  // Red-zone state, updated each frame in the current stage geometry. `render` draws from
  // Chart::blockAreaList; judgement consults blockedTouchIds().
  [[nodiscard]] const std::vector<OfficialBlock>& blocks() const noexcept { return blocks_; }
  [[nodiscard]] bool touchBlocked(std::int64_t id) const noexcept;

  // One finger a red zone has swallowed, in screen pixels. The zone pulse is drawn at these
  // points and the audio duck keys off the list being non-empty.
  struct BlockedTouch {
    std::int64_t id{};
    float x{};
    float y{};
  };

  // Fingers swallowed by a zone on the last frame, in screen pixels. A finger stays here for
  // the rest of its press even after leaving the zone, so the feedback does not flicker while
  // it moves.
  [[nodiscard]] const std::vector<BlockedTouch>& blockedTouches() const noexcept {
    return blockedTouches_;
  }
  [[nodiscard]] std::vector<BlockedTouch> liveTouches() const;
  // Whether a pointer id already has a live touch. Re-seating a held pointer must not reset
  // the touch that is already judging.
  [[nodiscard]] bool hasTouch(std::int64_t id) const noexcept {
    return touches_.find(id) != touches_.end();
  }

  // Receives the swallowed-finger edge. The audio duck and the effect chain both react to the
  // transition rather than the level, and the session is the only place that knows the edge:
  // the caller used to derive it from blockedTouches(), which is empty whenever no zone is
  // authored, so a latch released by a lift was never reported and the two stayed out of step.
  // The observer is called once per update with the new state; returning true asks the session
  // to re-publish the current state on the next frame, which keeps an observer that attaches
  // mid-run from missing the edge that is already in effect.
  using BlockTouchObserver = std::function<void(bool touchingAnyBlock)>;
  void setBlockTouchObserver(BlockTouchObserver observer);
  // Chart time the swallowed fingers were last confirmed, or -1 while none is held. The pulse
  // expands from this instant, and the reference re-stamps it every frame of the press.
  [[nodiscard]] double blockedTouchPulseSec() const noexcept { return blockedTouchPulseSec_; }
  [[nodiscard]] std::vector<std::array<float, 2>> blockedTouchPositions() const;

private:
  // Touch position arrives in screen pixels; the flick tracker consumes camera world
  // units so its speed threshold is independent of the screen resolution.
  struct Touch {
    float x{}, y{};
    bool started{true};
    JudgementEngine::FlickTracker flick;
    Touch(float px, float py, float worldX, float worldY) : x(px), y(py), flick(worldX, worldY) {}
  };
  struct Stage { float left{}, top{}, width{}, height{}, aspect{16.0F / 9.0F}; };

  [[nodiscard]] Stage stage() const noexcept;
  [[nodiscard]] float fingerWorldScale() const noexcept;
  [[nodiscard]] bool inStage(float x, float y) const noexcept;
  [[nodiscard]] NoteGeometry geometryFor(const Note& note, const JudgeLine::State& state,
      const Stage& view) const noexcept;
  void activateHold(Note& note, bool perfect, double chartTimeSec, double diffSec,
      bool recordPress) noexcept;
  bool commitNote(Note& note, JudgementEngine::Judgement judgement,
      double diffSec, double chartTimeSec, bool emitSound = true) noexcept;
  // Replay recording hooks: every commit produces an entry, and every hold head that is
  // still active but not yet recorded gets a press entry (which covers a hold that was
  // already active when recording started).
  void recordJudgement(const Note& note, JudgementEngine::Judgement judgement, double diffSec,
      double chartTimeSec);
  void recordHoldPress(const Note& note, bool perfect, double diffSec);
  void recordMissingHoldPresses();
  // Replay playback: applies every recorded entry whose time has come.
  void applyReplayEntries(double time);
  // Hold-body burst timer, as the reference drives it from the hold movement update.
  void updateHoldBodyBursts(double time, float frameDeltaSec);
  [[nodiscard]] static double holdBodyIntervalSec(const Note& note) noexcept;
  // Real seconds elapsed over a frame whose chart clock advanced by `frameDeltaSec`.
  [[nodiscard]] double realFrameDeltaSec(float frameDeltaSec) const noexcept;
  void updateChallengeTiming(float frameDeltaSec) noexcept;
  void judgeNewTouches(double time, const Stage& view);
  void judgeDrags(double time, const Stage& view);
  void judgeFlicks(double time, const Stage& view);
  void judgeTapHold(double time, const Stage& view);
  void settleHoldTails(double time);
  void detectMisses(double time);
  void updateBlocks(double time, float frameDeltaSec);
  // Publishes the current swallowed-finger state to the audio filter and the effect chain.
  // Runs on every path that can change it, including the early return taken when no zone is
  // authored, because the falling edge is what the two listeners act on.
  // Both react to the edge, not the level, so this runs once per frame from every path that
  // can change it -- including the early return taken when no zone is authored.
  void publishBlockTouchState(double time);

  Chart* chart_{};
  Viewport viewport_{};
  JudgementEngine judgement_{};
  std::unordered_map<std::int64_t, std::unique_ptr<Touch>> touches_;
  BlockTouchObserver blockTouchObserver_;
  // Last published swallowed state. Starts false so an observer attached before the first
  // frame is not told about an edge that has not happened.
  bool publishedBlockTouch_{};
  bool blockTouchRepublish_{};
  std::size_t judgeCursor_{};
  bool challengeMode_{};
  bool autoplay_{};
  double musicSpeed_{1.0};
  double challengeTimeSum_{};
  std::array<double, 10> frameTimes_{};
  // The challenge window is the mean of the last few frame durations, so the first frame
  // has nothing to average yet and only arms the measurement.
  bool challengeTimingStarted_{};
  std::vector<SoundEvent> soundEvents_;
  std::vector<JudgementEvent> judgementEvents_;
  std::size_t frameTimeIndex_{}, frameTimeCount_{};
  std::size_t autoNoteIndex_{};
  std::vector<OfficialBlock> blocks_;
  // Fingers latched inside a red zone emit no judge candidates for the rest of the press.
  std::unordered_set<std::int64_t> blockedTouchIds_;
  // The same set as positions, refreshed every frame for the renderer and the audio duck.
  std::vector<BlockedTouch> blockedTouches_;
  double blockedTouchPulseSec_{-1.0};
  // Replay state. `recordedHoldPressNotes_` holds note indices that already produced a
  // press entry, so a re-armed hold cannot record twice.
  bool replayRecording_{};
  bool replayPlayback_{};
  std::vector<ReplayJudgement> recordedReplay_;
  std::vector<ReplayJudgement> replayEntries_;
  std::size_t replayCursor_{};
  std::unordered_set<int> recordedHoldPressNotes_;
  // Holds whose body burst timer is running, so the per-frame work stays proportional to
  // the number of active holds rather than to the chart size.
  std::vector<Note*> holdFxNotes_;
  std::vector<Note*> activeHolds_;
  std::vector<HoldBodyBurst> holdBodyBursts_;
  std::vector<HoldHeadBurst> holdHeadBursts_;
};
}
