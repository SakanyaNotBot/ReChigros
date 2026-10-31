#pragma once

#include "rechigros/game/HoldBodyBurst.hpp"
#include "rechigros/game/JudgementEvent.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rechigros::game { class Chart; class JudgementEngine; }

namespace rechigros::render {

// Pause overlay state owned by the application shell.
// The pause overlay's state. The resume countdown ends the pause directly; there is no state
// between "countdown finished" and "playing".
enum class PauseMode { None, Menu, Countdown };

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
  // Play-field aspect ratio; zero follows the official 16:9 stage.
  float aspectRatio{0.0F};
  bool challengeMode{false};
  // Colours plain judge lines by the best judgement so far.
  bool apfcIndicator{false};

  // Display settings applied by the renderer; every default preserves the reference look.
  // Plain judge line width multiplier (0.5..2); storyboard textures and text lines keep
  // their own size.
  float judgeLineWidth{1.0F};
  // Renders the play field into a half-resolution target and upscales it.
  bool lowResolutionMode{false};
  // Renders through a full-resolution offscreen target and resolves it with FXAA.
  bool antialias{false};
  // Skips the storyboard/level-effect shader chain entirely.
  bool disableChartShader{false};
  // Uses the multi-press highlight note textures and width scales for flagged notes.
  bool multiPressHighlight{true};
  // Enables the story level effects; the Message level always plays its effects.
  bool storyMode{true};
  // Draws the chart-debug overlay: a marker at every judge line and every note, so an authored
  // chart's geometry can be compared against what is actually rendered.
  bool showChartDebug{false};
  // Draws the hit particles and the hold debris. Turning them off keeps the hit sound and the
  // judgement itself; only the visual burst is skipped.
  bool showHitParticles{true};
  // Reveals the play field: shrunk camera at the start and end plus hidden objects.
  bool chartReveal{false};
  // Resolved by the renderer from chartReveal for the frame it is drawing; the caller
  // leaves these at their defaults.
  bool revealCameraActive{false};
  float revealScale{1.0F};
  bool revealHiddenObjects{false};
  float revealHiddenFloorAlpha{0.0F};

  // Fingers currently held over the play field, in view pixels. The block composite uploads
  // them so the hover shader can light the touched spot; the reference clears a point only
  // when the finger lifts, so a moving finger keeps its glow.
  struct TouchState {
    struct Point {
      float x{};
      float y{};
      // 0..1 size of the hover disc. The reference grows one in and out over a short real-time
      // window instead of toggling it, so a finger leaving a zone does not pop.
      float scale{1.0F};
    };
    Point points[10]{};
    int count{};
  } touches;

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
    // Raw music playhead and its total length: the progress bar follows the music.
    double playheadSec{0.0};
    double totalDurationSec{0.0};
    // The accuracy readout follows a gameplay setting.
    bool showAccuracy{true};
    // Combo label variant for the run's mode (COMBO/AUTOPLAY/PRACTICE/REPLAY).
    std::string comboLabel{"COMBO"};
    // Normalised progress of the HUD start and exit animation tweens (idle: 1 and 0).
    float introProgress{1.0F};
    float outroProgress{0.0F};
    // Wall clock and the age of the last pause-button touch (negative when idle).
    double visualTimeSec{0.0};
    double pauseRingAgeSec{-1.0};
    // Caller-side mirrors for hit testing. Rendering resolves the authoritative state above.
    bool revealCameraActive{false};
    float revealScale{1.0F};
    // Optional catch-up animation of the score and accuracy readouts.
    bool scoreJumpEffect{false};
    // Judgement offset indicator setting (0 disabled, 1 sector dial, 2 line) and the
    // autoplay/replay suppression flag of its display.
    int hitOffsetIndicatorMode{0};
    bool hitOffsetSuppressed{false};
    // Global HUD opacity (the exit fade of the gameplay screen).
    float alpha{1.0F};
    // Frame-rate readout of the HUD.
    bool showFps{false};
    float fps{0.0F};
  } hud;

  struct PracticeState {
    bool enabled{false};
    float speed{1.0F};
    bool autoPlay{false};
  } practice;

  // True when a recording drives the run. Autoplay draws the chart's pre-rolled bursts
  // while a replay draws the ones its recorded judgements produce, so the two never run
  // together.
  bool replayPlayback{false};
  std::vector<game::JudgementEvent> judgementEvents;
  bool judgementEventsAuthoritative{};

  // Hold-body hit bursts produced since the previous frame. The gameplay session owns the
  // timing (one burst every half beat while a hold is active); the hit-effect pass draws one
  // burst per entry at the position the hold occupies.
  std::vector<game::HoldBodyBurst> holdBodyBursts;
  // Hold-head bursts: one per hold head hit, drawn where the head was when it was hit.
  std::vector<game::HoldHeadBurst> holdHeadBursts;

  // Fingers currently held inside an active red zone, in stage pixels. A zone draws a pulse
  // disc and ring at each of these points while the finger stays down.
  std::vector<std::array<float, 2>> blockedTouchPositions;
  // Chart time when the last finger entered a zone, which drives the pulse's decay.
  double blockedTouchPulseSec{-1.0};

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
  // Starts decoding every gameplay texture (built-in assets and the skin's own files) on worker
  // threads, so the loading screen can keep animating while a large skin is read.
  void prefetchGameplayTextures(const std::filesystem::path& skinDirectory);
  // Prefetches the chart illustration too, so a large backdrop does not stall beginRun.
  void prefetchBackgroundImage(const std::filesystem::path& image);
  // True once all texture decodes started by prefetchGameplayTextures have finished.
  [[nodiscard]] bool gameplayTexturesReady() const;
  // Discards the animated score/accuracy readout so the next frame starts it over. A seek
  // replaces the run's position outright, so the displayed value has to follow it instead of
  // sliding toward it from wherever the previous position left it.
  void resetHudDisplay();
  // Restarts note judgement feedback after a seek or a new attempt, without changing materials.
  // `chart` must be the run's current chart (or null when there is none); the previous chart
  // pointer is never dereferenced after its owner has released it.
  void resetJudgementEffects(game::Chart* chart = nullptr);
  // Selects the chart illustration used as the play-field backdrop (and for the fixed side
  // panels). An empty or unreadable path keeps the built-in backdrop.
  void setBackgroundImage(const std::filesystem::path& image);
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

  // Offscreen frame capture for recording. Opens a target at the requested size and keeps it
  // for as long as the capture runs, so a long recording allocates once instead of per frame.
  // `beginCapture` prepares the renderer for the chart; `captureFrame` draws one frame with the
  // supplied input and returns its pixels in top-down RGBA order; `endCapture` restores the
  // interactive size. The caller decides when a frame exists and what time it shows, which is
  // what makes the output independent of the wall clock.
  bool beginCapture(const RenderFrameInput& input, game::Chart* chart, int width, int height);
  bool captureFrame(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPixels);
  // Draws one frame and returns it already converted to the encoder's planar colour layout, which
  // is a third of the bytes and needs no conversion inside the encoder. Falls back to the RGBA
  // form when the conversion is unavailable on this context.
  bool captureFrameYuv(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPlanes);
  // Draws the current frame and starts its transfer while collecting the preceding frame. The
  // first call reports no frame yet. takePendingCaptureFrame collects the last one; hasPending
  // says whether anything is outstanding, so a caller can drain before closing the encoder.
  bool captureFrameYuvAsync(const RenderFrameInput& input, game::Chart* chart,
      const game::JudgementEngine* judgement, std::vector<std::uint8_t>& outPlanes,
      bool& frameReady);
  bool takePendingCaptureFrame(std::vector<std::uint8_t>& outPlanes);
  [[nodiscard]] bool hasPendingCaptureFrame() const noexcept;
  void endCapture() noexcept;
  [[nodiscard]] int captureWidth() const noexcept;
  [[nodiscard]] int captureHeight() const noexcept;

private:
  // Points the renderer's offscreen targets at the capture size and prepares them for the
  // chart. Split out because a capture re-runs it whenever the chart changes.
  void applyCaptureSize(const RenderFrameInput& input, game::Chart* chart);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rechigros::render
