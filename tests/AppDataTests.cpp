// Tests for the application-layer data helpers: chart metadata labels, settings
// clamping and replay serialisation. These units are deliberately free of any interface
// or GPU dependency, so they run headless like the rest of the suites.

#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Settings.hpp"
#include "rechigros/game/PlayResult.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "TestScratch.hpp"

namespace {

int g_failures = 0;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

// U+2002 EN SPACE encoded as UTF-8, the separator the reference labels use.
constexpr const char* kEnSpace = "\xE2\x80\x82";

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

void testDifficultyNormalisation() {
  using rechigros::app::buildDifficultyLabel;
  using rechigros::app::ChartInfo;
  using rechigros::app::normalizeDifficultyText;

  // Runs of ASCII spaces collapse to one separator, and every separator becomes U+2002.
  require(normalizeDifficultyText("IN    Lv.15") == std::string("IN") + kEnSpace + "Lv.15",
      "a run of spaces collapses into one en space");
  require(normalizeDifficultyText("SP Lv.16") == std::string("SP") + kEnSpace + "Lv.16",
      "a single space becomes an en space");
  require(normalizeDifficultyText("  IN  15  ") ==
          std::string(kEnSpace) + "IN" + kEnSpace + "15" + kEnSpace,
      "leading and trailing spaces survive as single en spaces");
  require(normalizeDifficultyText("IN  Lv.15") == normalizeDifficultyText("IN Lv.15"),
      "normalisation is insensitive to the number of spaces");
  require(!contains(normalizeDifficultyText("IN Lv.15"), " "),
      "no ASCII space survives normalisation");
  // A typographic space written by an editor is normalised the same way.
  require(normalizeDifficultyText("IN\xE2\x80\x83Lv.15") == normalizeDifficultyText("IN Lv.15"),
      "an em space normalises like an ASCII space");
  require(normalizeDifficultyText("") .empty(), "empty text stays empty");
  require(normalizeDifficultyText("15") == "15", "text without spaces is unchanged");

  // The default label is the reference's "SP Lv.?" with an en space.
  ChartInfo info;
  require(buildDifficultyLabel(info, {}) == std::string("SP") + kEnSpace + "Lv.?",
      "the missing-difficulty label uses an en space");

  // A level string wins over the difficulty chain.
  info.level = "IN  Lv.15";
  require(buildDifficultyLabel(info, {}) == std::string("IN") + kEnSpace + "Lv.15",
      "a declared level is normalised and used as-is");

  // A numeric difficulty gets the prefix inferred from the chart file name.
  ChartInfo difficultyOnly;
  difficultyOnly.difficulty = "15";
  require(buildDifficultyLabel(difficultyOnly, std::filesystem::path("song.IN.json")) ==
          std::string("IN") + kEnSpace + "Lv.15",
      "a numeric difficulty gains the file-name prefix");
  // A label that already names its own level is passed through untouched.
  ChartInfo labelled;
  labelled.difficulty = "IN Lv.15";
  require(buildDifficultyLabel(labelled, std::filesystem::path("song.AT.json")) ==
          std::string("IN") + kEnSpace + "Lv.15",
      "an authored label is not prefixed again");
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
}

void testSettingsAspectRatio() {
  using rechigros::app::Settings;
  const auto directory = rechigros::test::scratchDirectory("app-data-settings");

  // A fresh configuration uses the reference's 16:9 stage.
  Settings defaults;
  require(std::abs(defaults.aspectRatio - 16.0F / 9.0F) < 1.0e-6F,
      "the default stage aspect is 16:9");

  // A stored ratio above the allowed range is clamped; zero keeps meaning "follow window".
  const auto clampedPath = directory / "clamped.ini";
  writeText(clampedPath, "aspectRatio=9.5\n");
  Settings clamped;
  require(clamped.load(clampedPath), "a settings file with a ratio loads");
  require(std::abs(clamped.aspectRatio - 5.0F) < 1.0e-6F,
      "an out-of-range aspect ratio is clamped to the top of the range");

  const auto narrowPath = directory / "narrow.ini";
  writeText(narrowPath, "aspectRatio=0.2\n");
  Settings narrow;
  require(narrow.load(narrowPath), "a narrow ratio loads");
  require(std::abs(narrow.aspectRatio - 0.5F) < 1.0e-6F,
      "an out-of-range aspect ratio is clamped to the bottom of the range");

  const auto followPath = directory / "follow.ini";
  writeText(followPath, "aspectRatio=0\n");
  Settings follow;
  require(follow.load(followPath), "a follow-window ratio loads");
  require(follow.aspectRatio == 0.0F, "zero keeps meaning follow the window");

  // A corrupt ratio falls back to the struct default instead of reaching the renderer.
  const auto corruptPath = directory / "corrupt.ini";
  writeText(corruptPath, "aspectRatio=not-a-number\n");
  Settings corrupt;
  require(corrupt.load(corruptPath), "a corrupt ratio file still loads");
  require(std::abs(corrupt.aspectRatio - 16.0F / 9.0F) < 1.0e-6F,
      "a corrupt ratio keeps the 16:9 default");
}

} // namespace

void testReplaySerialisation() {
  using namespace rechigros;
  const auto directory = test::scratchDirectory("app-data-replay");

  // A judgement-entry replay round-trips with its timing statistics intact.
  app::ReplayData recorded;
  recorded.meta.chartId = "chart-1";
  recorded.meta.songName = "Song";
  recorded.meta.difficulty = "IN 15";
  recorded.meta.score = 987654;
  recorded.meta.accuracy = 96.5;
  recorded.meta.maxCombo = 42;
  recorded.meta.totalNotes = 50;
  recorded.meta.perfect = 40;
  recorded.meta.good = 6;
  recorded.meta.bad = 2;
  recorded.meta.miss = 2;
  recorded.meta.timestampMs = 1700000000123LL;
  recorded.meta.musicSpeed = 1.25F;
  recorded.meta.challenge = true;
  game::ReplayJudgement tap;
  tap.type = game::ReplayJudgement::Type::Tap;
  tap.noteIndex = 3;
  tap.judgement = 0;
  tap.timeSec = 1.25;
  recorded.judgements.push_back(tap);
  game::ReplayJudgement press;
  press.type = game::ReplayJudgement::Type::HoldPress;
  press.noteIndex = 4;
  press.judgement = 1;
  press.timeSec = 2.0;
  recorded.judgements.push_back(press);
  game::ReplayJudgement release;
  release.type = game::ReplayJudgement::Type::HoldRelease;
  release.noteIndex = 4;
  release.judgement = 1;
  release.timeSec = 3.5;
  recorded.judgements.push_back(release);

  const auto replayPath = directory / "judgements.json";
  require(app::writeReplayFile(replayPath, recorded), "a judgement replay file is written");
  const auto loaded = app::readReplayFile(replayPath);
  require(loaded.has_value(), "a written judgement replay reads back");
  if (loaded) {
    require(loaded->usesJudgementModel(), "the written replay uses the judgement model");
    require(loaded->judgements.size() == 3, "every judgement entry survives the round trip");
    require(loaded->entries.empty(), "a judgement replay carries no touch samples");
    require(loaded->meta.score == recorded.meta.score, "the replay keeps its score");
    require(loaded->meta.musicSpeed == 1.25F, "the replay keeps its recorded music speed");
    require(loaded->meta.challenge, "the replay keeps its recorded challenge flag");
    require(loaded->meta.timestampMs == recorded.meta.timestampMs,
        "the replay keeps its timestamp");
    if (loaded->judgements.size() == 3) {
      require(loaded->judgements[0].type == game::ReplayJudgement::Type::Tap
              && loaded->judgements[0].noteIndex == 3
              && loaded->judgements[0].judgement == 0,
          "the first judgement entry keeps type, note index and judgement");
      require(loaded->judgements[1].type == game::ReplayJudgement::Type::HoldPress,
          "a hold press entry keeps its type");
      require(loaded->judgements[2].type == game::ReplayJudgement::Type::HoldRelease
              && loaded->judgements[2].timeSec == 3.5,
          "a hold release entry keeps its time");
    }
  }

  // A legacy touch-sample file still reads, and keeps its legacy model. This is also the
  // shape the tolerant importer writes.
  app::ReplayData legacy;
  legacy.version = 1;
  app::ReplayTouchEvent down;
  down.kind = app::ReplayTouchEvent::Kind::Down;
  down.x = 0.25F;
  down.y = 0.75F;
  down.timeSec = 0.5;
  legacy.entries.push_back(down);
  legacy.meta.score = 1234;
  const auto legacyPath = directory / "touch-samples.json";
  require(app::writeReplayFile(legacyPath, legacy), "a legacy replay file is written");
  const auto legacyLoaded = app::readReplayFile(legacyPath);
  require(legacyLoaded.has_value(), "a legacy replay reads back");
  if (legacyLoaded) {
    require(!legacyLoaded->usesJudgementModel(), "the legacy file keeps the touch model");
    require(legacyLoaded->entries.size() == 1 && legacyLoaded->judgements.empty(),
        "the legacy file keeps its touch samples");
    require(legacyLoaded->meta.score == 1234, "the legacy file keeps its metadata");
  }

  // A file in the reference's own spelling (v/meta/entries with t/ni/j/ts) is accepted.
  writeText(directory / "reference.json",
      R"({"v":1,"meta":{"score":1000000,"accuracy":100.0,"goodEarly":2,"goodLate":1,
"stdDevMs":3.5,"judgementOffsetBins":[0,1,0]},"entries":[{"t":1,"ni":0,"j":0,"ts":0.75}]})");
  const auto reference = app::readReplayFile(directory / "reference.json");
  require(reference.has_value(), "a reference-spelled replay reads");
  if (reference) {
    require(reference->usesJudgementModel(), "reference entries are judgement entries");
    require(reference->judgements.size() == 1 && reference->judgements[0].noteIndex == 0,
        "reference entry fields are bound");
    require(reference->meta.goodEarly == 2 && reference->meta.goodLate == 1,
        "reference timing metadata is bound");
    require(reference->meta.hasJudgementOffsetBins, "reference offset bins are bound");
  }

  // Recording a result fills in the timing statistics the settlement screen needs.
  app::ReplayData resultData;
  game::PlayResult result;
  result.score = 500000;
  result.goodEarly = 3;
  result.goodLate = 4;
  result.stdDevMs = 12.5;
  result.judgementOffsetBins[5] = 7;
  app::applyResultToReplayMeta(resultData, result);
  require(resultData.meta.goodEarly == 3 && resultData.meta.goodLate == 4,
      "the result's early/late split reaches the replay metadata");
  require(std::abs(resultData.meta.stdDevMs - 12.5) < 1.0e-9,
      "the result's standard deviation reaches the replay metadata");
  require(resultData.meta.hasJudgementOffsetBins
          && resultData.meta.judgementOffsetBins[5] == 7,
      "the result's offset histogram reaches the replay metadata");
}

void testReplayPlaybackResultMerge() {
  using namespace rechigros;
  app::ReplayMeta meta;
  meta.score = 900000;
  meta.accuracy = 95.0;
  meta.maxCombo = 100;
  meta.totalNotes = 120;
  meta.perfect = 110;
  meta.good = 8;
  meta.bad = 1;
  meta.miss = 1;
  meta.goodEarly = 5;
  meta.goodLate = 3;
  meta.stdDevMs = 9.5;

  // An empty live histogram falls back to the saved one.
  meta.judgementOffsetBins = {0, 0, 6, 0};
  meta.hasJudgementOffsetBins = true;
  game::PlayResult liveEmpty;
  liveEmpty.score = 999999;
  const auto saved = app::mergeReplayPlaybackResult(meta, liveEmpty);
  require(saved.score == 900000, "recorded statistics win over the live run");
  require(saved.judgementOffsetBins[2] == 6, "the saved histogram is used when live is empty");

  // A live histogram replaces the saved one; the recorded statistics still win.
  game::PlayResult live;
  live.judgementOffsetBins[0] = 4;
  const auto merged = app::mergeReplayPlaybackResult(meta, live);
  require(merged.judgementOffsetBins[0] == 4 && merged.judgementOffsetBins[2] == 0,
      "a live histogram replaces the saved bins");
  require(merged.score == 900000 && merged.goodEarly == 5,
      "the live histogram does not change the recorded statistics");

  // No saved histogram and no live data leaves an empty histogram, not a mismatch.
  app::ReplayMeta withoutBins;
  withoutBins.score = 1;
  const auto empty = app::mergeReplayPlaybackResult(withoutBins, game::PlayResult{});
  require(empty.score == 1, "a replay without a saved histogram keeps its statistics");
}

int main() {
  testDifficultyNormalisation();
  testSettingsAspectRatio();
  testReplaySerialisation();
  testReplayPlaybackResultMerge();

  if (g_failures != 0) {
    std::cerr << g_failures << " application data test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros application data tests passed\n";
  return 0;
}
