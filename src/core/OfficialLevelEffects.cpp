#include "rechigros/game/OfficialLevelEffects.hpp"

#include "rechigros/game/OfficialEase.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <limits>
#include <string>

namespace rechigros::game {
namespace {

using nlohmann::json;

// The shipped whitelist: chart digest to effect name. Charts outside it get no effect.
const std::unordered_map<std::string, std::string>& chartWhitelist() {
  static const std::unordered_map<std::string, std::string> table = {
      // Ametrine (two difficulty variants share the effect).
      {"677a9fabbca76601c9396acead104998", "ametrine"},
      {"76c0b07d551279f2b1373826775355fb", "ametrine"},
      {"26e295e34d33e93660c69fe09e2c48b4", "\u30cf\u30c6"},
      {"68d8b27ffc34007d6a01a4aa98faff1e", "desultorysignals"},
      {"d6d016913f5c54dbe781f1522cb5487e", "desultorysignals"},
      {"ab3c6079a43fa46ae4aa25fa147d51ee", "desultorysignals"},
      {"12bead3b4e916404ad89eaa2d632561b", "desultorysignals"},
      // The message pack is identified by its normalized digest; the table only knows the
      // effect name, so that digest is registered here as well.
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

std::vector<LevelEffectKey> parseKeys(const json& list) {
  std::vector<LevelEffectKey> keys;
  if (!list.is_array()) return keys;
  keys.reserve(list.size());
  for (const auto& item : list) {
    if (!item.is_object()) continue;
    LevelEffectKey key;
    const auto readNumber = [&item](const char* name, double fallback) {
      const auto it = item.find(name);
      return it != item.end() && it->is_number() ? it->get<double>() : fallback;
    };
    key.time = static_cast<float>(readNumber("t", 0.0));
    key.value = static_cast<float>(readNumber("v", 0.0));
    key.stepping = readNumber("s", 0.0) != 0.0;
    key.endValue = static_cast<float>(readNumber("ev", 0.0));
    key.startValue = static_cast<float>(readNumber("sv", 0.0));
    key.easeType = static_cast<int>(readNumber("e", 0.0));
    if (const auto curveIt = item.find("c");
        curveIt != item.end() && curveIt->is_array() && curveIt->size() >= 4) {
      for (std::size_t index = 0; index < 4; ++index) {
        const auto& element = (*curveIt)[index];
        key.curveWeights[index] = element.is_number() ? element.get<float>() : 0.0F;
      }
    }
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end(),
      [](const LevelEffectKey& first, const LevelEffectKey& second) {
        return first.time < second.time;
      });
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

std::shared_ptr<const OfficialLevelEffects> OfficialLevelEffects::load(
    const std::filesystem::path& assetRoot) {
  // The table lives beside the executable rather than inside a chart pack, so it is read
  // directly from the asset root.
  std::ifstream input(assetRoot / "official_effects" / "chapter9_phase2.json",
      std::ios::binary);
  if (!input) return nullptr;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const auto text = buffer.str();
  if (text.empty()) return nullptr;
  const auto root = json::parse(text, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return nullptr;

  auto table = std::make_shared<OfficialLevelEffects>();
  table->digestToEffect_ = chartWhitelist();

  if (const auto songsIt = root.find("songs"); songsIt != root.end() && songsIt->is_object()) {
    for (auto it = songsIt->begin(); it != songsIt->end(); ++it) {
      if (!it.value().is_object()) continue;
      const auto effectIt = it.value().find("effect");
      if (effectIt == it.value().end() || !effectIt->is_string()) continue;
      table->songToEffect_[normaliseSongName(it.key())] = effectIt->get<std::string>();
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
          data.tracks[track.key()] = parseKeys(track.value());
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
      if (const auto settingsIt = it.value().find("settings");
          settingsIt != it.value().end() && settingsIt->is_object()) {
        for (auto pass = settingsIt->begin(); pass != settingsIt->end(); ++pass) {
          if (!pass.value().is_object()) continue;
          for (auto field = pass.value().begin(); field != pass.value().end(); ++field) {
            if (field.value().is_number()) {
              data.settings[pass.key() + "/" + field.key()] =
                  static_cast<float>(field.value().get<double>());
            }
          }
        }
      }
      if (const auto hideIt = it.value().find("hideHud");
          hideIt != it.value().end() && hideIt->is_boolean()) {
        data.hideHud = hideIt->get<bool>();
      }
      table->effects_[it.key()] = std::move(data);
    }
  }
  return table;
}

std::shared_ptr<const OfficialLevelEffects> OfficialLevelEffects::forChart(
    const OfficialLevelEffects& table, const std::string& normalizedDigest,
    const std::string& songName) {
  std::string effectName;
  if (!normalizedDigest.empty()) {
    const auto it = table.digestToEffect_.find(normalizedDigest);
    if (it != table.digestToEffect_.end()) effectName = it->second;
  }
  if (effectName.empty() && !songName.empty()) {
    const auto it = table.songToEffect_.find(normaliseSongName(songName));
    if (it != table.songToEffect_.end()) effectName = it->second;
  }
  if (effectName.empty()) return nullptr;

  const auto dataIt = table.effects_.find(effectName);
  if (dataIt == table.effects_.end()) return nullptr;

  auto selected = std::make_shared<OfficialLevelEffects>();
  selected->effectName_ = effectName;
  selected->hideHud_ = dataIt->second.hideHud;
  selected->tracks_ = dataIt->second.tracks;
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

float OfficialLevelEffects::scalar(const std::string& field, float fallback) const {
  const auto it = scalars_.find(field);
  return it == scalars_.end() ? fallback : it->second;
}

float OfficialLevelEffects::settingFloat(const std::string& pass, const std::string& name,
    float fallback) const {
  const auto it = settings_.find(pass + "/" + name);
  return it == settings_.end() ? fallback : it->second;
}

std::vector<PrprEffect> OfficialLevelEffects::buildPrprEffects() const {
  std::vector<PrprEffect> result;
  if (effectName_.empty()) return result;

  // Each pass declares the shader it runs and the track names bound to its uniforms. The
  // effect stays mounted for the whole chart: passes idle to identity through their own
  // zero-valued tracks, so there is no window to expire.
  const auto addPass = [this, &result](const std::string& shader,
                            std::initializer_list<std::pair<const char*, const char*>> bindings,
                            const std::unordered_map<std::string, std::vector<float>>& constants) {
    PrprEffect effect;
    effect.startTime = 0.0;
    effect.endTime = std::numeric_limits<double>::infinity();
    effect.global = true;
    effect.shader = shader;

    for (const auto& [uniform, trackName] : bindings) {
      const auto it = tracks_.find(trackName);
      if (it == tracks_.end() || it->second.empty()) continue;
      PrprVariable variable;
      // A track becomes one eased span per key pair, which is exactly what the chain
      // evaluates.
      for (std::size_t index = 0; index < it->second.size(); ++index) {
        const auto& key = it->second[index];
        LineEvent event;
        event.startTime = key.time;
        event.endTime = (index + 1 < it->second.size()) ? it->second[index + 1].time : key.time;
        event.start = keyStartValue(key);
        event.end = keyEndValue(key);
        event.type = key.easeType;
        variable.events.push_back(event);
      }
      effect.variables[uniform] = std::move(variable);
    }
    for (const auto& [name, values] : constants) {
      effect.variables[name] = PrprVariable{values, {}};
    }
    if (!effect.variables.empty()) result.push_back(std::move(effect));
  };

  const std::unordered_map<std::string, std::vector<float>> noConstants{};

  if (effectName_ == "DesultorySignalsLevelEffects") {
    addPass("official_rgb_shift", {{"shiftRadius", "shiftRadius"}},
        {{"screenDeform", {0.45F}}});
    addPass("official_glitch", {{"glitchRange", "glitchRange"}},
        {{"rgbChannelOffset", {0.0F}},
            {"layer1Speed", {0.2F}}, {"layer2Speed", {0.1F}}, {"layer3Speed", {0.1F}},
            {"layer1Strength", {1.0F, 2.0F}}, {"layer2Strength", {0.5F, 0.5F}},
            {"layer3Strength", {1.0F, 2.0F}}});
    addPass("official_glitch_dog",
        {{"glitchDogScale", "glitchDogScale"},
            {"glitchMapBrightness", "glitchMapBrightness"},
            {"glitchMapContrast", "glitchMapContrast"}},
        noConstants);
    addPass("official_lens_distortion",
        {{"convergence", "convergence"}, {"lensSize", "lensSize"},
            {"edgeSmoothness", "edgeSmoothness"}},
        {{"lensAspect", {1.6F}}});
    addPass("official_bloom", {{"bloomBrightness", "bloomBrightness"}},
        {{"bloomThreshold", {0.308F}}, {"bloomRadius", {1.1F}}, {"bloomIntensity", {1.85F}},
            {"bloomSampleRate", {0.45F}}, {"bloomIteration", {3.0F}}});
    addPass("official_vignette_plus",
        {{"vignetteRadius", "vignetteRadius"}, {"vignetteDarkness", "vignetteDarkness"}},
        {{"vignetteSmoothness", {settingFloat("vignettePlus", "smoothness", 0.5F)}},
            {"vignetteNoiseSampleRate", {0.5F}}});
  } else if (effectName_ == "MessageLevelEffects") {
    addPass("official_glitch", {{"glitchRange", "glitchRange"}},
        {{"rgbChannelOffset", {0.0F}},
            {"layer1Speed", {0.25F}}, {"layer2Speed", {1.7F}}, {"layer3Speed", {0.25F}},
            {"layer1Strength", {0.31F, 0.0F}}, {"layer2Strength", {1.28F, 7.75F}},
            {"layer3Strength", {0.06F, -0.1F}}});
    addPass("official_light_band",
        {{"bandWidth", "bandWidth"}, {"bandSmoothness", "bandSmoothness"},
            {"bandDir", "bandDir"}, {"bandBrightness", "bandBrightness"}},
        {{"bandBrightness", {1.0F}}, {"bandDir", {0.0F}}});
    addPass("official_bloom", {{"bloomBrightness", "bloomBrightness"}},
        {{"bloomThreshold", {0.059F}}, {"bloomRadius", {0.6F}}, {"bloomIntensity", {1.61F}},
            {"bloomSampleRate", {0.35F}}, {"bloomIteration", {3.0F}}});
    addPass("official_vignette_plus",
        {{"vignetteRadius", "vignetteRadius"}, {"vignetteDarkness", "vignetteDarkness"},
            {"vignetteSmoothness", "vignetteSmoothness"},
            {"vignetteNoiseBrightness", "vignetteNoiseBrightness"},
            {"vignetteLineOpacity", "vignetteLineOpacity"}},
        {{"vignetteNoiseSampleRate", {0.5F}}});
  } else if (effectName_ == "HateLevelEffects") {
    addPass("official_glitch", {{"glitchRange", "glitchRange"}},
        {{"rgbChannelOffset", {0.0F}},
            {"layer1Speed", {0.2F}}, {"layer2Speed", {0.1F}}, {"layer3Speed", {0.1F}},
            {"layer1Strength", {1.0F, 2.0F}}, {"layer2Strength", {0.5F, 0.5F}},
            {"layer3Strength", {1.0F, 2.0F}}});
    addPass("official_rgb_shift", {{"shiftRadius", "shiftRadius"}},
        {{"screenDeform", {0.45F}}});
    addPass("official_vignette_plus",
        {{"vignetteRadius", "vignetteRadius"}, {"vignetteDarkness", "vignetteDarkness"},
            {"vignetteNoiseBrightness", "vignetteNoiseBrightness"},
            {"vignetteLineOpacity", "vignetteLineOpacity"}},
        {{"vignetteSmoothness", {settingFloat("vignettePlus", "smoothness", 0.8F)}},
            {"vignetteNoiseSaturation", {settingFloat("vignettePlus", "noiseSaturation", 0.0F)}},
            {"vignetteNoiseSampleRate", {settingFloat("vignettePlus", "noiseSampleRate", 0.5F)}}});
    addPass("official_lens_distortion",
        {{"convergence", "convergence"}, {"edgeSmoothness", "edgeSmoothness"}},
        {{"lensSize", {80.0F}}, {"lensAspect", {1.12F}}});
    addPass("official_esc",
        {{"brightness", "exposure"}, {"saturation", "saturate"}, {"contrast", "contrast"}},
        {{"averageColor", {0.5F, 0.5F, 0.5F}}});
  }
  return result;
}

} // namespace rechigros::game
