#include "rechigros/audio/Decoder.hpp"
#include "rechigros/audio/Mixer.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace rechigros::audio {
namespace {
constexpr double kTwoPi = 6.283185307179586;
/// Refuse absurdly long results so a corrupt header cannot exhaust memory.
constexpr std::uint64_t kMaxDecodedFrames = 48U * 60U * 60U * 192000U;
/// Frames requested per read; the decoder converts channels and rate internally.
constexpr ma_uint64 kChunkFrames = 4096;

AudioBuffer readAllFrames(ma_decoder& decoder, std::uint32_t targetSampleRate) {
  ma_uint64 announcedFrames{};
  if (ma_decoder_get_length_in_pcm_frames(&decoder, &announcedFrames) != MA_SUCCESS
      || announcedFrames > kMaxDecodedFrames) {
    announcedFrames = 0;
  }

  std::vector<float> frames;
  if (announcedFrames > 0) frames.reserve(static_cast<std::size_t>(announcedFrames) * 2);

  std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames) * 2);
  for (;;) {
    ma_uint64 read{};
    const auto result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &read);
    if (read == 0) break;
    frames.insert(frames.end(), chunk.begin(),
        chunk.begin() + static_cast<std::ptrdiff_t>(read * 2));
    if (result != MA_SUCCESS) break;
    if (frames.size() / 2 > kMaxDecodedFrames) return {};
  }

  if (frames.empty()) return {};
  return AudioBuffer{std::move(frames), targetSampleRate};
}
} // namespace

AudioBuffer::AudioBuffer(std::vector<float> interleavedStereo, std::uint32_t sampleRate)
    : samples_(std::move(interleavedStereo)), sampleRate_(sampleRate) {}

double AudioBuffer::durationSeconds() const noexcept {
  if (sampleRate_ == 0) return 0.0;
  return static_cast<double>(frameCount()) / static_cast<double>(sampleRate_);
}

void AudioBuffer::clear() noexcept {
  samples_.clear();
  samples_.shrink_to_fit();
  sampleRate_ = 0;
}

void AudioBuffer::assign(std::vector<float> interleavedStereo, std::uint32_t sampleRate) {
  samples_ = std::move(interleavedStereo);
  sampleRate_ = sampleRate;
}

AudioBuffer Decoder::decodeFile(const std::filesystem::path& path, std::uint32_t targetSampleRate) {
  if (path.empty() || targetSampleRate == 0) return {};

  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) return {};

  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
  ma_decoder decoder{};
  ma_result opened = MA_ERROR;
#ifdef _WIN32
  // The narrow entry point resolves the path through the ANSI code page, which fails
  // for any user or install path outside that code page.
  opened = ma_decoder_init_file_w(path.wstring().c_str(), &config, &decoder);
#else
  const auto utf8 = path.u8string();
  opened = ma_decoder_init_file(reinterpret_cast<const char*>(utf8.c_str()), &config, &decoder);
#endif
  if (opened != MA_SUCCESS) return {};

  const AudioBuffer result = readAllFrames(decoder, targetSampleRate);
  ma_decoder_uninit(&decoder);
  return result;
}

AudioBuffer Decoder::decodeMemory(const void* data, std::size_t size,
    std::uint32_t targetSampleRate) {
  if (data == nullptr || size == 0 || targetSampleRate == 0) return {};

  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, targetSampleRate);
  ma_decoder decoder{};
  if (ma_decoder_init_memory(data, size, &config, &decoder) != MA_SUCCESS) return {};

  const AudioBuffer result = readAllFrames(decoder, targetSampleRate);
  ma_decoder_uninit(&decoder);
  return result;
}

bool Decoder::isSupportedExtension(std::string_view extension) noexcept {
  if (extension.size() < 2 || extension.front() != '.') return false;

  char lowered[8]{};
  const auto length = std::min(extension.size(), sizeof(lowered) - 1);
  for (std::size_t index = 0; index < length; ++index) {
    const auto value = static_cast<unsigned char>(extension[index]);
    lowered[index] = static_cast<char>(value >= 'A' && value <= 'Z' ? value - 'A' + 'a' : value);
  }

  const std::string_view candidate{lowered, length};
  if (candidate == ".wav" || candidate == ".mp3" || candidate == ".flac") return true;
  return supportsVorbis() && candidate == ".ogg";
}

bool Decoder::supportsVorbis() noexcept {
#if RECHIGROS_AUDIO_HAS_VORBIS
  return true;
#else
  return false;
#endif
}

std::vector<float> synthesizeTone(float frequencyHz, float durationSeconds,
    std::uint32_t sampleRate, float amplitude) {
  std::vector<float> out;
  if (sampleRate == 0 || !(durationSeconds > 0.0F) || !std::isfinite(durationSeconds)) return out;

  const auto frameCount = static_cast<std::int64_t>(
      std::llround(static_cast<double>(sampleRate) * static_cast<double>(durationSeconds)));
  if (frameCount <= 0) return out;
  out.resize(static_cast<std::size_t>(frameCount) * 2);

  constexpr float kFadeInSeconds = 0.002F;
  constexpr float kFadeOutSeconds = 0.010F;
  const auto fadeInFrames = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(kFadeInSeconds * static_cast<float>(sampleRate)));
  const auto fadeOutFrames = std::max<std::int64_t>(
      1, static_cast<std::int64_t>(kFadeOutSeconds * static_cast<float>(sampleRate)));

  for (std::int64_t frame = 0; frame < frameCount; ++frame) {
    float envelope = 1.0F;
    if (frame < fadeInFrames) {
      envelope = static_cast<float>(frame) / static_cast<float>(fadeInFrames);
    }
    const auto remaining = frameCount - 1 - frame;
    if (remaining < fadeOutFrames) {
      envelope = std::min(envelope, static_cast<float>(remaining) / static_cast<float>(fadeOutFrames));
    }

    const double phase = kTwoPi * static_cast<double>(frequencyHz)
        * static_cast<double>(frame) / static_cast<double>(sampleRate);
    const auto value = static_cast<float>(std::sin(phase)) * amplitude * envelope;
    out[static_cast<std::size_t>(frame) * 2 + 0] = value;
    out[static_cast<std::size_t>(frame) * 2 + 1] = value;
  }
  return out;
}

} // namespace rechigros::audio
