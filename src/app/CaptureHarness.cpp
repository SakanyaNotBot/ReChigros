#include "rechigros/app/CaptureHarness.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/game/PlayResult.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

// A short multi-line chart used only when the capture data directory has no library yet.
// It gives the list, detail, gameplay and result screens realistic content to draw.
constexpr const char* kCaptureChart = R"JSON({
  "formatVersion": 3,
  "offset": 0,
  "judgeLineList": [
    {
      "bpm": 120.0,
      "notesAbove": [
        {"type": 1, "time": 32, "positionX": 0.0},
        {"type": 1, "time": 64, "positionX": -0.3},
        {"type": 1, "time": 96, "positionX": 0.3},
        {"type": 2, "time": 128, "positionX": -0.15},
        {"type": 2, "time": 160, "positionX": 0.15},
        {"type": 3, "time": 192, "holdTime": 96, "positionX": -0.25},
        {"type": 1, "time": 192, "positionX": 0.25},
        {"type": 4, "time": 288, "positionX": 0.0},
        {"type": 1, "time": 320, "positionX": -0.3},
        {"type": 1, "time": 352, "positionX": 0.3},
        {"type": 3, "time": 384, "holdTime": 64, "positionX": 0.0},
        {"type": 1, "time": 480, "positionX": 0.0}
      ],
      "notesBelow": [
        {"type": 1, "time": 96, "positionX": 0.4},
        {"type": 1, "time": 224, "positionX": -0.4},
        {"type": 2, "time": 320, "positionX": 0.4}
      ],
      "speedEvents": [
        {"startTime": 0, "endTime": 100000.0, "value": 1.0, "endValue": 1.0}
      ],
      "judgeLineMoveEvents": [
        {"startTime": 0, "endTime": 100000.0, "start": 0.5, "end": 0.5,
         "start2": 0.5, "end2": 0.5}
      ],
      "judgeLineRotateEvents": [
        {"startTime": 0, "endTime": 100000.0, "start": 0.0, "end": 0.0}
      ],
      "judgeLineDisappearEvents": [
        {"startTime": 0, "endTime": 100000.0, "start": 1.0, "end": 1.0}
      ]
    }
  ]
}
)JSON";

struct SyntheticChart {
  const char* name;
  const char* level;
};

// Names and levels mirror the content the interface is compared against, so the grid and
// the cards are exercised with strings of realistic length.
constexpr std::array<SyntheticChart, 8> kSyntheticCharts{{
    {"Message", "SP Lv.?"},
    {"ハテ", "IN Lv.16"},
    {"Desultory Signals", "AT Lv.18"},
    {"Entrance to the Chaos", "AT Lv.17"},
    {"Hate", "AT Lv.17"},
    {"彩", "IN Lv.16"},
    {"Message", "AT Lv.16"},
    {"ハテ", "AT Lv.17"},
}};

bool copyAsset(const std::filesystem::path& assetRoot, const std::string& relative,
    const std::filesystem::path& target) {
  const auto bytes = platform::AssetManager::readBinary(assetRoot, relative);
  if (bytes.empty()) return false;
  return platform::writeBinaryFile(target, bytes);
}

// Creates a small library so every screen has content when the caller did not supply one.
void ensureLibrary(AppContext& context) {
  // A capture opens the library that is already on disk. The scan is not forced: it is only
  // needed when nothing has been read yet, and forcing it here re-read every pack a second
  // time before the first frame is drawn.
  if (!context.library.records().empty()) return;
  const auto assetRoot = platform::assetRoot();
  const auto packsRoot = context.library.packsRoot();
  std::error_code error;
  std::filesystem::create_directories(packsRoot, error);
  for (std::size_t index = 0; index < kSyntheticCharts.size(); ++index) {
    const auto directory = packsRoot / ("capture-" + std::to_string(index + 1));
    std::filesystem::create_directories(directory, error);
    ChartInfo info;
    info.name = kSyntheticCharts[index].name;
    info.level = kSyntheticCharts[index].level;
    info.composer = "composer";
    info.charter = "charter";
    info.chart = "chart.json";
    info.music = "music.wav";
    info.illustration = "cover.jpg";
    (void)writeInfoJson(directory / "info.json", info);
    (void)platform::writeTextFile(directory / "chart.json", kCaptureChart);
    (void)copyAsset(assetRoot, "sample/chart.wav", directory / "music.wav");
    (void)copyAsset(assetRoot, "sample/cover.jpg", directory / "cover.jpg");
  }
  context.library.invalidate();
  (void)context.library.records(true);
}

std::string firstChartId(AppContext& context) {
  const auto& records = context.library.records();
  if (records.empty()) return {};
  return records.front().id;
}

// Finds a chart by a case-insensitive substring of its song name. Capture runs use it to
// open the same chart the interface is being compared against.
std::string chartIdForName(AppContext& context, const std::string& name) {
  if (!name.empty()) {
    std::string needle = name;
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    for (const auto& record : context.library.records()) {
      std::string haystack = record.songName;
      std::transform(haystack.begin(), haystack.end(), haystack.begin(),
          [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
      if (haystack.find(needle) != std::string::npos) return record.id;
    }
  }
  return firstChartId(context);
}

// Applies the preference values the reference screenshots were taken with, so switches,
// sliders and lists show the same state without hand-editing the settings file. A positive
// aspect override lets a capture reproduce the narrow-stage treatment.
void applyReferenceSettings(AppContext& context, float aspectRatioOverride) {
  Settings& settings = context.settings;
  settings.keyScale = 1.0F;
  settings.backgroundDim = 1.0F;
  settings.aspectRatio = aspectRatioOverride > 0.0F ? aspectRatioOverride : 1.77777F;
  settings.scrollSpeed = 1.0F;
  settings.judgeLineWidth = 1.0F;
  settings.musicVolume = 1.0F;
  settings.soundEffectVolume = 1.0F;
  settings.musicSpeed = 1.0F;
  settings.audioOffsetMs = 0;
  settings.hitOffsetIndicatorMode = 0;
  settings.apfcIndicator = true;
  settings.judgementDetailDistribution = true;
  settings.realtimeAccuracy = false;
  settings.scoreJumpEffect = false;
  settings.multiPressHighlight = true;
  settings.mirrorX = false;
  settings.autoplay = true;
  settings.challengeMode = false;
  settings.practiceMode = false;
  settings.lowResolutionMode = false;
  settings.antialias = false;
  settings.showFps = false;
  settings.showDebugInfo = false;
  settings.disableChartShader = false;
  settings.chartReveal = false;
  settings.storyMode = true;
  settings.replayEnabled = false;
  // An explicitly requested mode wins over the capture defaults, so a capture can reproduce
  // a state the reference screenshots were not taken in.
  if (context.playOverrides.mirrorX) settings.mirrorX = true;
  if (context.playOverrides.autoplay) settings.autoplay = true;
  if (context.playOverrides.challenge) settings.challengeMode = true;
  if (context.playOverrides.practice) settings.practiceMode = true;
}

void populatePlay(AppContext& context, const std::string& chartId) {
  const auto record = context.library.find(chartId);
  if (!record) return;
  context.activeChartId = record->id;
  context.play = PlaySetup{};
  context.play.chartId = record->id;
  context.play.songName = record->songName;
  context.play.difficulty = record->difficulty;
  context.play.chartFile = record->chartFile;
  context.play.musicFile = record->musicFile;
  context.play.bgFile = record->bgFile;
  context.play.chartOffsetMs = record->chartOffsetMs;
}

game::PlayResult representativeResult(int totalNotes, double stdDevMs) {
  // Values the settlement page is compared against: a full-perfect autoplay run. The note
  // count and timing deviation follow the chart the reference capture was taken on.
  game::PlayResult result;
  result.score = 1000000;
  result.accuracy = 1.0;
  result.maxCombo = totalNotes;
  result.totalNotes = totalNotes;
  result.perfect = totalNotes;
  result.good = 0;
  result.bad = 0;
  result.miss = 0;
  result.stdDevMs = stdDevMs;
  result.judgementOffsetBins[9] = totalNotes;
  return result;
}

void setupResult(AppContext& context, const std::string& chartId) {
  populatePlay(context, chartId);
  // The reference settlement captures were taken in autoplay; the challenge tag appears
  // only on the run that recorded it (the earlier Hate capture), not the current one.
  const bool challenge = context.play.songName.find("Entrance") == std::string::npos;
  const int totalNotes = challenge ? 1397 : 1249;
  const double stdDevMs = challenge ? 3.092 : 3.068;
  context.play.autoplay = true;
  context.play.challenge = challenge;
  context.lastResult = representativeResult(totalNotes, stdDevMs);
  context.lastRunWasReplay = false;
  // The reference settlement capture shows no replay actions, so the capture state has no
  // replay to save or play back.
  context.lastRunHasReplay = false;
  context.lastRunRecorded = false;
  context.lastReplayMeta = ReplayMeta{};
  context.lastReplayMeta.chartId = context.play.chartId;
  context.lastReplayMeta.songName = context.play.songName;
  context.lastReplayMeta.difficulty = context.play.difficulty;
  context.lastReplayMeta.score = context.lastResult.score;
  context.lastReplayMeta.accuracy = context.lastResult.accuracy;
  context.lastReplayMeta.maxCombo = context.lastResult.maxCombo;
  context.lastReplayMeta.totalNotes = context.lastResult.totalNotes;
  context.savedReplayId.clear();
  context.game.recorded = ReplayData{};
  context.screen = Screen::Result;
}

void startCaptureChart(AppContext& context, bool practice) {
  if (context.library.records().empty()) return;
  context.playOverrides.autoplay = true;
  startChart(context, practice);
  // Reported so a capture can be checked for the level effect it should carry: a chart whose
  // effect fails to select renders without the effect and without the HUD suppression, which
  // a screenshot alone cannot tell apart from a chart that genuinely has none.
  if (context.game.chart != nullptr) {
    const auto& effects = context.game.chart->levelEffects;
    std::printf("capture-level-effect: %s hideHud=%d song=%s\n",
        effects != nullptr ? effects->effectName().c_str() : "none",
        effects != nullptr && effects->hideHud() ? 1 : 0, context.play.songName.c_str());
  }
}
} // namespace

void setupCaptureScreen(AppContext& context, const std::string& spec, float aspectRatioOverride) {
  ensureLibrary(context);
  // A capture name may carry a chart selector, for example "result:Hate" or "detail:Message",
  // so a screen can be rendered with the same content as the reference capture.
  std::string screen = spec;
  std::string chartName;
  const auto separator = spec.find(':');
  if (separator != std::string::npos) {
    screen = spec.substr(0, separator);
    chartName = spec.substr(separator + 1);
  }
  applyReferenceSettings(context, aspectRatioOverride);
  const std::string chartId = chartIdForName(context, chartName);

  if (screen == "home") {
    context.screen = Screen::Home;
    return;
  }
  if (screen == "library") {
    context.libraryTab = 0;
    context.manageMode = false;
    context.selectedIds.clear();
    context.screen = Screen::Library;
    return;
  }
  if (screen == "render") {
    // The recording page describes the chart that is open, so the detail page's chart is loaded
    // first and the page is entered the same way the button does.
    if (!chartId.empty()) {
      openChartDetail(context, chartId);
      context.renderReturn = Screen::Detail;
      std::string error;
      if (prepareRenderChart(context, error)) context.screen = Screen::Render;
    }
    return;
  }
  if (screen == "library-online") {
    context.libraryTab = 1;
    context.manageMode = false;
    context.selectedIds.clear();
    context.screen = Screen::Library;
    return;
  }
  if (screen == "library-unpublished") {
    context.libraryTab = 2;
    context.manageMode = false;
    context.selectedIds.clear();
    context.screen = Screen::Library;
    return;
  }
  // Selection state of the local tab. Distinct from the catalogue's unpublished page, which
  // is a different screen and carries no selection at all.
  if (screen == "library-manage") {
    context.libraryTab = 0;
    context.manageMode = true;
    context.selectedIds.clear();
    const auto& records = context.library.records();
    if (records.size() > 1) {
      context.selectedIds.push_back(records[0].id);
      context.selectedIds.push_back(records[2].id);
    }
    context.screen = Screen::Library;
    return;
  }
  if (screen.rfind("settings", 0) == 0) {
    context.settingsTab = 0;
    if (screen == "settings-audio") context.settingsTab = 1;
    if (screen == "settings-replay") context.settingsTab = 2;
    if (screen == "settings-more") context.settingsTab = 3;
    context.settingsReturn = Screen::Home;
    if (screen == "settings-replay-on") {
      context.settingsTab = 2;
      context.settings.replayEnabled = true;
    }
    context.screen = Screen::Settings;
    return;
  }
  if (screen == "detail") {
    if (!chartId.empty()) openChartDetail(context, chartId);
    return;
  }
  if (screen == "result") {
    setupResult(context, chartId);
    return;
  }
  if (screen == "calibration") {
    context.screen = Screen::Calibration;
    return;
  }
  if (screen == "game" || screen == "game-pause" || screen == "game-practice"
      || screen == "game-practice-pause" || screen == "game-countdown") {
    if (!chartId.empty()) context.activeChartId = chartId;
    // The practice panel only exists while the practice run is paused, so the paused practice
    // capture turns practice on as well as opening the menu.
    const bool practice = screen == "game-practice" || screen == "game-practice-pause";
    startCaptureChart(context, practice);
    if (context.screen != Screen::Game) return;
    const bool pauseMenu = screen == "game-pause" || screen == "game-practice-pause";
    if (pauseMenu || screen == "game-countdown") {
      context.game.pause = pauseMenu ? GameRuntime::Pause::Menu
                                     : GameRuntime::Pause::Countdown;
      context.game.pauseElapsedSec = 0.0;
      context.game.pausePlayheadSec = context.game.playheadSec;
      context.game.countdown = 3;
      context.game.countdownElapsedSec = 0.0;
    }
    return;
  }
  // Unknown name: leave the application on its start screen.
}

} // namespace rechigros::app
