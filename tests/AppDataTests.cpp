// Tests for the application-layer data helpers: chart metadata labels, settings
// clamping and replay serialisation. These units are deliberately free of any interface
// or GPU dependency, so they run headless like the rest of the suites.

#include "rechigros/app/ChartLibrary.hpp"
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
#include <sstream>
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

} // namespace

int main() {
  testDifficultyNormalisation();
  testSettingsAspectRatio();
  testReplaySerialisation();
  testReplayPlaybackResultMerge();
  testInfoFileFormats();
  testChartLibraryPackDetection();
  if (g_failures != 0) {
    std::cerr << g_failures << " application data test(s) failed\n";
    return 1;
  }
  std::cout << "All ReChigros application data tests passed\n";
  return 0;
}
