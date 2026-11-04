#include "rechigros/app/InfoFile.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace rechigros::app {
namespace {

using Json = nlohmann::json;

constexpr char kNormalSpace = ' ';

// How much of a chart file is read when looking for its metadata block. The block is
// authored at the head of the document, so a generous window covers every real chart and a
// document that puts it elsewhere falls back to reading the whole file.
constexpr std::size_t kChartMetaHeadBytes = 256 * 1024;
// How far past the head window the metadata-block search looks before giving up. Every format
// this reads puts the block ahead of the note data, so this is a bound on a search that only
// ever walks the document's opening.
constexpr std::size_t kChartMetaSearchBytes = 1024 * 1024;

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

// Several editors write spacing as a typographic space rather than a plain one. The
// interface font has no glyph for those, so they would draw as empty boxes; every such
// code point becomes a plain space instead.
std::string normaliseUnicodeSpaces(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
      result.push_back(static_cast<char>(lead));
      ++index;
      continue;
    }
    // Decode the code point length from the lead byte.
    std::size_t length = 0;
    std::uint32_t code = 0;
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      code = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      code = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      code = lead & 0x07U;
    } else {
      result.push_back(static_cast<char>(lead));
      ++index;
      continue;
    }
    bool valid = index + length <= text.size();
    for (std::size_t offset = 1; valid && offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(text[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        valid = false;
        break;
      }
      code = (code << 6U) | (continuation & 0x3FU);
    }
    if (!valid) {
      result.push_back(static_cast<char>(lead));
      ++index;
      continue;
    }
    const bool isSpace = code == 0x00A0U || code == 0x2000U || code == 0x2001U ||
        code == 0x2002U || code == 0x2003U || code == 0x2004U || code == 0x2005U ||
        code == 0x2006U || code == 0x2007U || code == 0x2008U || code == 0x2009U ||
        code == 0x200AU || code == 0x200BU || code == 0x202FU || code == 0x3000U;
    if (isSpace) {
      result.push_back(' ');
    } else {
      result.append(text, index, length);
    }
    index += length;
  }
  return result;
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

// Codepoints trimmed from the edges of a difficulty label: the ASCII control separators plus
// every Unicode separator except the non-breaking ones (U+00A0, U+2007, U+202F), which are
// part of the text rather than padding.
bool isDifficultySpaceCodePoint(std::uint32_t code) {
  if (code == ' ' || (code >= 0x09U && code <= 0x0DU) || (code >= 0x1CU && code <= 0x1FU)) {
    return true;
  }
  if (code >= 0x2000U && code <= 0x200AU) return true;
  return code == 0x1680U || code == 0x2028U || code == 0x2029U || code == 0x205FU ||
      code == 0x3000U;
}

std::size_t nextCodePointLength(const std::string& text, std::size_t index) {
  const auto lead = static_cast<unsigned char>(text[index]);
  if ((lead & 0x80U) == 0) return 1;
  if ((lead & 0xE0U) == 0xC0U && index + 1 < text.size()) return 2;
  if ((lead & 0xF0U) == 0xE0U && index + 2 < text.size()) return 3;
  if ((lead & 0xF8U) == 0xF0U && index + 3 < text.size()) return 4;
  return 1;
}

std::uint32_t decodeCodePoint(const std::string& text, std::size_t index, std::size_t length) {
  if (length <= 1) return static_cast<unsigned char>(text[index]);
  std::uint32_t code = 0;
  switch (length) {
    case 2: code = static_cast<unsigned char>(text[index]) & 0x1FU; break;
    case 3: code = static_cast<unsigned char>(text[index]) & 0x0FU; break;
    default: code = static_cast<unsigned char>(text[index]) & 0x07U; break;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    code = (code << 6U) | (static_cast<unsigned char>(text[index + offset]) & 0x3FU);
  }
  return code;
}

std::string trimDifficultyWhitespace(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t length = nextCodePointLength(text, begin);
    if (!isDifficultySpaceCodePoint(decodeCodePoint(text, begin, length))) break;
    begin += length;
  }
  std::size_t end = text.size();
  while (end > begin) {
    // Step back to the start of the last codepoint so multi-byte spaces are trimmed too.
    std::size_t start = end - 1;
    while (start > begin && (static_cast<unsigned char>(text[start]) & 0xC0U) == 0x80U) --start;
    const std::size_t length = end - start;
    if (!isDifficultySpaceCodePoint(decodeCodePoint(text, start, length))) break;
    end = start;
  }
  return text.substr(begin, end - begin);
}

// Whether `token` appears in `text` delimited by anything outside [A-Z0-9]. `text` is the
// upper-cased file stem, so only capitals and digits can continue a token.
bool containsToken(const std::string& text, const std::string& token) {
  const auto isTokenCharacter = [](char character) {
    return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
  };
  std::size_t position = text.find(token);
  while (position != std::string::npos) {
    const bool leftOk = position == 0 || !isTokenCharacter(text[position - 1]);
    const auto after = position + token.size();
    const bool rightOk = after >= text.size() || !isTokenCharacter(text[after]);
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

// Reads a bounded window that starts at `offset`. Used to look for a marker just past the head
// window without reading the rest of a document that may be tens of megabytes.
std::string readRange(const std::filesystem::path& file, std::size_t offset, std::size_t maximum) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) return {};
  std::string window(maximum, '\0');
  input.read(window.data(), static_cast<std::streamsize>(window.size()));
  window.resize(static_cast<std::size_t>(input.gcount()));
  return window;
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
      if (iterator != lower.end() && !iterator->second.empty()) return iterator->second;
    }
    return std::string{};
  };

  // The key sets are the reference key/value reader's alias table, reproduced exactly: it is
  // deliberately narrower than the JSON reader's, so a key the reference ignores must not
  // change a label here either.
  info.name = find({"name"});
  info.music = find({"music", "song"});
  info.chart = find({"chart"});
  info.illustration = find({"illustration", "image", "picture"});
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
    bool keyValueText) {
  std::unordered_map<std::string, std::string> values;
  bool firstPhysicalLine = true;
  for (const auto& line : lines) {
    std::string trimmed = line;
    const auto firstNonSpace = trimmed.find_first_not_of(" \t\r");
    if (firstNonSpace == std::string::npos) {
      // The reference key/value text reader consumes its "first line" flag on the first
      // physical line even when that line is empty.
      if (keyValueText) firstPhysicalLine = false;
      continue;
    }
    trimmed = trimmed.substr(firstNonSpace);
    if (keyValueText) {
      // Only the first physical line may be a comment; later lines are parsed as entries.
      if (firstPhysicalLine) {
        firstPhysicalLine = false;
        if (trimmed.front() == '#') continue;
      }
    } else if (trimmed.front() == '#') {
      continue;
    }
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

// Jackson's JsonNode.asText() for value nodes: strings verbatim, integers in decimal,
// floating point numbers in their plain JSON spelling, booleans as "true"/"false".
// Container and null nodes have no text representation.
std::string jsonValueText(const Json& node) {
  if (node.is_string()) return node.get<std::string>();
  if (node.is_boolean()) return node.get<bool>() ? "true" : "false";
  if (node.is_number()) return node.dump();
  return {};
}

// Quotes unquoted object field names, the relaxed syntax the reference JSON mapper
// enables. Only called after the strict parser rejected the document.
std::string quoteUnquotedFieldNames(const std::string& text) {
  struct Range {
    std::size_t begin;
    std::size_t end;
  };
  std::vector<Range> ranges;
  bool inString = false;
  bool escaped = false;
  std::size_t previousSignificant = std::string::npos;
  std::size_t index = 0;
  while (index < text.size()) {
    const char character = text[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        inString = false;
      }
      previousSignificant = index;
      ++index;
      continue;
    }
    if (character == '"') {
      inString = true;
      previousSignificant = index;
      ++index;
      continue;
    }
    const bool afterOpening = previousSignificant != std::string::npos
        && (text[previousSignificant] == '{' || text[previousSignificant] == ',');
    const bool identifierStart = (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z') || character == '_' || character == '$';
    if (afterOpening && identifierStart) {
      std::size_t end = index + 1;
      while (end < text.size()) {
        const char next = text[end];
        const bool identifier =
            (next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')
            || (next >= '0' && next <= '9') || next == '_' || next == '$';
        if (!identifier) break;
        ++end;
      }
      std::size_t after = end;
      while (after < text.size()
          && (text[after] == ' ' || text[after] == '\t' || text[after] == '\r'
              || text[after] == '\n')) {
        ++after;
      }
      if (after < text.size() && text[after] == ':') {
        ranges.push_back(Range{index, end});
        previousSignificant = after;
        index = after;
        continue;
      }
    }
    if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
      previousSignificant = index;
    }
    ++index;
  }
  if (ranges.empty()) return text;
  std::string out;
  out.reserve(text.size() + ranges.size() * 2);
  std::size_t cursor = 0;
  for (const auto& range : ranges) {
    out.append(text, cursor, range.begin - cursor);
    out.push_back('"');
    out.append(text, range.begin, range.end - range.begin);
    out.push_back('"');
    cursor = range.end;
  }
  out.append(text, cursor, text.size() - cursor);
  return out;
}

// Parses an object document, retrying with quoted field names for the relaxed syntax.
Json parseJsonObject(const std::string& content) {
  Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded()) {
    const std::string quoted = quoteUnquotedFieldNames(content);
    if (quoted != content) root = Json::parse(quoted, nullptr, false);
  }
  if (root.is_discarded() || !root.is_object()) return Json::object();
  return root;
}

ChartInfo readJsonInfo(const std::filesystem::path& file) {
  ChartInfo info;
  const auto content = platform::readTextFile(file);
  if (content.empty()) return info;
  const Json root = parseJsonObject(content);
  if (root.empty()) return info;

  const auto getString = [&root](std::initializer_list<const char*> keys) -> std::string {
    for (const char* key : keys) {
      const auto iterator = root.find(key);
      if (iterator == root.end() || iterator->is_null()) continue;
      if (iterator->is_array() || iterator->is_object()) continue;
      const std::string value = jsonValueText(*iterator);
      if (!value.empty()) return value;
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

  // The reference integer reader accepts numeric nodes (truncating fractions) and textual
  // node values; a value it cannot parse falls through to the next alias.
  for (const char* key : {"chartOffsetMs", "chart_offset_ms", "chartOffset", "offsetMs"}) {
    const auto iterator = root.find(key);
    if (iterator == root.end() || iterator->is_null()) continue;
    if (iterator->is_number()) {
      const double number = iterator->get<double>();
      info.chartOffsetMs = static_cast<int>(std::clamp(number, -2147483648.0, 2147483647.0));
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
  // The reference boolean reader accepts booleans, numbers (non-zero is true) and the
  // textual forms "true"/"1"/"false"/"0".
  for (const char* key : {"useAttachUiFix", "use_attach_ui_fix"}) {
    const auto iterator = root.find(key);
    if (iterator == root.end() || iterator->is_null()) continue;
    if (iterator->is_boolean()) {
      info.useAttachUiFix = iterator->get<bool>();
      info.hasUseAttachUiFix = true;
      break;
    }
    if (iterator->is_number()) {
      info.useAttachUiFix = !(iterator->get<double>() == 0.0);
      info.hasUseAttachUiFix = true;
      break;
    }
    if (iterator->is_string()) {
      std::string text = iterator->get<std::string>();
      const auto first = text.find_first_not_of(" \t\r\n");
      const auto last = text.find_last_not_of(" \t\r\n");
      if (first == std::string::npos) continue;
      text = toLower(text.substr(first, last - first + 1));
      if (text == "true" || text == "1") {
        info.useAttachUiFix = true;
        info.hasUseAttachUiFix = true;
        break;
      }
      if (text == "false" || text == "0") {
        info.useAttachUiFix = false;
        info.hasUseAttachUiFix = true;
        break;
      }
    }
  }
  return info;
}

std::string getNestedString(const Json& object, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->is_null()) continue;
    if (iterator->is_array() || iterator->is_object()) continue;
    const std::string value = jsonValueText(*iterator);
    if (!value.empty()) return value;
  }
  return {};
}

} // namespace

std::string normalizeDifficultyText(const std::string& text) {
  if (text.empty()) return {};
  // The reference collapses runs of ASCII spaces and then turns every remaining ASCII space
  // into an EN SPACE (U+2002). The typographic space variants editors write are mapped to
  // ASCII first, so all of them end up as the one separator the interface font can draw.
  const std::string ascii = normaliseUnicodeSpaces(text);
  std::string collapsed;
  collapsed.reserve(ascii.size());
  bool previousWasSpace = false;
  for (const char character : ascii) {
    if (character == ' ') {
      if (previousWasSpace) continue;
      previousWasSpace = true;
    } else {
      previousWasSpace = false;
    }
    collapsed.push_back(character);
  }
  std::string result;
  result.reserve(collapsed.size() + 8);
  for (const char character : collapsed) {
    // U+2002 EN SPACE, encoded as UTF-8, is the separator the reference labels use.
    if (character == ' ') {
      result += "\xE2\x80\x82";
    } else {
      result.push_back(character);
    }
  }
  return result;
}

std::string inferDifficultyPrefix(const std::filesystem::path& chartFile) {
  if (chartFile.empty()) return {};
  std::string base = platform::pathToUtf8(chartFile.stem());
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
  if (info.difficulty.empty()) return std::string("SP\xE2\x80\x82Lv.?");
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
  // Only a metadata block is wanted here, and a chart can be tens of megabytes. Parsing the
  // whole document for it made building the library list the single most expensive step of a
  // launch: every installed chart was read end to end on every start.
  //
  // Two cheap tests come first, both of which settle an official-format chart without parsing
  // anything. Such a chart carries no metadata block at all -- its fields come from the pack's
  // own info file -- so scanning it for one can only ever fail slowly.
  {
    const auto head = readHead(chartFile, kChartMetaHeadBytes);
    if (head.empty()) return info;
    // A chart whose document reaches end-of-file inside the head window is small enough to
    // parse whole, so it needs no further screening. Anything longer is searched for a
    // metadata block first: a chart without one would otherwise be parsed end to end, notes
    // and all, to learn that there is nothing to read.
    const bool wholeFileInHead = head.size() < kChartMetaHeadBytes;
    const bool blockInHead = head.find("\"META\"") != std::string::npos
        || head.find("\"meta\"") != std::string::npos
        || head.find("\"Meta\"") != std::string::npos;
    if (!wholeFileInHead && !blockInHead) {
      // The block, when there is one, sits ahead of the note data in every format this reads.
      // A bounded scan past the first window finds it without walking the notes.
      const auto window = readRange(chartFile, kChartMetaHeadBytes, kChartMetaSearchBytes);
      const bool blockBeyondHead = window.find("\"META\"") != std::string::npos
          || window.find("\"meta\"") != std::string::npos
          || window.find("\"Meta\"") != std::string::npos;
      if (!blockBeyondHead) return info;
    }
  }
  Json root = parseJsonObject(readHead(chartFile, kChartMetaHeadBytes));
  if (root.empty()) {
    // The block sits past the head window and the window is not a parsable prefix; parse the
    // document, but only because the bounded scan above proved the block is in it.
    const auto content = platform::readTextFile(chartFile);
    if (content.empty()) return info;
    root = parseJsonObject(content);
  }
  if (root.empty()) return info;
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
  const std::string name = toLower(platform::pathToUtf8(file.filename()));
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
