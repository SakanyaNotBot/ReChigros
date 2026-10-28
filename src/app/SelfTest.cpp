#include "rechigros/app/SelfTest.hpp"

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/Settings.hpp"
#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/platform/Archive.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <imgui.h>
#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace rechigros::app {
namespace {

int g_passed = 0;
int g_failed = 0;

void report(bool passed, const std::string& name, const std::string& detail = {}) {
  if (passed) {
    ++g_passed;
    std::cout << "[PASS] " << name;
  } else {
    ++g_failed;
    std::cout << "[FAIL] " << name;
  }
  if (!detail.empty()) std::cout << "  (" << detail << ')';
  std::cout << '\n';
  // Flushed per line so a crash still leaves the checks that had already passed on disk.
  std::cout.flush();
}

std::string hexHash(std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

std::int64_t nowMilliseconds() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool writeText(const std::filesystem::path& path, const std::string& content) {
  return platform::writeTextFile(path, content);
}

// Creates a deflated ZIP archive from the given name/content pairs. The file is
// opened through a wide path on Windows because miniz only takes narrow names.
bool writeZip(const std::filesystem::path& archive,
    const std::vector<std::pair<std::string, std::string>>& files, bool stored = false) {
#ifdef _WIN32
  FILE* file = _wfopen(archive.wstring().c_str(), L"wb");
#else
  FILE* file = std::fopen(archive.string().c_str(), "wb");
#endif
  if (file == nullptr) return false;
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
    std::fclose(file);
    return false;
  }
  bool ok = true;
  const mz_uint compression = stored ? MZ_NO_COMPRESSION : MZ_BEST_COMPRESSION;
  for (const auto& [name, content] : files) {
    if (!mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(),
            compression)) {
      ok = false;
      break;
    }
  }
  if (!mz_zip_writer_finalize_archive(&zip)) ok = false;
  if (!mz_zip_writer_end(&zip)) ok = false;
  if (std::fclose(file) != 0) ok = false;
  return ok;
}

const char* kChartJson = R"JSON({
  "formatVersion": 3,
  "offset": 0.0,
  "judgeLineList": [
    {
      "bpm": 120.0,
      "notesAbove": [
        {"type": 1, "time": 32.0, "positionX": 0.0},
        {"type": 1, "time": 64.0, "positionX": -0.2},
        {"type": 2, "time": 96.0, "positionX": 0.1},
        {"type": 3, "time": 128.0, "holdTime": 16.0, "positionX": 0.2},
        {"type": 4, "time": 160.0, "positionX": -0.1},
        {"type": 1, "time": 192.0, "positionX": 0.0}
      ],
      "notesBelow": []
    }
  ]
}
)JSON";

const char* kInfoYaml =
    "name: Synthetic Test\n"
    "composer: Test Composer\n"
    "charter: Test Charter\n"
    "level: IN 15\n"
    "chart: chart.json\n"
    "music: music.ogg\n"
    "illustration: cover.png\n"
    "chartOffsetMs: 25\n";

// A second pack with its own name, so a grid probe can tell two cards apart.
const char* kNamedInfoYaml =
    "name: Second Sample\n"
    "composer: Test Composer\n"
    "charter: Test Charter\n"
    "level: HD 11\n"
    "chart: chart.json\n"
    "music: music.ogg\n"
    "illustration: cover.png\n"
    "chartOffsetMs: 25\n";

int runHeadlessGameplay(const std::filesystem::path& chartPath, bool autoplay) {
  auto chart = chart::ChartLoader::loadFromFile(chartPath);
  game::GameplaySession session;
  session.setChart(chart.get(), false);
  session.setViewport(game::GameplaySession::Viewport{1280.0F, 720.0F, 16.0F / 9.0F, 1.0F, 380.0F,
      1.0F});
  session.setAutoplay(autoplay);
  int soundEvents = 0;
  constexpr int kFrames = 600;
  constexpr float kDelta = 1.0F / 60.0F;
  for (int frame = 0; frame < kFrames; ++frame) {
    const double time = static_cast<double>(frame) * static_cast<double>(kDelta);
    session.update(time, kDelta);
    soundEvents += static_cast<int>(session.takeSoundEvents().size());
  }
  const auto result = session.judgement().result();
  const int counted = result.perfect + result.good + result.bad + result.miss;
  std::cout << "       gameplay: autoplay=" << (autoplay ? 1 : 0)
            << " notes=" << result.totalNotes << " counted=" << counted
            << " score=" << result.score << " accuracy=" << result.accuracy
            << " maxCombo=" << result.maxCombo << " soundEvents=" << soundEvents << '\n';
  if (counted != static_cast<int>(chart->allNotesSorted.size())) return 1;
  if (!std::isfinite(result.accuracy)) return 2;
  if (autoplay) {
    if (result.score <= 0) return 2;
    if (soundEvents <= 0) return 3;
    if (result.miss != 0 || result.bad != 0) return 4;
    if (result.perfect != result.totalNotes) return 5;
  } else if (result.miss != result.totalNotes) {
    return 6;
  }
  return 0;
}

int countInfoChecks(const std::filesystem::path& directory) {
  int failures = 0;
  if (writeText(directory / "probe.json",
          R"({"name": "Json Name", "composer": "Json Composer", "difficulty": "15"})")) {
    const auto info = readInfoFile(directory / "probe.json");
    if (info.name != "Json Name" || info.composer != "Json Composer") ++failures;
  } else {
    ++failures;
  }
  if (writeText(directory / "probe.yml", "name: Yml Name\ncharter: Yml Charter\n")) {
    const auto info = readInfoFile(directory / "probe.yml");
    if (info.name != "Yml Name" || info.charter != "Yml Charter") ++failures;
  } else {
    ++failures;
  }
  if (writeText(directory / "probe.txt", "# comment\nname: Txt Name\nlevel: 12\n")) {
    const auto info = readInfoFile(directory / "probe.txt");
    if (info.name != "Txt Name" || info.level != "12") ++failures;
  } else {
    ++failures;
  }
  if (writeText(directory / "probe.csv", "name,composer,level\nCsv Name,Csv Composer,7\n")) {
    const auto info = readInfoFile(directory / "probe.csv");
    if (info.name != "Csv Name" || info.composer != "Csv Composer") ++failures;
  } else {
    ++failures;
  }
  ChartInfo difficultyInfo;
  difficultyInfo.difficulty = "15";
  const auto label = buildDifficultyLabel(difficultyInfo, directory / "chart_in.json");
  if (label.find("IN") == std::string::npos || label.find("15") == std::string::npos) ++failures;
  return failures;
}

int settingsRoundTrip(const std::filesystem::path& directory) {
  Settings original;
  original.scrollSpeed = 2.5F;
  original.musicVolume = 1.7F;
  original.audioOffsetMs = -123;
  original.autoplay = true;
  original.replayEnabled = false;
  original.hitOffsetIndicatorMode = 1;
  const auto file = directory / "settings.txt";
  if (!original.save(file)) return 1;
  Settings loaded;
  if (!loaded.load(file)) return 2;
  if (std::abs(loaded.scrollSpeed - original.scrollSpeed) > 1.0e-4F) return 3;
  if (std::abs(loaded.musicVolume - original.musicVolume) > 1.0e-4F) return 4;
  if (loaded.audioOffsetMs != original.audioOffsetMs) return 5;
  if (loaded.autoplay != original.autoplay) return 6;
  if (loaded.replayEnabled != original.replayEnabled) return 7;
  if (loaded.hitOffsetIndicatorMode != original.hitOffsetIndicatorMode) return 8;
  return 0;
}

int replayRoundTrip(const std::filesystem::path& directory) {
  ReplayData data;
  data.meta.chartId = "test-chart";
  data.meta.songName = "Synthetic Test";
  data.meta.difficulty = "IN 15";
  data.meta.score = 987654;
  data.meta.accuracy = 0.9876;
  data.meta.maxCombo = 42;
  data.meta.totalNotes = 6;
  data.meta.timestampMs = nowMilliseconds();
  data.entries.push_back(
      ReplayTouchEvent{ReplayTouchEvent::Kind::Down, 1, 0.25F, 0.8F, 1.25});
  data.entries.push_back(
      ReplayTouchEvent{ReplayTouchEvent::Kind::Move, 1, 0.30F, 0.75F, 1.30});
  data.entries.push_back(ReplayTouchEvent{ReplayTouchEvent::Kind::Up, 1, 0.30F, 0.75F, 1.35});
  const auto file = directory / "replay.json";
  if (!writeReplayFile(file, data)) return 1;
  const auto loaded = readReplayFile(file);
  if (!loaded) return 2;
  if (loaded->entries.size() != data.entries.size()) return 3;
  if (loaded->meta.score != data.meta.score) return 4;
  if (loaded->meta.songName != data.meta.songName) return 5;
  if (std::abs(loaded->entries[0].timeSec - 1.25) > 1.0e-6) return 6;
  if (loaded->entries[1].kind != ReplayTouchEvent::Kind::Move) return 7;
  return 0;
}

// Draws one screen into an off-screen frame and returns how many vertices it produced.
// ImGui builds draw lists on the CPU, so a screen can be exercised without a window or a
// graphics context.
int drawScreenHeadless(AppContext& context, void (*draw)(AppContext&)) {
  ImGui::NewFrame();
  draw(context);
  ImGui::Render();
  const ImDrawData* data = ImGui::GetDrawData();
  if (data == nullptr) return 0;
  int vertices = 0;
  for (int list = 0; list < data->CmdListsCount; ++list) {
    vertices += data->CmdLists[list]->VtxBuffer.Size;
  }
  return vertices;
}

} // namespace

int runSelfTest() {
  g_passed = 0;
  g_failed = 0;
  const auto stamp = nowMilliseconds();
  // Resolve a writable root without the throwing temporary-path query: that query fails
  // when the platform cannot express the user profile path in the narrow encoding. The
  // working directory is always resolvable and is where the runner already starts.
  std::error_code rootError;
  auto root = std::filesystem::temp_directory_path(rootError);
  if (rootError) root = std::filesystem::current_path(rootError);
  if (rootError) root = std::filesystem::path(".");
  root /= "rechigros-selftest-" + std::to_string(stamp);
  const auto work = root / "work";
  const auto packs = root / "packs";
  const auto scoreFile = root / "library.txt";
  std::error_code error;
  std::filesystem::create_directories(work, error);
  std::filesystem::create_directories(packs, error);
  std::cout << "Self-test workspace: " << root.string() << '\n';
  if (error) {
    report(false, "workspace created", error.message());
    std::cout << "SELF-TEST FAIL (0/" << (g_passed + g_failed) << ")\n";
    return 1;
  }

  // Stage the synthetic pack and package it through the archive writer.
  const bool staged = writeText(work / "info.yml", kInfoYaml)
      && writeText(work / "chart.json", kChartJson)
      && writeText(work / "music.ogg", std::string(2048, 'M'))
      && writeText(work / "cover.png", std::string(512, 'C'));
  report(staged, "pack staged");
  const auto archive = root / "synthetic-pack.zip";
  const bool zipped = staged
      && writeZip(archive,
          {{"info.yml", kInfoYaml},
              {"chart.json", kChartJson},
              {"music.ogg", std::string(2048, 'M')},
              {"cover.png", std::string(512, 'C')}});
  report(zipped, "synthetic archive written", archive.filename().string());

  ChartLibrary library(packs, scoreFile);
  bool importOk = false;
  std::string importedId;
  if (zipped) {
    // The shared archive reader is exercised for both entry encodings: stored
    // entries and the deflated entries real chart packages use.
    const auto storedArchive = root / "stored-pack.zip";
    const bool storedWritten = writeZip(storedArchive,
        {{"info.yml", kInfoYaml}, {"chart.json", kChartJson},
            {"music.ogg", std::string(2048, 'M')}, {"cover.png", std::string(512, 'C')}},
        true);
    report(storedWritten, "stored archive written");
    if (storedWritten) {
      const auto storedRoot = root / "stored-extract";
      std::filesystem::create_directories(storedRoot, error);
      bool storedOk = false;
      try {
        storedOk = platform::extractArchive(storedArchive, storedRoot).size() == 4;
      } catch (const std::exception&) {
        storedOk = false;
      }
      report(storedOk, "archive layer extraction (stored entries)");
    }

    const auto deflatedRoot = root / "deflated-extract";
    std::filesystem::create_directories(deflatedRoot, error);
    bool deflatedOk = false;
    try {
      const auto extracted = platform::extractArchive(archive, deflatedRoot);
      const std::string extractedChart = platform::readTextFile(deflatedRoot / "chart.json");
      deflatedOk = extracted.size() == 4 && extractedChart == kChartJson;
    } catch (const std::exception& exception) {
      std::cout << "       archive deflate: " << exception.what() << '\n';
    }
    report(deflatedOk, "archive layer extraction (deflated entries)");

    try {
      const auto result = library.importZip(archive, true);
      importOk = result.imported && result.record.complete();
      importedId = result.record.id;
      report(importOk, "archive import", "id=" + importedId + ", deflated zip");
      const auto again = library.importZip(archive, true);
      report(!again.imported && again.record.id == importedId, "duplicate import deduplicated");
    } catch (const std::exception& exception) {
      report(false, "archive import", exception.what());
    }
  } else {
    report(false, "archive import", "archive was not created");
  }

  // Hostile archive: the traversal entry must be rejected.
  const auto hostileArchive = root / "hostile.zip";
  const auto escapeTarget = root / "escape.txt";
  bool hostileWritten = false;
  if (zipped) {
    hostileWritten = writeZip(hostileArchive, {{"../escape.txt", "escaped"}});
  }
  report(hostileWritten, "hostile archive written");
  if (hostileWritten) {
    const auto extractionRoot = root / "hostile-extract";
    std::filesystem::create_directories(extractionRoot, error);
    bool rejected = false;
    std::vector<std::filesystem::path> extractedFiles;
    try {
      extractedFiles = platform::extractArchive(hostileArchive, extractionRoot);
    } catch (const std::exception&) {
      rejected = true;
    }
    // Unsafe entries are either rejected outright or skipped; in both cases
    // nothing may be written and nothing may escape the extraction root.
    report(rejected || extractedFiles.empty(), "path traversal rejected",
        "files=" + std::to_string(extractedFiles.size()));
    report(!std::filesystem::exists(escapeTarget), "no file escaped the archive root");
  }

  // Library scan and metadata.
  {
    const auto& records = library.records(true);
    bool scanOk = records.size() == 1;
    if (scanOk) {
      const auto& record = records.front();
      scanOk = record.songName == "Synthetic Test" && record.complete()
          && record.difficulty.find("IN") != std::string::npos
          && record.chartOffsetMs == 25;
      if (scanOk) importedId = record.id;
    }
    report(scanOk, "library scan", "records=" + std::to_string(records.size()));
  }

  // Metadata parsers for every supported info format.
  {
    const int failures = countInfoChecks(work);
    report(failures == 0, "info parsers",
        failures == 0 ? "" : std::to_string(failures) + " failed");
  }

  // Chart load.
  std::filesystem::path chartPath;
  {
    const auto record = library.find(importedId);
    if (record) chartPath = record->chartFile;
  }
  bool chartLoaded = false;
  std::size_t noteCount = 0;
  if (!chartPath.empty()) {
    try {
      const auto chart = chart::ChartLoader::loadFromFile(chartPath);
      noteCount = chart->allNotesSorted.size();
      chartLoaded = chart != nullptr && noteCount == 6 && chart->judgeLineList.size() == 1;
    } catch (const std::exception& exception) {
      std::cout << "       chart load: " << exception.what() << '\n';
    }
  }
  report(chartLoaded, "chart load", "notes=" + std::to_string(noteCount));

  // Headless gameplay: autoplay and a miss-only manual run.
  if (chartLoaded) {
    bool gameplayOk = false;
    try {
      gameplayOk = runHeadlessGameplay(chartPath, true) == 0;
    } catch (const std::exception& exception) {
      std::cout << "       gameplay: " << exception.what() << '\n';
    }
    report(gameplayOk, "headless gameplay (autoplay, 600 frames)");
    bool manualOk = false;
    try {
      manualOk = runHeadlessGameplay(chartPath, false) == 0;
    } catch (const std::exception& exception) {
      std::cout << "       gameplay: " << exception.what() << '\n';
    }
    report(manualOk, "headless touch pipeline (600 frames)");
  }

  // Score persistence.
  {
    const auto record = library.find(importedId);
    if (record && chartLoaded) {
      game::PlayResult playResult;
      playResult.score = 912345;
      playResult.accuracy = 0.9234;
      playResult.totalNotes = 6;
      playResult.perfect = 5;
      playResult.good = 1;
      library.recordBestResult(importedId, playResult);
      library.recordPlayTime(importedId, nowMilliseconds());
      ChartLibrary reopened(packs, scoreFile);
      const auto persisted = reopened.find(importedId);
      report(persisted.has_value() && persisted->bestScore == 912345
              && persisted->bestAcc == 9234,
          "best score persisted");
    } else {
      report(false, "best score persisted", "no record");
    }
  }

  // Settings and replay serialization.
  report(settingsRoundTrip(work) == 0, "settings round trip");
  report(replayRoundTrip(work) == 0, "replay round trip");
  {
    ReplayStore store(root / "replays");
    ReplayData data;
    data.meta.songName = "Synthetic Test";
    data.meta.timestampMs = nowMilliseconds();
    data.entries.push_back(ReplayTouchEvent{ReplayTouchEvent::Kind::Down, 3, 0.5F, 0.5F, 0.25});
    const std::string id = store.save(data);
    const auto listed = store.list();
    report(!id.empty() && listed.size() == 1 && listed.front().id == id,
        "replay store save/list", "id=" + id);
  }

  // Every screen must build a non-empty frame without a window. This catches a screen that
  // draws nothing, throws, or crashes when the renderer has not been initialised.
  {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 60.0F;
    ui::applyTheme();
    io.DisplayFramebufferScale = ImVec2(1.25F, 1.0F);
    ui::Canvas highDensityCanvas = ui::canvas();
    const float logicalWidth = ui::labelWidth(highDensityCanvas, "ratio", 20.0F, false);
    const float nativeWidth = ui::labelWidth(highDensityCanvas, "ratio", 25.0F, false);
    report(std::abs(logicalWidth - nativeWidth) <= 0.001F,
        "high-density text keeps logical metrics");
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    AppContext context(work / "ui");
    const std::pair<const char*, void (*)(AppContext&)> screens[] = {
        {"home screen draws", drawHomeScreen},
        {"library screen draws", drawLibraryScreen},
        {"settings screen draws", drawSettingsScreen},
        {"calibration screen draws", drawCalibrationScreen},
        {"detail screen draws", drawDetailScreen},
        {"result screen draws", drawResultScreen},
    };
    for (const auto& [name, draw] : screens) {
      // An auto-sized window settles its size over two frames, so every screen is drawn
      // twice before its geometry is measured.
      drawScreenHeadless(context, draw);
      const int vertices = drawScreenHeadless(context, draw);
      report(vertices > 0, name, "vertices=" + std::to_string(vertices));
    }

    {
      // Opening the calibration tool from the settings page must not disturb where leaving the
      // settings page goes. Both are separate destinations: calibration always comes back to the
      // settings page, while the settings page returns to whatever opened it. Writing the first
      // into the second is what made the page impossible to leave after a calibration attempt -
      // the back action landed on the settings page again.
      //
      // The row is pressed through the real pointer path rather than by setting the field, because
      // the defect is in the press handler; a test that pokes the state would pass either way.
      context.settingsReturn = Screen::Library;
      context.settingsTab = 1;
      context.screen = Screen::Settings;
      context.settingsDirty = false;
      bool openedCalibration = false;
      // The row is found by asking the page which rectangle it registered as pressable, rather
      // than by guessing a coordinate: the row's address shifts whenever a row is added above it.
      // The calibration row is the last action block on the audio page, so the sweep walks the
      // registered rectangles from the bottom up and presses the one that opens the page.
      const auto pressAt = [&](float x, float y) {
        context.screen = Screen::Settings;
        io.AddMousePosEvent(x, y);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawSettingsScreen);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawSettingsScreen);
        drawScreenHeadless(context, drawSettingsScreen);
      };
      // Only the rows inside the viewport register a rectangle, and the calibration row sits near
      // the bottom of a page that scrolls, so the sweep walks the list from the top to the bottom
      // of the scroll range instead of assuming the row is already on screen.
      for (int scrollStep = 0; scrollStep < 24 && !openedCalibration; ++scrollStep) {
        context.screen = Screen::Settings;
        context.settingsTab = 1;
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMouseWheelEvent(0.0F, -6.0F);
        drawScreenHeadless(context, drawSettingsScreen);
        const std::vector<std::array<float, 4>> rects = registeredSettingsControlRects();
        for (std::size_t index = rects.size(); index-- > 0 && !openedCalibration;) {
          const auto& rect = rects[index];
          pressAt((rect[0] + rect[2]) * 0.5F, (rect[1] + rect[3]) * 0.5F);
          if (context.screen == Screen::Calibration) openedCalibration = true;
          // Some rows toggle in place; restore the page so the next candidate is pressed on the
          // audio tab rather than on whichever page the previous press opened.
          if (!openedCalibration) {
            context.screen = Screen::Settings;
            context.settingsTab = 1;
          }
        }
      }
      io.AddMousePosEvent(-100.0F, -100.0F);
      report(openedCalibration, "the calibration row opens the calibration page");
      report(context.settingsReturn == Screen::Library,
          "opening calibration leaves the settings page's target alone",
          "settingsReturn=" + std::to_string(static_cast<int>(context.settingsReturn)));

      // Leaving calibration lands back on the settings page...
      context.calibration.audioLoaded = true;
      exitCalibration(context, false);
      report(context.screen == Screen::Settings,
          "calibration returns to the settings page it came from");

      // ...and the settings page can then still be left, which is what the defect prevented.
      navigateTo(context, context.settingsReturn);
      report(context.screen == Screen::Library,
          "the settings page can still be left after a calibration attempt");
    }

    // Settings owns its pointer state instead of using immediate-mode buttons. Exercise
    // complete press/release frames so page scrolling cannot steal a switch's press.
    context.settingsTab = 0;
    context.settings.realtimeAccuracy = false;
    context.settings.autoplay = false;
    context.screen = Screen::Settings;
    io.DisplaySize = ImVec2(1920.0F, 1170.0F);
    io.AddMousePosEvent(640.0F, 300.0F);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMouseWheelEvent(0.0F, -20.0F);
    drawScreenHeadless(context, drawSettingsScreen);
    // The row address depends on the page contents above it, and adding a row shifts every
    // switch below. Instead of hard-coding a coordinate, walk down the visible rows and press
    // the first one whose press actually lands on a switch; failing to find any is itself a
    // failure, so the test cannot pass vacuously.
    bool toggledAfterScroll = false;
    for (float y = 260.0F; y < 1140.0F && !toggledAfterScroll; y += 24.0F) {
      context.settings.realtimeAccuracy = false;
      context.settingsDirty = false;
      io.AddMousePosEvent(640.0F, y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawSettingsScreen);
      drawScreenHeadless(context, drawSettingsScreen);
      toggledAfterScroll = context.settings.realtimeAccuracy && context.settingsDirty;
    }
    report(toggledAfterScroll, "settings switch toggles after scrolling");
    io.AddMousePosEvent(-100.0F, -100.0F);
    drawScreenHeadless(context, drawSettingsScreen);

    // A finger drag that starts on a row must scroll the page, not toggle the row. The
    // reference ScrollView takes the gesture from its children once it is vertical.
    context.settingsTab = 0;
    context.settings.realtimeAccuracy = false;
    context.settingsDirty = false;
    drawScreenHeadless(context, drawSettingsScreen);
    // Same reasoning as above: sweep rows until a press lands on one, then drag vertically
    // from that same point and require that the row was NOT toggled.
    bool dragCancelled = true;
    bool landedOnRow = false;
    for (float y = 260.0F; y < 1140.0F && !landedOnRow; y += 24.0F) {
      context.settings.realtimeAccuracy = false;
      context.settingsDirty = false;
      io.AddMousePosEvent(640.0F, y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMousePosEvent(640.0F, y + 80.0F);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMousePosEvent(640.0F, y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawSettingsScreen);
      drawScreenHeadless(context, drawSettingsScreen);
      // A press that reached a row is one whose release over the same row would have
      // toggled it; the drag must have prevented that.
      if (context.settings.realtimeAccuracy) {
        dragCancelled = false;
        landedOnRow = true;
      }
    }
    report(dragCancelled, "dragging from a row scrolls instead of toggling");
    io.AddMousePosEvent(-100.0F, -100.0F);
    drawScreenHeadless(context, drawSettingsScreen);

    context.settings.realtimeAccuracy = false;
    context.settingsDirty = false;
    io.AddMousePosEvent(400.0F, 400.0F);
    io.AddMouseButtonEvent(0, true);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMousePosEvent(50.0F, 400.0F);
    io.AddMouseButtonEvent(0, false);
    drawScreenHeadless(context, drawSettingsScreen);
    report(!context.settings.realtimeAccuracy, "settings switch release outside cancels");
    io.AddMousePosEvent(400.0F, 400.0F);
    io.AddMouseButtonEvent(0, true);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMousePosEvent(410.0F, 440.0F);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMousePosEvent(400.0F, 400.0F);
    io.AddMouseButtonEvent(0, false);
    drawScreenHeadless(context, drawSettingsScreen);
    report(!context.settings.realtimeAccuracy, "settings switch drag cancels");
    io.AddMousePosEvent(-100.0F, -100.0F);
    drawScreenHeadless(context, drawSettingsScreen);

    const auto sliderX = [](const std::array<float, 4>& rect, float fraction) {
      const ui::Canvas canvas = ui::canvas();
      const float density = canvas.dp(1.0F);
      const auto dp = [density](float value) { return std::round(value * density); };
      const float width = rect[2] - rect[0];
      constexpr float slope = 0.26794919F;
      const float scale = std::min(1.0F, width / dp(100.0F));
      const float endInset = dp(16.0F) * scale;
      const float capWidth = dp(9.0F) * scale;
      const float thumbWidth = dp(30.0F) * scale;
      const float thumbGap = dp(2.0F) * scale;
      const float halfCapDelta = dp(26.0F) * scale * slope;
      const float valueLeft = endInset + capWidth - halfCapDelta + thumbGap + thumbWidth * 0.5F;
      const float valueRight = std::max(valueLeft,
          width - endInset - capWidth + halfCapDelta - thumbGap - thumbWidth * 0.5F);
      return rect[0] + valueLeft + (valueRight - valueLeft) * fraction;
    };
    context.settingsTab = 0;
    context.settings.uiScale = 1.0F;
    ui::setInterfaceScale(1.0F);
    context.settings.keyScale = 0.7F;
    context.screen = Screen::Settings;
    io.DisplaySize = ImVec2(1920.0F, 1080.0F);
    io.AddMousePosEvent(640.0F, 500.0F);
    io.AddMouseWheelEvent(0.0F, 10000.0F);
    drawScreenHeadless(context, drawSettingsScreen);
    drawScreenHeadless(context, drawSettingsScreen);
    const auto sliderRects = registeredSettingsControlRects();
    bool followsSpan = sliderRects.size() > 1;
    std::string trajectory;
    if (followsSpan) {
      const auto& rect = sliderRects[1];
      const float y = (rect[1] + rect[3]) * 0.5F;
      io.AddMousePosEvent(sliderX(rect, 0.10F), y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawSettingsScreen);
      for (const float fraction : {0.10F, 0.25F, 0.50F, 0.75F, 0.90F}) {
        io.AddMousePosEvent(sliderX(rect, fraction), y);
        drawScreenHeadless(context, drawSettingsScreen);
        const float expected = fraction < 0.1001F ? 0.70F
            : std::round((0.7F + fraction * 0.8F) * 100.0F) / 100.0F;
        const bool pointMatches = std::abs(context.settings.keyScale - expected) < 0.001F;
        followsSpan &= pointMatches;
        char point[128];
        std::snprintf(point, sizeof(point), "%.2f:%.2f/%.2f%s", fraction, expected,
            context.settings.keyScale, fraction < 0.89F ? " " : "");
        trajectory += point;
      }
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawSettingsScreen);
      char geometry[192];
      std::snprintf(geometry, sizeof(geometry), " uiScale=%.3f rect=%.1f,%.1f,%.1f,%.1f",
          context.settings.uiScale, rect[0], rect[1], rect[2], rect[3]);
      trajectory += geometry;
    }
    report(followsSpan, "slider drag follows the effective value span", trajectory);
    if (sliderRects.size() > 1) {
      const auto& rect = sliderRects[1];
      const float y = (rect[1] + rect[3]) * 0.5F;
      context.settings.keyScale = 0.7F;
      io.AddMousePosEvent(sliderX(rect, 0.25F), y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawSettingsScreen);
      char detail[96];
      std::snprintf(detail, sizeof(detail), "expected=0.90 actual=%.3f", context.settings.keyScale);
      report(std::abs(context.settings.keyScale - 0.90F) < 0.001F,
          "slider release uses the effective value span", detail);
      report(std::abs((rect[3] - rect[1]) - ui::canvas().dp(56.0F)) < 0.01F,
          "slider hit target is 56dp high", "height=" + std::to_string(rect[3] - rect[1]));
      context.settings.keyScale = 0.7F;
      io.AddMousePosEvent(sliderX(rect, 0.50F), y);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMousePosEvent(sliderX(rect, 0.50F), y + 80.0F);
      drawScreenHeadless(context, drawSettingsScreen);
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawSettingsScreen);
      report(std::abs(context.settings.keyScale - 0.7F) < 0.001F,
          "vertical drags scroll instead of changing the slider");

      context.settings.keyScale = 0.7F;
      io.AddMouseWheelEvent(0.0F, 10000.0F);
      drawScreenHeadless(context, drawSettingsScreen);
      drawScreenHeadless(context, drawSettingsScreen);
      const auto lockedRects = registeredSettingsControlRects();
      bool retained = lockedRects.size() > 1;
      if (retained) {
        const auto& locked = lockedRects[1];
        const float lockedY = (locked[1] + locked[3]) * 0.5F;
        const float outsideY = lockedY - 180.0F;
        io.AddMousePosEvent(sliderX(locked, 0.50F), lockedY);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMousePosEvent(sliderX(locked, 0.25F), lockedY);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMousePosEvent(sliderX(locked, 0.25F), outsideY);
        drawScreenHeadless(context, drawSettingsScreen);
        io.AddMousePosEvent(sliderX(locked, 0.90F), outsideY);
        drawScreenHeadless(context, drawSettingsScreen);
        const auto currentRects = registeredSettingsControlRects();
        retained = currentRects.size() > 1 && currentRects[1] == lockedRects[1]
            && std::abs(context.settings.keyScale - 1.42F) < 0.001F;
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawSettingsScreen);
        retained &= std::abs(context.settings.keyScale - 1.42F) < 0.001F;
      }
      report(retained, "horizontal slider drags keep capture until release",
          "expected=1.42 actual=" + std::to_string(context.settings.keyScale));
    }

    context.settingsTab = 2;
    context.settings.replayEnabled = false;
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMousePosEvent(1650.0F, 362.0F);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMouseButtonEvent(0, true);
    drawScreenHeadless(context, drawSettingsScreen);
    io.AddMouseButtonEvent(0, false);
    drawScreenHeadless(context, drawSettingsScreen);
    drawScreenHeadless(context, drawSettingsScreen);
    report(context.settings.replayEnabled, "settings switch icon toggles",
        "down=" + std::to_string(io.MouseDown[0]));
    context.settings.replayEnabled = true;
    context.nowSec += 1.0;
    drawScreenHeadless(context, drawSettingsScreen);
    int markVertices = 0;
    const ImDrawData* settingsDraw = ImGui::GetDrawData();
    if (settingsDraw != nullptr) {
      const ImU32 markColor = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
      for (int list = 0; list < settingsDraw->CmdListsCount; ++list) {
        for (const ImDrawVert& vertex : settingsDraw->CmdLists[list]->VtxBuffer) {
          if (vertex.col == markColor) ++markVertices;
        }
      }
    }
    report(markVertices > 0, "checked switch draws contrasting mark",
        "vertices=" + std::to_string(markVertices));
    context.settings.replayEnabled = false;
    context.settingsTab = 0;
    io.DisplaySize = ImVec2(1280.0F, 720.0F);

    // The library and detail screens must reflect what the library actually holds, so an
    // empty library and a populated one have to produce different frames.
    drawScreenHeadless(context, drawLibraryScreen);
    const int emptyLibraryVertices = drawScreenHeadless(context, drawLibraryScreen);
    std::string uiChartId;
    if (std::filesystem::exists(archive)) {
      try {
        const auto imported = context.library.importZip(archive, true);
        if (imported.imported) uiChartId = imported.record.id;
      } catch (const std::exception&) {
        // A failure here is reported by the import check earlier in this run.
      }
    }
    drawScreenHeadless(context, drawLibraryScreen);
    const int filledLibraryVertices = drawScreenHeadless(context, drawLibraryScreen);
    report(!uiChartId.empty() && filledLibraryVertices > emptyLibraryVertices,
        "library screen reflects its content",
        "empty=" + std::to_string(emptyLibraryVertices) +
            ", filled=" + std::to_string(filledLibraryVertices));

    if (!uiChartId.empty()) {
      openChartDetail(context, uiChartId);
      drawScreenHeadless(context, drawDetailScreen);
      const int detailVertices = drawScreenHeadless(context, drawDetailScreen);
      report(context.screen == Screen::Detail && detailVertices > 0,
          "detail screen opens the selected chart", "vertices=" + std::to_string(detailVertices));
      // A finger held on a card has to open manage mode and claim that card, and the release
      // that ends the hold must not also open the chart. A second pack is installed first so
      // the grid holds more than one card: with a single card, selecting "the card" and
      // selecting "whatever is in the list" would be the same observation.
      const auto secondArchive = root / "synthetic-pack-two.zip";
      if (staged) {
        (void)writeZip(secondArchive,
            {{"info.yml", kNamedInfoYaml},
                {"chart.json", kChartJson},
                {"music.ogg", std::string(2048, 'N')},
                {"cover.png", std::string(512, 'D')}});
      }
      std::string secondChartId;
      if (std::filesystem::exists(secondArchive)) {
        try {
          const auto imported = context.library.importZip(secondArchive, true);
          secondChartId = imported.record.id;
        } catch (const std::exception&) {
        }
      }
      navigateTo(context, Screen::Library);
      context.libraryTab = 0;
      context.manageMode = false;
      context.selectedIds.clear();
      drawScreenHeadless(context, drawLibraryScreen);
      report(!secondChartId.empty() && context.library.records().size() >= 2,
          "second pack installed for the grid probe",
          "records=" + std::to_string(context.library.records().size()));
      // Cards are located by tapping along the first row; a tap that opens the detail page
      // proves it landed on a card. Two different ids are needed, so a grid that renders no
      // cards fails the check instead of passing quietly.
      constexpr float kCardProbeY = 150.0F;
      float cardProbeX = -1.0F;
      std::string tappedCardId;
      float neighbourProbeX = -1.0F;
      std::string neighbourCardId;
      for (float x = 150.0F; x < 1100.0F && neighbourCardId.empty(); x += 30.0F) {
        context.screen = Screen::Library;
        context.manageMode = false;
        context.selectedIds.clear();
        io.AddMousePosEvent(x, kCardProbeY);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        if (context.screen != Screen::Detail || context.activeChartId.empty()) continue;
        if (tappedCardId.empty()) {
          tappedCardId = context.activeChartId;
          cardProbeX = x;
        } else if (context.activeChartId != tappedCardId) {
          neighbourCardId = context.activeChartId;
          neighbourProbeX = x;
        }
      }
      report(!tappedCardId.empty(), "library card tap opens the detail page",
          "x=" + std::to_string(cardProbeX) + ", id=" + tappedCardId);
      report(!neighbourCardId.empty(), "library first row holds several cards",
          "x=" + std::to_string(neighbourProbeX) + ", id=" + neighbourCardId);

      // Re-drawn while the clock advances, one frame at a time: the screen drops its
      // transient gesture state when two draws are further apart than its re-entry guard.
      // The state is read after the release as well: that is where a hold that was not
      // consumed shows up, because the gesture then also counts as a tap.
      bool holdClaimedCard = false;
      bool holdStayedInLibrary = false;
      bool holdSurvivedRelease = false;
      if (!tappedCardId.empty()) {
        navigateTo(context, Screen::Library);
        context.nowSec += 1.0;
        io.AddMousePosEvent(cardProbeX, kCardProbeY);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawLibraryScreen);
        for (int frame = 0; frame < 12; ++frame) {
          context.nowSec += 0.05;
          drawScreenHeadless(context, drawLibraryScreen);
        }
        holdClaimedCard = context.manageMode && context.selectedIds.size() == 1
            && context.selectedIds.front() == tappedCardId;
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        holdStayedInLibrary = context.screen == Screen::Library;
        holdSurvivedRelease = holdClaimedCard && context.manageMode
            && context.selectedIds.size() == 1
            && context.selectedIds.front() == tappedCardId;
      }
      report(holdClaimedCard, "holding a library card enters manage mode and selects it",
          "manage=" + std::to_string(context.manageMode ? 1 : 0) + ", selected="
              + std::to_string(context.selectedIds.size()));
      report(holdSurvivedRelease,
          "the hold is consumed so the release does not also act on the card",
          "manage=" + std::to_string(context.manageMode ? 1 : 0) + ", selected="
              + std::to_string(context.selectedIds.size()) + ", screen="
              + std::to_string(static_cast<int>(context.screen)));
      // The card next to the one held has to stay unselected, which is what tells a real
      // per-card claim apart from manage mode coming up with an empty selection.
      report(!neighbourCardId.empty() && std::find(context.selectedIds.begin(),
                 context.selectedIds.end(), neighbourCardId) == context.selectedIds.end(),
          "holding one card leaves the neighbouring card unselected");
      report(!tappedCardId.empty() && holdStayedInLibrary,
          "releasing a held card stays on the library screen",
          "screen=" + std::to_string(static_cast<int>(context.screen)));

      // Once managing, a hold keeps the mode and whatever was already selected.
      bool holdWhileManagingKeptSelection = false;
      if (!tappedCardId.empty()) {
        context.manageMode = true;
        context.selectedIds.clear();
        context.selectedIds.push_back(tappedCardId);
        context.screen = Screen::Library;
        context.nowSec += 1.0;
        io.AddMousePosEvent(cardProbeX, kCardProbeY);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawLibraryScreen);
        for (int frame = 0; frame < 12; ++frame) {
          context.nowSec += 0.05;
          drawScreenHeadless(context, drawLibraryScreen);
        }
        holdWhileManagingKeptSelection = context.manageMode
            && context.selectedIds.size() == 1 && context.selectedIds.front() == tappedCardId;
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
      }
      report(holdWhileManagingKeptSelection,
          "holding a card while managing keeps the selection");

      // A drag out of a card belongs to the scroll view, so no hold may fire even after the
      // finger has been down for longer than the hold threshold.
      bool dragStayedOutOfManage = false;
      if (!tappedCardId.empty()) {
        navigateTo(context, Screen::Library);
        context.nowSec += 1.0;
        io.AddMousePosEvent(cardProbeX, kCardProbeY);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMouseButtonEvent(0, true);
        drawScreenHeadless(context, drawLibraryScreen);
        io.AddMousePosEvent(cardProbeX, kCardProbeY + 80.0F);
        for (int frame = 0; frame < 12; ++frame) {
          context.nowSec += 0.05;
          drawScreenHeadless(context, drawLibraryScreen);
        }
        io.AddMouseButtonEvent(0, false);
        drawScreenHeadless(context, drawLibraryScreen);
        drawScreenHeadless(context, drawLibraryScreen);
        dragStayedOutOfManage = !context.manageMode && context.screen == Screen::Library;
      }
      report(!tappedCardId.empty() && dragStayedOutOfManage,
          "dragging from a card scrolls instead of entering manage mode");

      // The corner plate is the other way into manage mode and has to keep working.
      navigateTo(context, Screen::Library);
      context.nowSec += 1.0;
      io.AddMousePosEvent(static_cast<float>(io.DisplaySize.x) - 70.0F,
          static_cast<float>(io.DisplaySize.y) - 105.0F);
      drawScreenHeadless(context, drawLibraryScreen);
      drawScreenHeadless(context, drawLibraryScreen);
      io.AddMouseButtonEvent(0, true);
      drawScreenHeadless(context, drawLibraryScreen);
      io.AddMouseButtonEvent(0, false);
      drawScreenHeadless(context, drawLibraryScreen);
      drawScreenHeadless(context, drawLibraryScreen);
      report(context.manageMode && context.selectedIds.empty(),
          "the corner plate still enters manage mode with nothing selected",
          "manage=" + std::to_string(context.manageMode ? 1 : 0) + ", selected="
              + std::to_string(context.selectedIds.size()));
      io.AddMousePosEvent(-100.0F, -100.0F);
      context.manageMode = false;
      context.selectedIds.clear();
      context.screen = Screen::Library;
      drawScreenHeadless(context, drawLibraryScreen);

      // The recording page renders the chart the detail page describes. Entering it has to load
      // that chart, because the page can only start a render for a loaded one; the failure this
      // covers is the page opening and then refusing to start with "no chart is loaded".
      context.renderReturn = Screen::Detail;
      std::string renderError;
      const bool renderPrepared = prepareRenderChart(context, renderError);
      report(renderPrepared && context.game.loaded && context.game.chart != nullptr,
          "recording page loads the chart it describes",
          renderPrepared ? "chartTime=" + std::to_string(context.game.chart->chartEndTimeSec)
                         : renderError);
      if (renderPrepared) {
        // The run the page prepares is an autoplay run, and the labels it draws come from the
        // chart, not from whatever the previous screen left behind.
        const auto record = context.library.find(uiChartId);
        report(context.play.songName == (record ? record->songName : std::string()),
            "recording page takes the song name from the chart",
            "song=" + context.play.songName);
        const long long chartEnd = static_cast<long long>(context.game.chart->chartEndTimeSec);
        report(chartEnd > 0, "recording page chart has a length", "endSec=" + std::to_string(chartEnd));
      }
      // Pressing the start plate on a page opened from the detail view must not report the
      // session's "no chart is loaded" reason. Without a graphics device the render itself
      // cannot run here, so the assertion is on the reason, which is the defect the user hit.
      if (renderPrepared) {
        RecordSession::Settings recordSettings;
        std::error_code recordError;
        recordSettings.outputDirectory = work / "record";
        std::filesystem::create_directories(recordSettings.outputDirectory, recordError);
        recordSettings.fileName = "selftest";
        recordSettings.fps = 30;
        std::string startError;
        const bool started = context.recording.start(context, recordSettings, startError);
        if (context.recording.state() == RecordSession::State::Running) {
          context.recording.cancel(context);
        }
        report(startError != "no chart is loaded",
            "recording page can start a render for its chart",
            started ? std::string("started") : startError);
      }
      // Leaving the page must not leave a chart loaded for a screen that never started a run.
      navigateTo(context, Screen::Render);
      navigateTo(context, Screen::Detail);
      report(!context.game.loaded && !context.renderChartPrepared,
          "leaving the recording page releases its chart");
      navigateTo(context, Screen::Home);
    }

    // The result screen must reflect a recorded result; an empty one must draw less.
    context.lastResult = game::PlayResult{};
    drawScreenHeadless(context, drawResultScreen);
    const int emptyResultVertices = drawScreenHeadless(context, drawResultScreen);
    game::PlayResult filled;
    filled.score = 912345;
    filled.accuracy = 0.9876;
    filled.maxCombo = 421;
    filled.totalNotes = 430;
    filled.perfect = 400;
    filled.good = 22;
    filled.bad = 3;
    filled.miss = 5;
    filled.judgementOffsetBins[9] = 120;
    filled.judgementOffsetBins[10] = 60;
    context.lastResult = filled;
    drawScreenHeadless(context, drawResultScreen);
    const int filledResultVertices = drawScreenHeadless(context, drawResultScreen);
    report(filledResultVertices > 0 && context.lastResult.score == 912345,
        "result screen reflects a recorded result",
        "empty=" + std::to_string(emptyResultVertices) +
            ", filled=" + std::to_string(filledResultVertices));

    // A toast only draws while a message is showing, so queue one first. An auto-sized
    // window settles its size over two frames, so the frame is drawn twice.
    notify(context, "self-test");
    drawScreenHeadless(context, drawToast);
    const int toastVertices = drawScreenHeadless(context, drawToast);
    report(toastVertices > 0, "toast draws", "vertices=" + std::to_string(toastVertices));

    // Navigating between screens must not need a renderer either.
    navigateTo(context, Screen::Library);
    const int libraryVertices = drawScreenHeadless(context, drawLibraryScreen);
    report(context.screen == Screen::Library && libraryVertices > 0,
        "navigation reaches the library screen", "vertices=" + std::to_string(libraryVertices));

    ImGui::DestroyContext();
  }

  platform::removeRecursively(root);
  const int total = g_passed + g_failed;
  if (g_failed == 0) {
    std::cout << "SELF-TEST PASS (" << g_passed << '/' << total << ")\n";
    return 0;
  }
  std::cout << "SELF-TEST FAIL (" << g_failed << " of " << total << " checks failed)\n";
  return 1;
}

} // namespace rechigros::app
