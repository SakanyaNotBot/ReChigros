#include "rechigros/app/Replay.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <sstream>

namespace rechigros::app {
namespace {

using Json = nlohmann::json;

// Version 1 files store touch samples; version 2 files store judgement entries keyed by
// note index. The reader accepts both, plus the reference's own field spellings.
constexpr int kTouchSampleVersion = 1;
constexpr int kJudgementVersion = 2;

// Two recordings describe the same play when they share the song, the score and a timestamp
// this close together. The window absorbs the gap between finishing a run and pressing save.
constexpr long long kDuplicateWindowMs = 5000;

std::string entryKindName(int kind) {
  switch (kind) {
    case 0: return "down";
    case 1: return "move";
    case 2: return "up";
    default: return "down";
  }
}

int parseEntryKind(const std::string& name) {
  if (name == "move") return 1;
  if (name == "up") return 2;
  return 0;
}

int judgementTypeIndex(game::ReplayJudgement::Type type) {
  return static_cast<int>(type);
}

Json metaToJson(const ReplayMeta& meta) {
  Json node = Json::object();
  node["chartId"] = meta.chartId;
  node["songName"] = meta.songName;
  node["difficulty"] = meta.difficulty;
  node["score"] = meta.score;
  node["accuracy"] = meta.accuracy;
  node["maxCombo"] = meta.maxCombo;
  node["totalNotes"] = meta.totalNotes;
  node["perfect"] = meta.perfect;
  node["good"] = meta.good;
  node["bad"] = meta.bad;
  node["miss"] = meta.miss;
  node["goodEarly"] = meta.goodEarly;
  node["goodLate"] = meta.goodLate;
  node["stdDevMs"] = meta.stdDevMs;
  if (meta.hasJudgementOffsetBins) {
    Json bins = Json::array();
    for (const int value : meta.judgementOffsetBins) bins.push_back(value);
    node["judgementOffsetBins"] = std::move(bins);
  }
  node["timestampMs"] = meta.timestampMs;
  node["timestamp"] = meta.timestampMs;
  node["autoplay"] = meta.autoplay;
  node["challenge"] = meta.challenge;
  node["mirrorX"] = meta.mirrorX;
  node["practice"] = meta.practice;
  node["musicSpeed"] = meta.musicSpeed;
  node["scrollSpeed"] = meta.scrollSpeed;
  node["chartOffsetMs"] = meta.chartOffsetMs;
  node["audioOffsetMs"] = meta.audioOffsetMs;
  return node;
}

ReplayMeta metaFromJson(const Json& node) {
  ReplayMeta meta;
  const auto string = [&node](const char* key) {
    const auto iterator = node.find(key);
    return iterator != node.end() && iterator->is_string() ? iterator->get<std::string>()
                                                           : std::string{};
  };
  const auto integer = [&node](const char* key, long long fallback) {
    const auto iterator = node.find(key);
    return iterator != node.end() && iterator->is_number() ? iterator->get<long long>()
                                                           : fallback;
  };
  const auto number = [&node](const char* key, double fallback) {
    const auto iterator = node.find(key);
    return iterator != node.end() && iterator->is_number() ? iterator->get<double>() : fallback;
  };
  const auto boolean = [&node](const char* key, bool fallback) {
    const auto iterator = node.find(key);
    return iterator != node.end() && iterator->is_boolean() ? iterator->get<bool>() : fallback;
  };
  meta.chartId = string("chartId");
  meta.songName = string("songName");
  meta.difficulty = string("difficulty");
  meta.score = static_cast<int>(integer("score", 0));
  meta.accuracy = number("accuracy", 0.0);
  meta.maxCombo = static_cast<int>(integer("maxCombo", 0));
  meta.totalNotes = static_cast<int>(integer("totalNotes", 0));
  meta.perfect = static_cast<int>(integer("perfect", 0));
  meta.good = static_cast<int>(integer("good", 0));
  meta.bad = static_cast<int>(integer("bad", 0));
  meta.miss = static_cast<int>(integer("miss", 0));
  meta.goodEarly = static_cast<int>(integer("goodEarly", 0));
  meta.goodLate = static_cast<int>(integer("goodLate", 0));
  meta.stdDevMs = number("stdDevMs", 0.0);
  // File names differ between this port and the reference; accept both.
  meta.timestampMs = integer("timestampMs", integer("timestamp", 0));
  meta.autoplay = boolean("autoplay", false);
  meta.challenge = boolean("challenge", false);
  meta.mirrorX = boolean("mirrorX", false);
  meta.practice = boolean("practice", false);
  meta.musicSpeed = static_cast<float>(number("musicSpeed", 1.0));
  meta.scrollSpeed = static_cast<float>(number("scrollSpeed", 1.0));
  meta.chartOffsetMs = static_cast<int>(integer("chartOffsetMs", 0));
  meta.audioOffsetMs = static_cast<int>(integer("audioOffsetMs", 0));
  if (const auto bins = node.find("judgementOffsetBins");
      bins != node.end() && bins->is_array()) {
    meta.hasJudgementOffsetBins = true;
    for (std::size_t index = 0; index < meta.judgementOffsetBins.size() && index < bins->size();
        ++index) {
      const auto& value = (*bins)[index];
      if (value.is_number()) {
        meta.judgementOffsetBins[index] = static_cast<int>(value.get<double>());
      }
    }
  }
  return meta;
}

bool hasBinData(const std::array<int, game::PlayResult::JUDGEMENT_DETAIL_BIN_COUNT>& bins) {
  for (const int value : bins) {
    if (value != 0) return true;
  }
  return false;
}

} // namespace

game::PlayResult ReplayMeta::toPlayResult() const {
  game::PlayResult result;
  result.score = score;
  result.accuracy = accuracy;
  result.maxCombo = maxCombo;
  result.totalNotes = totalNotes;
  result.perfect = perfect;
  result.good = good;
  result.bad = bad;
  result.miss = miss;
  result.goodEarly = goodEarly;
  result.goodLate = goodLate;
  result.stdDevMs = stdDevMs;
  if (hasJudgementOffsetBins) result.judgementOffsetBins = judgementOffsetBins;
  return result;
}

game::PlayResult mergeReplayPlaybackResult(const ReplayMeta& meta, const game::PlayResult& live) {
  const game::PlayResult saved = meta.toPlayResult();
  const bool savedHasBins = meta.hasJudgementOffsetBins && hasBinData(meta.judgementOffsetBins);
  if (!hasBinData(live.judgementOffsetBins) && savedHasBins) {
    // No live samples (for example a replay recorded by a build that did not collect them):
    // the saved histogram is the only data there is.
    return saved;
  }
  game::PlayResult merged = saved;
  merged.judgementOffsetBins = live.judgementOffsetBins;
  return merged;
}

void applyResultToReplayMeta(ReplayData& data, const game::PlayResult& result) {
  auto& meta = data.meta;
  meta.score = result.score;
  meta.accuracy = result.accuracy;
  meta.maxCombo = result.maxCombo;
  meta.totalNotes = result.totalNotes;
  meta.perfect = result.perfect;
  meta.good = result.good;
  meta.bad = result.bad;
  meta.miss = result.miss;
  meta.goodEarly = result.goodEarly;
  meta.goodLate = result.goodLate;
  meta.stdDevMs = result.stdDevMs;
  meta.judgementOffsetBins = result.judgementOffsetBins;
  meta.hasJudgementOffsetBins = true;
}

bool writeReplayFile(const std::filesystem::path& file, const ReplayData& data) {
  const bool legacy = data.judgements.empty() && !data.entries.empty();
  Json root = Json::object();
  root["version"] = legacy ? kTouchSampleVersion : kJudgementVersion;
  root["meta"] = metaToJson(data.meta);
  Json entries = Json::array();
  entries.get_ref<Json::array_t&>().reserve(legacy ? data.entries.size() : data.judgements.size());
  if (legacy) {
    for (const auto& entry : data.entries) {
      Json node = Json::object();
      node["k"] = entryKindName(static_cast<int>(entry.kind));
      node["id"] = entry.pointerId;
      node["x"] = entry.x;
      node["y"] = entry.y;
      node["t"] = entry.timeSec;
      entries.push_back(std::move(node));
    }
  } else {
    // One entry per judgement: type, note index, judgement and chart time. The key names
    // match the reference format so a file can travel between the two.
    for (const auto& entry : data.judgements) {
      Json node = Json::object();
      node["t"] = judgementTypeIndex(entry.type);
      node["ni"] = entry.noteIndex;
      node["j"] = entry.judgement;
      node["ts"] = entry.timeSec;
      entries.push_back(std::move(node));
    }
  }
  root["entries"] = std::move(entries);
  return platform::writeTextFile(file, root.dump());
}

std::optional<ReplayData> readReplayFile(const std::filesystem::path& file) {
  const std::string content = platform::readTextFile(file);
  if (content.empty()) return std::nullopt;
  const Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return std::nullopt;
  ReplayData data;
  int version = 1;
  for (const char* key : {"version", "v"}) {
    const auto iterator = root.find(key);
    if (iterator != root.end() && iterator->is_number()) {
      version = static_cast<int>(iterator->get<double>());
      break;
    }
  }
  const auto meta = root.find("meta");
  if (meta != root.end() && meta->is_object()) data.meta = metaFromJson(*meta);
  const auto entries = root.find("entries");
  const bool hasEntries = entries != root.end() && entries->is_array();
  // The version number alone cannot distinguish the two layouts (early judgement files were
  // written with version 1 as well), so the entry shape decides.
  bool judgementModel = version >= kJudgementVersion;
  if (hasEntries && !entries->empty()) {
    const auto& first = entries->front();
    judgementModel = first.is_object()
        && (first.contains("ni") || first.contains("ts") || first.contains("j"));
  }
  data.version = judgementModel ? kJudgementVersion : kTouchSampleVersion;
  if (hasEntries) {
    data.judgements.reserve(judgementModel ? entries->size() : 0U);
    data.entries.reserve(judgementModel ? 0U : entries->size());
    for (const auto& node : *entries) {
      if (!node.is_object()) continue;
      if (judgementModel) {
        game::ReplayJudgement entry;
        const auto type = node.find("t");
        if (type != node.end() && type->is_number()) {
          entry.type = static_cast<game::ReplayJudgement::Type>(type->get<int>());
        }
        const auto noteIndex = node.find("ni");
        if (noteIndex != node.end() && noteIndex->is_number()) {
          entry.noteIndex = noteIndex->get<int>();
        }
        const auto judgement = node.find("j");
        if (judgement != node.end() && judgement->is_number()) {
          entry.judgement = judgement->get<int>();
        }
        const auto time = node.find("ts");
        if (time != node.end() && time->is_number()) entry.timeSec = time->get<double>();
        data.judgements.push_back(entry);
      } else {
        ReplayTouchEvent entry;
        const auto kind = node.find("k");
        if (kind != node.end() && kind->is_string()) {
          entry.kind =
              static_cast<ReplayTouchEvent::Kind>(parseEntryKind(kind->get<std::string>()));
        }
        const auto id = node.find("id");
        if (id != node.end() && id->is_number()) entry.pointerId = id->get<long long>();
        const auto x = node.find("x");
        if (x != node.end() && x->is_number()) entry.x = x->get<float>();
        const auto y = node.find("y");
        if (y != node.end() && y->is_number()) entry.y = y->get<float>();
        const auto time = node.find("t");
        if (time != node.end() && time->is_number()) entry.timeSec = time->get<double>();
        data.entries.push_back(entry);
      }
    }
  }
  return data;
}

ReplayStore::ReplayStore(std::filesystem::path root) : root_(std::move(root)) {}

std::string ReplayStore::save(const ReplayData& data, bool* duplicate) {
  if (duplicate != nullptr) *duplicate = false;
  std::error_code error;
  std::filesystem::create_directories(root_, error);
  if (error && !std::filesystem::is_directory(root_)) return {};
  // A recording is kept at most once. The same song, the same score and a timestamp within a
  // few seconds of an existing entry describe the same play, so saving again from a later
  // settlement must not add a second copy. The reference applies exactly this test, and the
  // caller turns the empty id into its "already saved" message.
  for (const auto& existing : list()) {
    if (existing.meta.songName != data.meta.songName) continue;
    if (existing.meta.score != data.meta.score) continue;
    const long long gap = existing.meta.timestampMs > data.meta.timestampMs
        ? existing.meta.timestampMs - data.meta.timestampMs
        : data.meta.timestampMs - existing.meta.timestampMs;
    if (gap < kDuplicateWindowMs) {
      if (duplicate != nullptr) *duplicate = true;
      return {};
    }
  }
  const long long timestamp = data.meta.timestampMs > 0 ? data.meta.timestampMs : 0;
  std::string id = std::to_string(timestamp);
  int suffix = 1;
  while (std::filesystem::exists(root_ / id)) {
    id = std::to_string(timestamp) + "-" + std::to_string(suffix++);
    if (suffix > 999) return {};
  }
  const auto directory = root_ / id;
  std::filesystem::create_directories(directory, error);
  if (error) return {};
  if (!writeReplayFile(directory / "replay.json", data)) {
    platform::removeRecursively(directory);
    return {};
  }
  return id;
}

std::vector<ReplayInfo> ReplayStore::list() const {
  std::vector<ReplayInfo> replays;
  std::error_code error;
  if (!std::filesystem::is_directory(root_, error)) return replays;
  for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
    if (error) break;
    if (!entry.is_directory(error)) continue;
    const auto file = entry.path() / "replay.json";
    if (!platform::fileExists(file)) continue;
    const auto data = readReplayFile(file);
    if (!data) continue;
    ReplayInfo info;
    info.id = entry.path().filename().string();
    info.directory = entry.path();
    info.file = file;
    info.meta = data->meta;
    replays.push_back(std::move(info));
  }
  std::sort(replays.begin(), replays.end(), [](const ReplayInfo& left, const ReplayInfo& right) {
    if (left.meta.timestampMs != right.meta.timestampMs) {
      return left.meta.timestampMs > right.meta.timestampMs;
    }
    return left.id > right.id;
  });
  return replays;
}

std::optional<ReplayInfo> ReplayStore::find(const std::string& id) const {
  const auto directory = root_ / platform::sanitiseFileName(id);
  const auto file = directory / "replay.json";
  if (!platform::fileExists(file)) return std::nullopt;
  const auto data = readReplayFile(file);
  if (!data) return std::nullopt;
  ReplayInfo info;
  info.id = directory.filename().string();
  info.directory = directory;
  info.file = file;
  info.meta = data->meta;
  return info;
}

bool ReplayStore::remove(const std::string& id) {
  if (id.empty()) return false;
  return platform::removeRecursively(root_ / platform::sanitiseFileName(id));
}

} // namespace rechigros::app
