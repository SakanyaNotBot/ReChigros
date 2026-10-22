// Batch check for the file import flows.
//
// Creates synthetic chart, replay and skin packages in a scratch directory, runs each
// import flow from ImportCore against a temporary library / replay store, and verifies the
// installed state. The application calls the same functions from its import actions, so a
// passing run covers the non-interactive part of every import path.
#include "ImportCore.hpp"

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <miniz.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using rechigros::app::ChartLibrary;
using rechigros::app::ImportOutcome;
using rechigros::app::ReplayStore;
using rechigros::app::SkinImportOutcome;

int g_failures = 0;

void report(bool ok, const std::string& name, const std::string& detail = {}) {
  std::string line = ok ? "PASS  " : "FAIL  ";
  line += name;
  if (!detail.empty()) {
    line += "  [";
    line += detail;
    line += ']';
  }
  std::printf("%s\n", line.c_str());
  if (!ok) ++g_failures;
}

bool writeZip(const std::filesystem::path& archive,
    const std::vector<std::pair<std::string, std::string>>& files) {
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
  for (const auto& [name, content] : files) {
    if (!mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(),
            MZ_BEST_COMPRESSION)) {
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
        {"type": 1, "time": 64.0, "positionX": 0.2}
      ],
      "notesBelow": []
    }
  ]
}
)JSON";

const char* kInfoYaml =
    "name: Import Check\n"
    "charter: Tester\n"
    "level: IN 14\n"
    "chart: chart.json\n"
    "music: music.ogg\n"
    "illustration: cover.png\n";

// The reference replay package stores its judgement entries under the chart's note index;
// this check uses the same document shape.
const char* kReplayJson = R"JSON({
  "v": 1,
  "meta": {
    "songName": "Import Check",
    "difficulty": "IN Lv.14",
    "score": 998877,
    "accuracy": 0.9876,
    "maxCombo": 120,
    "totalNotes": 125,
    "perfect": 120,
    "good": 4,
    "bad": 1,
    "miss": 0,
    "goodEarly": 2,
    "goodLate": 2,
    "stdDevMs": 12.5,
    "timestamp": 1729000000000,
    "mirrorX": false,
    "challenge": true,
    "autoplay": false,
    "practice": false,
    "musicSpeed": 1.0,
    "scrollSpeed": 1.0
  },
  "entries": [
    {"t": 1, "ni": 12, "j": 0, "ts": 1.25}
  ]
}
)JSON";

void checkChartPackage(const std::filesystem::path& root, ChartLibrary& library) {
  const auto archive = root / "chart-pack.zip";
  const bool written = writeZip(archive,
      {{"info.yml", kInfoYaml},
          {"chart.json", kChartJson},
          {"music.ogg", std::string(2048, 'M')},
          {"cover.png", std::string(512, 'C')}});
  report(written, "chart package written", archive.string());
  const ImportOutcome first = rechigros::app::importChartPackage(library, archive);
  report(first.imported, "chart package import", first.message);
  report(first.message == "谱面导入成功", "chart package success message", first.message);
  const ImportOutcome second = rechigros::app::importChartPackage(library, archive);
  report(!second.imported && second.chartId == first.chartId,
      "chart package duplicate deduplicated", second.message);
  report(second.message == "本地已存在该谱面", "chart package duplicate message", second.message);
}

void checkLooseFiles(const std::filesystem::path& root, ChartLibrary& library) {
  const auto folder = root / "loose";
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  rechigros::platform::writeTextFile(folder / "loose-chart.json", kChartJson);
  rechigros::platform::writeBinaryFile(folder / "loose-music.ogg",
      std::vector<std::uint8_t>(1024, 'M'));
  rechigros::platform::writeBinaryFile(folder / "loose-cover.png",
      std::vector<std::uint8_t>(256, 'C'));
  const ImportOutcome outcome = rechigros::app::importLooseChartFiles(library,
      {folder / "loose-chart.json", folder / "loose-music.ogg", folder / "loose-cover.png"});
  report(outcome.imported, "loose file import", outcome.message);
  if (outcome.imported) {
    const auto record = library.find(outcome.chartId);
    report(record.has_value(), "loose file pack installed", outcome.chartId);
    if (record) {
      report(record->complete(), "loose file pack complete", record->id);
      report(record->songName == "loose-music", "loose file song name from audio",
          record->songName);
    }
  }
  const ImportOutcome partial = rechigros::app::importLooseChartFiles(library,
      {folder / "loose-chart.json"});
  report(!partial.imported && partial.message == "请先选择音乐、谱面与背景",
      "loose file import requires every source", partial.message);
}

void checkChartFolder(const std::filesystem::path& root, ChartLibrary& library) {
  const auto folder = root / "pack-folder";
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  rechigros::platform::writeTextFile(folder / "info.yml", kInfoYaml);
  rechigros::platform::writeTextFile(folder / "chart.json", kChartJson);
  rechigros::platform::writeBinaryFile(folder / "music.ogg",
      std::vector<std::uint8_t>(1024, 'M'));
  rechigros::platform::writeBinaryFile(folder / "cover.png",
      std::vector<std::uint8_t>(256, 'C'));
  const ImportOutcome outcome = rechigros::app::importChartDirectory(library, folder);
  report(outcome.imported, "chart folder import", outcome.message);
}

void checkReplay(const std::filesystem::path& root, ReplayStore& replays) {
  const auto archive = root / "replay.cgrp";
  const bool written = writeZip(archive,
      {{"replay.json", kReplayJson},
          {"chart.json", kChartJson},
          {"music.ogg", std::string(1024, 'M')},
          {"cover.png", std::string(256, 'C')}});
  report(written, "replay package written", archive.string());
  const ImportOutcome outcome = rechigros::app::importReplayArchive(archive, replays);
  report(outcome.imported, "replay package import", outcome.message);
  report(outcome.message == "回放导入成功", "replay success message", outcome.message);
  const auto list = replays.list();
  report(list.size() == 1, "replay stored", "count=" + std::to_string(list.size()));
  if (!list.empty()) {
    report(list.front().meta.songName == "Import Check", "replay metadata song name",
        list.front().meta.songName);
    report(list.front().meta.score == 998877, "replay metadata score",
        std::to_string(list.front().meta.score));
    const auto stored = rechigros::app::readReplayFile(list.front().file);
    report(stored.has_value() && stored->judgements.size() == 1,
        "replay judgement entries preserved",
        stored ? std::to_string(stored->judgements.size()) : std::string("unreadable"));
    if (stored && !stored->judgements.empty()) {
      report(stored->judgements.front().noteIndex == 12
              && stored->judgements.front().type == rechigros::game::ReplayJudgement::Type::Tap,
          "replay judgement fields mapped");
    }
  }

  // A plain replay document takes the same path.
  const auto plain = root / "plain-replay.json";
  rechigros::platform::writeTextFile(plain, kReplayJson);
  const ImportOutcome plainOutcome = rechigros::app::importReplayArchive(plain, replays);
  report(plainOutcome.imported, "plain replay import", plainOutcome.message);
  report(replays.list().size() == 2, "plain replay stored",
      "count=" + std::to_string(replays.list().size()));

  // A package without a replay document must be rejected with the reference wording.
  const auto broken = root / "broken.cgrp";
  writeZip(broken, {{"chart.json", kChartJson}});
  const ImportOutcome brokenOutcome = rechigros::app::importReplayArchive(broken, replays);
  report(!brokenOutcome.imported, "replay package without document rejected",
      brokenOutcome.message);
}

void checkSkin(const std::filesystem::path& root, const std::filesystem::path& skinsRoot) {
  const auto archive = root / "skin.zip";
  const bool written = writeZip(archive,
      {{"skin.json", R"JSON({"name": "Import Skin", "hitFxScale": 1.2})JSON"},
          {"respack.json", R"JSON({"hitFx": [8, 2]})JSON"},
          {"note.png", std::string(256, 'N')},
          {"assets/nested-image.png", std::string(128, 'I')}});
  report(written, "skin package written", archive.string());
  const SkinImportOutcome outcome = rechigros::app::importSkinPackage(archive, skinsRoot);
  report(outcome.imported, "skin package import", outcome.message);
  report(outcome.message == "皮肤导入成功", "skin success message", outcome.message);
  report(outcome.skinId == "Import Skin", "skin name from config", outcome.skinId);
  const auto destination = skinsRoot / outcome.skinId;
  report(std::filesystem::is_directory(destination), "skin directory installed",
      destination.string());
  report(rechigros::platform::fileExists(destination / "respack.json"),
      "skin respack installed");
  report(rechigros::platform::fileExists(destination / "nested-image.png"),
      "skin nested asset flattened");
}

} // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::filesystem::path root;
  if (argc > 1 && argv[1] != nullptr) {
    root = std::filesystem::path(std::string(argv[1]));
  } else {
    std::error_code error;
    root = std::filesystem::current_path(error) / "import-check-data";
  }
  rechigros::platform::removeRecursively(root);
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    std::printf("FAIL  cannot create scratch directory: %s\n", root.string().c_str());
    return 1;
  }

  const auto packs = root / "packs";
  const auto replaysRoot = root / "replays";
  const auto skinsRoot = root / "skins";
  ChartLibrary library(packs, root / "library.txt");
  ReplayStore replays(replaysRoot);

  checkChartPackage(root, library);
  checkLooseFiles(root, library);
  checkChartFolder(root, library);
  checkReplay(root, replays);
  checkSkin(root, skinsRoot);

  const auto records = library.records(true);
  report(records.size() >= 3, "library holds every imported chart",
      "count=" + std::to_string(records.size()));

  rechigros::platform::removeRecursively(root);
  if (g_failures == 0) {
    std::printf("IMPORT-CHECK PASS\n");
    return 0;
  }
  std::printf("IMPORT-CHECK FAIL (%d checks)\n", g_failures);
  return 1;
}
