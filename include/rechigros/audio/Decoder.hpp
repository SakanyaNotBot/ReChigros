#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace rechigros::audio {

/// A block of decoded audio, always interleaved stereo at the sample rate it was
/// decoded for. Decoding and rate conversion happen at load time so the mixing
/// path never has to convert anything.
class AudioBuffer final {
public:
  AudioBuffer() = default;
  AudioBuffer(std::vector<float> interleavedStereo, std::uint32_t sampleRate);

  [[nodiscard]] const float* data() const noexcept { return samples_.data(); }
  [[nodiscard]] float* data() noexcept { return samples_.data(); }
  [[nodiscard]] std::int64_t frameCount() const noexcept {
    return static_cast<std::int64_t>(samples_.size() / 2);
  }
  [[nodiscard]] bool empty() const noexcept { return samples_.empty(); }
  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] double durationSeconds() const noexcept;

  void clear() noexcept;
  void assign(std::vector<float> interleavedStereo, std::uint32_t sampleRate);

private:
  std::vector<float> samples_;
  std::uint32_t sampleRate_{};
};

/// Decodes audio files into interleaved stereo float PCM. Every decoder path
/// performs its channel and sample-rate conversion during the call, so results
/// are ready to mix without further work.
class Decoder final {
public:
  /// Decodes one file. `targetSampleRate` must be non-zero; the result is
  /// resampled and upmixed to stereo when the source differs.
  static AudioBuffer decodeFile(const std::filesystem::path& path, std::uint32_t targetSampleRate);

  /// Decodes an in-memory encoded file, using the same rules as decodeFile.
  static AudioBuffer decodeMemory(const void* data, std::size_t size, std::uint32_t targetSampleRate);

  /// True when `extension` (with the leading dot, case-insensitive) names a
  /// container this build can decode.
  [[nodiscard]] static bool isSupportedExtension(std::string_view extension) noexcept;

  /// Number of container formats the build can decode. Vorbis support depends on
  /// the bundled decoder being compiled in.
  [[nodiscard]] static bool supportsVorbis() noexcept;
};

/// Deterministic fallback tone used when an effect asset is missing. Produces
/// interleaved stereo f32 with short fade in/out edges so triggering it cannot click.
std::vector<float> synthesizeTone(float frequencyHz, float durationSeconds,
    std::uint32_t sampleRate, float amplitude);

} // namespace rechigros::audio
