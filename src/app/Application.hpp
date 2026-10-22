#pragma once

#include "rechigros/app/TriGridBackdrop.hpp"

#include <filesystem>
#include <memory>

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
  double captureTimeSec_{-1.0};
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
