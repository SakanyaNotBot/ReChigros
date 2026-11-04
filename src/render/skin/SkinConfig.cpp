#include "rechigros/render/skin/SkinConfig.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace rechigros::render::skin {
namespace {

using Json = nlohmann::json;

std::string readFileText(const std::filesystem::path& file) {
  // Route skin documents through the platform text reader so a config written in the legacy
  // code page decodes like every other metadata document.
  return platform::readTextFile(file);
}

std::string trimmed(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

std::string stripQuotes(const std::string& text) {
  if (text.size() >= 2) {
    const char front = text.front();
    const char back = text.back();
    if ((front == '"' && back == '"') || (front == '\'' && back == '\'')) {
      return text.substr(1, text.size() - 2);
    }
  }
  return text;
}

std::string lowered(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return text;
}

// Java's Float.parseFloat accepts surrounding whitespace and rejects trailing junk.
bool parseFloatStrict(const std::string& text, float& out) {
  const std::string value = trimmed(text);
  if (value.empty()) return false;
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) return false;
  while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
    ++end;
  }
  if (end == nullptr || *end != '\0') return false;
  out = static_cast<float>(parsed);
  return true;
}

// Java's Integer.parseInt on a trimmed token: optional sign, decimal digits only.
bool parseIntStrict(const std::string& text, int& out) {
  const std::string value = trimmed(text);
  if (value.empty()) return false;
  try {
    std::size_t consumed = 0;
    const long long parsed = std::stoll(value, &consumed, 10);
    if (consumed != value.size()) return false;
    if (parsed < -2147483648LL || parsed > 2147483647LL) return false;
    out = static_cast<int>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

// The reference skin reader strips every bracket, trims each comma separated token and
// parses it as a decimal integer; one bad token rejects the whole field.
bool parseIntArray(const std::string& text, std::vector<int>& out) {
  std::string inner;
  inner.reserve(text.size());
  for (const char character : text) {
    if (character == '[' || character == ']') continue;
    inner.push_back(character);
  }
  inner = trimmed(inner);
  if (inner.empty()) {
    out.clear();
    return false;
  }
  std::vector<int> values;
  std::size_t start = 0;
  while (true) {
    const auto comma = inner.find(',', start);
    const std::string token =
        comma == std::string::npos ? inner.substr(start) : inner.substr(start, comma - start);
    int value = 0;
    if (!parseIntStrict(token, value)) return false;
    values.push_back(value);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  out = std::move(values);
  return true;
}

// The reference skin reader treats only "true" and "yes" as true; everything else is false.
bool parseSkinBool(const std::string& text) {
  const std::string value = lowered(trimmed(text));
  return value == "true" || value == "yes";
}

std::optional<std::vector<int>> jsonIntArray(const Json& value) {
  if (!value.is_array()) return std::nullopt;
  std::vector<int> result;
  result.reserve(value.size());
  for (const auto& element : value) {
    if (!element.is_number()) return std::nullopt;
    const double number = element.get<double>();
    if (!std::isfinite(number)) return std::nullopt;
    // The reference integer binder truncates fractional values.
    result.push_back(static_cast<int>(number));
  }
  return result;
}

// Colour fields: the reference spelling is a text colour, but earlier packages wrote a
// [red, green, blue(, alpha)] float array. An array is converted to the equivalent
// "#AARRGGBB" text so the rest of the pipeline only ever sees the text form.
std::optional<std::string> colourTextFromJsonValue(const Json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (!value.is_array() || value.size() < 3 || value.size() > 4) return std::nullopt;
  float channels[4] = {1.0F, 1.0F, 1.0F, 1.0F};
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (!value[index].is_number()) return std::nullopt;
    channels[index] = std::clamp(static_cast<float>(value[index].get<double>()), 0.0F, 1.0F);
  }
  const auto byte = [](float channel) {
    return static_cast<int>(std::lround(channel * 255.0F));
  };
  char text[16];
  std::snprintf(text, sizeof(text), "0x%02X%02X%02X%02X", byte(channels[3]), byte(channels[0]),
      byte(channels[1]), byte(channels[2]));
  return std::string(text);
}

} // namespace

SkinConfig loadBuiltInPack(const std::filesystem::path& assetRoot) {
  const auto bytes = platform::AssetManager::readBinary(assetRoot, "res/respack.json");
  if (bytes.empty()) return SkinConfig{};
  const std::string text(bytes.begin(), bytes.end());
  const auto parsed = parseSkinConfigJson(text);
  return parsed ? *parsed : SkinConfig{};
}

std::optional<SkinConfig> loadSkinConfig(const std::filesystem::path& skinDirectory) {
  if (skinDirectory.empty()) return std::nullopt;
  std::error_code error;
  const auto yamlFile = skinDirectory / "info.yml";
  if (std::filesystem::is_regular_file(yamlFile, error)) {
    // The reference returns the line-parsed config whenever info.yml can be read, so a
    // present but empty document still counts as "a skin is selected".
    return parseSkinConfigText(readFileText(yamlFile));
  }
  const auto jsonFile = skinDirectory / "skin.json";
  if (std::filesystem::is_regular_file(jsonFile, error)) {
    const std::string text = readFileText(jsonFile);
    if (text.empty()) return std::nullopt;
    return parseSkinConfigJson(text);
  }
  // Compatibility path: earlier importers wrote the material table as respack.json, which
  // this reader accepts when neither reference document exists.
  const auto packFile = skinDirectory / "respack.json";
  if (std::filesystem::is_regular_file(packFile, error)) {
    const std::string text = readFileText(packFile);
    if (text.empty()) return std::nullopt;
    return parseSkinConfigJson(text);
  }
  return std::nullopt;
}

SkinConfig parseSkinConfigText(const std::string& content) {
  SkinConfig config;
  std::size_t start = 0;
  while (start <= content.size()) {
    const auto newline = content.find('\n', start);
    const std::string raw = newline == std::string::npos
        ? content.substr(start)
        : content.substr(start, newline - start);
    start = newline == std::string::npos ? content.size() + 1 : newline + 1;
    const std::string line = trimmed(raw);
    if (line.empty() || line.front() == '#') continue;
    const auto separator = line.find(':');
    if (separator == std::string::npos) continue;
    const std::string key = trimmed(line.substr(0, separator));
    std::string value = trimmed(line.substr(separator + 1));
    const auto comment = value.find('#');
    if (comment != std::string::npos) value = trimmed(value.substr(0, comment));
    value = stripQuotes(value);

    // Keys are case sensitive in the reference reader; the snake_case spellings are the
    // documented alternates.
    if (key == "name") {
      config.name = value;
    } else if (key == "author") {
      config.author = value;
    } else if (key == "description") {
      config.description = value;
    } else if (key == "hitFx" || key == "hit_fx") {
      std::vector<int> parsed;
      if (parseIntArray(value, parsed)) config.hitFx = std::move(parsed);
    } else if (key == "holdAtlas" || key == "hold_atlas") {
      std::vector<int> parsed;
      if (parseIntArray(value, parsed)) config.holdAtlas = std::move(parsed);
    } else if (key == "holdAtlasMH" || key == "hold_atlas_mh") {
      std::vector<int> parsed;
      if (parseIntArray(value, parsed)) config.holdAtlasMH = std::move(parsed);
    } else if (key == "hitFxScale" || key == "hit_fx_scale") {
      float parsed = 0.0F;
      if (parseFloatStrict(value, parsed)) config.hitFxScale = parsed;
    } else if (key == "hitFxDuration" || key == "hit_fx_duration") {
      float parsed = 0.0F;
      if (parseFloatStrict(value, parsed)) config.hitFxDuration = parsed;
    } else if (key == "hitFxRotate" || key == "hit_fx_rotate") {
      config.hitFxRotate = parseSkinBool(value);
    } else if (key == "hideParticles" || key == "hide_particles") {
      config.hideParticles = parseSkinBool(value);
    } else if (key == "hitFxTinted" || key == "hit_fx_tinted") {
      config.hitFxTinted = parseSkinBool(value);
    } else if (key == "holdKeepHead" || key == "hold_keep_head") {
      config.holdKeepHead = parseSkinBool(value);
    } else if (key == "holdRepeat" || key == "hold_repeat") {
      config.holdRepeat = parseSkinBool(value);
    } else if (key == "holdCompact" || key == "hold_compact") {
      config.holdCompact = parseSkinBool(value);
    } else if (key == "colorPerfect" || key == "color_perfect") {
      config.colorPerfect = value;
    } else if (key == "colorGood" || key == "color_good") {
      config.colorGood = value;
    }
  }
  return config;
}

std::optional<SkinConfig> parseSkinConfigJson(const std::string& content) {
  const Json root = Json::parse(content, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return std::nullopt;
  SkinConfig config;
  bool failed = false;

  const auto readString = [&root, &failed](const char* key) -> std::optional<std::string> {
    const auto iterator = root.find(key);
    if (iterator == root.end()) return std::nullopt;
    if (iterator->is_null()) return std::nullopt;
    if (!iterator->is_string()) {
      failed = true;
      return std::nullopt;
    }
    return iterator->get<std::string>();
  };
  const auto readFloat = [&root, &failed](const char* key) -> std::optional<float> {
    const auto iterator = root.find(key);
    if (iterator == root.end()) return std::nullopt;
    if (iterator->is_null()) return std::nullopt;
    if (!iterator->is_number()) {
      failed = true;
      return std::nullopt;
    }
    return static_cast<float>(iterator->get<double>());
  };
  const auto readBool = [&root, &failed](const char* key) -> std::optional<bool> {
    const auto iterator = root.find(key);
    if (iterator == root.end()) return std::nullopt;
    if (iterator->is_null()) return std::nullopt;
    if (!iterator->is_boolean()) {
      failed = true;
      return std::nullopt;
    }
    return iterator->get<bool>();
  };
  const auto readIntArray = [&root, &failed](const char* key) -> std::optional<std::vector<int>> {
    const auto iterator = root.find(key);
    if (iterator == root.end()) return std::nullopt;
    if (iterator->is_null()) return std::nullopt;
    auto parsed = jsonIntArray(*iterator);
    if (!parsed) {
      failed = true;
      return std::nullopt;
    }
    return parsed;
  };
  const auto readColour = [&root, &failed](const char* key) -> std::optional<std::string> {
    const auto iterator = root.find(key);
    if (iterator == root.end()) return std::nullopt;
    if (iterator->is_null()) return std::nullopt;
    auto text = colourTextFromJsonValue(*iterator);
    if (!text) {
      failed = true;
      return std::nullopt;
    }
    return text;
  };

  config.name = readString("name");
  config.author = readString("author");
  config.description = readString("description");
  config.holdAtlas = readIntArray("holdAtlas");
  config.holdAtlasMH = readIntArray("holdAtlasMH");
  config.hitFx = readIntArray("hitFx");
  config.hitFxScale = readFloat("hitFxScale");
  config.hitFxDuration = readFloat("hitFxDuration");
  config.hitFxRotate = readBool("hitFxRotate");
  config.hideParticles = readBool("hideParticles");
  config.hitFxTinted = readBool("hitFxTinted");
  config.holdKeepHead = readBool("holdKeepHead");
  config.holdRepeat = readBool("holdRepeat");
  config.holdCompact = readBool("holdCompact");
  config.colorPerfect = readColour("colorPerfect");
  config.colorGood = readColour("colorGood");

  if (failed) return std::nullopt;
  return config;
}

std::string skinDisplayName(const SkinConfig& config) {
  if (!config.name || config.name->empty()) return {};
  return *config.name;
}

namespace {

// Shared colour-string parser. Returns whether the text failed, carried no alpha (six
// digits / decimal below 0x1000000) or carried an explicit alpha.
enum class ColourParse { Failed, WithoutAlpha, WithAlpha };

ColourParse parseColourText(const std::string& text, SkinColour& colour, float& alphaOut) {
  std::string value = trimmed(text);
  if (value.empty()) return ColourParse::Failed;
  // An explicit `0x` prefix forces the hexadecimal reading. Without it a token that happens
  // to consist only of hex digits is ambiguous, and the decimal reading is the one the config
  // documents: a bare value such as `16777215` is the decimal spelling of white, not the hex
  // digits `0x16777215` with an accidental alpha byte of 0x01. Treating it as hex made that
  // documented spelling resolve to a dark, nearly transparent colour.
  bool explicitHex = false;
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    value = value.substr(2);
    explicitHex = true;
  }
  const auto allHex = [](const std::string& token) {
    for (const char character : token) {
      if (std::isxdigit(static_cast<unsigned char>(character)) == 0) return false;
    }
    return !token.empty();
  };
  const auto allDigits = [](const std::string& token) {
    for (const char character : token) {
      if (character < '0' || character > '9') return false;
    }
    return !token.empty();
  };
  // A numeric-only token is decimal unless it was explicitly prefixed; a token carrying
  // letters can only be hexadecimal.
  const bool hexReadable = value.size() >= 6 && allHex(value);
  const bool preferDecimal = allDigits(value);
  if (hexReadable && (explicitHex || !preferDecimal)) {
    unsigned long long packed = 0;
    try {
      packed = std::stoull(value, nullptr, 16);
    } catch (...) {
      return ColourParse::Failed;
    }
    float alpha = -1.0F;
    const bool hasAlpha = value.size() >= 8;
    if (hasAlpha) alpha = static_cast<float>((packed >> 24) & 0xFFULL) / 255.0F;
    colour.red = static_cast<float>((packed >> 16) & 0xFFULL) / 255.0F;
    colour.green = static_cast<float>((packed >> 8) & 0xFFULL) / 255.0F;
    colour.blue = static_cast<float>(packed & 0xFFULL) / 255.0F;
    if (!hasAlpha) return ColourParse::WithoutAlpha;
    alphaOut = alpha;
    return ColourParse::WithAlpha;
  }
  long long decimal = 0;
  try {
    std::size_t consumed = 0;
    decimal = std::stoll(value, &consumed, 10);
    if (consumed != value.size()) return ColourParse::Failed;
  } catch (...) {
    return ColourParse::Failed;
  }
  const bool hasAlpha = decimal > 0xFFFFFFLL;
  colour.red = static_cast<float>((decimal >> 16) & 0xFFLL) / 255.0F;
  colour.green = static_cast<float>((decimal >> 8) & 0xFFLL) / 255.0F;
  colour.blue = static_cast<float>(decimal & 0xFFLL) / 255.0F;
  // A loose decimal without an alpha digit means fully opaque, whereas a six-digit hex
  // colour means "keep the caller's alpha" - the two spellings differ in this one respect.
  if (!hasAlpha) {
    alphaOut = 1.0F;
    return ColourParse::WithAlpha;
  }
  alphaOut = static_cast<float>((decimal >> 24) & 0xFFLL) / 255.0F;
  return ColourParse::WithAlpha;
}

} // namespace

float parseSkinColourInto(const std::string& text, SkinColour& colour) {
  float alpha = -1.0F;
  const ColourParse result = parseColourText(text, colour, alpha);
  if (result == ColourParse::Failed) return -1.0F;
  if (result == ColourParse::WithoutAlpha) return -1.0F;
  return alpha;
}

std::optional<SkinColour> parseSkinColour(const std::string& text) {
  SkinColour colour;
  float alpha = 1.0F;
  const ColourParse result = parseColourText(text, colour, alpha);
  if (result == ColourParse::Failed) return std::nullopt;
  colour.alpha = alpha;
  return colour;
}

namespace {

float resolveFloat(bool hasSkin, const std::optional<float>& skinValue,
    const std::optional<float>& packValue, float defaultValue) {
  const std::optional<float>& source = hasSkin ? skinValue : packValue;
  // The reference treats non-positive tuning values as "not specified".
  return (source && *source > 0.0F) ? *source : defaultValue;
}

bool resolveBool(bool hasSkin, const std::optional<bool>& skinValue,
    const std::optional<bool>& packValue, bool defaultValue) {
  const std::optional<bool>& source = hasSkin ? skinValue : packValue;
  return source ? *source : defaultValue;
}

void applyAtlas(const std::optional<std::vector<int>>& value, std::array<int, 2>& out) {
  if (!value || value->size() < 2) return;
  out = {(*value)[0], (*value)[1]};
}

} // namespace

ResolvedMaterials resolveMaterials(const SkinConfig& pack, const SkinConfig* skin) {
  const bool hasSkin = skin != nullptr;
  ResolvedMaterials materials;

  // Texture-atlas fields fall back to the pack when the skin leaves them out.
  applyAtlas(pack.holdAtlas, materials.holdAtlas);
  applyAtlas(pack.holdAtlasMH, materials.holdAtlasMH);
  applyAtlas(pack.hitFx, materials.hitFx);
  if (hasSkin) {
    applyAtlas(skin->holdAtlas, materials.holdAtlas);
    applyAtlas(skin->holdAtlasMH, materials.holdAtlasMH);
    applyAtlas(skin->hitFx, materials.hitFx);
  }

  // Scalar tuning: a selected skin replaces the pack layer entirely; a key the skin leaves
  // out uses the library default, never the pack's value.
  materials.hitFxScale =
      resolveFloat(hasSkin, hasSkin ? skin->hitFxScale : std::nullopt, pack.hitFxScale, 1.0F);
  materials.hitFxDuration = resolveFloat(hasSkin,
      hasSkin ? skin->hitFxDuration : std::nullopt, pack.hitFxDuration, 0.5F);
  materials.hitFxRotate =
      resolveBool(hasSkin, hasSkin ? skin->hitFxRotate : std::nullopt, pack.hitFxRotate, false);
  materials.hitFxTinted =
      resolveBool(hasSkin, hasSkin ? skin->hitFxTinted : std::nullopt, pack.hitFxTinted, true);
  materials.hideParticles = resolveBool(hasSkin,
      hasSkin ? skin->hideParticles : std::nullopt, pack.hideParticles, false);
  materials.holdCompact =
      resolveBool(hasSkin, hasSkin ? skin->holdCompact : std::nullopt, pack.holdCompact, false);
  materials.holdKeepHead = resolveBool(hasSkin,
      hasSkin ? skin->holdKeepHead : std::nullopt, pack.holdKeepHead, false);
  materials.holdRepeat =
      resolveBool(hasSkin, hasSkin ? skin->holdRepeat : std::nullopt, pack.holdRepeat, false);

  // Judgement colours: the selected layer's text (skin when present, otherwise the pack)
  // replaces the defaults; an 8 digit value also replaces the alpha.
  materials.perfectFromSkin = hasSkin && skin->colorPerfect.has_value();
  materials.goodFromSkin = hasSkin && skin->colorGood.has_value();
  const std::optional<std::string>& perfectText =
      hasSkin ? skin->colorPerfect : pack.colorPerfect;
  const std::optional<std::string>& goodText = hasSkin ? skin->colorGood : pack.colorGood;
  if (perfectText && !perfectText->empty()) {
    const float alpha = parseSkinColourInto(*perfectText, materials.perfect);
    if (alpha >= 0.0F) materials.perfect.alpha = alpha;
  }
  if (goodText && !goodText->empty()) {
    const float alpha = parseSkinColourInto(*goodText, materials.good);
    if (alpha >= 0.0F) materials.good.alpha = alpha;
  }
  return materials;
}

std::filesystem::path resolveSkinFile(const std::filesystem::path& skinDirectory,
    const std::string& fileName) {
  if (skinDirectory.empty() || fileName.empty()) return {};
  std::error_code error;
  const auto candidate = skinDirectory / fileName;
  if (std::filesystem::is_regular_file(candidate, error)) return candidate;
  return {};
}

} // namespace rechigros::render::skin
