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
#include <array>
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
// How far a package-relative asset reference may climb before it is abandoned.
constexpr int kResourceSearchDepth = 4;
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
std::string normalizedDigest(const std::string_view content) {
  std::size_t start = 0;
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF) {
    start = 3;
  }
  // Copy the kept bytes into a staging window and hash it a block at a time. Feeding the
  // digest one byte per call made this the most expensive step of loading a chart: a chart
  // is tens of megabytes, and the digest is taken on every load.
  std::array<char, 64 * 1024> window{};
  std::size_t filled = 0;
  Md5 digest;
  for (std::size_t index = start; index < content.size(); ++index) {
    const char character = content[index];
    if (character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
        character == '\f' || character == '\v') {
      continue;
    }
    window[filled++] = character;
    if (filled == window.size()) {
      digest.update(window.data(), filled);
      filled = 0;
    }
  }
  if (filled > 0) digest.update(window.data(), filled);
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
  const auto component = [&node](std::size_t index, double fallback) {
    return node.size() > index && node[index].is_number() ? node[index].get<double>() : fallback;
  };
  double denominator = component(2, 1.0);
  if (denominator == 0.0) denominator = 1.0;
  return component(0, 0.0) + component(1, 0.0) / denominator;
}

// Reads the storyboard BPM list. It may be an array of segments, a single segment object,
// or a bare number holding the BPM for the whole chart.
std::vector<BpmSegment> parseBpmSegments(const json& node) {
  std::vector<BpmSegment> segments;
  const auto append = [&segments](const json& item) {
    if (!item.is_object()) return;
    BpmSegment segment;
    const auto beatIt = item.find("time");
    const json* beatNode = beatIt != item.end() ? &*beatIt : nullptr;
    if (beatNode == nullptr) {
      const auto startIt = item.find("startTime");
      if (startIt != item.end()) beatNode = &*startIt;
    }
    segment.startBeat = beatNode != nullptr ? parseBeatTriple(*beatNode) : 0.0;
    const auto bpmIt = item.find("bpm");
    segment.bpm = bpmIt != item.end() && bpmIt->is_number() ? bpmIt->get<double>() : 120.0;
    segments.push_back(segment);
  };

  if (node.is_array()) {
    for (const auto& item : node) append(item);
  } else if (node.is_object()) {
    append(node);
  } else if (node.is_number()) {
    BpmSegment segment;
    segment.startBeat = 0.0;
    segment.bpm = node.get<double>();
    segments.push_back(segment);
  }

  std::stable_sort(segments.begin(), segments.end(),
      [](const BpmSegment& first, const BpmSegment& second) {
        return first.startBeat < second.startBeat;
      });
  // One forward pass turns each segment's start beat into seconds. A zero BPM keeps the
  // default beat length; a negative one is used as authored, which the reference allows.
  double lastBeat = 0.0;
  double lastTime = 0.0;
  double beatTime = 0.5;
  for (auto& segment : segments) {
    const double deltaBeat = std::max(0.0, segment.startBeat - lastBeat);
    segment.startTime = lastTime + beatTime * deltaBeat;
    segment.beatTime = 60.0 / (segment.bpm == 0.0 ? 120.0 : segment.bpm);
    lastBeat = segment.startBeat;
    lastTime = segment.startTime;
    beatTime = segment.beatTime;
  }
  return segments;
}

// Converts a beat into seconds using the segment at or before `beat`. Without any segment
// the storyboard falls back to a flat half-second beat.
double beatToSeconds(const std::vector<BpmSegment>& segments, double beat) {
  for (const auto& segment : segments) {
    if (segment.startBeat > beat) continue;
    return segment.startTime + (beat - segment.startBeat) * segment.beatTime;
  }
  return beat * 0.5;
}

// Storyboard effect variables are a constant vector, a list of keyframes or the prpr
// First matching member of `node`, or nullptr when none of the spellings is present.
const json* findFirst(const json& node, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const auto it = node.find(key);
    if (it != node.end()) return &*it;
  }
  return nullptr;
}

// Effect variables are either a constant (number or numeric array) or a list of eased
// spans. Each span carries its own `startTime`/`endTime` beat triples, so a key is a full
// interval rather than a point that has to be paired with its neighbour.
void readEffectVariables(const json& varsNode, game::PrprEffect& effect,
    const std::vector<BpmSegment>& segments) {
  if (!varsNode.is_object()) return;
  const auto readNumber = [](const json& node, double fallback) {
    return node.is_number() ? node.get<double>() : fallback;
  };

  for (auto it = varsNode.begin(); it != varsNode.end(); ++it) {
    const std::string& name = it.key();
    const json& value = it.value();
    if (value.is_null()) continue;
    game::PrprVariable variable;

    const auto readSpans = [&variable, &segments, &readNumber](const json& list) {
      for (const auto& key : list) {
        if (!key.is_object()) continue;
        const json* startTimeNode = findFirst(key, {"startTime", "start"});
        const json* endTimeNode = findFirst(key, {"endTime", "end"});
        if (startTimeNode == nullptr || endTimeNode == nullptr) continue;

        game::LineEvent event;
        event.startTime = beatToSeconds(segments, parseBeatTriple(*startTimeNode));
        event.endTime = beatToSeconds(segments, parseBeatTriple(*endTimeNode));
        const auto valueIt = key.find("start");
        event.start = valueIt != key.end() ? readNumber(*valueIt, 0.0) : 0.0;
        const auto endIt = key.find("end");
        event.end = endIt != key.end() ? readNumber(*endIt, event.start) : event.start;
        event.type = std::max(0, static_cast<int>(readNumber(
            key.contains("easingType") ? key["easingType"] : json(), 1.0)) - 1);
        const auto leftIt = key.find("easingLeft");
        event.left = leftIt != key.end() ? static_cast<float>(readNumber(*leftIt, 0.0)) : 0.0F;
        const auto rightIt = key.find("easingRight");
        event.right = rightIt != key.end() ? static_cast<float>(readNumber(*rightIt, 1.0)) : 1.0F;
        const auto bezierIt = key.find("bezier");
        event.bezier = bezierIt != key.end() && static_cast<int>(readNumber(*bezierIt, 0.0)) == 1;
        if (event.bezier) {
          if (const auto pointsIt = key.find("bezierPoints");
              pointsIt != key.end() && pointsIt->is_array() && pointsIt->size() >= 4) {
            event.bezierPoints = {
                static_cast<float>(readNumber((*pointsIt)[0], 0.0)),
                static_cast<float>(readNumber((*pointsIt)[1], 0.0)),
                static_cast<float>(readNumber((*pointsIt)[2], 1.0)),
                static_cast<float>(readNumber((*pointsIt)[3], 1.0))};
          }
        }
        variable.events.push_back(std::move(event));
      }
      std::stable_sort(variable.events.begin(), variable.events.end(),
          [](const game::LineEvent& first, const game::LineEvent& second) {
            return first.startTime < second.startTime;
          });
    };

    if (value.is_number()) {
      variable.values = {static_cast<float>(value.get<double>())};
    } else if (value.is_array()) {
      bool onlyNumbers = !value.empty();
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
        if (variable.values.size() == 1) {
          // A single component is a scalar, not a one-element vector.
        }
      } else {
        readSpans(value);
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

// Searches `start` and its first `maxDepth` parents for one of `names`, so a depth of six
// inspects seven directories in total.
std::filesystem::path findUpwards(std::filesystem::path start, int maxDepth,
    std::initializer_list<const char*> names) {
  for (int depth = 0; depth <= maxDepth && !start.empty(); ++depth) {
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

// Resolves a package-relative asset by walking up from `base`; the first directory that
// actually holds the file wins.
std::filesystem::path resolveUpwards(std::filesystem::path base, const std::filesystem::path& relative,
    int maxDepth) {
  for (int depth = 0; depth <= maxDepth && !base.empty(); ++depth) {
    const auto candidate = base / relative;
    std::error_code error;
    if (std::filesystem::exists(candidate, error)) return candidate;
    if (!base.has_parent_path() || base.parent_path() == base) break;
    base = base.parent_path();
  }
  return {};
}

// Resolves an asset reference against the primary base and then the secondary one. An
// absolute reference that exists is used as written.
std::filesystem::path resolveResource(const std::filesystem::path& primaryBase,
    const std::filesystem::path& secondaryBase, const std::string& relativePath) {
  if (relativePath.empty()) return {};
  const std::filesystem::path given(relativePath);
  std::error_code error;
  if (given.is_absolute() && std::filesystem::exists(given, error)) return given;
  if (auto found = resolveUpwards(primaryBase, given, kResourceSearchDepth); !found.empty()) {
    return found;
  }
  return resolveUpwards(secondaryBase, given, kResourceSearchDepth);
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

    // A row only overrides the texture when the referenced file can actually be resolved;
    // an unresolvable reference leaves the line's own texture untouched while its scale
    // override still applies.
    if (!row->image.empty()) {
      const auto texture = resolveResource(csvDirectory, chartDirectory, row->image);
      if (!texture.empty()) {
        line.texture = texture.string();
        line.useOfficialScale = true;
      }
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
// Video placement: a number, or one of the named forms. Anything else means crop-centre.
int parseVideoScaleType(const json& node) {
  if (node.is_number()) return static_cast<int>(node.get<double>());
  if (!node.is_string()) return game::PrprVideo::CropCenter;
  std::string text = node.get<std::string>();
  std::transform(text.begin(), text.end(), text.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  text.erase(std::remove_if(text.begin(), text.end(),
                 [](char c) { return c == '_' || c == '-'; }),
      text.end());
  if (text == "inside" || text == "contain") return game::PrprVideo::Inside;
  if (text == "fit" || text == "stretch") return game::PrprVideo::Fit;
  return game::PrprVideo::CropCenter;
}

// Reads one scalar-or-track video property. A bare number is the constant value; an array
// of numbers uses the first entry; an array of objects is a keyframed track whose keys carry
// `startTime`/`endTime` beat triples and `start`/`end` values.
void readVideoTrack(const json& owner, const char* name, float fallback,
    const std::vector<BpmSegment>& segments, float& defaultValue,
    std::vector<game::LineEvent>& events) {
  defaultValue = fallback;
  const auto it = owner.find(name);
  if (it == owner.end() || it->is_null()) return;
  if (it->is_number()) {
    defaultValue = static_cast<float>(it->get<double>());
    return;
  }
  if (!it->is_array() || it->empty()) return;

  const bool allNumbers = std::all_of(it->begin(), it->end(),
      [](const json& value) { return value.is_number(); });
  if (allNumbers) {
    defaultValue = static_cast<float>(it->front().get<double>());
    return;
  }

  for (const auto& key : *it) {
    if (!key.is_object()) continue;
    const json* startTimeNode = findFirst(key, {"startTime", "start"});
    const json* endTimeNode = findFirst(key, {"endTime", "end"});
    if (startTimeNode == nullptr || endTimeNode == nullptr) continue;
    if (!startTimeNode->is_array() || !endTimeNode->is_array()) continue;

    game::LineEvent event;
    event.startTime = beatToSeconds(segments, parseBeatTriple(*startTimeNode));
    event.endTime = beatToSeconds(segments, parseBeatTriple(*endTimeNode));
    const auto valueIt = key.find("start");
    event.start = valueIt != key.end() && valueIt->is_number()
        ? valueIt->get<double>() : static_cast<double>(defaultValue);
    const auto endIt = key.find("end");
    event.end = endIt != key.end() && endIt->is_number()
        ? endIt->get<double>() : event.start;
    const auto easingIt = key.find("easingType");
    event.type = easingIt != key.end() && easingIt->is_number()
        ? std::max(0, static_cast<int>(easingIt->get<double>()) - 1) : 0;
    const auto leftIt = key.find("easingLeft");
    event.left = leftIt != key.end() && leftIt->is_number()
        ? static_cast<float>(leftIt->get<double>()) : 0.0F;
    const auto rightIt = key.find("easingRight");
    event.right = rightIt != key.end() && rightIt->is_number()
        ? static_cast<float>(rightIt->get<double>()) : 1.0F;
    events.push_back(event);
  }
  std::stable_sort(events.begin(), events.end(),
      [](const game::LineEvent& a, const game::LineEvent& b) { return a.startTime < b.startTime; });

  // A track that starts after time zero holds its default from the beginning, so the value
  // does not jump when the first key is reached.
  if (!events.empty() && events.front().startTime > 1.0e-9) {
    game::LineEvent lead;
    lead.startTime = 0.0;
    lead.endTime = events.front().startTime;
    lead.start = defaultValue;
    lead.end = defaultValue;
    events.insert(events.begin(), lead);
  }
}

// Whether a shader reference names a file rather than a built-in preset.
bool isShaderFileName(const std::string& shader) {
  std::string lowered = lowercase(trimSpaces(shader));
  return lowered.size() >= 5 && (lowered.compare(lowered.size() - 5, 5, ".glsl") == 0
      || lowered.compare(lowered.size() - 3, 3, ".fs") == 0
      || lowered.compare(lowered.size() - 5, 5, ".frag") == 0);
}

// Reads the source of every file-backed shader an effect refers to. A reference is either
// rooted ("/shader.glsl") or a bare file name; preset names resolve to nothing and keep
// using the built-in program.
void loadShaderSources(game::Chart& chart, const std::filesystem::path& chartDirectory) {
  if (chart.prprEffects.empty()) return;
  for (const auto& effect : chart.prprEffects) {
    std::string relative;
    if (!effect.shader.empty() && effect.shader.front() == '/' && effect.shader.size() > 1) {
      relative = effect.shader.substr(1);
    } else if (isShaderFileName(effect.shader)) {
      relative = effect.shader;
    } else {
      continue;
    }
    if (chart.prprShaderSources.contains(effect.shader)) continue;
    const auto shaderFile = resolveResource(chart.packDir, chartDirectory, relative);
    if (shaderFile.empty() || !std::filesystem::is_regular_file(shaderFile)) continue;
    auto source = readFile(shaderFile);
    if (source.empty()) continue;
    chart.prprShaderSources.emplace(effect.shader, std::move(source));
  }
}

void applyPrprExtra(game::Chart& chart, const std::filesystem::path& chartDirectory) {
  std::filesystem::path directory = chartDirectory;
  std::filesystem::path extraPath;
  for (int depth = 0; depth <= kExtraSearchDepth && !directory.empty(); ++depth) {
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

      // Effect spans accept the `start`/`end` names first, then the `startTime`/`endTime`
      // pair; a missing end collapses onto the start.
      const json* startIt = findFirst(item, {"start", "startTime"});
      const json* endIt = findFirst(item, {"end", "endTime"});
      const double startBeat = startIt != nullptr ? parseBeatTriple(*startIt) : 0.0;
      const double endBeat = endIt != nullptr ? parseBeatTriple(*endIt) : startBeat;
      effect.startTime = beatToSeconds(segments, startBeat);
      effect.endTime = beatToSeconds(segments, endBeat);

      if (const auto globalIt = item.find("global");
          globalIt != item.end() && globalIt->is_boolean()) {
        effect.global = globalIt->get<bool>();
      } else if (const auto isGlobalIt = item.find("isGlobal");
                 isGlobalIt != item.end() && isGlobalIt->is_boolean()) {
        effect.global = isGlobalIt->get<bool>();
      }

      if (const auto varsIt = item.find("vars"); varsIt != item.end()) {
        readEffectVariables(*varsIt, effect, segments);
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

      // The start may be written as `time` or `startTime`; only the array (beat triple)
      // form is a time, everything else falls back to the first segment.
      const json* timeNode = findFirst(item, {"time", "startTime"});
      const double startBeat =
          timeNode != nullptr && timeNode->is_array() ? parseBeatTriple(*timeNode) : 0.0;
      video.startTimeSec = beatToSeconds(segments, startBeat);

      const auto scaleIt = item.find("scale");
      video.scaleType = scaleIt != item.end()
          ? game::PrprVideo::sanitizeScaleType(parseVideoScaleType(*scaleIt))
          : game::PrprVideo::CropCenter;

      readVideoTrack(item, "alpha", 1.0F, segments, video.alphaDefault, video.alphaEvents);
      readVideoTrack(item, "dim", 0.0F, segments, video.dimDefault, video.dimEvents);

      const auto attachIt = item.find("attach");
      if (attachIt != item.end()) {
        if (attachIt->is_number()) {
          video.attachLineIndex = std::max(-1, static_cast<int>(attachIt->get<double>()));
        } else if (attachIt->is_object()) {
          const auto lineIt = attachIt->find("line");
          if (lineIt != attachIt->end() && lineIt->is_number()) {
            video.attachLineIndex = std::max(-1, static_cast<int>(lineIt->get<double>()));
          }
        }
      }
      chart.prprVideos.push_back(std::move(video));
    }
  }
}

} // namespace

std::unique_ptr<game::Chart> ChartLoader::loadFromFile(const std::filesystem::path& path,
    const std::filesystem::path& assetRoot, const std::string& songName, bool storyMode) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open chart: " + path.string());
  // Read straight into one buffer. Streaming through a string stream copied the whole chart a
  // second time, which on a dense chart is tens of megabytes of avoidable copying.
  std::string content;
  {
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0, std::ios::beg);
    if (length > 0) content.resize(static_cast<std::size_t>(length));
    if (!content.empty()) {
      input.read(content.data(), static_cast<std::streamsize>(content.size()));
      content.resize(static_cast<std::size_t>(input.gcount()));
    }
  }
  if (content.empty()) throw std::runtime_error("Cannot read chart: " + path.string());
  const ChartFormat format = detectFormat(content);
  game::Chart chart;
  switch (format) {
    case ChartFormat::Official:
      // The large arrays carry nearly all of a dense chart's bytes, so they are parsed on a
      // worker pool; small documents take the regular path inside parseParallel.
      chart = OfficialChartParser::parseParallel(std::string_view(content));
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
  // The pack root starts at the chart's own directory and moves to the directory holding
  // `extra.json` when one is found, which is the anchor the storyboard paths resolve from.
  chart.packDir = path.parent_path();
  chart.normalizedDigest = normalizedDigest(content);
  chart.songName = songName;
  // A CSV beside the chart pack overrides per-line textures and scales. It only applies to
  // the official and level-editor formats, which are the ones that address lines by index.
  if (format == ChartFormat::Official || format == ChartFormat::JsonObject ||
      format == ChartFormat::PhiEdit) {
    applyLineCsv(chart, path);
  }
  // `extra.json` may relocate the pack root, so it runs after the CSV pass and its result
  // must not be overwritten afterwards.
  applyPrprExtra(chart, path.parent_path());
  // A pack's metadata may switch the attach-UI correction off; this applies for every chart
  // format, after the chart and its storyboard companions have been read.
  applyAttachUiFixFlag(chart, path);

  // File-backed storyboard shaders carry their source in the pack; presets use the built-in
  // programs, so a missing source is not an error.
  loadShaderSources(chart, path.parent_path());

  // A level effect replaces the chart's own storyboard list when one is selected; the
  // effect table lives beside the executable, not in the chart pack. Its passes are appended
  // here rather than at play time because the effect table is only reachable from the
  // loader; the renderer still skips the whole storyboard chain when shaders are disabled,
  // which is the same suppression the selection performs at play time.
  if (const auto table = game::OfficialLevelEffects::load(assetRoot)) {
    if (const auto effect = game::OfficialLevelEffects::forChart(*table, chart.normalizedDigest,
            chart.songName, storyMode)) {
      chart.levelEffects = effect;
      auto built = effect->buildPrprEffects();
      chart.prprEffects.insert(chart.prprEffects.end(), built.begin(), built.end());
    }
  }
  return std::make_unique<game::Chart>(std::move(chart));
}
} // namespace rechigros::chart
