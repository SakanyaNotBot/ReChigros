#include "rechigros/game/OfficialLevelEffects.hpp"

#include "rechigros/game/OfficialEase.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace rechigros::game {
namespace {

using nlohmann::json;

// The shipped whitelist: chart digest to song id. Charts outside it get no effect.
const std::unordered_map<std::string, std::string>& chartWhitelist() {
  static const std::unordered_map<std::string, std::string> table = {
      // Ametrine: the HD and IN variants only; EZ carries block areas but no level mod.
      {"677a9fabbca76601c9396acead104998", "ametrine"},
      {"76c0b07d551279f2b1373826775355fb", "ametrine"},
      // Hate: IN only.
      {"26e295e34d33e93660c69fe09e2c48b4", "\u30cf\u30c6"},
      // Desultory Signals: EZ, HD, IN and AT.
      {"68d8b27ffc34007d6a01a4aa98faff1e", "desultorysignals"},
      {"d6d016913f5c54dbe781f1522cb5487e", "desultorysignals"},
      {"ab3c6079a43fa46ae4aa25fa147d51ee", "desultorysignals"},
      {"12bead3b4e916404ad89eaa2d632561b", "desultorysignals"},
      // Message SP always gets its effect.
      {"8d5072369f4e120775a469e7541dd9e8", "message"},
  };
  return table;
}

std::string normaliseSongName(std::string name) {
  std::string lowered;
  lowered.reserve(name.size());
  for (const char character : name) {
    const auto value = static_cast<unsigned char>(character);
    lowered.push_back(value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : character);
  }
  // Drop spaces and punctuation so the whitelist tolerates display-name variants.
  std::string compact;
  compact.reserve(lowered.size());
  for (const char character : lowered) {
    const auto value = static_cast<unsigned char>(character);
    const bool keep = (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
        value >= 0x80;
    if (keep) compact.push_back(character);
  }
  return compact;
}

std::string normaliseDigest(const std::string& digest) {
  std::string lowered;
  lowered.reserve(digest.size());
  for (const char character : digest) {
    const auto value = static_cast<unsigned char>(character);
    lowered.push_back(value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : character);
  }
  // A packed digest may carry surrounding whitespace; only that is stripped, the hex digits
  // themselves are compared exactly.
  const auto first = lowered.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) return {};
  const auto last = lowered.find_last_not_of(" \t\n\r");
  return lowered.substr(first, last - first + 1);
}

std::vector<LevelEffectKey> parseKeys(const json& list) {
  std::vector<LevelEffectKey> keys;
  if (!list.is_array()) return keys;
  keys.reserve(list.size());
  const auto readNumber = [](const json& item, const char* name, double fallback) {
    const auto it = item.find(name);
    return it != item.end() && it->is_number() ? it->get<double>() : fallback;
  };
  for (const auto& item : list) {
    if (!item.is_object()) continue;
    LevelEffectKey key;
    key.time = static_cast<float>(readNumber(item, "t", 0.0));
    key.value = static_cast<float>(readNumber(item, "v", 0.0));
    key.stepping = readNumber(item, "s", 0.0) != 0.0;
    key.endValue = static_cast<float>(readNumber(item, "ev", 0.0));
    key.startValue = static_cast<float>(readNumber(item, "sv", 0.0));
    key.easeType = static_cast<int>(readNumber(item, "e", 0.0));
    if (const auto curveIt = item.find("c");
        curveIt != item.end() && curveIt->is_array() && curveIt->size() >= 4) {
      for (std::size_t index = 0; index < 4; ++index) {
        const auto& element = (*curveIt)[index];
        key.curveWeights[index] = element.is_number() ? element.get<float>() : 0.0F;
      }
    }
    keys.push_back(key);
  }
  return keys;
}

std::vector<LevelEffectSlashKey> parseSlashKeys(const json& list) {
  std::vector<LevelEffectSlashKey> keys;
  if (!list.is_array()) return keys;
  keys.reserve(list.size());
  const auto readNumber = [](const json& item, const char* name, double fallback) {
    const auto it = item.find(name);
    return it != item.end() && it->is_number() ? it->get<double>() : fallback;
  };
  for (const auto& item : list) {
    if (!item.is_object()) continue;
    LevelEffectSlashKey key;
    key.time = static_cast<float>(readNumber(item, "t", 0.0));
    key.x = static_cast<float>(readNumber(item, "x", 0.0));
    key.y = static_cast<float>(readNumber(item, "y", 0.0));
    key.lineDir = static_cast<float>(readNumber(item, "ld", 0.0));
    key.slashDir = static_cast<float>(readNumber(item, "sd", 0.0));
    keys.push_back(key);
  }
  return keys;
}

// Value a key contributes on entry / exit. A stepping key carries explicit start and end
// values; an eased key interpolates from its own value to the next key's value.
float keyStartValue(const LevelEffectKey& key) {
  return key.stepping ? key.startValue : key.value;
}

float keyEndValue(const LevelEffectKey& key) {
  return key.stepping ? key.endValue : key.value;
}

std::string slashTrackName(int index, const char* field) {
  return "slash" + std::to_string(index) + field;
}

} // namespace

float OfficialLevelEffects::evaluateTrack(const std::vector<LevelEffectKey>& keys,
    float timeSec) {
  if (keys.empty()) return 0.0F;
  // Before the first key the track holds that key's entry value.
  if (timeSec < keys.front().time) return keyEndValue(keys.front());

  std::size_t index = 0;
  for (std::size_t cursor = 0; cursor < keys.size(); ++cursor) {
    if (keys[cursor].time <= timeSec) {
      index = cursor;
    } else {
      break;
    }
  }
  if (index >= keys.size() - 1) return keyStartValue(keys[index]);

  const auto& current = keys[index];
  const auto& next = keys[index + 1];
  const float span = next.time - current.time;
  const float progress = std::abs(span) < 1.0e-6F ? 1.0F : (timeSec - current.time) / span;

  std::array<float, 10> curveKey{};
  curveKey[6] = current.curveWeights[0];
  curveKey[7] = current.curveWeights[1];
  curveKey[8] = current.curveWeights[2];
  curveKey[9] = current.curveWeights[3];
  const double eased = OfficialEase::evaluate(current.easeType, static_cast<double>(progress),
      curveKey);
  const float a = keyStartValue(current);
  const float b = keyEndValue(next);
  return static_cast<float>(a + (b - a) * eased);
}

namespace {
// Null until the application installs one; the plain file path below covers a desktop tree.
OfficialLevelEffects::AssetReader& assetReader() {
  static OfficialLevelEffects::AssetReader reader;
  return reader;
}
} // namespace

void OfficialLevelEffects::setAssetReader(AssetReader reader) {
  assetReader() = std::move(reader);
}

std::shared_ptr<const OfficialLevelEffects> OfficialLevelEffects::load(
    const std::filesystem::path& assetRoot) {
  constexpr std::string_view kRelative = "official_effects/chapter9_phase2.json";
  std::string text;
  // The asset reader is the only way to reach a packaged asset; the file open below is the
  // fallback for a tree whose assets are ordinary files.
  if (assetReader()) {
    const auto bytes = assetReader()(assetRoot, kRelative);
    text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  if (text.empty()) {
    std::ifstream input(assetRoot / "official_effects" / "chapter9_phase2.json",
        std::ios::binary);
    if (!input) return nullptr;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
  }
  if (text.empty()) return nullptr;
  const auto root = json::parse(text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return nullptr;

  auto table = std::make_shared<OfficialLevelEffects>();
  table->digestToSong_ = chartWhitelist();

  // Songs carry the effect binding and the HUD/audio flags; the effects table carries the
  // curves. They are separate sections and are joined by `forChart`.
  if (const auto songsIt = root.find("songs"); songsIt != root.end() && songsIt->is_object()) {
    for (auto it = songsIt->begin(); it != songsIt->end(); ++it) {
      if (!it.value().is_object()) continue;
      SongEntry entry;
      if (const auto effectIt = it.value().find("effect");
          effectIt != it.value().end() && effectIt->is_string()) {
        entry.effect = effectIt->get<std::string>();
      }
      if (const auto hideIt = it.value().find("hideHud");
          hideIt != it.value().end() && hideIt->is_boolean()) {
        entry.hideHud = hideIt->get<bool>();
      }
      if (const auto lowPassIt = it.value().find("lowPassOnBlock");
          lowPassIt != it.value().end() && lowPassIt->is_boolean()) {
        entry.lowPassOnBlock = lowPassIt->get<bool>();
      }
      table->songs_[normaliseSongName(it.key())] = std::move(entry);
    }
  }

  if (const auto effectsIt = root.find("effects");
      effectsIt != root.end() && effectsIt->is_object()) {
    for (auto it = effectsIt->begin(); it != effectsIt->end(); ++it) {
      if (!it.value().is_object()) continue;
      EffectData data;
      if (const auto tracksIt = it.value().find("tracks");
          tracksIt != it.value().end() && tracksIt->is_object()) {
        for (auto track = tracksIt->begin(); track != tracksIt->end(); ++track) {
          auto keys = parseKeys(track.value());
          std::stable_sort(keys.begin(), keys.end(),
              [](const LevelEffectKey& first, const LevelEffectKey& second) {
                return first.time < second.time;
              });
          data.tracks[track.key()] = std::move(keys);
        }
      }
      // Slash keyframes live per light line under `slashes` as `slashNInfo` arrays.
      if (const auto slashesIt = it.value().find("slashes");
          slashesIt != it.value().end() && slashesIt->is_object()) {
        for (auto track = slashesIt->begin(); track != slashesIt->end(); ++track) {
          data.slashes[track.key()] = parseSlashKeys(track.value());
        }
      }
      if (const auto scalarsIt = it.value().find("scalars");
          scalarsIt != it.value().end() && scalarsIt->is_object()) {
        for (auto scalar = scalarsIt->begin(); scalar != scalarsIt->end(); ++scalar) {
          if (scalar.value().is_number()) {
            data.scalars[scalar.key()] = static_cast<float>(scalar.value().get<double>());
          }
        }
      }
      // Settings are flattened out of each pass's `params` block into "<pass>/<name>" keys.
      // A pass also carries colours and other sub-objects, which are not scalars and must
      // not be read as zero.
      if (const auto settingsIt = it.value().find("settings");
          settingsIt != it.value().end() && settingsIt->is_object()) {
        for (auto pass = settingsIt->begin(); pass != settingsIt->end(); ++pass) {
          if (!pass.value().is_object()) continue;
          const auto paramsIt = pass.value().find("params");
          if (paramsIt == pass.value().end() || !paramsIt->is_object()) continue;
          for (auto field = paramsIt->begin(); field != paramsIt->end(); ++field) {
            if (field.value().is_number()) {
              data.settings[pass.key() + "/" + field.key()] =
                  static_cast<float>(field.value().get<double>());
            }
          }
        }
      }
      table->effects_[it.key()] = std::move(data);
    }
  }
  return table;
}

std::shared_ptr<OfficialLevelEffects> OfficialLevelEffects::forChart(
    const OfficialLevelEffects& table, const std::string& normalizedDigest,
    const std::string& songName, bool storyMode) {
  // Selection is by normalised digest alone: a chart carries a level effect only when its
  // content is one of the whitelisted charts. The song name is not a substitute, because a
  // fan-made chart can reuse an official song's name and artwork while its content is not the
  // chart the effect was authored against.
  (void)songName;
  std::string songKey;
  if (const auto digest = normaliseDigest(normalizedDigest); !digest.empty()) {
    if (const auto it = table.digestToSong_.find(digest); it != table.digestToSong_.end()) {
      songKey = it->second;
    }
  }
  if (songKey.empty()) return nullptr;

  // The Message level plays its effects in every mode; every other chart only carries one
  // while story mode is on and otherwise plays as a plain chart. Block areas authored in the
  // chart itself still render either way, because those belong to the chart.
  const bool isMessage = normaliseSongName(songKey) == "message";
  if (!isMessage && !storyMode) return nullptr;

  const auto songIt = table.songs_.find(normaliseSongName(songKey));
  if (songIt == table.songs_.end() || songIt->second.effect.empty()) return nullptr;
  const auto& effectName = songIt->second.effect;

  const auto dataIt = table.effects_.find(effectName);
  if (dataIt == table.effects_.end()) return nullptr;

  auto selected = std::make_shared<OfficialLevelEffects>();
  selected->songKey_ = songKey;
  selected->effectName_ = effectName;
  selected->hideHud_ = songIt->second.hideHud;
  selected->lowPassOnBlock_ = songIt->second.lowPassOnBlock;
  selected->tracks_ = dataIt->second.tracks;
  selected->slashes_ = dataIt->second.slashes;
   selected->scalars_ = dataIt->second.scalars;
  selected->settings_ = dataIt->second.settings;
  return selected;
}

std::shared_ptr<OfficialLevelEffects> OfficialLevelEffects::forSongName(
    const OfficialLevelEffects& table, const std::string& songName) {
  const auto songIt = table.songs_.find(normaliseSongName(songName));
  if (songIt == table.songs_.end() || songIt->second.effect.empty()) return nullptr;
  const auto dataIt = table.effects_.find(songIt->second.effect);
  if (dataIt == table.effects_.end()) return nullptr;

  auto selected = std::make_shared<OfficialLevelEffects>();
  selected->songKey_ = songIt->first;
  selected->effectName_ = songIt->second.effect;
  selected->hideHud_ = songIt->second.hideHud;
  selected->lowPassOnBlock_ = songIt->second.lowPassOnBlock;
  selected->tracks_ = dataIt->second.tracks;
  selected->slashes_ = dataIt->second.slashes;
  selected->scalars_ = dataIt->second.scalars;
  selected->settings_ = dataIt->second.settings;
  return selected;
}

float OfficialLevelEffects::track(const std::string& name, float timeSec) const {
  const auto it = tracks_.find(name);
  if (it == tracks_.end()) return 0.0F;
  return evaluateTrack(it->second, timeSec);
}

bool OfficialLevelEffects::hasTrack(const std::string& name) const {
  return tracks_.find(name) != tracks_.end();
}

const std::vector<LevelEffectKey>* OfficialLevelEffects::trackKeys(
    const std::string& name) const {
  const auto it = tracks_.find(name);
  return it == tracks_.end() ? nullptr : &it->second;
}

float OfficialLevelEffects::scalar(const std::string& field, float fallback) const {
  const auto it = scalars_.find(field);
  return it == scalars_.end() ? fallback : it->second;
}

float OfficialLevelEffects::settingFloat(const std::string& pass, const std::string& name,
    float fallback) const {
  const auto it = settings_.find(pass + "/" + name);
  return it == settings_.end() ? fallback : it->second;
}

int OfficialLevelEffects::slashCount() const {
  int count = 0;
  while (slashes_.find(slashTrackName(count + 1, "Info")) != slashes_.end()) ++count;
  return count;
}

bool OfficialLevelEffects::slashAt(int index, float timeSec,
    std::array<float, 4>& out) const {
  const auto it = slashes_.find(slashTrackName(index, "Info"));
  if (it == slashes_.end() || it->second.empty()) return false;
  const auto& keys = it->second;

  const LevelEffectSlashKey* current = &keys.front();
  const LevelEffectSlashKey* next = &keys.back();
  for (std::size_t cursor = 0; cursor < keys.size(); ++cursor) {
    const auto& key = keys[cursor];
    if (key.time <= timeSec) {
      current = &key;
      next = (cursor + 1 < keys.size()) ? &keys[cursor + 1] : &key;
    } else {
      break;
    }
  }
  const float span = next->time - current->time;
  const float raw = std::abs(span) < 1.0e-6F ? 0.0F : (timeSec - current->time) / span;
  const float progress = std::clamp(raw, 0.0F, 1.0F);
  out[0] = current->x + (next->x - current->x) * progress;
  out[1] = current->y + (next->y - current->y) * progress;
  out[2] = current->lineDir + (next->lineDir - current->lineDir) * progress;
  out[3] = current->slashDir + (next->slashDir - current->slashDir) * progress;
  return true;
}

void OfficialLevelEffects::resetChallenge() noexcept {
  challengeFailed_ = false;
  touchingBlock_ = false;
  ametrineVignette_ = 0.0F;
}

void OfficialLevelEffects::onBadOrMiss(float chartTimeSec, bool isMiss) {
  if (effectName_ == "AmetrineLevelMod") {
    if (challengeFailed_) return;
    const float start = scalar("eventStartTime", 0.0F);
    const float end = scalar("eventEndTime", std::numeric_limits<float>::max());
    if (chartTimeSec < start || chartTimeSec > end) return;
    challengeFailed_ = true;
  } else if (effectName_ == "PetrichorLevelMod" && isMiss) {
    // A Miss while a finger holds a red zone fails the Petrichor challenge.
    if (challengeFailed_ || !touchingBlock_) return;
    challengeFailed_ = true;
  }
}

void OfficialLevelEffects::updateAmetrineVignette(float chartTimeSec, float deltaTimeSec) {
  if (effectName_ != "AmetrineLevelMod") return;
  constexpr float kFadeSec = 2.0F;
  const float start = scalar("eventStartTime", -1.0F);
  const float end = scalar("eventEndTime", -1.0F);
  const float target = scalar("targetVignetteDarkness", 0.0F);
  const bool inWindow = start >= 0.0F && chartTimeSec >= start && chartTimeSec <= end;
  float want = 0.0F;
  if (inWindow) {
    want = target;
  } else if (chartTimeSec > end && !isChallengeFailed()) {
    want = 0.0F;
  } else if (chartTimeSec > end) {
    // A failed check keeps the screen darkened to the end of the level.
    want = target;
  }
  const float step = deltaTimeSec / kFadeSec;
  if (ametrineVignette_ < want) {
    ametrineVignette_ = std::min(want, ametrineVignette_ + step);
  } else if (ametrineVignette_ > want) {
    ametrineVignette_ = std::max(want, ametrineVignette_ - step);
  }
}

namespace {

// Converts AnimationDirector keys into the LineEvent stream the storyboard binder
// evaluates. `evaluateTrack` is the exact reference evaluation, so the stream reproduces
// its three special cases: a lead span before the first key, a tail span after the last
// one, and the two step easings that no easing id can express.
std::vector<LineEvent> toLineEvents(const std::vector<LevelEffectKey>& keys) {
  std::vector<LineEvent> events;
  if (keys.empty()) return events;
  if (keys.front().time > 0.0F) {
    // Everything before the first key holds that key's entry value.
    LineEvent lead;
    lead.startTime = 0.0;
    lead.endTime = keys.front().time;
    lead.start = keyEndValue(keys.front());
    lead.end = lead.start;
    lead.type = 1;
    events.push_back(lead);
  }
  for (std::size_t index = 0; index + 1 < keys.size(); ++index) {
    const auto& current = keys[index];
    const auto& next = keys[index + 1];
    const int ease = current.easeType;
    const float a = keyStartValue(current);
    const float b = keyEndValue(next);
    if (ease == 15 && current.curveWeights[0] > 0.0F) {
      // A weighted AnimationCurve cannot be expressed by an easing id, so the span is
      // sampled into linear pieces.
      constexpr int kSteps = 64;
      std::array<float, 10> curveKey{};
      curveKey[6] = current.curveWeights[0];
      curveKey[7] = current.curveWeights[1];
      curveKey[8] = current.curveWeights[2];
      curveKey[9] = current.curveWeights[3];
      for (int step = 0; step < kSteps; ++step) {
        const double t0 = static_cast<double>(step) / kSteps;
        const double t1 = static_cast<double>(step + 1) / kSteps;
        LineEvent piece;
        piece.startTime = current.time + (next.time - current.time) * t0;
        piece.endTime = current.time + (next.time - current.time) * t1;
        piece.start = static_cast<float>(a + (b - a) * OfficialEase::evaluate(15, t0, curveKey));
        piece.end = static_cast<float>(a + (b - a) * OfficialEase::evaluate(15, t1, curveKey));
        piece.type = 0;
        events.push_back(piece);
      }
      continue;
    }
    if (ease == 13 || ease == 14) {
      // Zero and One give a constant eased progress over the whole span, so the track
      // either holds its start value or jumps straight to the next key's value.
      const float held = ease == 13 ? a : b;
      LineEvent event;
      event.startTime = current.time;
      event.endTime = next.time;
      event.start = held;
      event.end = held;
      event.type = 0;
      events.push_back(event);
      continue;
    }
    LineEvent event;
    event.startTime = current.time;
    event.endTime = next.time;
    event.start = a;
    event.end = b;
    event.type = OfficialEase::toRpeEasing(ease);
    events.push_back(event);
  }
  // After the last key the track holds its exit value.
  LineEvent tail;
  tail.startTime = keys.back().time;
  tail.endTime = tail.startTime + 1.0e6;
  tail.start = keyStartValue(keys.back());
  tail.end = tail.start;
  tail.type = 0;
  events.push_back(tail);
  return events;
}

// One DimensionSlash geometry component as a step stream: the reference picks the last
// keyframe at or before t, so every span holds its start value instead of interpolating.
std::vector<LineEvent> slashPositionEvents(const std::vector<LevelEffectSlashKey>& keys,
    int field) {
  std::vector<LineEvent> events;
  events.reserve(keys.size());
  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto& current = keys[index];
    const auto& next = (index + 1 < keys.size()) ? keys[index + 1] : current;
    float value = current.x;
    if (field == 1) value = current.y;
    else if (field == 2) value = current.lineDir;
    else if (field == 3) value = current.slashDir;
    LineEvent event;
    event.startTime = current.time;
    event.endTime = next.time;
    event.start = value;
    event.end = value;
    event.type = 1;
    if (event.endTime <= event.startTime) event.endTime = event.startTime + 1.0e6;
    events.push_back(event);
  }
  return events;
}

// A pass's constant uniforms.
struct ConstantSpec {
  std::string name;
  std::vector<float> values;
};

// One prpr shader pass: shader key, track-to-uniform bindings, and constant uniforms.
struct PassSpec {
  std::string shader;
  std::vector<std::pair<std::string, std::string>> bindings;
  std::vector<ConstantSpec> constants;
  bool slashGeometry{false};
  bool ametrineVignette{false};
};

} // namespace

std::vector<PrprEffect> OfficialLevelEffects::buildPrprEffects() const {
  std::vector<PrprEffect> result;
  if (effectName_.empty()) return result;

  std::vector<PassSpec> specs;

  // The chain order below is the reference's own AddEffect order; every uniform keeps the
  // AnimationDirector track name so the values are the shipped ones.
  if (effectName_ == "HateLevelEffects") {
    {
      PassSpec pass;
      pass.shader = "official_glitch";
      pass.bindings = {{"glitchRange", "glitchRange"}};
      pass.constants = {{"rgbChannelOffset", {0.0F}},
          {"layer1Speed", {0.2F}}, {"layer2Speed", {0.1F}}, {"layer3Speed", {0.1F}},
          {"layer1Strength", {1.0F, 2.0F}}, {"layer2Strength", {0.5F, 0.5F}},
          {"layer3Strength", {1.0F, 2.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_rgb_shift";
      pass.bindings = {{"shiftRadius", "shiftRadius"}};
      pass.constants = {{"screenDeform", {0.45F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_vignette_plus";
      pass.bindings = {{"vignetteRadius", "vignetteRadius"},
          {"vignetteDarkness", "vignetteDarkness"},
          {"vignetteNoiseBrightness", "vignetteNoiseBrightness"},
          {"vignetteLineOpacity", "vignetteLineOpacity"}};
      pass.constants = {
          {"vignetteSmoothness", {settingFloat("vignettePlus", "smoothness", 0.8F)}},
          {"vignetteNoiseSaturation", {settingFloat("vignettePlus", "noiseSaturation", 0.0F)}},
          {"vignetteNoiseSampleRate", {settingFloat("vignettePlus", "noiseSampleRate", 0.5F)}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_lens_distortion";
      pass.bindings = {{"convergence", "convergence"}, {"edgeSmoothness", "edgeSmoothness"}};
      pass.constants = {{"lensSize", {80.0F}}, {"lensAspect", {1.12F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_esc";
      pass.bindings = {{"brightness", "exposure"}, {"saturation", "saturate"},
          {"contrast", "contrast"}};
      pass.constants = {{"averageColor", {0.5F, 0.5F, 0.5F}}};
      specs.push_back(std::move(pass));
    }
  } else if (effectName_ == "DesultorySignalsLevelEffects") {
    {
      PassSpec pass;
      pass.shader = "official_rgb_shift";
      pass.bindings = {{"shiftRadius", "shiftRadius"}};
      pass.constants = {{"screenDeform", {0.45F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_glitch";
      pass.bindings = {{"glitchRange", "glitchRange"}};
      pass.constants = {{"rgbChannelOffset", {0.0F}},
          {"layer1Speed", {0.2F}}, {"layer2Speed", {0.1F}}, {"layer3Speed", {0.1F}},
          {"layer1Strength", {1.0F, 2.0F}}, {"layer2Strength", {0.5F, 0.5F}},
          {"layer3Strength", {1.0F, 2.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_glitch_dog";
      pass.bindings = {{"glitchDogScale", "glitchDogScale"},
          {"glitchMapBrightness", "glitchMapBrightness"},
          {"glitchMapContrast", "glitchMapContrast"}};
      pass.constants = {{"noiseFrameCount", {11.0F}}, {"noiseMoveSpeed", {2.0F}},
          {"noiseEvolveSpeed", {2.0F}}, {"noiseScale", {0.3F, 0.36F}},
          {"noiseOffset", {0.0F, 0.0F}}, {"seamBlend", {0.02F}},
          {"displaceStrength", {0.2F}}, {"displaceMapOffset", {0.11F, 0.0F}},
          {"displaceMapScale", {1.0F, 1.0F}}, {"displaceSpeed", {1.0F}},
          {"glitchMapOffset", {0.0F, 0.0F}}, {"glitchMapScale", {5.6F, 1.0F}},
          {"centerMaskRadius", {0.094F}}, {"centerMaskSmoothness", {0.07F}},
          {"edgeMaskRadius", {0.12F}}, {"edgeMaskSmoothness", {0.41F}},
          {"centerColor", {0.0F, 0.2896262F, 0.6037736F}},
          {"edgeColor", {0.06194376F, 0.08723756F, 0.1509434F}},
          {"colorBlend", {0.776F}}};
      specs.push_back(std::move(pass));
    }
    {
      // Only convergence is animated here; the lens size and edge softness are constants,
      // so binding a track to them would read a track that does not exist.
      PassSpec pass;
      pass.shader = "official_lens_distortion";
      pass.bindings = {{"convergence", "convergence"}};
      pass.constants = {{"lensSize", {50.0F}}, {"edgeSmoothness", {0.0F}},
          {"lensAspect", {1.6F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_bloom";
      pass.bindings = {{"bloomBrightness", "bloomBrightness"}};
      pass.constants = {{"bloomThreshold", {0.308F}}, {"bloomRadius", {1.1F}},
          {"bloomIntensity", {1.85F}}, {"bloomSampleRate", {0.45F}},
          {"bloomIteration", {3.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_dimension_slash";
      pass.slashGeometry = true;
      pass.constants = {{"SlashLightStrength", {30.0F}}, {"SlashLightFalloff", {35.0F}},
          {"slashCount", {5.0F}}, {"slashLightColor", {0.759434F, 0.93157786F, 1.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_vignette_plus";
      pass.bindings = {{"vignetteRadius", "vignetteRadius"},
          {"vignetteDarkness", "vignetteDarkness"}};
      pass.constants = {{"vignetteSmoothness", {0.5F}}, {"vignetteNoiseSampleRate", {0.5F}}};
      specs.push_back(std::move(pass));
    }
  } else if (effectName_ == "MessageLevelEffects") {
    {
      PassSpec pass;
      pass.shader = "official_glitch";
      pass.bindings = {{"glitchRange", "glitchRange"}};
      pass.constants = {{"rgbChannelOffset", {0.0F}},
          {"layer1Speed", {0.25F}}, {"layer2Speed", {1.7F}}, {"layer3Speed", {0.25F}},
          {"layer1Strength", {0.31F, 0.0F}}, {"layer2Strength", {1.28F, 7.75F}},
          {"layer3Strength", {0.06F, -0.1F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_esc";
      pass.bindings = {{"brightness", "exposure"}, {"saturation", "saturation"},
          {"contrast", "contrast"}};
      pass.constants = {{"averageColor", {0.5F, 0.5F, 0.5F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_light_band";
      pass.bindings = {{"bandWidth", "bandWidth"}, {"bandSmoothness", "bandSmoothness"}};
      pass.constants = {{"bandBrightness", {1.0F}}, {"bandDir", {0.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_bloom";
      pass.bindings = {{"bloomBrightness", "bloomBrightness"}};
      pass.constants = {{"bloomThreshold", {0.059F}}, {"bloomRadius", {0.6F}},
          {"bloomIntensity", {1.61F}}, {"bloomSampleRate", {0.35F}},
          {"bloomIteration", {3.0F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_vignette_plus";
      pass.bindings = {{"vignetteRadius", "vignetteRadius"},
          {"vignetteDarkness", "vignetteDarkness"},
          {"vignetteSmoothness", "vignetteSmoothness"},
          {"vignetteNoiseBrightness", "vignetteNoiseBrightness"},
          {"vignetteLineOpacity", "vignetteLineOpacity"}};
      pass.constants = {{"vignetteNoiseSampleRate", {0.5F}}};
      specs.push_back(std::move(pass));
    }
    {
      PassSpec pass;
      pass.shader = "official_lens_distortion";
      pass.bindings = {{"convergence", "conv"}, {"edgeSmoothness", "edgeSmoothness"}};
      pass.constants = {{"lensSize", {125.0F}}, {"lensAspect", {1.12F}}};
      specs.push_back(std::move(pass));
    }
  } else if (effectName_ == "AmetrineLevelMod") {
    PassSpec pass;
    pass.shader = "official_simple_vignette";
    pass.constants = {{"vignetteRadius", {0.24F}}, {"vignetteSmoothness", {0.55F}},
        {"vignetteLineOpacity", {0.0F}}};
    pass.ametrineVignette = true;
    specs.push_back(std::move(pass));
  }

  for (auto& spec : specs) {
    PrprEffect effect;
    effect.startTime = 0.0;
    effect.endTime = std::numeric_limits<double>::infinity();
    effect.global = true;
    effect.shader = spec.shader;

    for (const auto& [uniform, trackName] : spec.bindings) {
      const auto it = tracks_.find(trackName);
      if (it == tracks_.end() || it->second.empty()) continue;
      effect.variables[uniform] = PrprVariable{{}, toLineEvents(it->second)};
    }
    if (spec.ametrineVignette) {
      // The Ametrine pass darkens through the challenge vignette rather than a track; the
      // renderer drives this uniform from `ametrineVignette()`.
      effect.variables["vignetteDarkness"] = PrprVariable{{0.0F}, {}};
    }
    for (const auto& constant : spec.constants) {
      effect.variables[constant.name] = PrprVariable{constant.values, {}};
    }
    if (spec.slashGeometry) {
      for (int index = 1; index <= 5; ++index) {
        const auto keys = slashes_.find(slashTrackName(index, "Info"));
        if (keys == slashes_.end() || keys->second.empty()) continue;
        effect.variables["slash" + std::to_string(index) + "PosX"] =
            PrprVariable{{}, slashPositionEvents(keys->second, 0)};
        effect.variables["slash" + std::to_string(index) + "PosY"] =
            PrprVariable{{}, slashPositionEvents(keys->second, 1)};
        effect.variables["slash" + std::to_string(index) + "LineDir"] =
            PrprVariable{{}, slashPositionEvents(keys->second, 2)};
        effect.variables["slash" + std::to_string(index) + "SlashDir"] =
            PrprVariable{{}, slashPositionEvents(keys->second, 3)};
        const auto strengthName = "slash" + std::to_string(index) + "Strength";
        if (const auto strength = tracks_.find(strengthName);
            strength != tracks_.end() && !strength->second.empty()) {
          effect.variables[strengthName] = PrprVariable{{}, toLineEvents(strength->second)};
        }
      }
    }
    if (!effect.variables.empty()) result.push_back(std::move(effect));
  }
  return result;
}

} // namespace rechigros::game
