#include "rechigros/app/InfoFile.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace rechigros::app {
namespace {

using Json = nlohmann::json;

constexpr char kNormalSpace = ' ';
constexpr char kEnSpace[] = "\xE2\x80\x82";

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string stripQuotes(const std::string& value) {
  std::string trimmed = value;
  const auto first = trimmed.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = trimmed.find_last_not_of(" \t\r\n");
  trimmed = trimmed.substr(first, last - first + 1);
  if (trimmed.size() >= 2) {
    const char front = trimmed.front();
    const char back = trimmed.back();
    if ((front == '"' && back == '"') || (front == '\'' && back == '\'')) {
      return trimmed.substr(1, trimmed.size() - 2);
    }
  }
  return trimmed;
}

bool isBlank(const std::string& value) {
  return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::string firstNonEmpty(const std::vector<std::string>& values) {
  for (const auto& value : values) {
    if (!isBlank(value)) return value;
  }
  return {};
}

std::string collapseNormalSpaceRuns(const std::string& text) {
  if (text.find(kNormalSpace) == std::string::npos) return text;
  std::string result;
  result.reserve(text.size());
  bool previousWasSpace = false;
  for (const char character : text) {
    if (character == kNormalSpace) {
      if (!previousWasSpace) {
        result.push_back(character);
        previousWasSpace = true;
      }
    } else {
      result.push_back(character);
      previousWasSpace = false;
    }
  }
  return result;
}

bool isDifficultyEdgeWhitespace(char character) {
  return character == ' ' || std::isspace(static_cast<unsigned char>(character)) != 0;
}

std::string trimDifficultyWhitespace(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && isDifficultyEdgeWhitespace(text[begin])) ++begin;
  while (end > begin && isDifficultyEdgeWhitespace(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

bool containsToken(const std::string& text, const std::string& token) {
  std::size_t position = text.find(token);
  while (position != std::string::npos) {
    const bool leftOk = position == 0
        || !(std::isalnum(static_cast<unsigned char>(text[position - 1])) != 0);
    const auto after = position + token.size();
    const bool rightOk = after >= text.size()
        || !(std::isalnum(static_cast<unsigned char>(text[after])) != 0);
    if (leftOk && rightOk) return true;
    position = text.find(token, position + 1);
  }
  return false;
}

bool looksLikeChartHead(const std::string& head) {
  if (head.empty()) return false;
  if (head.find("\"formatVersion\"") != std::string::npos) return true;
  if (head.find("\"judgeLineList\"") == std::string::npos) return false;
  return head.find("\"BPMList\"") != std::string::npos
      || head.find("\"META\"") != std::string::npos
      || head.find("\"RPEVersion\"") != std::string::npos
      || head.find("\"JudgeLineList\"") != std::string::npos;
}

std::string readHead(const std::filesystem::path& file, std::size_t maximum) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  std::string head(maximum, '\0');
  input.read(head.data(), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(input.gcount()));
  return head;
}

std::vector<std::string> readLines(const std::filesystem::path& file) {
  std::vector<std::string> lines;
  const std::string content = platform::readTextFile(file);
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::string> parseCsvLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool inQuotes = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (character == '"') {
      if (inQuotes && index + 1 < line.size() && line[index + 1] == '"') {
        current.push_back('"');
        ++index;
      } else {
        inQuotes = !inQuotes;
      }
    } else if (character == ',' && !inQuotes) {
      fields.push_back(stripQuotes(current));
      current.clear();
    } else {
      current.push_back(character);
    }
  }
  fields.push_back(stripQuotes(current));
  return fields;
}

void assignFromKeyValues(ChartInfo& info, const std::unordered_map<std::string, std::string>& kv) {
  std::unordered_map<std::string, std::string> lower;
  for (const auto& [key, value] : kv) lower[toLower(key)] = value;

  const auto find = [&lower](std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
      const auto iterator = lower.find(key);
      if (iterator != lower.end() && !isBlank(iterator->second)) return iterator->second;
    }
    return std::string{};
  };

  info.name = find({"name", "songname", "title"});
  info.music = find({"music", "song", "audio"});
  info.chart = find({"chart", "chartpath"});
  info.illustration = find({"illustration", "image", "picture", "bg", "background"});
  info.level = normalizeDifficultyText(find({"level"}));
  info.difficulty = normalizeDifficultyText(find({"difficulty", "diff"}));
  info.composer = find({"composer", "artist", "musician"});
  info.charter = find({"charter", "designer"});
  info.illustrator = find({"illustrator"});

  const std::string offset = find(
      {"chartoffsetms", "chart_offset_ms", "chartoffset", "offsetms", "offset"});
  if (!offset.empty()) {
    try {
      info.chartOffsetMs = std::stoi(offset);
      info.hasChartOffset = true;
    } catch (...) {
      info.hasChartOffset = false;
    }
  }

  const std::string attachFix = find({"useattachuifix", "use_attach_ui_fix"});
  if (!attachFix.empty()) {
    const std::string lowered = toLower(attachFix);
    if (lowered == "true" || lowered == "1") {
      info.useAttachUiFix = true;
      info.hasUseAttachUiFix = true;
    } else if (lowered == "false" || lowered == "0") {
      info.useAttachUiFix = false;
      info.hasUseAttachUiFix = true;
    }
  }
}

std::unordered_map<std::string, std::string> readKeyValueLines(const std::vector<std::string>& lines,
    bool skipFirstComment) {
  std::unordered_map<std::string, std::string> values;
  bool first = true;
  for (const auto& line : lines) {
    std::string trimmed = line;
    const auto firstNonSpace = trimmed.find_first_not_of(" \t\r");
    if (firstNonSpace == std::string::npos) continue;
    trimmed = trimmed.substr(firstNonSpace);
    if (skipFirstComment && first) {
      first = false;
      if (!trimmed.empty() && trimmed.front() == '#') continue;
    }
    if (!trimmed.empty() && trimmed.front() == '#') continue;
    const auto separator = trimmed.find(':');
    if (separator == std::string::npos || separator == 0) continue;
    std::string key = trimmed.substr(0, separator);
    std::string value = trimmed.substr(separator + 1);
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())) != 0) key.pop_back();
    values[key] = stripQuotes(value);
  }
  return values;
}

ChartInfo readYamlLike(const std::filesystem::path& file) {
  ChartInfo info;
  assignFromKeyValues(info, readKeyValueLines(readLines(file), false));
  return info;
}

ChartInfo readKeyValueText(const std::filesystem::path& file) {
  ChartInfo info;
  assignFromKeyValues(info, readKeyValueLines(readLines(file), true));
  return info;
}

ChartInfo readCsv(const std::filesystem::path& file) {
  ChartInfo info;
  const auto lines = readLines(file);
  std::vector<std::string> header;
  std::vector<std::string> values;
  for (const auto& line : lines) {
    if (isBlank(line)) continue;
    if (header.empty()) {
      header = parseCsvLine(line);
    } else {
      values = parseCsvLine(line);
      break;
    }
  }
  if (header.empty() || values.empty()) return info;
  std::unordered_map<std::string, std::string> keyValues;
  const auto count = std::min(header.size(), values.size());
  for (std::size_t index = 0; index < count; ++index) {
    if (!isBlank(header[index])) keyValues[header[index]] = values[index];
  }
  assignFromKeyValues(info, keyValues);
  return info;
}

ChartInfo readJsonInfo(const std::filesystem::path& file) {
  ChartInfo info;
  const auto content = platform::readTextFile(file);
  if (content.empty()) return info;
  const Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return info;

  const auto getString = [&root](std::initializer_list<const char*> keys) -> std::string {
    for (const char* key : keys) {
      const auto iterator = root.find(key);
      if (iterator == root.end() || iterator->is_null()) continue;
      if (iterator->is_string()) {
        const auto value = iterator->get<std::string>();
        if (!value.empty()) return value;
      } else if (iterator->is_number_integer()) {
        return std::to_string(iterator->get<long long>());
      } else if (iterator->is_number_float()) {
        return std::to_string(iterator->get<double>());
      } else if (iterator->is_boolean()) {
        return iterator->get<bool>() ? "true" : "false";
      }
    }
    return {};
  };

  info.name = getString({"name", "Name", "songName", "title"});
  info.chart = getString({"chart", "Chart", "chartPath"});
  info.music = getString({"music", "Music", "song", "Song", "audio"});
  info.illustration = getString({"illustration", "Illustration", "image", "Image", "picture",
      "Picture", "bg", "background"});
  info.level = normalizeDifficultyText(getString({"level", "Level"}));
  info.difficulty = normalizeDifficultyText(getString({"difficulty", "Difficulty", "diff", "Diff"}));
  info.composer = getString({"composer", "Composer", "artist", "Artist", "musician", "Musician"});
  info.charter = getString({"charter", "Charter", "designer", "Designer"});
  info.illustrator = getString({"illustrator", "Illustrator"});

  for (const char* key : {"chartOffsetMs", "chart_offset_ms", "chartOffset", "offsetMs"}) {
    const auto iterator = root.find(key);
    if (iterator == root.end()) continue;
    if (iterator->is_number_integer()) {
      info.chartOffsetMs = iterator->get<int>();
      info.hasChartOffset = true;
      break;
    }
    if (iterator->is_string()) {
      try {
        info.chartOffsetMs = std::stoi(iterator->get<std::string>());
        info.hasChartOffset = true;
        break;
      } catch (...) {
      }
    }
  }
  for (const char* key : {"useAttachUiFix", "use_attach_ui_fix"}) {
    const auto iterator = root.find(key);
    if (iterator == root.end()) continue;
    if (iterator->is_boolean()) {
      info.useAttachUiFix = iterator->get<bool>();
      info.hasUseAttachUiFix = true;
      break;
    }
  }
  return info;
}

std::string getNestedString(const Json& object, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) continue;
    if (iterator->is_string()) {
      const auto value = iterator->get<std::string>();
      if (!value.empty()) return value;
    }
  }
  return {};
}

} // namespace

std::string normalizeDifficultyText(const std::string& text) {
  if (text.empty()) return {};
  const std::string collapsed = collapseNormalSpaceRuns(text);
  if (collapsed.find(kNormalSpace) == std::string::npos) return collapsed;
  std::string result;
  result.reserve(collapsed.size() + 8);
  for (const char character : collapsed) {
    if (character == kNormalSpace) {
      result.append(kEnSpace);
    } else {
      result.push_back(character);
    }
  }
  return result;
}

std::string inferDifficultyPrefix(const std::filesystem::path& chartFile) {
  if (chartFile.empty()) return {};
  std::string base = chartFile.stem().string();
  std::string upper = base;
  std::transform(upper.begin(), upper.end(), upper.begin(),
      [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  for (const char* token : {"EZ", "HD", "IN", "AT"}) {
    if (containsToken(upper, token)) return token;
  }
  if (upper == "0") return "EZ";
  if (upper == "1") return "HD";
  if (upper == "2") return "IN";
  if (upper == "3") return "AT";
  return {};
}

std::string buildDifficultyLabel(const ChartInfo& info, const std::filesystem::path& chartFile) {
  if (!info.level.empty()) return normalizeDifficultyText(info.level);
  if (info.difficulty.empty()) return std::string("SP") + kEnSpace + "Lv.?";
  const std::string trimmed = normalizeDifficultyText(trimDifficultyWhitespace(info.difficulty));
  if (trimmed.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
          != std::string::npos
      && toLower(trimmed).find("lv") != std::string::npos) {
    return trimmed;
  }
  const std::string prefix = inferDifficultyPrefix(chartFile);
  if (!prefix.empty()) return normalizeDifficultyText(prefix + " Lv." + trimmed);
  return "Lv." + trimmed;
}

ChartInfo readInfoFile(const std::filesystem::path& file) {
  if (!platform::fileExists(file)) return {};
  const std::string extension = toLower(file.extension().string());
  try {
    if (extension == ".json") return readJsonInfo(file);
    if (extension == ".yml" || extension == ".yaml") return readYamlLike(file);
    if (extension == ".txt") return readKeyValueText(file);
    if (extension == ".csv") return readCsv(file);
    return readYamlLike(file);
  } catch (...) {
    return {};
  }
}

ChartInfo readInfoFromChartFile(const std::filesystem::path& chartFile) {
  ChartInfo info;
  if (!platform::fileExists(chartFile)) return info;
  if (toLower(chartFile.extension().string()) != ".json") return info;
  const auto content = platform::readTextFile(chartFile);
  if (content.empty()) return info;
  const Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return info;
  const Json* meta = nullptr;
  for (const char* key : {"META", "meta", "Meta"}) {
    const auto iterator = root.find(key);
    if (iterator != root.end() && iterator->is_object()) {
      meta = &(*iterator);
      break;
    }
  }
  if (meta == nullptr) return info;
  info.name = getNestedString(*meta, {"name", "Name", "title", "songName"});
  info.level = normalizeDifficultyText(getNestedString(*meta, {"level", "Level"}));
  info.difficulty = normalizeDifficultyText(
      getNestedString(*meta, {"difficulty", "Difficulty", "diff", "Diff"}));
  info.illustration = getNestedString(*meta, {"background", "Background", "bg", "Bg",
      "illustration", "Illustration", "image", "Image", "picture", "Picture"});
  info.music = getNestedString(*meta, {"music", "Music", "song", "Song", "audio", "Audio"});
  info.composer = getNestedString(*meta, {"composer", "Composer", "artist", "Artist", "musician",
      "Musician"});
  info.charter = getNestedString(*meta, {"charter", "Charter", "designer", "Designer"});
  info.illustrator = getNestedString(*meta, {"illustrator", "Illustrator"});
  return info;
}

bool writeInfoJson(const std::filesystem::path& file, const ChartInfo& info) {
  Json root = Json::object();
  root["name"] = info.name;
  root["chart"] = info.chart;
  root["music"] = info.music;
  root["illustration"] = info.illustration;
  root["level"] = info.level;
  root["difficulty"] = info.difficulty;
  root["composer"] = info.composer;
  root["charter"] = info.charter;
  root["illustrator"] = info.illustrator;
  root["chartOffsetMs"] = info.chartOffsetMs;
  if (info.hasUseAttachUiFix) root["useAttachUiFix"] = info.useAttachUiFix;
  return platform::writeTextFile(file, root.dump(2));
}

bool isChartCandidate(const std::filesystem::path& file) {
  const std::string name = toLower(file.filename().string());
  if (name == "info.json" || name == "extra.json" || name == "respack.json"
      || name == "replay.json") {
    return false;
  }
  const std::string extension = toLower(file.extension().string());
  if (extension == ".pec" || extension == ".rpe") return true;
  if (extension != ".json") return false;
  return looksLikeChartHead(readHead(file, 64 * 1024));
}

bool isAudioCandidate(const std::filesystem::path& file) {
  const std::string extension = toLower(file.extension().string());
  return extension == ".ogg" || extension == ".mp3" || extension == ".wav"
      || extension == ".m4a" || extension == ".aac" || extension == ".flac";
}

bool isImageCandidate(const std::filesystem::path& file) {
  const std::string extension = toLower(file.extension().string());
  return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
      || extension == ".webp";
}

} // namespace rechigros::app
