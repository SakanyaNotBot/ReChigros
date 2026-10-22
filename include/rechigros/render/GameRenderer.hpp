#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace rechigros::game { class Chart; class JudgementEngine; }

namespace rechigros::render {

// Pause overlay state owned by the application shell.
enum class PauseMode { None, Menu, Countdown, FadeOut };

// Everything the renderer needs that the chart itself does not carry. Every field
// is defaulted, so the plain three-argument render() overload keeps working.
struct RenderFrameInput {
  // Chart clock, already corrected by the chart offset and the user offset.
  double chartTimeSec{0.0};
  // Raw music playhead; drives the visual clock and the HUD progress bar.
  double playheadSec{0.0};
  // Wall clock in seconds; drives GIF playback and hit-effect animation.
  double visualTimeSec{0.0};
  // User-configured offset in seconds. Presentation only.
  double userOffsetSec{0.0};

  float dpi{380.0F};
  float scrollSpeed{1.0F};
  // Scales note and effect geometry independently of the play-field size.
  float keyScale{1.0F};
  // Mirrors the play field horizontally (demo mode).
  bool mirrorX{false};
  // 1 means a fully black play field.
  float backgroundDim{0.0F};
  bool challengeMode{false};
  // Colours plain judge lines by the best judgement so far.
  bool apfcIndicator{false};

  struct PauseState {
    PauseMode mode{PauseMode::None};
    // Seconds since the pause overlay started animating.
    float elapsedSec{0.0F};
  } pause;

  struct TransitionState {
    // 0..1 growth of the decorative opening line; 1 means gameplay.
    float introLineScale{1.0F};
    bool inIntro{false};
    bool inOutro{false};
    // Seconds since the outro started.
    float outroElapsedSec{0.0F};
  } transition;

  struct HudState {
    bool visible{true};
    int combo{0};
    int score{0};
    double accuracy{1.0};
    int maxCombo{0};
    int perfect{0};
    int good{0};
    int bad{0};
    int miss{0};
    // 0..1 playback progress shown by the top bar.
    double progress{0.0};
    float alpha{1.0F};
    // The accuracy readout follows a gameplay setting.
    bool showAccuracy{true};
  } hud;

  struct PracticeState {
    bool enabled{false};
    float speed{1.0F};
    bool autoPlay{false};
  } practice;

  std::string songName;
  std::string levelName;
};

class GameRenderer final {
public:
  GameRenderer();
  ~GameRenderer();
  GameRenderer(const GameRenderer&) = delete;
  GameRenderer& operator=(const GameRenderer&) = delete;

  bool initialize(const std::filesystem::path& assetRoot);

  // Applies a skin directory on top of the built-in resource pack. A skin may override any
  // subset of the material settings; fields it leaves out keep the built-in value. Passing
  // an empty path clears the skin and restores the built-in values.
  void setSkinDirectory(const std::filesystem::path& skinDirectory);
  void shutdown() noexcept;
  void resize(int width, int height) noexcept;

  // Renders one gameplay frame into the currently bound target.
  void render(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement = nullptr);

  // Convenience overload for callers that only track the chart clock.
  void render(double chartTimeSeconds, game::Chart* chart,
      const game::JudgementEngine* judgement = nullptr);

  // Renders `frames` frames offscreen and writes the last one as a PNG. A current
  // GL context is required; returns false when the target cannot be allocated or
  // the file cannot be written.
  bool renderOffscreen(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, const std::filesystem::path& outputPng,
      int frames, int width, int height);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rechigros::render
