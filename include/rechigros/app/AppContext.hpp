#pragma once

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/CoverCache.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Settings.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/game/PracticeJudgeWindows.hpp"
#include "rechigros/game/PlayResult.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rechigros::audio {
class AudioEngine;
}
namespace rechigros::render {
class GameRenderer;
}

namespace rechigros::app {

enum class Screen { Home, Library, Detail, Game, Result, Calibration, Settings };

// Everything needed to start one gameplay run.
struct PlaySetup {
  std::string chartId;
  std::string songName;
  std::string difficulty;
  std::filesystem::path chartFile;
  std::filesystem::path musicFile;
  std::filesystem::path bgFile;
  int chartOffsetMs{};
  int audioOffsetMs{};
  bool practice{};
  bool replayMode{};
  std::filesystem::path replayFile;
  bool autoplay{};
  bool challenge{};
  bool mirrorX{};
  float musicSpeed{1.0F};
  float scrollSpeed{1.0F};
};

// Per-run gameplay state owned by the application shell.
struct GameRuntime {
  std::unique_ptr<game::Chart> chart;
  game::GameplaySession session;
  bool loaded{};
  bool started{};
  bool inIntro{true};
  bool inOutro{};
  bool finished{};
  double introElapsedSec{};
  double outroElapsedSec{};
  double chartTimeSec{};
  double playheadSec{};
  double visualTimeSec{};
  double userOffsetSec{};
  double chartOffsetSec{};
  double musicDurationSec{};
  double lastChartTimeSec{};
  double stalledSec{};
  float frameDeltaSec{};
  bool musicLoaded{};
  // Pause state machine: menu -> countdown -> play.
  enum class Pause : int { None, Menu, Countdown };
  Pause pause{Pause::None};
  double pauseElapsedSec{};
  double pausePlayheadSec{};
  int countdown{};
  double countdownElapsedSec{};
  // Practice seeking and per-attempt judgement windows.
  double seekRequestSec{-1.0};
  game::PracticeJudgeWindows practiceWindows;
  // Replay recording/playback.
  bool replayPlayback{};
  std::size_t replayCursor{};
  ReplayData playback;
  bool recording{};
  ReplayData recorded;
  // Judgement popup driven by HUD count changes.
  int popupJudgement{-1};
  double popupAgeSec{};
  int lastCounts[4]{};
  bool hudVisible{true};
  bool debugOverlay{};
  bool autoplay{};
  bool challenge{};
  bool practice{};
  bool mirrorX{};
  float musicSpeed{1.0F};
  float scrollSpeed{1.0F};
};

struct CalibrationMarker {
  double yNorm{};
  double ageSec{};
};

// Calibration screen state. Audio is a two second loop; the falling note is a
// pure function of the loop position, so entering the screen only loads audio.
struct CalibrationRuntime {
  bool audioLoaded{};
  float offsetMs{};
  double previousLoopTime{-1.0};
  std::vector<CalibrationMarker> markers;
};

// Play-mode overrides applied on top of whatever a chart's own setup produces. Used by the
// headless verification runs to exercise the practice, challenge and mirror paths.
struct PlayOverrides {
  bool practice{};
  bool challenge{};
  bool mirrorX{};
  bool autoplay{};
};

struct AppContext {
  explicit AppContext(std::filesystem::path dataRoot);

  // Copies the example chart that ships with the build into the library, once. A packaged
  // build cannot read its own assets as files, so without this the library starts empty.
  void seedBundledSample();

  std::filesystem::path dataRoot;
  std::filesystem::path settingsFile;
  Settings settings;
  PlayOverrides playOverrides;
  ChartLibrary library;
  ReplayStore replays;
  TextureCache textures;
  audio::AudioEngine* audio{};
  render::GameRenderer* renderer{};

  Screen screen{Screen::Home};
  Screen settingsReturn{Screen::Home};
  Screen detailReturn{Screen::Library};
  bool quitRequested{};
  // A run started with an explicit replay output path records even when the stored
  // preference has replay recording disabled. It lives outside the gameplay state so a
  // new run's reset cannot clear it.
  bool recordRequested{};
  bool settingsDirty{};
  float displayDpi{380.0F};
  int windowWidth{1280};
  int windowHeight{720};
  int drawableWidth{1280};
  int drawableHeight{720};
  float fps{};
  double nowSec{};
  double deltaSec{};

  std::string activeChartId;
  std::string searchQuery;
  std::vector<std::string> selectedIds;
  bool manageMode{};
  int settingsTab{};
  int libraryTab{};

  PlaySetup play;
  GameRuntime game;
  CalibrationRuntime calibration;
  game::PlayResult lastResult;
  ReplayMeta lastReplayMeta;
  bool lastRunHasReplay{};
  bool lastRunWasReplay{};
  bool lastRunRecorded{}; // recording was active
  std::string savedReplayId;

  std::string toastText;
  double toastDeadlineSec{};
  std::string loadError;
  // Pointer events that reached gameplay input; used by the batch verification mode.
  int forwardedPointerEvents{};
  int totalNotesJudgedForBatch{};
};

} // namespace rechigros::app
