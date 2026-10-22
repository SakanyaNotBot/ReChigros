#include "rechigros/chart/ChartLoader.hpp"

#include "rechigros/chart/OfficialChartParser.hpp"
#include "rechigros/chart/PecChartParser.hpp"
#include "rechigros/chart/PlainChartParser.hpp"
#include "rechigros/chart/RpeChartParser.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/OfficialLevelEffects.hpp"
#include "rechigros/game/PrprEffect.hpp"
#include "rechigros/chart/Md5.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rechigros::chart {
namespace {

using nlohmann::json;

constexpr int kExtraSearchDepth = 6;
// Format detection only reads this much of the file.
constexpr std::size_t kDetectionPrefixBytes = 64 * 1024;

// Chart file format, decided from a 64 KiB prefix rather than the file extension: charts
// are commonly renamed, and the identifying keys are only in the header.
enum class ChartFormat { PhiEdit, Official, Rpe, JsonObject };

// Skips a byte-order mark and leading whitespace, returning the index of the first
// significant byte or npos when there is none.
std::size_t firstSignificantByte(std::string_view text) {
  std::size_t index = 0;
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    index = 3;
  }
  while (index < text.size()) {
    const auto value = static_cast<unsigned char>(text[index]);
    if (value != ' ' && value != '\t' && value != '\r' && value != '\n' && value != '\f' &&
        value != '\v') {
      break;
    }
    ++index;
  }
  return index < text.size() ? index : std::string_view::npos;
}

bool looksLikeJsonObject(std::string_view text) {
  const auto first = firstSignificantByte(text);
  return first != std::string_view::npos && text[first] == '{';
}

// Official charts carry `formatVersion`; the level-editor format carries one of its four
// header keys. Anything else is the plain-text editor format.
ChartFormat detectFormat(std::string_view content) {
  const auto prefix = content.substr(0, std::min(content.size(), kDetectionPrefixBytes));
  if (!looksLikeJsonObject(prefix)) return ChartFormat::PhiEdit;
  if (prefix.find("\"formatVersion\"") != std::string_view::npos) return ChartFormat::Official;
  if (prefix.find("\"RPEVersion\"") != std::string_view::npos ||
      prefix.find("\"BPMList\"") != std::string_view::npos ||
      prefix.find("\"META\"") != std::string_view::npos ||
      prefix.find("\"JudgeLineList\"") != std::string_view::npos) {
    return ChartFormat::Rpe;
  }
  return ChartFormat::JsonObject;
}

// Identifies a chart by MD5 over its bytes with the byte-order mark and ASCII whitespace
// removed, so cosmetic reformatting does not change the identity.
std::string normalizedDigest(const std::string& content) {
  std::size_t start = 0;
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF) {
    start = 3;
  }
  Md5 digest;
  for (std::size_t index = start; index < content.size(); ++index) {
    const char character = content[index];
    if (character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
        character == '\f' || character == '\v') {
      continue;
    }
    digest.update(&character, 1);
  }
  return digest.hexDigest();
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Lenient boolean binding: a boolean, a number (non-zero is true) or one of the strings the
// metadata formats write.
std::optional<bool> parseFlagValue(const json& node) {
  if (node.is_boolean()) return node.get<bool>();
  if (node.is_number()) return node.get<double>() != 0.0;
  if (!node.is_string()) return std::nullopt;
  std::string text = node.get<std::string>();
  std::transform(text.begin(), text.end(), text.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (text == "true" || text == "1") return true;
  if (text == "false" || text == "0") return false;
  return std::nullopt;
}

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return text;
}

std::string trimSpaces(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return text.substr(begin, end - begin);
}

std::string stripQuotes(const std::string& text) {
  if (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') ||
                              (text.front() == '\'' && text.back() == '\''))) {
    return text.substr(1, text.size() - 2);
  }
  return text;
}

bool isAttachUiFixKey(const std::string& key) {
  const std::string lower = lowercase(trimSpaces(key));
  return lower == "useattachuifix" || lower == "use_attach_ui_fix";
}

// Reads the `useAttachUiFix` flag out of one metadata file. JSON is parsed as JSON; every
// other shape is the line-oriented `key: value` form, which is what the metadata files use.
std::optional<bool> parseAttachUiFix(const std::string& content, bool jsonFile) {
  if (jsonFile) {
    const auto root = json::parse(content, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return std::nullopt;
    for (const char* key : {"useAttachUiFix", "use_attach_ui_fix"}) {
      const auto iterator = root.find(key);
      if (iterator == root.end()) continue;
      if (const auto flag = parseFlagValue(*iterator)) return flag;
    }
    return std::nullopt;
  }
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = trimSpaces(line);
    if (trimmed.empty() || trimmed.front() == '#') continue;
    const auto separator = trimmed.find(':');
    if (separator == std::string::npos || separator == 0) continue;
    if (!isAttachUiFixKey(trimmed.substr(0, separator))) continue;
    const std::string value = lowercase(stripQuotes(trimSpaces(trimmed.substr(separator + 1))));
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return std::nullopt;
  }
  return std::nullopt;
}

// Applies a pack's `useAttachUiFix` choice. The nearest metadata file found at or above the
// chart file's directory supplies the flag; a file that does not declare it leaves the
// chart's own value (default true) in place.
void applyAttachUiFixFlag(game::Chart& chart, const std::filesystem::path& chartPath) {
  std::error_code error;
  std::filesystem::path directory = chartPath.parent_path();
  for (int depth = 0; depth < kExtraSearchDepth && !directory.empty(); ++depth) {
    for (const char* name : {"info.yml", "info.yaml", "info.json", "Info.yml", "Info.json"}) {
      const auto candidate = directory / name;
      if (!std::filesystem::is_regular_file(candidate, error)) continue;
      const bool jsonFile = lowercase(candidate.extension().string()) == ".json";
      if (const auto flag = parseAttachUiFix(readFile(candidate), jsonFile)) {
        chart.useAttachUiFix = *flag;
      }
      return;
    }
    if (!directory.has_parent_path() || directory.parent_path() == directory) break;
    directory = directory.parent_path();
  }
}

// A BPM segment of the storyboard timeline: beats become seconds through it.
struct BpmSegment {
  double startBeat{};
  double bpm{120.0};
  double startTime{};
  double beatTime{0.5};
};

// The storyboard timebase stores beats as a triple of whole beats, numerator and
// denominator, or as a plain number of beats.
double parseBeatTriple(const json& node) {
  if (node.is_number()) return node.get<double>();
  if (!node.is_array() || node.size() < 3) return 0.0;
  const auto component = [&node](std::size_t index) {
    return node.size() > index && node[index].is_number() ? node[index].get<double>() : 0.0;
  };
  return component(0) + component(1) / std::max(1.0, component(2));
}

std::vector<BpmSegment> parseBpmSegments(const json& node) {
  std::vector<BpmSegment> segments;
  if (!node.is_array()) return segments;
  for (const auto& item : node) {
    if (!item.is_object()) continue;
    BpmSegment segment;
    const auto beatIt = item.find("startTime");
    segment.startBeat = beatIt != item.end() ? parseBeatTriple(*beatIt) : 0.0;
    const auto bpmIt = item.find("bpm");
    segment.bpm = bpmIt != item.end() && bpmIt->is_number() ? bpmIt->get<double>() : 120.0;
    if (!(segment.bpm > 0.0)) segment.bpm = 120.0;
    segments.push_back(segment);
  }
  std::sort(segments.begin(), segments.end(),
      [](const BpmSegment& first, const BpmSegment& second) {
        return first.startBeat < second.startBeat;
      });
  // One forward pass turns each segment's start beat into seconds.
  double lastBeat = 0.0;
  double lastTime = 0.0;
  double beatTime = 0.5;
  for (auto& segment : segments) {
    const double deltaBeat = std::max(0.0, segment.startBeat - lastBeat);
    segment.startTime = lastTime + beatTime * deltaBeat;
    segment.beatTime = 60.0 / segment.bpm;
    lastBeat = segment.startBeat;
    lastTime = segment.startTime;
    beatTime = segment.beatTime;
  }
  return segments;
}

// Converts a beat into seconds by walking the segments from the end; the last segment
// at or before `beat` owns the rate.
double beatToSeconds(const std::vector<BpmSegment>& segments, double beat) {
  if (segments.empty()) return beat * 0.5;
  for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
    const bool last = (it + 1) == segments.rend();
    if (beat >= it->startBeat || last) {
      return it->startTime + (beat - it->startBeat) * it->beatTime;
    }
  }
  return beat * 0.5;
}

// Storyboard effect variables are a constant vector, a list of keyframes or the prpr
// object form `{ "values": [...], "events": [...] }`. Keyframes become eased tracks.
void readEffectVariables(const json& varsNode, game::PrprEffect& effect) {
  if (!varsNode.is_object()) return;
  for (auto it = varsNode.begin(); it != varsNode.end(); ++it) {
    const std::string& name = it.key();
    const json& value = it.value();
    game::PrprVariable variable;

    const auto readKeyframes = [&variable](const json& list) {
      for (const auto& key : list) {
        if (!key.is_object()) continue;
        game::LineEvent event;
        const auto timeIt = key.find("time");
        if (timeIt == key.end() || !timeIt->is_number()) continue;
        event.startTime = timeIt->get<double>();
        event.endTime = event.startTime;
        const auto valueIt = key.find("value");
        if (valueIt != key.end() && valueIt->is_number()) {
          event.start = valueIt->get<double>();
          event.end = event.start;
        }
        // A following key's time closes this event's span, so a keyframe is a step
        // until the track is paired up below.
        variable.events.push_back(event);
      }
      // Pair consecutive keys into spans and carry the easing of the earlier key.
      for (std::size_t index = 0; index + 1 < variable.events.size(); ++index) {
        auto& current = variable.events[index];
        const auto& next = variable.events[index + 1];
        current.endTime = next.startTime;
        current.end = next.start;
      }
      if (!variable.events.empty()) {
        variable.events.back().endTime = variable.events.back().startTime;
        variable.events.back().end = variable.events.back().start;
      }
    };

    if (value.is_number()) {
      variable.values = {static_cast<float>(value.get<double>())};
    } else if (value.is_array()) {
      bool onlyNumbers = true;
      for (const auto& element : value) {
        if (!element.is_number()) {
          onlyNumbers = false;
          break;
        }
      }
      if (onlyNumbers) {
        variable.values.clear();
        for (const auto& element : value) {
          variable.values.push_back(static_cast<float>(element.get<double>()));
        }
        if (variable.values.empty()) variable.values.push_back(0.0F);
      } else {
        readKeyframes(value);
      }
    } else if (value.is_object()) {
      if (const auto valuesIt = value.find("values");
          valuesIt != value.end() && valuesIt->is_array()) {
        variable.values.clear();
        for (const auto& element : *valuesIt) {
          if (element.is_number()) variable.values.push_back(static_cast<float>(element.get<double>()));
        }
        if (variable.values.empty()) variable.values.push_back(0.0F);
      }
      if (const auto eventsIt = value.find("events");
          eventsIt != value.end() && eventsIt->is_array()) {
        readKeyframes(*eventsIt);
      }
    }
    effect.variables[name] = std::move(variable);
  }
}
// Splits one CSV line, honouring quoted fields and doubled quotes.
std::vector<std::string> parseCsvLine(const std::string& line) {
  std::vector<std::string> cells;
  std::string cell;
  bool inQuotes = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (inQuotes) {
      if (character == '"') {
        if (index + 1 < line.size() && line[index + 1] == '"') {
          cell.push_back('"');
          ++index;
        } else {
          inQuotes = false;
        }
      } else {
        cell.push_back(character);
      }
    } else if (character == '"') {
      inQuotes = true;
    } else if (character == ',') {
      cells.push_back(cell);
      cell.clear();
    } else {
      cell.push_back(character);
    }
  }
  cells.push_back(cell);
  return cells;
}

std::string trimCopy(std::string text) {
  const auto notSpace = [](unsigned char value) { return !std::isspace(value); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
  text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
  return text;
}

std::string lowercaseCopy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
      [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  return text;
}

std::string stripExtension(std::string name) {
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name.resize(dot);
  return name;
}

// One row of `line.csv`: which line uses which texture and at what scale.
struct LineCsvRow {
  std::string chart;
  std::string image;
  int lineId{-1};
  float horz{1.0F};
  float vert{1.0F};
};

// Searches `start` and up to `maxDepth` parents for one of `names`.
std::filesystem::path findUpwards(std::filesystem::path start, int maxDepth,
    std::initializer_list<const char*> names) {
  for (int depth = 0; depth < maxDepth && !start.empty(); ++depth) {
    for (const char* name : names) {
      const auto candidate = start / name;
      std::error_code error;
      if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    if (!start.has_parent_path() || start.parent_path() == start) break;
    start = start.parent_path();
  }
  return {};
}

// Applies `line.csv`: per-line textures and scale overrides, matched to the chart by name.
void applyLineCsv(game::Chart& chart, const std::filesystem::path& chartPath) {
  if (chart.judgeLineList.empty()) return;
  const auto csvPath = findUpwards(chartPath.parent_path(), kExtraSearchDepth,
      {"line.csv", "Line.csv"});
  if (csvPath.empty()) return;

  std::ifstream csv(csvPath, std::ios::binary);
  if (!csv) return;
  std::string headerLine;
  if (!std::getline(csv, headerLine)) return;
  if (headerLine.size() >= 3 && static_cast<unsigned char>(headerLine[0]) == 0xEF &&
      static_cast<unsigned char>(headerLine[1]) == 0xBB &&
      static_cast<unsigned char>(headerLine[2]) == 0xBF) {
    headerLine.erase(0, 3);
  }
  const auto header = parseCsvLine(headerLine);
  std::unordered_map<std::string, std::size_t> columns;
  for (std::size_t index = 0; index < header.size(); ++index) {
    columns[trimCopy(header[index])] = index;
  }
  const auto cell = [&columns](const std::vector<std::string>& cells, const char* key)
      -> std::string {
    const auto it = columns.find(key);
    if (it == columns.end() || it->second >= cells.size()) return {};
    return trimCopy(cells[it->second]);
  };

  std::vector<LineCsvRow> rows;
  std::string line;
  while (std::getline(csv, line)) {
    if (trimCopy(line).empty()) continue;
    const auto cells = parseCsvLine(line);
    LineCsvRow row;
    row.chart = cell(cells, "Chart");
    row.image = cell(cells, "Image");
    const auto idText = cell(cells, "LineId");
    if (!idText.empty()) {
      try {
        row.lineId = std::stoi(idText);
      } catch (const std::exception&) {
        row.lineId = -1;
      }
    }
    const auto readFloat = [&cell, &cells](const char* key, float fallback) {
      const auto text = cell(cells, key);
      if (text.empty()) return fallback;
      try {
        const float value = std::stof(text);
        return std::isfinite(value) ? value : fallback;
      } catch (const std::exception&) {
        return fallback;
      }
    };
    row.horz = readFloat("Horz", 1.0F);
    row.vert = readFloat("Vert", 1.0F);
    rows.push_back(std::move(row));
  }
  if (rows.empty()) return;

  // Rows may be scoped to a chart file name; unscoped rows always apply.
  const auto chartName = chartPath.filename().string();
  const auto chartStem = stripExtension(chartName);
  std::vector<const LineCsvRow*> candidates;
  std::unordered_set<std::string> namesInCsv;
  for (const auto& row : rows) {
    if (!row.chart.empty()) namesInCsv.insert(lowercaseCopy(row.chart));
    if (row.chart.empty()) {
      candidates.push_back(&row);
      continue;
    }
    if (lowercaseCopy(row.chart) == lowercaseCopy(chartName) ||
        lowercaseCopy(stripExtension(row.chart)) == lowercaseCopy(chartStem)) {
      candidates.push_back(&row);
    }
  }
  // A file that only describes one chart applies to whichever chart is loaded.
  if (candidates.empty() && namesInCsv.size() == 1) {
    for (const auto& row : rows) candidates.push_back(&row);
  }
  if (candidates.empty()) return;

  const auto csvDirectory = csvPath.parent_path();
  const auto chartDirectory = chartPath.parent_path();
  for (const LineCsvRow* row : candidates) {
    if (row->lineId < 0 || static_cast<std::size_t>(row->lineId) >= chart.judgeLineList.size()) {
      continue;
    }
    auto& lineSlot = chart.judgeLineList[static_cast<std::size_t>(row->lineId)];
    if (lineSlot == nullptr) continue;
    auto& line = *lineSlot;

    if (!row->image.empty()) {
      std::filesystem::path texture = csvDirectory / row->image;
      std::error_code error;
      if (!std::filesystem::exists(texture, error)) texture = chartDirectory / row->image;
      line.texture = texture.string();
      line.useOfficialScale = true;
    }

    const float scaleX = (std::isfinite(row->horz) && row->horz != 0.0F) ? row->horz : 1.0F;
    const float scaleY = (std::isfinite(row->vert) && row->vert != 0.0F) ? row->vert : 1.0F;
    game::MoveEvent scale;
    scale.startTime = -1.0e9;
    scale.endTime = 1.0e9;
    scale.start = scaleX;
    scale.end = scaleX;
    scale.start2 = scaleY;
    scale.end2 = scaleY;
    line.judgeLineScaleEvents.clear();
    line.judgeLineScaleEvents.push_back(scale);
  }
}

// Applies `extra.json`: storyboard effects and videos.
void applyPrprExtra(game::Chart& chart, const std::filesystem::path& chartDirectory) {
  std::filesystem::path directory = chartDirectory;
  std::filesystem::path extraPath;
  for (int depth = 0; depth < kExtraSearchDepth && !directory.empty(); ++depth) {
    for (const char* name : {"extra.json", "Extra.json"}) {
      const auto candidate = directory / name;
      std::error_code error;
      if (std::filesystem::is_regular_file(candidate, error)) {
        extraPath = candidate;
        break;
      }
    }
    if (!extraPath.empty()) break;
    if (!directory.has_parent_path() || directory.parent_path() == directory) break;
    directory = directory.parent_path();
  }
  if (extraPath.empty()) return;

  chart.packDir = extraPath.parent_path();
  const auto text = readFile(extraPath);
  if (text.empty()) return;
  const auto root = json::parse(text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return;

  const auto bpmIt = root.find("bpm");
  const auto segments = bpmIt != root.end() ? parseBpmSegments(*bpmIt) : std::vector<BpmSegment>{};

  if (const auto effectsIt = root.find("effects");
      effectsIt != root.end() && effectsIt->is_array()) {
    for (const auto& item : *effectsIt) {
      if (!item.is_object()) continue;
      const auto shaderIt = item.find("shader");
      if (shaderIt == item.end() || !shaderIt->is_string()) continue;
      game::PrprEffect effect;
      effect.shader = shaderIt->get<std::string>();
      if (effect.shader.empty()) continue;

      const auto startIt = item.find("start");
      const auto endIt = item.find("end");
      const double startBeat = startIt != item.end() ? parseBeatTriple(*startIt) : 0.0;
      const double endBeat = endIt != item.end() ? parseBeatTriple(*endIt) : startBeat;
      effect.startTime = beatToSeconds(segments, startBeat);
      effect.endTime = beatToSeconds(segments, endBeat);
      if (effect.endTime < effect.startTime) effect.endTime = effect.startTime;

      if (const auto globalIt = item.find("global");
          globalIt != item.end() && globalIt->is_boolean()) {
        effect.global = globalIt->get<bool>();
      } else if (const auto isGlobalIt = item.find("isGlobal");
                 isGlobalIt != item.end() && isGlobalIt->is_boolean()) {
        effect.global = isGlobalIt->get<bool>();
      }

      if (const auto varsIt = item.find("vars"); varsIt != item.end()) {
        readEffectVariables(*varsIt, effect);
      }
      chart.prprEffects.push_back(std::move(effect));
    }
  }

  if (const auto videosIt = root.find("videos");
      videosIt != root.end() && videosIt->is_array()) {
    for (const auto& item : *videosIt) {
      if (!item.is_object()) continue;
      const auto pathIt = item.find("path");
      if (pathIt == item.end() || !pathIt->is_string()) continue;
      game::PrprVideo video;
      video.path = pathIt->get<std::string>();
      if (video.path.empty()) continue;
      const auto startIt = item.find("start");
      const auto endIt = item.find("end");
      const double startBeat = startIt != item.end() ? parseBeatTriple(*startIt) : 0.0;
      const double endBeat = endIt != item.end() ? parseBeatTriple(*endIt) : startBeat;
      video.startTime = beatToSeconds(segments, startBeat);
      video.endTime = beatToSeconds(segments, endBeat);
      if (const auto alphaIt = item.find("alpha"); alphaIt != item.end() && alphaIt->is_number()) {
        video.alpha = static_cast<float>(alphaIt->get<double>());
      }
      if (const auto loopIt = item.find("loop"); loopIt != item.end() && loopIt->is_boolean()) {
        video.loop = loopIt->get<bool>();
      }
      chart.prprVideos.push_back(std::move(video));
    }
  }
}

} // namespace

std::unique_ptr<game::Chart> ChartLoader::loadFromFile(const std::filesystem::path& path,
    const std::filesystem::path& assetRoot, const std::string& songName) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open chart: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  auto content = buffer.str();
  const ChartFormat format = detectFormat(content);
  game::Chart chart;
  switch (format) {
    case ChartFormat::Official:
      chart = OfficialChartParser::parse(std::string_view(content));
      break;
    case ChartFormat::Rpe:
      chart = RpeChartParser::parse(content, path.parent_path());
      break;
    case ChartFormat::JsonObject:
      // A JSON object with none of the identifying headers binds like a plain chart: the
      // format version stays 0 and no official-parser coercion applies.
      chart = PlainChartParser::parse(std::string_view(content));
      break;
    case ChartFormat::PhiEdit:
    default:
      // The plain-text editor format.
      if (looksLikeJsonObject(content)) chart = PlainChartParser::parse(std::string_view(content));
      else chart = PecChartParser::parse(content);
      break;
  }
  chart.packDir = path.parent_path();
  chart.normalizedDigest = normalizedDigest(content);
  chart.songName = songName;
  // A CSV beside the chart pack overrides per-line textures and scales. It only applies to
  // the official and level-editor formats, which are the ones that address lines by index.
  if (format == ChartFormat::Official || format == ChartFormat::JsonObject ||
      format == ChartFormat::PhiEdit) {
    applyLineCsv(chart, path);
  }
  applyPrprExtra(chart, path.parent_path());
  // A pack's metadata may switch the attach-UI correction off; the reference applies it for
  // every chart format, after the chart and its storyboard companions have been read.
  applyAttachUiFixFlag(chart, path);

  // A level effect replaces the chart's own storyboard list when one is selected; the
  // effect table lives beside the executable, not in the chart pack.
  if (const auto table = game::OfficialLevelEffects::load(assetRoot)) {
    if (const auto effect = game::OfficialLevelEffects::forChart(*table, chart.normalizedDigest,
            chart.songName)) {
      chart.levelEffects = effect;
      auto built = effect->buildPrprEffects();
      chart.prprEffects.insert(chart.prprEffects.end(), built.begin(), built.end());
    }
  }
  return std::make_unique<game::Chart>(std::move(chart));
}
} // namespace rechigros::chart
