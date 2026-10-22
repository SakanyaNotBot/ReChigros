#pragma once

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/HoldBodyBurst.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/NoteGeometry.hpp"
#include "rechigros/game/ReplayLog.hpp"

#include <array>
#include <cstdint>
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
  void setAutoplay(bool value) noexcept;

  void touchDown(std::int64_t id, float x, float y);
  void touchMove(std::int64_t id, float x, float y) noexcept;
  void touchUp(std::int64_t id) noexcept;
  void cancelTouches() noexcept;
  void settleTouches() noexcept;
  void update(double chartTimeSec, float frameDeltaSec);

  [[nodiscard]] Chart* chart() const noexcept { return chart_; }
  [[nodiscard]] const JudgementEngine& judgement() const noexcept { return judgement_; }
  [[nodiscard]] JudgementEngine& judgement() noexcept { return judgement_; }
  [[nodiscard]] bool autoplay() const noexcept { return autoplay_; }
  [[nodiscard]] bool finished(double chartTimeSec) const noexcept;
  std::vector<int> takeSoundEvents();

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
  void updateChallengeTiming(float frameDeltaSec) noexcept;
  void judgeNewTouches(double time, const Stage& view);
  void judgeDrags(double time, const Stage& view);
  void judgeFlicks(double time, const Stage& view);
  void judgeTapHold(double time, const Stage& view);
  void settleHoldTails(double time);
  void detectMisses(double time);
  void updateBlocks(double time, float frameDeltaSec);

  Chart* chart_{};
  Viewport viewport_{};
  JudgementEngine judgement_{};
  std::unordered_map<std::int64_t, std::unique_ptr<Touch>> touches_;
  std::size_t judgeCursor_{};
  bool challengeMode_{};
  bool autoplay_{};
  double challengeTimeSum_{};
  std::array<double, 10> frameTimes_{};
  std::vector<int> soundEvents_;
  std::size_t frameTimeIndex_{}, frameTimeCount_{};
  std::size_t autoNoteIndex_{};
  std::vector<OfficialBlock> blocks_;
  // Fingers latched inside a red zone emit no judge candidates for the rest of the press.
  std::unordered_set<std::int64_t> blockedTouchIds_;
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
  std::vector<HoldBodyBurst> holdBodyBursts_;
  std::vector<HoldHeadBurst> holdHeadBursts_;
};
}
