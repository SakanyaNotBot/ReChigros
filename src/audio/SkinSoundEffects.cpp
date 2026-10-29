#include "rechigros/audio/SkinSoundEffects.hpp"

#include "rechigros/platform/AssetManager.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace rechigros::audio {
namespace {
struct EffectSpec {
  int noteType;
  const char* skinName;
  const char* assetPath;
  float frequencyHz;
  float seconds;
  float amplitude;
};

constexpr std::array<EffectSpec, 3> kEffects{{
    {game::NOTE_TAP, "click", "audio/tap.wav", 880.0F, 0.030F, 0.35F},
    {game::NOTE_DRAG, "drag", "audio/drag.wav", 660.0F, 0.035F, 0.30F},
    {game::NOTE_FLICK, "flick", "audio/flick.wav", 1100.0F, 0.025F, 0.35F},
}};

struct DecodedEffect {
  AudioBuffer buffer;
  std::filesystem::path path;
};

std::string synthesizedLabel(const EffectSpec& spec) {
  return "synthesized:" + std::to_string(static_cast<int>(std::lround(spec.frequencyHz))) + "Hz";
}

DecodedEffect decodeSkinEffect(const std::filesystem::path& skinDirectory,
    const EffectSpec& spec, std::uint32_t sampleRate) {
  if (skinDirectory.empty()) return {};
  std::error_code error;
  if (!std::filesystem::is_directory(skinDirectory, error) || error) return {};

  const std::string baseName = spec.skinName;
  for (const char* extension : kSoundEffectExtensions) {
    error.clear();
    const auto candidate = skinDirectory / (baseName + extension);
    if (!std::filesystem::is_regular_file(candidate, error) || error) continue;
    auto decoded = Decoder::decodeFile(candidate, sampleRate);
    if (!decoded.empty()) return {std::move(decoded), candidate};
  }
  return {};
}

DecodedEffect decodeBuiltInEffect(const std::filesystem::path& assetRoot,
    const EffectSpec& spec, std::uint32_t sampleRate) {
  const auto path = platform::AssetManager::materialize(assetRoot, spec.assetPath);
  if (path.empty()) return {};
  return {Decoder::decodeFile(path, sampleRate), path};
}
} // namespace

bool SoundEffectSet::anyLoaded() const noexcept {
  for (const auto& bank : banks) {
    if (!bank.empty()) return true;
  }
  return false;
}

SoundEffectSet resolveSoundEffects(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory, std::uint32_t sampleRate) {
  SoundEffectSet result;
  for (const auto& spec : kEffects) {
    const auto slot = static_cast<std::size_t>(soundEffectBankFor(spec.noteType));

    auto skin = decodeSkinEffect(skinDirectory, spec, sampleRate);
    if (!skin.buffer.empty()) {
      const auto label = "skin:" + skin.path.generic_string();
      result.banks[slot] = std::move(skin.buffer);
      result.status[slot] = {SoundEffectSource::Skin, std::move(skin.path), label};
      continue;
    }

    auto builtIn = decodeBuiltInEffect(assetRoot, spec, sampleRate);
    if (!builtIn.buffer.empty()) {
      result.banks[slot] = std::move(builtIn.buffer);
      result.status[slot] = {SoundEffectSource::BuiltIn, builtIn.path,
          "built-in:" + std::string(spec.assetPath)};
      continue;
    }

    result.banks[slot].assign(
        synthesizeTone(spec.frequencyHz, spec.seconds, sampleRate, spec.amplitude), sampleRate);
    result.status[slot] = {SoundEffectSource::Synthesized, {}, synthesizedLabel(spec)};
  }
  return result;
}

} // namespace rechigros::audio
