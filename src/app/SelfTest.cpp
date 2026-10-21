#include "rechigros/app/SelfTest.hpp"

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
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
    report(filledResultVertices > 0 && filledResultVertices != emptyResultVertices,
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
