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
  if (!context.library.records(true).empty()) return;
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
  const auto& records = context.library.records(true);
  if (records.empty()) return {};
  return records.front().id;
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

game::PlayResult representativeResult() {
  game::PlayResult result;
  result.score = 912345;
  result.accuracy = 0.9876;
  result.maxCombo = 421;
  result.totalNotes = 430;
  result.perfect = 400;
  result.good = 22;
  result.bad = 3;
  result.miss = 5;
  result.stdDevMs = 12.5;
  result.judgementOffsetBins[8] = 40;
  result.judgementOffsetBins[9] = 120;
  result.judgementOffsetBins[10] = 60;
  result.judgementOffsetBins[11] = 18;
  return result;
}

void setupResult(AppContext& context, const std::string& chartId) {
  populatePlay(context, chartId);
  context.lastResult = representativeResult();
  context.lastRunWasReplay = false;
  context.lastRunHasReplay = true;
  context.lastRunRecorded = true;
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
  if (context.library.records(true).empty()) return;
  context.playOverrides.autoplay = true;
  startChart(context, practice);
}

} // namespace

void setupCaptureScreen(AppContext& context, const std::string& screen) {
  ensureLibrary(context);
  const std::string chartId = firstChartId(context);

  if (screen == "home") {
    context.screen = Screen::Home;
    return;
  }
  if (screen == "library") {
    context.libraryTab = 0;
    context.manageMode = false;
    context.selectedIds.clear();
    context.library.invalidate();
    context.screen = Screen::Library;
    return;
  }
  if (screen == "library-online") {
    context.libraryTab = 1;
    context.screen = Screen::Library;
    return;
  }
  if (screen == "library-manage") {
    context.libraryTab = 0;
    context.manageMode = true;
    context.selectedIds.clear();
    const auto& records = context.library.records(true);
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
      || screen == "game-countdown") {
    startCaptureChart(context, screen == "game-practice");
    if (context.screen != Screen::Game) return;
    if (screen == "game-pause" || screen == "game-countdown") {
      context.game.pause = screen == "game-pause" ? GameRuntime::Pause::Menu
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
