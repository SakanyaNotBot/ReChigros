#include "ImportCore.hpp"

#include "rechigros/app/ChartLibrary.hpp"
#include "rechigros/app/InfoFile.hpp"
#include "rechigros/app/Replay.hpp"
#include "rechigros/platform/Archive.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/asset/ImageCache.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <system_error>

namespace rechigros::app {
namespace {

using Json = nlohmann::json;

constexpr const char* kChartSuccess = "谱面导入成功";
constexpr const char* kChartExists = "本地已存在该谱面";
constexpr const char* kChartPickRequired = "请先选择音乐、谱面与背景";
constexpr const char* kReplaySuccess = "回放导入成功";
constexpr const char* kSkinSuccess = "皮肤导入成功";

std::string lowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension;
}

std::string lowerFileName(const std::filesystem::path& path) {
  std::string name = platform::pathToUtf8(path.filename());
  std::transform(name.begin(), name.end(), name.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return name;
}

std::int64_t nowMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// A unique scratch directory under the application cache. Importers always remove it, and
// the counter keeps two imports in one session from sharing state.
std::filesystem::path scratchDirectory(const char* prefix) {
  static unsigned counter = 0;
  std::error_code error;
  const auto root = platform::cacheRoot();
  std::filesystem::create_directories(root, error);
  return root / (std::string(prefix) + "-" + std::to_string(nowMilliseconds()) + "-"
      + std::to_string(++counter));
}

std::string failureMessage(const char* prefix, const std::exception& exception) {
  std::string message(prefix);
  const std::string detail = exception.what();
  if (!detail.empty()) message += detail;
  return message;
}

// ---------------------------------------------------------------------------
// Reference replay documents
// ---------------------------------------------------------------------------

int jsonInt(const Json& node, const char* key, int fallback) {
  const auto iterator = node.find(key);
  return iterator != node.end() && iterator->is_number() ? iterator->get<int>() : fallback;
}

double jsonNumber(const Json& node, const char* key, double fallback) {
  const auto iterator = node.find(key);
  return iterator != node.end() && iterator->is_number() ? iterator->get<double>() : fallback;
}

bool jsonBool(const Json& node, const char* key, bool fallback) {
  const auto iterator = node.find(key);
  return iterator != node.end() && iterator->is_boolean() ? iterator->get<bool>() : fallback;
}

std::string jsonString(const Json& node, const char* key) {
  const auto iterator = node.find(key);
  return iterator != node.end() && iterator->is_string() ? iterator->get<std::string>()
                                                         : std::string{};
}

// Reads a replay document that the store's reader does not understand on its own. The
// reference package stores a judgement-entry document, which the replay reader accepts
// directly, so this fallback only carries the metadata of unusual uploads.
std::optional<ReplayData> parseReferenceReplayDocument(const std::string& content) {
  const Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return std::nullopt;

  const Json* meta = &root;
  const auto metaNode = root.find("meta");
  if (metaNode != root.end() && metaNode->is_object()) meta = &*metaNode;

  const bool hasMeta = meta->is_object()
      && (meta->contains("songName") || meta->contains("score") || meta->contains("accuracy")
          || meta->contains("totalNotes"));
  const auto entriesNode = root.find("entries");
  const bool hasEntries = entriesNode != root.end() && entriesNode->is_array();
  if (!hasMeta && !hasEntries) return std::nullopt;

  ReplayData data;
  ReplayMeta& out = data.meta;
  out.songName = jsonString(*meta, "songName");
  out.difficulty = jsonString(*meta, "difficulty");
  out.chartId = jsonString(*meta, "chartId");
  out.score = jsonInt(*meta, "score", 0);
  out.accuracy = jsonNumber(*meta, "accuracy", 0.0);
  out.maxCombo = jsonInt(*meta, "maxCombo", 0);
  out.totalNotes = jsonInt(*meta, "totalNotes", 0);
  out.perfect = jsonInt(*meta, "perfect", 0);
  out.good = jsonInt(*meta, "good", 0);
  out.bad = jsonInt(*meta, "bad", 0);
  out.miss = jsonInt(*meta, "miss", 0);
  out.timestampMs = static_cast<std::int64_t>(
      jsonNumber(*meta, "timestamp", static_cast<double>(nowMilliseconds())));
  out.autoplay = jsonBool(*meta, "autoplay", false);
  out.challenge = jsonBool(*meta, "challenge", false);
  out.mirrorX = jsonBool(*meta, "mirrorX", false);
  out.practice = jsonBool(*meta, "practice", false);
  out.musicSpeed = static_cast<float>(jsonNumber(*meta, "musicSpeed", 1.0));
  out.scrollSpeed = static_cast<float>(jsonNumber(*meta, "scrollSpeed", 1.0));
  return data;
}

// Finds the replay document inside an extracted replay package. The package stores it
// together with the play resources, so the shallowest match wins.
std::filesystem::path findReplayDocument(const std::filesystem::path& root) {
  std::filesystem::path best;
  std::size_t bestDepth = static_cast<std::size_t>(-1);
  for (const auto& file : platform::listFiles(root)) {
    if (lowerFileName(file) != "replay.json") continue;
    const auto relative = file.lexically_relative(root);
    const auto depth = static_cast<std::size_t>(
        std::distance(relative.begin(), relative.end()));
    if (depth < bestDepth) {
      bestDepth = depth;
      best = file;
    }
  }
  return best;
}

std::optional<ReplayData> loadReplayDocument(const std::filesystem::path& file) {
  if (auto parsed = readReplayFile(file)) return parsed;
  return parseReferenceReplayDocument(platform::readTextFile(file));
}

std::string skinNameFromConfig(const std::string& text, bool isJson) {
  if (isJson) {
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return {};
    return jsonString(root, "name");
  }
  // The YAML-like metadata file is read line by line: only a top-level `name:` entry is
  // used, and simple quote styles are stripped.
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto end = text.find('\n', cursor);
    std::string line = text.substr(cursor, end == std::string::npos ? std::string::npos
                                                                   : end - cursor);
    cursor = end == std::string::npos ? text.size() : end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string key = line.substr(0, colon);
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
    if (key != "name") continue;
    std::string value = line.substr(colon + 1);
    const auto first = value.find_first_not_of(" \t");
    value = first == std::string::npos ? std::string{} : value.substr(first);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
                                 || (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    return value;
  }
  return {};
}

} // namespace

ImportOutcome importChartPackage(ChartLibrary& library, const std::filesystem::path& archive,
    bool requireComplete) {
  if (!platform::fileExists(archive)) {
    return {false, "处理压缩包失败：文件不存在", {}};
  }
  try {
    const auto result = library.importZip(archive, requireComplete);
    ImportOutcome outcome;
    outcome.imported = result.imported;
    outcome.chartId = result.record.id;
    outcome.message = result.imported ? kChartSuccess : kChartExists;
    return outcome;
  } catch (const std::exception& exception) {
    return {false, failureMessage("处理压缩包失败：", exception), {}};
  }
}

ImportOutcome importChartDirectory(ChartLibrary& library,
    const std::filesystem::path& directory) {
  if (!platform::directoryExists(directory)) {
    return {false, "处理压缩包失败：目录不存在", {}};
  }
  try {
    const auto result = library.importDirectory(directory);
    ImportOutcome outcome;
    outcome.imported = result.imported;
    outcome.chartId = result.record.id;
    outcome.message = result.imported ? kChartSuccess : kChartExists;
    return outcome;
  } catch (const std::exception& exception) {
    return {false, failureMessage("处理压缩包失败：", exception), {}};
  }
}

ImportOutcome importLooseChartFiles(ChartLibrary& library,
    const std::vector<std::filesystem::path>& files) {
  std::filesystem::path chartFile;
  std::filesystem::path musicFile;
  std::filesystem::path bgFile;
  for (const auto& file : files) {
    if (!platform::fileExists(file)) continue;
    if (chartFile.empty() && isChartCandidate(file)) {
      chartFile = file;
    } else if (musicFile.empty() && isAudioCandidate(file)) {
      musicFile = file;
    } else if (bgFile.empty() && isImageCandidate(file)) {
      bgFile = file;
    }
  }
  if (chartFile.empty() || musicFile.empty() || bgFile.empty()) {
    return {false, kChartPickRequired, {}};
  }

  const auto staging = scratchDirectory("loose-chart");
  std::error_code error;
  std::filesystem::create_directories(staging, error);
  if (error) return {false, "处理压缩包失败：无法创建临时目录", {}};

  // The pack is built in a staging folder with the metadata the reference derives from
  // the pickers: the song name comes from the audio file, the rest from the chart.
  ChartInfo info;
  info.name = platform::pathToUtf8(musicFile.stem());
  const auto copy = [&](const std::filesystem::path& source) {
    const auto target = staging / source.filename();
    std::filesystem::copy_file(source, target,
        std::filesystem::copy_options::overwrite_existing, error);
    return target;
  };
  const auto chartTarget = copy(chartFile);
  const auto musicTarget = copy(musicFile);
  const auto bgTarget = copy(bgFile);
  if (error) {
    platform::removeRecursively(staging);
    return {false, "处理压缩包失败：无法写入临时目录", {}};
  }
  info.chart = platform::pathToUtf8(chartTarget.filename());
  info.music = platform::pathToUtf8(musicTarget.filename());
  info.illustration = platform::pathToUtf8(bgTarget.filename());
  (void)writeInfoJson(staging / "info.json", info);

  try {
    const auto result = library.importDirectory(staging);
    platform::removeRecursively(staging);
    ImportOutcome outcome;
    outcome.imported = result.imported;
    outcome.chartId = result.record.id;
    outcome.message = result.imported ? kChartSuccess : kChartExists;
    return outcome;
  } catch (const std::exception& exception) {
    platform::removeRecursively(staging);
    return {false, failureMessage("处理压缩包失败：", exception), {}};
  }
}

ImportOutcome importReplayArchive(const std::filesystem::path& archive, ReplayStore& replays) {
  if (!platform::fileExists(archive)) {
    return {false, "导入回放失败：文件不存在", {}};
  }
  const std::string extension = lowerExtension(archive);
  try {
    std::optional<ReplayData> data;
    if (extension == ".json") {
      data = loadReplayDocument(archive);
    } else {
      const auto staging = scratchDirectory("replay-import");
      std::error_code error;
      std::filesystem::create_directories(staging, error);
      if (error) return {false, "导入回放失败：无法创建临时目录", {}};
      std::filesystem::path document;
      try {
        (void)platform::extractArchive(archive, staging);
        document = findReplayDocument(staging);
      } catch (const std::exception& exception) {
        platform::removeRecursively(staging);
        return {false, failureMessage("导入回放失败：", exception), {}};
      }
      if (document.empty()) {
        platform::removeRecursively(staging);
        return {false, "导入回放失败：回放文件损坏：缺少 replay.json", {}};
      }
      data = loadReplayDocument(document);
      platform::removeRecursively(staging);
    }
    if (!data) {
      return {false, "导入回放失败：回放文件损坏", {}};
    }
    if (data->meta.timestampMs <= 0) data->meta.timestampMs = nowMilliseconds();
    const std::string id = replays.save(*data);
    if (id.empty()) {
      return {false, "导入回放失败：无法创建回放目录", {}};
    }
    return {true, kReplaySuccess, {}};
  } catch (const std::exception& exception) {
    return {false, failureMessage("导入回放失败：", exception), {}};
  }
}

SkinImportOutcome importSkinPackage(const std::filesystem::path& archive,
    const std::filesystem::path& skinsRoot) {
  if (!platform::fileExists(archive)) {
    return {false, "导入皮肤失败：文件不存在", {}};
  }
  try {
    const auto entries = platform::listArchive(archive);
    if (entries.empty()) {
      return {false, "导入皮肤失败：压缩包为空或无法解析", {}};
    }

    // Display name: the first skin metadata file found in the package decides it.
    std::string displayName;
    for (const auto& entry : entries) {
      const std::string name = lowerFileName(platform::pathFromUtf8(entry.name));
      if (name != "skin.json" && name != "info.yml" && name != "info.yaml") continue;
      const auto bytes = platform::readArchiveEntry(archive, entry.name);
      if (bytes.empty()) continue;
      // Skin metadata may carry the legacy code page; decode it like every other metadata
      // document so the display name is not mojibake.
      const std::string text = platform::decodeLegacyText(
          std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      displayName = skinNameFromConfig(text, name == "skin.json");
      if (!displayName.empty()) break;
    }
    std::string folderName = platform::sanitiseFileName(
        displayName.empty() ? platform::pathToUtf8(archive.stem()) : displayName);
    if (folderName.empty()) folderName = "skin";
    const auto destination = skinsRoot / platform::pathFromUtf8(folderName);
    platform::removeRecursively(destination);
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) return {false, "导入皮肤失败：无法创建皮肤目录", {}};

    // The renderer loads the skin from an unpacked, flat directory, so archive paths are
    // reduced to their leaf names the way the reference importer stores them.
    std::size_t written = 0;
    for (const auto& entry : entries) {
      if (!entry.name.empty() && entry.name.back() == '/') continue;
      const auto leaf = platform::sanitiseFileName(platform::pathToUtf8(
          platform::pathFromUtf8(entry.name).filename()));
      if (leaf.empty()) continue;
      const auto bytes = platform::readArchiveEntry(archive, entry.name);
      if (bytes.empty()) continue;
      if (!platform::writeBinaryFile(destination / platform::pathFromUtf8(leaf), bytes)) {
        platform::removeRecursively(destination);
        return {false, "导入皮肤失败：无法写入皮肤文件", {}};
      }
      ++written;
    }
    if (written == 0) {
      platform::removeRecursively(destination);
      return {false, "导入皮肤失败：压缩包内没有皮肤文件", {}};
    }
    if (platform::fileExists(destination / "hit_fx.png")) {
      (void)render::asset::prepareImageCache(destination / "hit_fx.png");
    }
    SkinImportOutcome outcome;
    outcome.imported = true;
    outcome.message = kSkinSuccess;
    outcome.skinId = folderName;
    return outcome;
  } catch (const std::exception& exception) {
    return {false, failureMessage("导入皮肤失败：", exception), {}};
  }
}

} // namespace rechigros::app
