#pragma once

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/NoteGeometry.hpp"

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
  [[nodiscard]] float effectiveJudgeArea(const Note& note, double chartTimeSec) const noexcept;

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
  void activateHold(Note& note, bool perfect, double chartTimeSec, double diffSec) noexcept;
  bool commitNote(Note& note, JudgementEngine::Judgement judgement,
      double diffSec, double chartTimeSec, bool emitSound = true) noexcept;
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
};
}
