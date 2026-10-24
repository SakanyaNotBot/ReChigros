// Tests for the application-layer data helpers: chart metadata labels, settings
// clamping and replay serialisation. These units are deliberately free of any interface
// or GPU dependency, so they run headless like the rest of the suites.

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/app/Settings.hpp"
#include "rechigros/app/SmoothingClock.hpp"
#include "rechigros/game/PlayResult.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <miniz.h>

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

// Writes a real deflated archive so the import path is exercised end to end rather than a
// hand-made directory copy.
bool writeZipArchive(const std::filesystem::path& archive,
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

namespace {

// A minimal official-format chart with a META block; the chart detector keys on the
// judge-line list plus the format version, exactly like the reference importer.
std::string chartJson(const std::string& name, const std::string& level,
    const std::string& background, const std::string& music, bool withMeta = true) {
  std::ostringstream out;
  out << R"({"formatVersion":3,"offset":0,)";
  if (withMeta) {
    out << R"("META":{"songName":")" << name << R"(","level":")" << level
        << R"(","background":")" << background << R"(","music":")" << music << R"("},)";
  }
  out << R"("judgeLineList":[{"bpm":120.0,"notesAbove":[],"notesBelow":[],
"speedEvents":[{"startTime":0,"endTime":100000.0,"value":1.0,"endValue":1.0}],
"judgeLineMoveEvents":[],"judgeLineRotateEvents":[],"judgeLineDisappearEvents":[]}]})";
  return out.str();
}

void requireEqual(const std::string& actual, const std::string& expected, const char* what) {
  require(actual == expected, std::string(what) + " (got '" + actual + "')");
}

// The metadata reader's field set, alias table and value coercions follow the reference
// readers: the key/value formats (yml/yaml/txt/csv) use the narrower alias table, the JSON
// document uses the wider one, and typed fields accept the same scalar forms.
void testInfoFileFormats() {
  using namespace rechigros::app;
  const auto directory = rechigros::test::scratchDirectory("app-data-info");
  const std::string en = kEnSpace;

  // Key/value metadata: every alias of the reference key/value reader.
  writeText(directory / "info.yml",
      "# pack metadata\n"
      "name: YAML Song\n"
      "song: hidden-music.ogg\n"
      "chart: charts/main.json\n"
      "picture: art.png\n"
      "level: IN 15\n"
      "artist: Composer\n"
      "designer: Charter\n"
      "illustrator: Illustrator\n"
      "chart_offset_ms: -120\n"
      "use_attach_ui_fix: 1\n"
      "music_volume: 10\n");
  const auto yaml = readInfoFile(directory / "info.yml");
  requireEqual(yaml.name, "YAML Song", "yml name alias");
  requireEqual(yaml.music, "hidden-music.ogg", "yml song alias for music");
  requireEqual(yaml.chart, "charts/main.json", "yml chart");
  requireEqual(yaml.illustration, "art.png", "yml picture alias for illustration");
  requireEqual(yaml.level, std::string("IN") + en + "15", "yml level is normalised");
  requireEqual(yaml.composer, "Composer", "yml artist alias for composer");
  requireEqual(yaml.charter, "Charter", "yml designer alias for charter");
  requireEqual(yaml.illustrator, "Illustrator", "yml illustrator");
  require(yaml.hasChartOffset && yaml.chartOffsetMs == -120, "yml offset alias is read");
  require(yaml.hasUseAttachUiFix && yaml.useAttachUiFix, "yml attach-ui flag is read");
  require(yaml.difficulty.empty(), "an unknown key does not fill difficulty");

  // The key/value reader must not accept the JSON-only aliases: a pack that relies on one
  // has to fall back exactly like the reference does.
  writeText(directory / "narrow.yml",
      "title: Title\nsongName: SongName\naudio: a.ogg\nbg: b.png\nchartPath: c.json\n");
  const auto narrow = readInfoFile(directory / "narrow.yml");
  require(narrow.name.empty(), "yml title is not a name alias");
  require(narrow.music.empty(), "yml audio is not a music alias");
  require(narrow.illustration.empty(), "yml bg is not an illustration alias");
  require(narrow.chart.empty(), "yml chartPath is not a chart alias");

  // YAML comments are skipped on any line; the unknown extension parses as YAML.
  writeText(directory / "info.yaml", "# one\n# two\nname: Commented\n");
  requireEqual(readInfoFile(directory / "info.yaml").name, "Commented",
      "yaml comments are skipped");
  writeText(directory / "info.data", "name: Fallback\n");
  requireEqual(readInfoFile(directory / "info.data").name, "Fallback",
      "an unknown extension parses like yaml");

  // Key/value text only treats the first physical line as a possible comment.
  writeText(directory / "info.txt", "name: Text Song\n# composer: Hidden\nmusic: text.ogg\n");
  const auto text = readInfoFile(directory / "info.txt");
  requireEqual(text.name, "Text Song", "txt name");
  requireEqual(text.music, "text.ogg", "txt music");
  require(text.composer.empty(), "txt comment lines are not metadata");
  writeText(directory / "csv-info.csv",
      "name,music,chart,illustration,level\nCSV Song,csv.ogg,csv.json,csv.png,15\n");
  const auto csv = readInfoFile(directory / "csv-info.csv");
  requireEqual(csv.name, "CSV Song", "csv name column");
  requireEqual(csv.music, "csv.ogg", "csv music column");
  requireEqual(csv.chart, "csv.json", "csv chart column");
  requireEqual(csv.illustration, "csv.png", "csv illustration column");
  requireEqual(csv.level, "15", "csv level column");

  // JSON metadata: wider alias table (JSON-only names are accepted here).
  writeText(directory / "info.json",
      R"({"songName":"JSON Song","Chart":"json-chart.json","audio":"json.ogg",
"Image":"json.png","Level":"AT 16","Artist":"JComposer","Designer":"JCharter",
"Illustrator":"JIllustrator","chart_offset_ms":12.9,"use_attach_ui_fix":1})");
  const auto json = readInfoFile(directory / "info.json");
  requireEqual(json.name, "JSON Song", "json songName alias");
  requireEqual(json.chart, "json-chart.json", "json Chart alias");
  requireEqual(json.music, "json.ogg", "json audio alias");
  requireEqual(json.illustration, "json.png", "json Image alias");
  requireEqual(json.level, std::string("AT") + en + "16", "json level is normalised");
  requireEqual(json.composer, "JComposer", "json Artist alias");
  requireEqual(json.charter, "JCharter", "json Designer alias");
  requireEqual(json.illustrator, "JIllustrator", "json Illustrator alias");
  require(json.hasChartOffset && json.chartOffsetMs == 12,
      "a fractional json offset truncates like the reference integer reader");
  require(json.hasUseAttachUiFix && json.useAttachUiFix, "a numeric attach-ui flag is true");

  // Value nodes become text for string fields, exactly like the reference string reader.
  writeText(directory / "numeric.json", R"({"name":12345,"composer":true,"illustrator":1.5})");
  const auto numeric = readInfoFile(directory / "numeric.json");
  requireEqual(numeric.name, "12345", "an integer name becomes its decimal text");
  requireEqual(numeric.composer, "true", "a boolean composer becomes true/false text");
  requireEqual(numeric.illustrator, "1.5", "a floating illustrator keeps its plain spelling");

  // Typed fields accept textual forms and fall through to the next alias when unparseable.
  writeText(directory / "typed.json",
      R"({"useAttachUiFix":"false","chartOffsetMs":"bad","offsetMs":-7})");
  const auto typed = readInfoFile(directory / "typed.json");
  require(typed.hasChartOffset && typed.chartOffsetMs == -7,
      "an unparseable offset falls through to the next alias");
  require(typed.hasUseAttachUiFix && !typed.useAttachUiFix, "a textual boolean flag is read");

  // The reference JSON reader enables unquoted field names; the port accepts them too.
  writeText(directory / "loose.json", "{name: \"Loose\", music: \"loose.ogg\", level: \"IN 12\"}");
  const auto loose = readInfoFile(directory / "loose.json");
  requireEqual(loose.name, "Loose", "unquoted field names are accepted");
  requireEqual(loose.music, "loose.ogg", "unquoted field names keep the alias table");
  requireEqual(loose.level, std::string("IN") + en + "12", "unquoted level is normalised");

  // Chart-embedded metadata fills the same fields, with the same value coercions.
  writeText(directory / "meta-chart.json",
      chartJson("Meta Song", "15", "bg.png", "meta.ogg"));
  const auto meta = readInfoFromChartFile(directory / "meta-chart.json");
  requireEqual(meta.name, "Meta Song", "chart META songName");
  requireEqual(meta.level, "15", "a numeric chart META level becomes text");
  requireEqual(meta.illustration, "bg.png", "chart META background");
  requireEqual(meta.music, "meta.ogg", "chart META music");

  // The canonical writer round-trips every field it stores.
  ChartInfo written;
  written.name = "Written";
  written.chart = "c.json";
  written.music = "m.ogg";
  written.illustration = "i.png";
  written.level = "IN 14";
  written.difficulty = "14";
  written.composer = "C";
  written.charter = "Ch";
  written.illustrator = "I";
  written.chartOffsetMs = 33;
  written.hasChartOffset = true;
  require(writeInfoJson(directory / "written.json", written), "canonical metadata is written");
  const auto reread = readInfoFile(directory / "written.json");
  requireEqual(reread.name, "Written", "canonical name round trip");
  requireEqual(reread.chart, "c.json", "canonical chart round trip");
  requireEqual(reread.music, "m.ogg", "canonical music round trip");
  requireEqual(reread.illustration, "i.png", "canonical illustration round trip");
  requireEqual(reread.composer, "C", "canonical composer round trip");
  require(reread.hasChartOffset && reread.chartOffsetMs == 33, "canonical offset round trip");
}

void testChartLibraryPackDetection() {
  using namespace rechigros::app;
  const auto work = rechigros::test::scratchDirectory("app-data-packs");
  const auto packs = work / "packs";
  std::error_code error;
  std::filesystem::create_directories(packs, error);
  const auto makeFile = [&error](const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path(), error);
    writeText(path, content);
  };
  // Nested layout with metadata in the pack root and assets in subdirectories.
  const auto alpha = packs / "alpha";
  makeFile(alpha / "info.yml",
      "name: Alpha\nchart: data/chart.json\nmusic: audio/song.ogg\n"
      "illustration: art/cover.png\nlevel: IN 13\nchart_offset_ms: 42\n");
  makeFile(alpha / "data" / "chart.json",
      chartJson("Alpha", "13", "art/cover.png", "audio/song.ogg"));
  makeFile(alpha / "audio" / "song.ogg", "OggS");
  makeFile(alpha / "art" / "cover.png", "PNG");
  // References that do not match the extracted file names resolve through the fallback
  // scans (the shape a percent-encoded archive produces after extraction).
  const auto beta = packs / "beta";
  makeFile(beta / "info.yml",
      "name: Beta\nchart: song v2.json\nmusic: music v2.ogg\nillustration: cover art.png\n");
  makeFile(beta / "song%20v2.json", chartJson("Beta", "14", "cover%20art.png", "music%20v2.ogg"));
  makeFile(beta / "music%20v2.ogg", "OggS");
  makeFile(beta / "cover%20art.png", "PNG");
  // No metadata file: the chart's own META block supplies the labels.
  const auto gamma = packs / "gamma";
  makeFile(gamma / "beat.json", chartJson("Gamma", "15", "image.png", "theme.ogg"));
  makeFile(gamma / "theme.ogg", "OggS");
  makeFile(gamma / "image.png", "PNG");
  // Missing illustration and music: the reference only indexes complete packs.
  const auto delta = packs / "delta";
  makeFile(delta / "info.yml", "name: Delta\nchart: delta.json\n");
  makeFile(delta / "delta.json", chartJson("Delta", "12", "", ""));
  ChartLibrary library(packs, work / "scores.txt");
  const auto alphaRecord = library.loadRecord("alpha");
  require(alphaRecord.has_value(), "a nested pack loads");
  if (alphaRecord) {
    requireEqual(alphaRecord->songName, "Alpha", "nested pack name");
    require(alphaRecord->chartOffsetMs == 42, "nested pack offset");
    require(alphaRecord->complete(), "a nested pack with every asset is complete");
    requireEqual(alphaRecord->chartFile.filename().string(), "chart.json",
        "nested pack chart is resolved from the metadata path");
    requireEqual(alphaRecord->musicFile.filename().string(), "song.ogg",
        "nested pack music is resolved from the metadata path");
    requireEqual(alphaRecord->bgFile.filename().string(), "cover.png",
        "nested pack illustration is resolved from the metadata path");
    // The canonical metadata file is written into the pack root with relative paths.
    require(std::filesystem::exists(alpha / "info.json"), "canonical metadata is written");
    std::ifstream canonical(alpha / "info.json", std::ios::binary);
    std::stringstream buffer;
    buffer << canonical.rdbuf();
    const std::string content = buffer.str();
    require(content.find("data/chart.json") != std::string::npos,
        "canonical metadata stores the chart relative to the pack");
  }
  const auto betaRecord = library.loadRecord("beta");
  require(betaRecord.has_value(), "a pack with mismatched references still loads");
  if (betaRecord) {
    requireEqual(betaRecord->chartFile.filename().string(), "song%20v2.json",
        "a mismatched chart reference falls back to content detection");
    requireEqual(betaRecord->musicFile.filename().string(), "music%20v2.ogg",
        "a mismatched music reference falls back to the audio scan");
    requireEqual(betaRecord->bgFile.filename().string(), "cover%20art.png",
        "a mismatched illustration reference falls back to the image scan");
    require(betaRecord->complete(), "the fallback-resolved pack is complete");
  }

  const auto gammaRecord = library.loadRecord("gamma");
  require(gammaRecord.has_value(), "a pack without metadata loads");
  if (gammaRecord) {
    requireEqual(gammaRecord->songName, "Gamma", "the chart META supplies the song name");
    require(std::filesystem::exists(gamma / "info.json"),
        "metadata is materialised for a pack without an info file");
  }

  const auto deltaRecord = library.loadRecord("delta");
  require(deltaRecord.has_value(), "an incomplete pack still yields a record");
  if (deltaRecord) {
    require(!deltaRecord->complete(), "a pack without music/illustration is incomplete");
  }
  const auto& records = library.records(true);
  bool sawDelta = false;
  bool sawAlpha = false;
  for (const auto& record : records) {
    if (record.id == "delta") sawDelta = true;
    if (record.id == "alpha") sawAlpha = true;
  }
  require(!sawDelta, "incomplete packs stay out of the indexed list");
  require(sawAlpha, "complete packs appear in the indexed list");
}

// An imported package has to be installed in the persistent library, not kept in a cache
// directory that the next launch cannot see: a fresh ChartLibrary built on the same data
// root must list the imported chart with its metadata. This is the regression that made an
// imported chart start as a temporary play and never show up in the local list.
void testImportPersistsAcrossReload() {
  using namespace rechigros::app;
  const auto root = rechigros::test::scratchDirectory("app-data-import-persist");
  // The scratch directory survives between runs; start from a clean library.
  std::filesystem::remove_all(root);
  const auto packs = root / "packs";
  std::filesystem::create_directories(packs);
  const auto archive = root / "Imported Pack.zip";
  const std::string packInfo =
      "name: Persisted Song\n"
      "composer: Persisted Artist\n"
      "charter: Persisted Charter\n"
      "level: IN Lv.13\n"
      "chart: chart.json\n"
      "music: music.ogg\n"
      "illustration: cover.png\n";
  require(writeZipArchive(archive,
              {{"info.yml", packInfo},
                  {"chart.json", chartJson("Persisted Song", "IN Lv.13", "cover.png",
                       "music.ogg")},
                  {"music.ogg", "OggS"},
                  {"cover.png", "PNG"}}),
      "an importable chart archive is written");

  ChartLibrary library(packs, root / "library.txt");
  const auto imported = library.importZip(archive, false);
  require(imported.imported, "a chart package imports into the library");
  const std::string importedId = imported.record.id;
  require(!importedId.empty(), "the imported chart gets a library identifier");

  // (a) The pack lives in the persistent library, not in the cache.
  const auto installedRoot = packs / importedId;
  require(std::filesystem::is_directory(installedRoot),
      "the imported pack is installed under the persistent packs directory");
  require(std::filesystem::exists(installedRoot / "chart.json"),
      "the imported pack carries its chart file");
  require(std::filesystem::exists(installedRoot / "info.json"),
      "the imported pack carries its canonical metadata");
  bool sawTemporaryDirectory = false;
  for (const auto& entry : std::filesystem::directory_iterator(packs)) {
    if (entry.path().extension() == ".tmp") sawTemporaryDirectory = true;
  }
  require(!sawTemporaryDirectory, "the import leaves no temporary pack directory behind");

  // (b) A fresh library over the same data root behaves like an application restart.
  ChartLibrary reopened(packs, root / "library.txt");
  const auto& records = reopened.records(true);
  const ChartRecord* found = nullptr;
  for (const auto& record : records) {
    if (record.id == importedId) found = &record;
  }
  require(found != nullptr, "an imported chart is listed after a restart");
  if (found != nullptr) {
    requireEqual(found->songName, "Persisted Song", "the persisted chart keeps its name");
    requireEqual(found->info.composer, "Persisted Artist",
        "the persisted chart keeps its composer");
    requireEqual(found->info.charter, "Persisted Charter",
        "the persisted chart keeps its charter");
    require(contains(found->difficulty, "IN"), "the persisted chart keeps its level prefix");
    require(contains(found->difficulty, "13"), "the persisted chart keeps its level number");
    require(found->complete(), "the persisted chart is complete and playable");
  }

  // Importing the same archive again is the reference's 本地已存在该谱面 case: the result
  // reports "not imported" and the list keeps a single entry for it.
  const auto duplicate = reopened.importZip(archive, false);
  require(!duplicate.imported && duplicate.record.id == importedId,
      "a duplicate import is detected instead of installing a second copy");
  std::size_t matches = 0;
  for (const auto& record : reopened.records(true)) {
    if (record.id == importedId) ++matches;
  }
  require(matches == 1, "the local list holds the imported chart exactly once");
}

void requireNear(float actual, float expected, const char* what) {
  require(std::abs(actual - expected) < 1.0e-4F,
      std::string(what) + " (got " + std::to_string(actual) + ", want " + std::to_string(expected)
          + ")");
}

// Settings behaviour: the reference's field initialisers, the save/load round trip and the
// load-time clamps, plus the reveal -> autoplay coupling.
void testSettingsDefaultsPersistenceAndClamps() {
  using namespace rechigros::app;
  const auto directory = rechigros::test::scratchDirectory("app-data-settings-behaviour");

  // Defaults match the reference field initialisers (ChartSelectActivity.java:210-238).
  Settings defaults;
  requireNear(defaults.keyScale, 1.0F, "default key scale");
  requireNear(defaults.backgroundDim, 0.25F, "default background dim");
  requireNear(defaults.aspectRatio, 16.0F / 9.0F, "default stage aspect");
  requireNear(defaults.scrollSpeed, 1.0F, "default scroll speed");
  requireNear(defaults.judgeLineWidth, 1.0F, "default judge line width");
  requireNear(defaults.musicVolume, 1.0F, "default music volume");
  requireNear(defaults.soundEffectVolume, 1.0F, "default sound effect volume");
  requireNear(defaults.musicSpeed, 1.0F, "default music speed");
  require(defaults.audioOffsetMs == 0, "default audio offset");
  require(defaults.hitOffsetIndicatorMode == 0, "default hit offset indicator is disabled");
  require(!defaults.apfcIndicator, "default apfc indicator");
  require(!defaults.judgementDetailDistribution, "default judgement detail distribution");
  require(!defaults.realtimeAccuracy, "default realtime accuracy");
  require(!defaults.scoreJumpEffect, "default score jump effect");
  require(defaults.multiPressHighlight, "default multi press highlight");
  require(!defaults.mirrorX && !defaults.autoplay && !defaults.challengeMode,
      "default play modes");
  require(!defaults.lowResolutionMode && !defaults.antialias && !defaults.showFps,
      "default render toggles");
  require(!defaults.showDebugInfo && !defaults.disableChartShader && !defaults.chartReveal,
      "default debug and chart toggles");
  require(defaults.storyMode, "default story mode");
  require(!defaults.replayEnabled, "default replay recording is off");
  require(defaults.selectedSkin.empty(), "default skin is the built-in one");

  // Every persisted field survives a save/load round trip.
  Settings custom;
  custom.keyScale = 1.23F;
  custom.backgroundDim = 0.4F;
  custom.aspectRatio = 1.5F;
  custom.scrollSpeed = 2.2F;
  custom.judgeLineWidth = 1.7F;
  custom.musicVolume = 3.5F;
  custom.soundEffectVolume = 0.1F;
  custom.musicSpeed = 1.75F;
  custom.audioOffsetMs = -120;
  custom.hitOffsetIndicatorMode = 2;
  custom.apfcIndicator = true;
  custom.judgementDetailDistribution = true;
  custom.realtimeAccuracy = true;
  custom.scoreJumpEffect = true;
  custom.multiPressHighlight = false;
  custom.mirrorX = true;
  custom.autoplay = true;
  custom.challengeMode = true;
  custom.practiceMode = true;
  custom.lowResolutionMode = true;
  custom.antialias = true;
  custom.showFps = true;
  custom.showDebugInfo = true;
  custom.disableChartShader = true;
  custom.chartReveal = true;
  custom.storyMode = false;
  custom.replayEnabled = true;
  custom.selectedSkin = "Skin A";
  require(custom.save(directory / "settings.ini"), "settings are saved");
  Settings loaded;
  require(loaded.load(directory / "settings.ini"), "saved settings load again");
  requireNear(loaded.keyScale, 1.23F, "key scale round trip");
  requireNear(loaded.backgroundDim, 0.4F, "background dim round trip");
  requireNear(loaded.aspectRatio, 1.5F, "stage aspect round trip");
  requireNear(loaded.scrollSpeed, 2.2F, "scroll speed round trip");
  requireNear(loaded.judgeLineWidth, 1.7F, "judge line width round trip");
  requireNear(loaded.musicVolume, 3.5F, "music volume round trip");
  requireNear(loaded.soundEffectVolume, 0.1F, "sound effect volume round trip");
  requireNear(loaded.musicSpeed, 1.75F, "music speed round trip");
  require(loaded.audioOffsetMs == -120, "audio offset round trip");
  require(loaded.hitOffsetIndicatorMode == 2, "hit offset indicator mode round trip");
  require(loaded.apfcIndicator && loaded.judgementDetailDistribution && loaded.realtimeAccuracy
          && loaded.scoreJumpEffect && !loaded.multiPressHighlight,
      "judgement display toggles round trip");
  require(loaded.mirrorX && loaded.autoplay && loaded.challengeMode && loaded.practiceMode,
      "play mode toggles round trip");
  require(loaded.lowResolutionMode && loaded.antialias && loaded.showFps && loaded.showDebugInfo
          && loaded.disableChartShader && loaded.chartReveal,
      "render and debug toggles round trip");
  require(!loaded.storyMode && loaded.replayEnabled, "story and replay toggles round trip");
  require(loaded.selectedSkin == "Skin A", "selected skin round trip");

  // Out-of-range stored values are clamped to the reference ranges on load.
  const auto write = [&directory](const char* name, const std::string& text) {
    writeText(directory / name, text);
    return directory / name;
  };
  Settings high;
  require(high.load(write("clamp-high.ini",
              "keyScale=5\nscrollSpeed=9\njudgeLineWidth=3\nbackgroundDim=2\nmusicVolume=9\n"
              "soundEffectVolume=9\nmusicSpeed=4\naudioOffsetMs=5000\n"
              "hitOffsetIndicatorMode=7\naspectRatio=12\n")),
      "high clamp file loads");
  requireNear(high.keyScale, 1.5F, "key scale clamps to the top");
  requireNear(high.scrollSpeed, 5.0F, "scroll speed clamps to the top");
  requireNear(high.judgeLineWidth, 2.0F, "judge line width clamps to the top");
  requireNear(high.backgroundDim, 1.0F, "background dim clamps to the top");
  requireNear(high.musicVolume, 5.0F, "music volume clamps to 500 percent");
  requireNear(high.soundEffectVolume, 5.0F, "sound effect volume clamps to 500 percent");
  requireNear(high.musicSpeed, 2.0F, "music speed clamps to the top");
  require(high.audioOffsetMs == 500, "audio offset clamps to the top");
  // The reference's sanitizer maps an out-of-range mode to "disabled" rather than to the
  // nearest valid value, so a stored 7 turns the indicator off instead of into a line mark.
  require(high.hitOffsetIndicatorMode == 0, "out-of-range indicator mode falls back to disabled");
  requireNear(high.aspectRatio, 5.0F, "stage aspect clamps to the top");

  Settings low;
  require(low.load(write("clamp-low.ini",
              "keyScale=0.1\nscrollSpeed=0\njudgeLineWidth=0\nbackgroundDim=-1\n"
              "musicVolume=-2\nsoundEffectVolume=-1\nmusicSpeed=0.1\naudioOffsetMs=-5000\n"
              "hitOffsetIndicatorMode=-3\naspectRatio=-3\n")),
      "low clamp file loads");
  requireNear(low.keyScale, 0.7F, "key scale clamps to the bottom");
  requireNear(low.scrollSpeed, 0.1F, "scroll speed clamps to the bottom");
  requireNear(low.judgeLineWidth, 0.5F, "judge line width clamps to the bottom");
  requireNear(low.backgroundDim, 0.0F, "background dim clamps to the bottom");
  requireNear(low.musicVolume, 0.0F, "music volume clamps to zero");
  requireNear(low.soundEffectVolume, 0.0F, "sound effect volume clamps to zero");
  requireNear(low.musicSpeed, 0.5F, "music speed clamps to the bottom");
  require(low.audioOffsetMs == -500, "audio offset clamps to the bottom");
  require(low.hitOffsetIndicatorMode == 0, "negative indicator mode falls back to disabled");
  requireNear(low.aspectRatio, 0.5F, "stage aspect clamps to the bottom");

  // Zero means "follow the window"; a non-finite stored ratio is repaired to it as well.
  Settings follow;
  require(follow.load(write("aspect-zero.ini", "aspectRatio=0\n")), "zero aspect file loads");
  require(follow.aspectRatio == 0.0F, "zero aspect means follow the window");
  Settings brokenAspect;
  require(brokenAspect.load(write("aspect-nan.ini", "aspectRatio=nan\n")),
      "non-finite aspect file loads");
  require(brokenAspect.aspectRatio == 0.0F, "a non-finite aspect is repaired to follow-window");

  // Revealing the chart detail forces the automatic demonstration on, at load time like the
  // reference (ChartSelectActivity.java:2514-2518), and both values then persist together.
  Settings reveal;
  require(reveal.load(write("reveal.ini", "chartReveal=1\nautoplay=0\n")), "reveal file loads");
  require(reveal.chartReveal, "reveal is read");
  require(reveal.autoplay, "reveal forces autoplay on load");
  require(reveal.save(directory / "reveal-out.ini"), "reveal pair is saved");
  Settings revealOut;
  require(revealOut.load(directory / "reveal-out.ini"), "reveal pair loads again");
  require(revealOut.chartReveal && revealOut.autoplay, "reveal and autoplay persist together");

  // A missing file reports failure and keeps the constructor defaults.
  Settings missing;
  require(!missing.load(directory / "absent.ini"), "a missing settings file reports failure");
  requireNear(missing.keyScale, 1.0F, "a missing settings file keeps the defaults");
}

// Sidecar discovery for charts played outside the library: only files directly in the
// chart's own directory may be used, so a neighbouring pack's metadata or artwork cannot be
// mistaken for this chart's (the regression that made tests/fixtures/plain.json report the
// skin fixture's info.yml as its metadata).
void testSidecarDiscovery() {
  using namespace rechigros::app;
  const auto root = rechigros::test::scratchDirectory("app-data-sidecar");
  // The scratch directory survives between runs, so start from a clean tree: this test
  // asserts the absence of sidecars before writing them.
  std::filesystem::remove_all(root);
  const auto chartDirectory = root / "chart";
  std::filesystem::create_directories(chartDirectory);
  writeText(chartDirectory / "plain.json", "{\"formatVersion\":3}");

  // A neighbouring directory that looks like a pack must not supply this chart's sidecars.
  std::filesystem::create_directories(root / "neighbour" / "art");
  writeText(root / "neighbour" / "info.yml",
      "name: Neighbour\nmusic: song.ogg\nillustration: art/cover.png\n");
  writeText(root / "neighbour" / "song.ogg", "OggS");
  writeText(root / "neighbour" / "art" / "cover.png", "PNG");
  require(findSidecarInfoFile(chartDirectory).empty(),
      "a sibling pack's metadata is not this chart's sidecar");
  require(findSidecarMusicFile(chartDirectory).empty(),
      "a sibling pack's audio is not this chart's music");
  require(findSidecarImageFile(chartDirectory).empty(),
      "a sibling pack's artwork is not this chart's illustration");

  // Files nested below the chart directory are not sidecars either.
  std::filesystem::create_directories(chartDirectory / "nested");
  writeText(chartDirectory / "nested" / "info.json", "{\"name\":\"Nested\"}");
  writeText(chartDirectory / "nested" / "song.ogg", "OggS");
  writeText(chartDirectory / "nested" / "cover.png", "PNG");
  require(findSidecarInfoFile(chartDirectory).empty(), "a nested info file is not a sidecar");
  require(findSidecarMusicFile(chartDirectory).empty(), "a nested audio file is not a sidecar");
  require(findSidecarImageFile(chartDirectory).empty(), "a nested image is not a sidecar");

  // Direct files in the chart's own directory are found.
  writeText(chartDirectory / "info.yml", "name: Local\n");
  writeText(chartDirectory / "song.ogg", "OggS");
  writeText(chartDirectory / "cover.png", "PNG");
  require(findSidecarInfoFile(chartDirectory) == chartDirectory / "info.yml",
      "the direct info file is the sidecar");
  require(findSidecarMusicFile(chartDirectory) == chartDirectory / "song.ogg",
      "the direct audio file is the sidecar");
  require(findSidecarImageFile(chartDirectory) == chartDirectory / "cover.png",
      "the direct illustration is the sidecar");

  // Candidates are sorted by path, so the canonical json spelling wins deterministically.
  writeText(chartDirectory / "info.json", "{}");
  require(findSidecarInfoFile(chartDirectory) == chartDirectory / "info.json",
      "info.json sorts before info.yml");
  require(findSidecarInfoFile(root / "missing").empty(), "a missing directory yields no sidecar");
}

// Saved replay rows on the settings page are backed by this store: adding, listing newest
// first and deleting must all work without the interface.
void testReplayStoreRows() {
  using namespace rechigros::app;
  const auto root = rechigros::test::scratchDirectory("app-data-replay-store");
  // The scratch directory survives between runs; start with an empty store so the row
  // counts below are deterministic.
  std::filesystem::remove_all(root / "replays");
  ReplayStore store(root / "replays");

  const auto makeReplay = [](std::int64_t timestamp, const std::string& song, int score) {
    ReplayData data;
    data.meta.chartId = "chart";
    data.meta.songName = song;
    data.meta.difficulty = "IN 15";
    data.meta.score = score;
    data.meta.totalNotes = 1;
    data.meta.timestampMs = timestamp;
    rechigros::game::ReplayJudgement judgement;
    judgement.type = rechigros::game::ReplayJudgement::Type::Tap;
    judgement.noteIndex = 0;
    judgement.judgement = 0;
    judgement.timeSec = 1.0;
    data.judgements.push_back(judgement);
    return data;
  };

  const std::string first = store.save(makeReplay(1700000000000LL, "First Song", 111111));
  require(!first.empty(), "a saved replay gets an identifier");
  const std::string second = store.save(makeReplay(1700000100000LL, "Second Song", 222222));
  require(!second.empty() && second != first, "a second replay gets its own identifier");

  // The same song, the same score and a timestamp inside the duplicate window describe the
  // same play, so a second save is refused rather than adding a copy. Outside the window, or
  // with a different score, it is a different play and must be kept.
  const std::string duplicate =
      store.save(makeReplay(1700000100000LL + 3000, "Second Song", 222222));
  require(duplicate.empty(), "an identical replay is not saved twice");
  const std::string laterLap =
      store.save(makeReplay(1700000100000LL + 60000, "Second Song", 222222));
  require(!laterLap.empty(), "the same score much later is a separate play");
  const std::string betterScore =
      store.save(makeReplay(1700000100000LL + 1000, "Second Song", 999999));
  require(!betterScore.empty(), "a different score is a separate play");
  require(store.remove(laterLap), "the later lap is removed for the count below");
  require(store.remove(betterScore), "the better score is removed for the count below");

  const auto listed = store.list();
  require(listed.size() == 2, "the replay store lists both rows");
  if (listed.size() == 2) {
    require(listed[0].meta.songName == "Second Song", "replay rows list newest first");
    require(listed[0].meta.score == 222222, "a replay row carries its score");
    require(listed[0].meta.difficulty == "IN 15", "a replay row carries its difficulty label");
  }
  const auto found = store.find(first);
  require(found.has_value() && found->meta.songName == "First Song",
      "a replay row is opened by its identifier");

  require(store.remove(first), "the selected replay row is deleted");
  require(!store.find(first).has_value(), "a deleted replay no longer resolves");
  const auto remaining = store.list();
  require(remaining.size() == 1 && remaining[0].id == second,
      "deleting one row keeps the other rows");
}

// The drawing clock follows the audio stream without copying its reporting steps. These
// tests pin the properties that make that safe: the output never strays more than the
// documented tolerance from the reported position, a steady stream is tracked smoothly, and a
// seek snaps instead of being averaged across.
void testSmoothingClock() {
  using rechigros::app::SmoothingClock;

  // The first reading is taken as-is; there is no history to correct against.
  {
    SmoothingClock clock;
    const double value = clock.advance(1.0, 0.0, 1.0);
    require(value == 1.0, "the first reading passes through");
    require(clock.valid(), "the clock is valid after the first reading");
  }

  // A steady stream: the drawing clock must be monotonic and free of the reporting steps that
  // the raw value would show. The invariant the reference guarantees is a bound relative to
  // the *reported* position, not to wall time: with a stream that only reports on whole
  // seconds the reported value itself is up to a second behind, and the clock is anchored to
  // it, so a comparison against wall time would measure the stream's quantisation, not the
  // smoothing.
  {
    SmoothingClock clock;
    clock.reset(0.0, 0.0);
    double worstFromReported = 0.0;
    double largestStep = 0.0;
    double previous = 0.0;
    for (int frame = 1; frame <= 240; ++frame) {
      const double nowSec = static_cast<double>(frame) / 60.0;
      // The stream reports its position only on whole seconds.
      const double reported = std::floor(nowSec);
      const double drawn = clock.advance(reported, nowSec, 1.0);
      require(drawn + 1.0e-9 >= previous, "the drawing clock never runs backwards");
      largestStep = std::max(largestStep, std::abs(drawn - previous));
      previous = drawn;
      worstFromReported = std::max(worstFromReported, std::abs(drawn - reported));
    }
    require(worstFromReported <= SmoothingClock::kMaxDeviationSec + 1.0e-9,
        "the drawing clock stays within the documented band of the reported position");
  }

  // With a stream that advances in short steps, which is what a real audio callback
  // produces, the emitted clock must follow the frame cadence rather than the report cadence:
  // the average advance per frame is one frame time, and most frames move less than the
  // report step would. The band around the reported position still bounds individual frames,
  // which is deliberate — it is what keeps the drawing clock from drifting away from the
  // judged one.
  {
    SmoothingClock clock;
    clock.reset(0.0, 0.0);
    double totalAdvance = 0.0;
    double previous = 0.0;
    int frames = 0;
    for (int frame = 1; frame <= 120; ++frame) {
      const double nowSec = static_cast<double>(frame) / 60.0;
      // A report every four frames, quantised to 15 Hz.
      const double reported = std::floor(nowSec * 15.0) / 15.0;
      const double drawn = clock.advance(reported, nowSec, 1.0);
      totalAdvance += drawn - previous;
      previous = drawn;
      ++frames;
    }
    const double meanAdvance = totalAdvance / frames;
    require(std::abs(meanAdvance - 1.0 / 60.0) < 0.002,
        "the drawing clock advances at the frame rate on average");
    require(std::abs(previous - 2.0) < 0.020,
        "the drawing clock keeps up with the stream over two seconds");
  }

  // A seek backwards past the discontinuity threshold snaps to the new position and drops the
  // history, so the correction does not drag the clock back towards the old one.
  {
    SmoothingClock clock;
    clock.reset(10.0, 0.0);
    for (int frame = 1; frame <= 30; ++frame) {
      (void)clock.advance(10.0, static_cast<double>(frame) / 60.0, 1.0);
    }
    const double afterSeek = clock.advance(2.0, 0.5, 1.0);
    require(std::abs(afterSeek - 2.0) < 1.0e-9, "a backwards seek snaps to the reported position");
  }

  // A playback speed other than one is accounted for in the prediction.
  {
    SmoothingClock clock;
    clock.reset(0.0, 0.0);
    double drawn = 0.0;
    for (int frame = 1; frame <= 60; ++frame) {
      const double nowSec = static_cast<double>(frame) / 60.0;
      // At half speed the stream reports 0.5 s after 1 s of wall time.
      drawn = clock.advance(nowSec * 0.5, nowSec, 0.5);
    }
    require(std::abs(drawn - 0.5) < 0.0100001, "the prediction follows the playback speed");
  }

  // A non-finite reading must not poison the clock.
  {
    SmoothingClock clock;
    clock.reset(3.0, 0.0);
    const double drawn = clock.advance(std::nan(""), 0.016, 1.0);
    require(std::isfinite(drawn), "a non-finite reading is handled");
  }

  // A small forward seek must take effect immediately. The clock's discontinuity detection
  // only fires on a large or backwards jump, so a practice-mode nudge of a fraction of a
  // second would otherwise be treated as normal advance and the drawn position would keep
  // running ahead of where the run actually is. The app resets the clock on every seek; this
  // pins the reset itself.
  {
    SmoothingClock clock;
    clock.reset(5.0, 0.0);
    for (int frame = 1; frame <= 30; ++frame) {
      (void)clock.advance(5.0, static_cast<double>(frame) / 60.0, 1.0);
    }
    clock.reset(5.2, 0.5);
    const double afterSeek = clock.advance(5.2, 0.516, 1.0);
    require(std::abs(afterSeek - 5.2) < 0.0100001,
        "a small forward seek is adopted immediately");
    // The next frames continue from the seek point rather than from the pre-seek position.
    const double next = clock.advance(5.2333, 0.533, 1.0);
    require(next > 5.2 && next < 5.25, "the clock keeps advancing from the seek point");
  }
}

} // namespace

int main() {
  testDifficultyNormalisation();  testSettingsAspectRatio();
  testReplaySerialisation();
  testReplayPlaybackResultMerge();
  testInfoFileFormats();
  testChartLibraryPackDetection();
  testImportPersistsAcrossReload();
  testSettingsDefaultsPersistenceAndClamps();
  testSidecarDiscovery();
  testReplayStoreRows();
  testSmoothingClock();
  if (g_failures != 0) {
    std::cerr << g_failures << " application data test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros application data tests passed\n";
  return 0;
}
