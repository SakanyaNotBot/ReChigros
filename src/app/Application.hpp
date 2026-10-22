#pragma once

#include "rechigros/app/TriGridBackdrop.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct SDL_Window;
using SDL_GLContext = void*;
union SDL_Event;

namespace rechigros::audio {
class AudioEngine;
}
namespace rechigros::render {
class GameRenderer;
}

namespace rechigros::app {

struct AppContext;

// Owns the window, the GL context, the ImGui context and the screen state
// machine. One instance per process; the destructor releases everything.
class Application final {
public:
  Application(int argc, char** argv);
  ~Application();
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

private:
  bool initialize();
  void shutdown() noexcept;
  void processEvents(bool& running);
  void handleEvent(const SDL_Event& event, bool& running);
  void handleKeyDown(const SDL_Event& event);
  void syncWindowMetrics();
  void updateAudioFromSettings();
  void drawCurrentScreen();
  void updateBatchMode();
  void printBatchSummary();
  void captureFramebufferIfRequested();
  // Writes the collected frame pacing samples and prints their distribution.
  void writeFrameStats() const;
  // True when the gameplay frame has no immediate-mode interface on top of it: no pause
  // menu, practice panel, dialog, toast or debug text. The gameplay HUD is drawn by the
  // renderer, so those frames do not need an ImGui pass at all.
  [[nodiscard]] bool gameplayImGuiIdle() const;
  // Present-path diagnostics: an optional GPU timer query and the swap timing columns.
  void initGpuTimer();
  void shutdownGpuTimer() noexcept;
  void pollGpuTimer();
  void beginGpuTimer();
  void endGpuTimer();

  SDL_Window* window_{};
  SDL_GLContext context_{};
  std::unique_ptr<AppContext> app_;
  std::unique_ptr<audio::AudioEngine> audio_;
  std::unique_ptr<render::GameRenderer> renderer_;
  std::unique_ptr<TriGridBackdrop> backdrop_;
  std::filesystem::path initialPath_;
  std::filesystem::path replayPlaybackPath_;
  std::filesystem::path recordReplayPath_;
  std::filesystem::path capturePath_;
  // Overrides the per-user data directory, so an automated run can start from a known
  // library without touching the real one.
  std::filesystem::path dataRootOverride_;
  // Opens a named screen with representative state before the loop starts.
  std::string screenSetup_;
  // Locks the frame delta to 1/60 s, which makes frame-count-bounded runs deterministic.
  bool fixedDelta_{};
  // Captures at an explicit frame instead of the per-screen default wait.
  int captureAtFrame_{-1};
  double captureTimeSec_{-1.0};
  // Stage aspect ratio for a capture screen; zero keeps the reference capture value.
  float aspectRatioOverride_{0.0F};
  // Per-frame pacing samples: the wall interval between frames and the time spent before
  // the buffer swap. Written as CSV when the run ends, which is how stutters are measured.
  std::filesystem::path frameStatsPath_;
  struct FrameSample {
    double intervalMs{};
    double cpuMs{};
    int screen{};
    double nowSec{};
    // Present-path diagnostics: time spent inside the buffer swap, GPU time for the frame
    // (from a timer query, zero when unsupported) and wall-clock seconds at swap return.
    double swapMs{};
    double gpuMs{};
    double wallSec{};
  };
  std::vector<FrameSample> frameSamples_;
  // --swap-interval N: present pacing experiment; negative selects adaptive vsync.
  int swapInterval_{1};
  // Skips the immediate-mode frame for plain gameplay frames (default on; --no-imgui-skip
  // turns it off for an A/B run).
  bool imguiSkipIdle_{true};
  // GPU timer query state: two queries rotate so the previous frame's result can be read
  // without stalling. Zero when the platform has no timer query support.
  bool gpuTimerSupported_{false};
  unsigned int gpuQueries_[2]{};
  int gpuActiveSlot_{-1};
  int gpuNextSlot_{};
  // Sample index each query slot belongs to; -1 when the slot is idle.
  int gpuSlotFrame_[2]{-1, -1};
  // Swap interval the driver actually applied, for the frame-stats summary.
  int appliedSwapInterval_{0};
  std::uint64_t runStartCounter_{};
  unsigned long long runFrequency_{1};
  // Diagnostic mode: reports the graphics driver strings once and every pass that sets
  // a GL error. Off unless the run asks for it.
  bool glDebug_{};
  int windowWidthOverride_{1280};
  int windowHeightOverride_{720};
  bool captureDone_{};
  bool captureFailed_{};
  bool imguiReady_{};
  bool suspended_{};
  bool frameClockReset_{};
  int lastDrawableWidth_{};
  int lastDrawableHeight_{};
  float displayDpi_{380.0F};
  float smoothedFps_{60.0F};

  // --frames N batch mode: runs the real event/update/render loop for N frames
  // and exits with a machine-checkable summary. Used for automated verification.
  int frameLimit_{};
  int renderedFrames_{};
  int forwardedPointerEvents_{};
  bool batchMode_{};
  bool batchOk_{};
  int screenFrameCounts_[7]{};
  // Synthetic input timing for the verification runs: a positive value presses the notes
  // late by that many milliseconds, which is how a mistimed judgement is produced.
  // Whether the platform text input is running, so the on-screen keyboard follows focus.
  bool textInputActive_{};
  double batchMistimeMs_{};
  // Widens the practice judgement window before the run, so the window can be verified
  // end to end rather than only through the engine.
  double practicePerfectMs_{};
  double practiceGoodMs_{};
  std::size_t batchNoteIndex_{};
  int batchInputStage_{};
  bool expectResult_{};
  bool expectPause_{};
  // Play-mode overrides for the verification runs.
  bool practiceOverride_{};
  bool challengeOverride_{};
  bool mirrorOverride_{};
  bool autoplayOverride_{};
  bool pauseMenuSeen_{};
  bool pauseCountdownSeen_{};
  bool pauseResumedSeen_{};
  bool replayMatch_{};
};

} // namespace rechigros::app
