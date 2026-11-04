#pragma once

#include "rechigros/audio/Decoder.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rechigros::audio {

/// Number of effect banks, indexed by note type. Hold shares the tap bank, so slot 3 is never
/// filled and slot 0 is unused.
inline constexpr std::size_t kSoundEffectBankCount = 5U;

/// Extensions tried for a skin effect file, in the order they are tried.
inline constexpr std::array<const char*, 3> kSoundEffectExtensions{{".wav", ".ogg", ".mp3"}};

/// Where a bank's samples came from.
enum class SoundEffectSource {
  None,
  Synthesized,
  BuiltIn,
  Skin,
};

/// One bank's provenance, together with a one-line label for diagnostics.
struct SoundEffectStatus {
  SoundEffectSource source{SoundEffectSource::None};
  std::filesystem::path path;
  std::string label;
};

/// The bank a note type reads. Hold reuses tap; an unknown type maps to 0, which is never
/// filled.
[[nodiscard]] constexpr int soundEffectBankFor(int noteType) noexcept {
  switch (noteType) {
    case game::NOTE_TAP:
      return game::NOTE_TAP;
    case game::NOTE_HOLD:
      return game::NOTE_TAP;
    case game::NOTE_DRAG:
      return game::NOTE_DRAG;
    case game::NOTE_FLICK:
      return game::NOTE_FLICK;
    default:
      return 0;
  }
}

/// Every effect bank resolved in one pass, indexed by note type. Hold notes read the tap bank.
struct SoundEffectSet {
  std::array<AudioBuffer, kSoundEffectBankCount> banks;
  std::array<SoundEffectStatus, kSoundEffectBankCount> status;

  [[nodiscard]] const AudioBuffer& bankFor(int noteType) const noexcept {
    return banks[static_cast<std::size_t>(soundEffectBankFor(noteType))];
  }

  [[nodiscard]] const SoundEffectStatus& statusFor(int noteType) const noexcept {
    return status[static_cast<std::size_t>(soundEffectBankFor(noteType))];
  }

  [[nodiscard]] bool anyLoaded() const noexcept;
};

/// Resolves tap, drag and flick effects at `sampleRate`. Each bank takes the first skin file
/// named click, drag or flick with an extension from `kSoundEffectExtensions` that decodes,
/// then the built-in asset below `assetRoot`, then a synthesised tone. A file that is present
/// but cannot be decoded does not stop the search. A `skinDirectory` that is not a directory
/// skips the skin step, so resolving again replaces any earlier skin choice.
[[nodiscard]] SoundEffectSet resolveSoundEffects(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory, std::uint32_t sampleRate);

} // namespace rechigros::audio
