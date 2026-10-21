#include "rechigros/app/Replay.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace rechigros::app {
namespace {

using Json = nlohmann::json;

constexpr int kReplayVersion = 1;

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
  node["timestampMs"] = meta.timestampMs;
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
  meta.timestampMs = integer("timestampMs", 0);
  meta.autoplay = boolean("autoplay", false);
  meta.challenge = boolean("challenge", false);
  meta.mirrorX = boolean("mirrorX", false);
  meta.practice = boolean("practice", false);
  meta.musicSpeed = static_cast<float>(number("musicSpeed", 1.0));
  meta.scrollSpeed = static_cast<float>(number("scrollSpeed", 1.0));
  meta.chartOffsetMs = static_cast<int>(integer("chartOffsetMs", 0));
  meta.audioOffsetMs = static_cast<int>(integer("audioOffsetMs", 0));
  return meta;
}

} // namespace

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
}

bool writeReplayFile(const std::filesystem::path& file, const ReplayData& data) {
  Json root = Json::object();
  root["version"] = kReplayVersion;
  root["meta"] = metaToJson(data.meta);
  Json entries = Json::array();
  entries.get_ref<Json::array_t&>().reserve(data.entries.size());
  for (const auto& entry : data.entries) {
    Json node = Json::object();
    node["k"] = entryKindName(static_cast<int>(entry.kind));
    node["id"] = entry.pointerId;
    node["x"] = entry.x;
    node["y"] = entry.y;
    node["t"] = entry.timeSec;
    entries.push_back(std::move(node));
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
  const auto meta = root.find("meta");
  if (meta != root.end() && meta->is_object()) data.meta = metaFromJson(*meta);
  const auto entries = root.find("entries");
  if (entries != root.end() && entries->is_array()) {
    data.entries.reserve(entries->size());
    for (const auto& node : *entries) {
      if (!node.is_object()) continue;
      ReplayTouchEvent entry;
      const auto kind = node.find("k");
      if (kind != node.end() && kind->is_string()) {
        entry.kind = static_cast<ReplayTouchEvent::Kind>(parseEntryKind(kind->get<std::string>()));
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
  return data;
}

ReplayStore::ReplayStore(std::filesystem::path root) : root_(std::move(root)) {}

std::string ReplayStore::save(const ReplayData& data) {
  std::error_code error;
  std::filesystem::create_directories(root_, error);
  if (error && !std::filesystem::is_directory(root_)) return {};
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
