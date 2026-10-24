#pragma once

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/CoverCache.hpp"
#include "rechigros/app/RecordSession.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Settings.hpp"
#include "rechigros/app/SmoothingClock.hpp"
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

enum class Screen { Home, Library, Detail, Game, Result, Calibration, Settings, Render };

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
  // Set once the settlement backdrop has been prepared for this run, so the one-off cost is
  // paid during the exit instead of on the settlement page's first frame.
  bool outroBlurPrepared{};
  double introElapsedSec{};
  double outroElapsedSec{};
  double chartTimeSec{};
  double playheadSec{};
  // Playhead used for drawing. The audio cursor reports the stream position only a few times
  // per second, so drawing straight from it makes notes step instead of glide. `smoothClock`
  // corrects it; judgement keeps using `playheadSec`, so the correction never moves a hit
  // window.
  double smoothedPlayheadSec{};
  SmoothingClock smoothClock;
  bool smoothClockValid{};
  double visualTimeSec{};
  double userOffsetSec{};
  double chartOffsetSec{};
  double musicDurationSec{};
  double lastChartTimeSec{};
  double stalledSec{};
  float frameDeltaSec{};
  bool musicLoaded{};
  // Pause state machine: menu -> countdown -> fadeout -> play. The fadeout state covers the
  // short window where the countdown has finished but the play field is still frozen, so a
  // pause taken during it resumes the same run instead of restarting one.
  enum class Pause : int { None, Menu, Countdown, Fadeout };
  Pause pause{Pause::None};
  double pauseElapsedSec{};
  double pausePlayheadSec{};
  // Set while the frozen playhead is meaningful. A pause that restarts the run freezes it at
  // zero, and the resume then rewinds instead of seeking to a stale position.
  bool pausePlayheadValid{};
  // True when the paused run resumes from the beginning rather than from its frozen playhead.
  bool pauseResumesFromZero{};
  int countdown{};
  double countdownElapsedSec{};
  // Elapsed time inside the resume fade that follows the countdown.
  double fadeoutElapsedSec{};
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
  Screen renderReturn{Screen::Detail};
  // The recording in progress, if any. It lives on the context because the interface, the render
  // loop and the shutdown path all reach it.
  RecordSession recording;
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

  // Level-effect challenge state of the current run: the bad+miss and miss totals the effect
  // was last told about, and whether a finger was holding a red zone on the previous frame.
  int levelEffectBadMissCount{};
  int levelEffectMissCount{};
  bool levelEffectTouchingBlock{};

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
