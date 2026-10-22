#pragma once

#include "rechigros/game/BpmTimeline.hpp"
#include "rechigros/game/Event.hpp"
#include "rechigros/game/EventCursor.hpp"
#include "rechigros/game/Note.hpp"

#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace rechigros::game {

class JudgeLine final {
public:
  static constexpr int NOTE_CTRL_ALPHA = 0;
  static constexpr int NOTE_CTRL_SCALE = 1;
  static constexpr int NOTE_CTRL_X = 2;
  static constexpr int NOTE_CTRL_Y = 3;
  static constexpr int NOTE_CTRL_SKEW = 4;
  static constexpr int NOTE_CTRL_JUDGE_AREA = 5;

  struct EventLayer {
    std::vector<LineEvent> judgeLineRotateEvents;
    std::vector<LineEvent> judgeLineMoveXEvents;
    std::vector<LineEvent> judgeLineMoveYEvents;
    std::vector<MoveEvent> judgeLineMoveEvents;
    std::vector<LineEvent> judgeLineDisappearEvents;
    EventCursor curRotate, curMoveX, curMoveY, curMove1, curMove2, curDisappear;
  };
  struct NoteControlPoint { float y{}, value{}; int easing{}; };
  struct State {
    float rotateDeg{}, xNorm{0.5F}, yNorm{0.5F}, alpha{}, scaleX{1}, scaleY{1};
    float inclineSinr{std::numeric_limits<float>::quiet_NaN()};
    float colorR{1}, colorG{1}, colorB{1}, anchorX{0.5F}, anchorY{0.5F};
    bool hideLine{}, hideNotes{};
    std::string text;
    bool hasColorEvent{};
    float gifPosition{std::numeric_limits<float>::quiet_NaN()};
    double floorPosition{};
  };

  std::shared_ptr<BpmTimeline> bpmTimeline;
  double bpm{120.0}, bpmfactor{1.0};
  std::vector<EventLayer> eventLayers;
  std::vector<std::vector<SpeedEvent>> speedEventLayers;
  std::vector<SpeedEvent> speedEvents;
  std::vector<LineEvent> judgeLineRotateEvents, judgeLineDisappearEvents;
  std::vector<MoveEvent> judgeLineMoveEvents;
  std::vector<Note> notesAbove, notesBelow, notes;
  // Per-type note index (indexed by note type), each list sorted by time. Built by the chart
  // runtime preparation: the note renderer visits only the requested type and starts at the
  // first note that can still be on screen, instead of scanning every note several times a
  // frame. `maxHoldDurationSec` bounds how early a hold can have started and still reach
  // beyond the current chart time.
  std::vector<std::vector<Note*>> notesByType;
  double maxHoldDurationSec{};
  int zOrder{}, attachUiElementId{}, father{-1};
  bool isCover{}, rotateWithFather{}, invertRotation{true};
  std::string texture, text;
  bool rpeTextureScaleInPixels{}, useOfficialScale{}, textureIsGif{};
  std::vector<GifEvent> gifEvents;
  std::vector<MoveEvent> judgeLineScaleEvents;
  std::vector<LineEvent> judgeLineScaleXEvents, judgeLineScaleYEvents, judgeLineInclineEvents;
  std::vector<ColorEvent> judgeLineColorEvents;
  std::vector<TextEvent> judgeLineTextEvents;
  float anchorX{0.5F}, anchorY{0.5F};
  std::vector<NoteControlPoint> noteAlphaControls, noteScaleControls, noteXControls,
      noteYControls, noteSkewControls, noteJudgeAreaControls;
  int index{-1};
  JudgeLine* fatherLine{};

  [[nodiscard]] double sec2beat(double second) const noexcept;
  [[nodiscard]] double globalSec2beat(double second) const noexcept;
  [[nodiscard]] double beat2sec(double beat) const noexcept;
  [[nodiscard]] double globalBeat2sec(double beat) const noexcept;
  void resolveFather(std::vector<std::unique_ptr<JudgeLine>>& lines) noexcept;
  void mergeNotes();
  void normalize();
  void initSpeedEventsPhi() noexcept;
  void resetCursors() noexcept;
  [[nodiscard]] State fillState(double second, float stageAspectRatio);
  [[nodiscard]] float calcNoteControl(float rpeHeightPx, int type, float fallback) const noexcept;

private:
  EventCursor curRotate_, curMove1_, curMove2_, curDisappear_, curScaleX_, curScaleY_;
  EventCursor curScale1_, curScale2_, curIncline_, curColor_, curText_, curGif_;
  State lastState_{};
  double lastStateTimeSec_{std::numeric_limits<double>::quiet_NaN()};
};
}
