#pragma once

#include <array>
#include <vector>

namespace rechigros::game {

// Official "block area" (red zone). Geometry is expressed in stage-relative 0..1
// percentages with (0.5, 0.5) at the stage centre, and every runtime value is resolved
// into screen pixels each frame.
//
// Touch semantics: a touch inside an active normal block is swallowed, unless it also
// lies inside an odd number of subtract blocks (a hole). Judgement must therefore be
// suppressed for blocked fingers rather than merely hidden.
class OfficialBlock final {
public:
  static constexpr float TOUCH_INSET_SCREEN_HEIGHT_RATIO = 0.05F;
  static constexpr float MAX_TOUCH_INSET_LOCAL = 0.25F;
  static constexpr float DISABLED_SHOW_DURATION = 0.5F;
  static constexpr float READY_LEAD_DURATION = 0.5F;

  struct RotateEvent {
    float anchorX{0.5F};
    float anchorY{0.5F};
    float time{};
    int easeType{};
    float rotation{};
  };
  struct MoveEvent {
    float posX{0.5F};
    float posY{0.5F};
    float time{};
    int easeTypeX{};
    int easeTypeY{};
  };
  struct ScaleEvent {
    float anchorX{0.5F};
    float anchorY{0.5F};
    float time{};
    int easeTypeX{};
    int easeTypeY{};
    float scaleX{1.0F};
    float scaleY{1.0F};
  };

  float bottomLeftX{}, bottomLeftY{};
  float topRightX{1.0F}, topRightY{1.0F};
  float appearTime{}, enableTime{};
  float disableTime{3.402823466e+38F}, disappearTime{3.402823466e+38F};
  bool isSubtract{false};

  std::vector<RotateEvent> rotateEvents;
  std::vector<MoveEvent> moveEvents;
  std::vector<ScaleEvent> scaleEvents;

  // Runtime state, recomputed by update().
  bool visible{false};
  bool active{false};
  bool showStarted{false};
  float centerX{}, centerY{};
  float halfW{}, halfH{};
  float rotationDeg{};

  [[nodiscard]] bool hasTransformEvents() const noexcept {
    return !moveEvents.empty() || !rotateEvents.empty() || !scaleEvents.empty();
  }

  // Advances the block to chart time t. `stageX`/`stageY` are the stage origin in screen
  // pixels, `screenW`/`screenH` its size, and `deltaTimeSec` the real frame delta used by
  // the one-shot disabled-fade coroutine.
  void update(float t, float stageX, float stageY, float screenW, float screenH, float deltaTimeSec);

  // Re-derives the phase after a time jump without replaying the one-shot fade.
  void syncToChartTime(float t);

  // 0 = disabled, 1 = ready, 2 = active.
  [[nodiscard]] int phase(float t) const noexcept;
  [[nodiscard]] float showProgress() const noexcept;
  [[nodiscard]] float showAlpha() const noexcept;

  // Screen point to unrotated block-local coordinates normalised to +/-0.5.
  void toLocal(float x, float y, std::array<float, 2>& out) const noexcept;
  [[nodiscard]] bool containsOriginal(float x, float y) const noexcept;
  [[nodiscard]] bool containsInset(float x, float y, float screenHeight) const noexcept;

  // True when the point is swallowed by some active block.
  static bool tryGetBlockingBlock(const std::vector<OfficialBlock>& blocks, float x, float y,
      float screenHeight);

  void resetRuntime() noexcept;

  // Four rotated corners of the drawn zone, in order (-w,-h) (-w,+h) (+w,-h) (+w,+h).
  void corners(std::array<float, 8>& out) const noexcept;

private:
  [[nodiscard]] std::array<float, 2> evalMove(int index, float screenW, float screenH) const noexcept;
  void rotateAround(std::array<float, 2>& out, float cx, float cy, float anchorPctX,
      float anchorPctY, float deltaDeg, float screenW, float screenH) const noexcept;

  float showElapsedSec_{};
  bool wasVisible_{false};
  float cosRot_{1.0F};
  float sinRot_{0.0F};
  float timeNow_{};
};
} // namespace rechigros::game
