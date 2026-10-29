#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/audio/Decoder.hpp"
#include "rechigros/audio/Mixer.hpp"
#include "rechigros/audio/SkinSoundEffects.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include "TestScratch.hpp"
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool close(double first, double second, double epsilon = 1e-5) {
  return std::abs(first - second) <= epsilon;
}

constexpr double kTwoPi = 6.283185307179586;

/// Serialises a 16-bit stereo PCM WAV file into memory.
std::vector<std::uint8_t> makeWav(const std::vector<float>& interleavedStereo,
    std::uint32_t sampleRate) {
  const auto frameCount = interleavedStereo.size() / 2;
  const auto dataBytes = static_cast<std::uint32_t>(frameCount * 2 * sizeof(std::int16_t));
  const auto riffSize = 36U + dataBytes;

  std::vector<std::uint8_t> out;
  out.reserve(riffSize + 8);

  auto pushU32 = [&out](std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  };
  auto pushU16 = [&out](std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  };
  auto pushTag = [&out](const char* tag) {
    for (int index = 0; index < 4; ++index) {
      out.push_back(static_cast<std::uint8_t>(tag[index]));
    }
  };

  pushTag("RIFF");
  pushU32(riffSize);
  pushTag("WAVE");
  pushTag("fmt ");
  pushU32(16);
  pushU16(1);                                     // PCM
  pushU16(2);                                     // stereo
  pushU32(sampleRate);
  pushU32(sampleRate * 2 * sizeof(std::int16_t)); // byte rate
  pushU16(2 * sizeof(std::int16_t));              // block align
  pushU16(16);                                    // bits per sample
  pushTag("data");
  pushU32(dataBytes);

  for (const float sample : interleavedStereo) {
    const auto scaled = static_cast<int>(std::lround(
        std::clamp(static_cast<double>(sample), -1.0, 1.0) * 32767.0));
    pushU16(static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
  }
  return out;
}

std::vector<float> makeSine(float frequencyHz, std::uint32_t sampleRate, std::uint32_t frameCount,
    float amplitude) {
  std::vector<float> out(static_cast<std::size_t>(frameCount) * 2);
  for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
    const auto value = static_cast<float>(
        std::sin(kTwoPi * static_cast<double>(frequencyHz) * static_cast<double>(frame)
            / static_cast<double>(sampleRate))) * amplitude;
    out[static_cast<std::size_t>(frame) * 2 + 0] = value;
    out[static_cast<std::size_t>(frame) * 2 + 1] = value;
  }
  return out;
}

double rms(const float* frames, std::int64_t frameCount) {
  if (frames == nullptr || frameCount <= 0) return 0.0;
  double sum = 0.0;
  for (std::int64_t index = 0; index < frameCount * 2; ++index) {
    sum += static_cast<double>(frames[index]) * static_cast<double>(frames[index]);
  }
  return std::sqrt(sum / static_cast<double>(frameCount * 2));
}

/// Counts zero crossings on the left channel to recover the signal frequency.
double measureFrequency(const float* frames, std::int64_t frameCount, std::uint32_t sampleRate) {
  if (frames == nullptr || frameCount < 2) return 0.0;
  std::int64_t crossings = 0;
  float previous = frames[0];
  for (std::int64_t frame = 1; frame < frameCount; ++frame) {
    const float current = frames[frame * 2];
    if ((previous <= 0.0F && current > 0.0F) || (previous >= 0.0F && current < 0.0F)) ++crossings;
    previous = current;
  }
  const double seconds = static_cast<double>(frameCount) / static_cast<double>(sampleRate);
  return seconds > 0.0 ? static_cast<double>(crossings) / (2.0 * seconds) : 0.0;
}

std::filesystem::path temporaryWavPath(const char* name) {
  return rechigros::test::scratchDirectory("audio") / name;
}

void testWavDecoding() {
  constexpr std::uint32_t kRate = 44100;
  constexpr std::uint32_t kFrames = 4410; // 100 ms
  constexpr float kFrequency = 440.0F;
  constexpr float kAmplitude = 0.5F;

  const auto pcm = makeSine(kFrequency, kRate, kFrames, kAmplitude);
  const auto wav = makeWav(pcm, kRate);
  const auto path = temporaryWavPath("rechigros_audio_sine.wav");
  {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(output.good(), "write temporary WAV");
    output.write(reinterpret_cast<const char*>(wav.data()),
        static_cast<std::streamsize>(wav.size()));
    require(output.good(), "flush temporary WAV");
  }

  const auto decoded = rechigros::audio::Decoder::decodeFile(path, kRate);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  require(!decoded.empty(), "WAV decode produced samples");
  require(decoded.frameCount() == static_cast<std::int64_t>(kFrames), "WAV frame count");
  require(decoded.sampleRate() == kRate, "WAV sample rate");
  require(close(decoded.durationSeconds(), 0.1, 1e-6), "WAV duration");

  // 16-bit quantisation keeps the amplitude within one LSB of the source.
  const double measuredRms = rms(decoded.data(), decoded.frameCount());
  const double expectedRms = static_cast<double>(kAmplitude) / std::sqrt(2.0);
  require(std::abs(measuredRms - expectedRms) < 0.01, "WAV RMS matches the source amplitude");

  const double measuredFrequency = measureFrequency(decoded.data(), decoded.frameCount(), kRate);
  require(std::abs(measuredFrequency - kFrequency) < 5.0, "WAV frequency recovered");
}

void testWavResampling() {
  constexpr std::uint32_t kSourceRate = 22050;
  constexpr std::uint32_t kTargetRate = 44100;
  constexpr std::uint32_t kFrames = 2205; // 100 ms

  const auto pcm = makeSine(300.0F, kSourceRate, kFrames, 0.4F);
  const auto wav = makeWav(pcm, kSourceRate);
  const auto path = temporaryWavPath("rechigros_audio_resample.wav");
  {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(reinterpret_cast<const char*>(wav.data()),
        static_cast<std::streamsize>(wav.size()));
    require(output.good(), "write resample WAV");
  }

  const auto decoded = rechigros::audio::Decoder::decodeFile(path, kTargetRate);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  require(!decoded.empty(), "resampled decode produced samples");
  require(decoded.sampleRate() == kTargetRate, "resampled decode reports the target rate");
  require(std::abs(decoded.durationSeconds() - 0.1) < 0.005, "resampling preserves duration");

  const double measuredFrequency = measureFrequency(decoded.data(), decoded.frameCount(), kTargetRate);
  require(std::abs(measuredFrequency - 300.0) < 8.0, "resampling preserves pitch");
}

void testMixerSumAndClamp() {
  using namespace rechigros::audio;

  const std::vector<float> first{0.25F, -0.25F, 0.5F, -0.5F};
  const std::vector<float> second{0.25F, -0.25F, 0.75F, -0.75F};

  StereoSample mixed{};
  mixer::accumulate(mixed, first.data(), 2, 0, 1.0F);
  mixer::accumulate(mixed, second.data(), 2, 0, 1.0F);
  require(close(mixed.left, 0.5), "two voices sum on the left channel");
  require(close(mixed.right, -0.5), "two voices sum on the right channel");

  mixer::accumulate(mixed, second.data(), 2, 1, 1.0F);
  require(close(mixed.left, 1.25) && close(mixed.right, -1.25), "mixing past unity is allowed");
  require(close(mixer::clampSample(mixed.left), 1.0), "positive clamp");
  require(close(mixer::clampSample(mixed.right), -1.0), "negative clamp");

  // Reading outside the buffer must contribute nothing rather than read past the end.
  StereoSample untouched{};
  mixer::accumulate(untouched, first.data(), 2, 5, 1.0F);
  mixer::accumulate(untouched, first.data(), 2, -1, 1.0F);
  mixer::accumulate(untouched, nullptr, 2, 0, 1.0F);
  require(close(untouched.left, 0.0) && close(untouched.right, 0.0), "out-of-range reads are silent");

  // Gain scales each voice independently.
  StereoSample gained{};
  mixer::accumulate(gained, first.data(), 2, 0, 0.5F);
  require(close(gained.left, 0.125) && close(gained.right, -0.125), "voice gain scaling");
}

void testGainAndSpeedClamping() {
  using namespace rechigros::audio;
  require(close(mixer::clampGain(-1.0F), 0.0), "negative gain clamps to zero");
  require(close(mixer::clampGain(2.5F), 2.5), "gain above unity is preserved");
  require(close(mixer::clampGain(9.0F), mixer::kMaxGain), "excessive gain clamps to the maximum");
  require(close(mixer::clampSpeed(0.0F), mixer::kMinPlaybackSpeed), "zero speed clamps to the minimum");
  require(close(mixer::clampSpeed(9.0F), mixer::kMaxPlaybackSpeed), "excessive speed clamps");
  require(close(mixer::clampSpeed(1.5F), 1.5), "in-range speed is preserved");
}

void testPlayheadAdvance() {
  using namespace rechigros::audio;

  double position = 0.0;
  for (int frame = 0; frame < 480; ++frame) position = mixer::advancePosition(position, 1.0F);
  require(close(position, 480.0), "playhead advances one frame per output frame");

  position = 0.0;
  for (int frame = 0; frame < 100; ++frame) position = mixer::advancePosition(position, 1.5F);
  require(close(position, 150.0), "playhead advances by the playback speed");

  // Half speed must consume half as much music for the same output duration.
  double halfSpeed = 0.0;
  double fullSpeed = 0.0;
  for (int frame = 0; frame < 1000; ++frame) {
    halfSpeed = mixer::advancePosition(halfSpeed, 0.5F);
    fullSpeed = mixer::advancePosition(fullSpeed, 1.0F);
  }
  require(close(halfSpeed * 2.0, fullSpeed), "half speed consumes half the music");
}

void testLoopWrap() {
  using namespace rechigros::audio;

  require(mixer::wrapFrameIndex(-1, 10) == 9, "negative index wraps from the end");
  require(mixer::wrapFrameIndex(10, 10) == 0, "index past the end wraps to the start");
  require(mixer::wrapFrameIndex(25, 10) == 5, "index wraps multiple times");
  require(mixer::wrapFrameIndex(3, 0) == 0, "empty buffer wraps to zero");

  require(close(mixer::wrapPosition(-0.25, 10), 9.75), "negative fractional position wraps");
  require(close(mixer::wrapPosition(10.5, 10), 0.5), "fractional position past the end wraps");
  require(close(mixer::wrapPosition(1234.75, 10), 4.75), "large position wraps");
  require(close(mixer::wrapPosition(3.5, 0), 0.0), "empty buffer position wraps to zero");

  // Sampling with looping enabled must stay inside the buffer forever.
  const std::vector<float> ramp{0.0F, 0.0F, 0.5F, 0.5F, 1.0F, 1.0F};
  double position = 0.0;
  for (int step = 0; step < 100; ++step) {
    const auto sample = mixer::sampleMusic(ramp.data(), 3, position, true);
    require(sample.left >= -0.01F && sample.left <= 1.01F, "looped sample stays in range");
    position = mixer::advancePosition(position, 0.7);
  }

  // Without looping, positions past the end read as silence.
  const auto past = mixer::sampleMusic(ramp.data(), 3, 3.0, false);
  require(close(past.left, 0.0) && close(past.right, 0.0), "unlooped read past the end is silent");

  const auto negative = mixer::sampleMusic(ramp.data(), 3, -1.0, false);
  require(close(negative.left, 0.0), "unlooped read before the start is silent");

  // Interpolation between neighbouring frames: the left channel holds the frames
  // 0.0, 0.5 and 1.0, so a position halfway between the first two reads 0.25.
  const auto midpoint = mixer::sampleMusic(ramp.data(), 3, 0.5, false);
  require(close(midpoint.left, 0.25), "linear interpolation between frames");

  // The last frame interpolates towards the first only when looping.
  const auto lastFrame = mixer::sampleMusic(ramp.data(), 3, 2.5, false);
  require(close(lastFrame.left, 1.0), "final frame holds without looping");
  const auto wrappedLastFrame = mixer::sampleMusic(ramp.data(), 3, 2.5, true);
  require(close(wrappedLastFrame.left, 0.5), "final frame interpolates to the start when looping");
}

void testPlayheadInterpolation() {
  using namespace rechigros::audio;

  // Before any callback has run, the reported position is the stored one.
  require(close(mixer::estimatePlayheadFrames(0.0, 0, 1.0F, 0.0, 48000.0, 123.0), 123.0),
      "no callback history returns the committed position");

  // Half of a 256-frame callback at normal speed lands mid-window.
  const double halfway = mixer::estimatePlayheadFrames(
      1000.0, 256, 1.0F, 128.0 / 48000.0, 48000.0, 1256.0);
  require(close(halfway, 1128.0, 1e-6), "playhead interpolates within a callback");

  // Interpolation must never run past the frames the callback produced.
  const double overshoot = mixer::estimatePlayheadFrames(
      1000.0, 256, 1.0F, 10.0, 48000.0, 1256.0);
  require(close(overshoot, 1256.0), "playhead is bounded by the callback end");

  // It must also never run past the committed playhead when that is smaller.
  const double clamped = mixer::estimatePlayheadFrames(
      1000.0, 256, 1.0F, 10.0, 48000.0, 1100.0);
  require(close(clamped, 1100.0), "playhead is bounded by the committed position");

  // At double speed the playhead advances twice as fast.
  const double fast = mixer::estimatePlayheadFrames(
      0.0, 256, 2.0F, 128.0 / 48000.0, 48000.0, 512.0);
  require(close(fast, 256.0, 1e-6), "playhead interpolation follows playback speed");

  // Negative elapsed time cannot rewind the reported position.
  const double negative = mixer::estimatePlayheadFrames(
      500.0, 256, 1.0F, -1.0, 48000.0, 756.0);
  require(close(negative, 500.0), "negative elapsed time clamps at the callback start");
}

void testLowPass() {
  using namespace rechigros::audio;

  LowPassFilter filter;
  filter.setSampleRate(48000.0);
  filter.reset();

  // An open filter passes a low-frequency signal essentially unchanged. The peak
  // amplitude is tracked across the whole run; the last sample alone is meaningless
  // because the 200 Hz period does not end on a crest.
  float openPeak = 0.0F;
  for (int frame = 0; frame < 48000; ++frame) {
    const auto value = static_cast<float>(
        std::sin(kTwoPi * 200.0 * static_cast<double>(frame) / 48000.0)) * 0.5F;
    float left = value;
    float right = value;
    filter.process(left, right);
    openPeak = std::max(openPeak, std::max(std::abs(left), std::abs(right)));
  }
  require(openPeak > 0.3F, "open low-pass keeps a 200 Hz tone");

  // Once the cutoff has ramped down, the same tone is strongly attenuated.
  filter.reset();
  filter.setSampleRate(48000.0);
  filter.setTargetCutoff(LowPassFilter::kClosedCutoffHz);
  // Skip the ramp itself so the measurement reflects the settled filter.
  for (int frame = 0; frame < static_cast<int>(LowPassFilter::kRampSeconds * 48000.0F) + 4800; ++frame) {
    filter.advance(1.0F / 48000.0F);
    const auto value = static_cast<float>(
        std::sin(kTwoPi * 5000.0 * static_cast<double>(frame) / 48000.0)) * 0.5F;
    float left = value;
    float right = value;
    filter.process(left, right);
  }

  float closedPeak = 0.0F;
  for (int frame = 0; frame < 4800; ++frame) {
    filter.advance(1.0F / 48000.0F);
    const auto value = static_cast<float>(
        std::sin(kTwoPi * 5000.0 * static_cast<double>(frame) / 48000.0)) * 0.5F;
    float left = value;
    float right = value;
    filter.process(left, right);
    closedPeak = std::max(closedPeak, std::max(std::abs(left), std::abs(right)));
  }
  require(closedPeak < 0.05F, "closed low-pass attenuates a 5 kHz tone");
  require(close(filter.cutoff(), LowPassFilter::kClosedCutoffHz, 1.0), "cutoff reaches the target");

  // The ramp must complete in roughly kRampSeconds rather than instantly.
  LowPassFilter ramp;
  ramp.setSampleRate(48000.0);
  ramp.reset();
  ramp.setTargetCutoff(LowPassFilter::kClosedCutoffHz);
  const auto steps = static_cast<int>(LowPassFilter::kRampSeconds * 48000.0F) / 2;
  for (int frame = 0; frame < steps; ++frame) ramp.advance(1.0F / 48000.0F);
  const float midway = ramp.cutoff();
  require(midway < LowPassFilter::kOpenCutoffHz && midway > LowPassFilter::kClosedCutoffHz,
      "cutoff ramps gradually");
}

void testSynthesizedTone() {
  using namespace rechigros::audio;

  constexpr std::uint32_t kRate = 48000;
  const auto tone = synthesizeTone(880.0F, 0.05F, kRate, 0.35F);
  require(!tone.empty(), "synthesised tone has samples");

  const auto frameCount = static_cast<std::int64_t>(tone.size() / 2);
  require(frameCount == static_cast<std::int64_t>(kRate * 0.05), "synthesised tone length");

  // Fades must start and end at silence so triggering cannot click.
  require(std::abs(tone.front()) < 1e-4F, "tone starts at silence");
  require(std::abs(tone[static_cast<std::size_t>(frameCount - 1) * 2]) < 1e-3F,
      "tone ends at silence");

  const double measured = measureFrequency(tone.data(), frameCount, kRate);
  require(std::abs(measured - 880.0) < 20.0, "synthesised tone frequency");

  const auto peak = *std::max_element(tone.begin(), tone.end());
  require(peak <= 0.36F && peak > 0.3F, "synthesised tone amplitude");

  require(synthesizeTone(440.0F, 0.0F, kRate, 0.5F).empty(), "zero-length tone is empty");
  require(synthesizeTone(440.0F, 0.1F, 0, 0.5F).empty(), "zero-rate tone is empty");
}

void testDecoderRejectsInvalidInput() {
  using namespace rechigros::audio;

  require(Decoder::decodeFile({}, 48000).empty(), "empty path decodes to nothing");
  require(Decoder::decodeFile(std::filesystem::path{"rechigros-missing-file.wav"}, 48000).empty(),
      "missing file decodes to nothing");

  const std::vector<std::uint8_t> garbage(64, 0x7F);
  require(Decoder::decodeMemory(garbage.data(), garbage.size(), 48000).empty(),
      "garbage input decodes to nothing");
  require(Decoder::decodeMemory(nullptr, 0, 48000).empty(), "null input decodes to nothing");

  require(Decoder::isSupportedExtension(".wav"), "wav extension supported");
  require(Decoder::isSupportedExtension(".MP3"), "mp3 extension is case-insensitive");
  require(Decoder::isSupportedExtension(".flac"), "flac extension supported");
  require(!Decoder::isSupportedExtension("wav"), "extension requires a leading dot");
  require(!Decoder::isSupportedExtension(".txt"), "unknown extension rejected");
  require(Decoder::supportsVorbis() == Decoder::isSupportedExtension(".ogg"),
      "ogg support matches the compiled decoder set");
}

void testResampleStereo() {
  using namespace rechigros::audio;

  const std::vector<float> input{0.0F, 0.0F, 1.0F, 1.0F};
  const auto upsampled = mixer::resampleStereo(input.data(), 2, 24000, 48000);
  require(upsampled.size() == 8, "upsampling doubles the frame count");
  require(close(upsampled.front(), 0.0), "upsampling keeps the first sample");
  require(std::abs(upsampled.back() - 1.0F) < 1e-6F, "upsampling keeps the last sample");

  const auto downsampled = mixer::resampleStereo(input.data(), 2, 48000, 24000);
  require(downsampled.size() == 2, "downsampling halves the frame count");

  const auto unchanged = mixer::resampleStereo(input.data(), 2, 48000, 48000);
  require(unchanged == input, "matching rates copy the input");

  require(mixer::resampleStereo(nullptr, 0, 48000, 44100).empty(), "null input resamples to nothing");
}

constexpr std::uint32_t kEffectRate = 48000;

void writeEffectWav(const std::filesystem::path& path, float frequencyHz, float seconds) {
  const auto frameCount = static_cast<std::uint32_t>(
      std::lround(static_cast<double>(seconds) * static_cast<double>(kEffectRate)));
  const auto wav = makeWav(makeSine(frequencyHz, kEffectRate, frameCount, 0.4F), kEffectRate);
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  require(output.good(), "write effect WAV");
  output.write(reinterpret_cast<const char*>(wav.data()),
      static_cast<std::streamsize>(wav.size()));
  require(output.good(), "flush effect WAV");
}

void writeDamagedEffect(const std::filesystem::path& path) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  require(output.good(), "write damaged effect file");
  const char bytes[32] = {'n', 'o', 't', ' ', 'a', 'u', 'd', 'i', 'o'};
  output.write(bytes, static_cast<std::streamsize>(sizeof(bytes)));
  require(output.good(), "flush damaged effect file");
}

std::filesystem::path freshEffectDirectory(const char* name) {
  auto path = rechigros::test::scratchDirectory(std::string("audio-effects/") + name);
  std::error_code error;
  std::filesystem::remove_all(path, error);
  std::filesystem::create_directories(path, error);
  return path;
}

void requireEffectFrequency(const rechigros::audio::AudioBuffer& buffer, double expected,
    const char* message) {
  require(!buffer.empty(), message);
  const double measured =
      measureFrequency(buffer.data(), buffer.frameCount(), buffer.sampleRate());
  require(std::abs(measured - expected) < 6.0, message);
}

void testSkinSoundEffects() {
  using namespace rechigros::audio;
  using namespace rechigros::game;

  const auto assetRoot = freshEffectDirectory("assets");
  std::filesystem::create_directories(assetRoot / "audio");
  writeEffectWav(assetRoot / "audio" / "tap.wav", 300.0F, 0.2F);
  writeEffectWav(assetRoot / "audio" / "drag.wav", 400.0F, 0.2F);
  writeEffectWav(assetRoot / "audio" / "flick.wav", 500.0F, 0.2F);

  // Without a skin every bank comes from the shipped asset set.
  const auto builtIn = resolveSoundEffects(assetRoot, {}, kEffectRate);
  require(builtIn.anyLoaded(), "built-in effects resolve");
  requireEffectFrequency(builtIn.bankFor(NOTE_TAP), 300.0, "built-in tap");
  requireEffectFrequency(builtIn.bankFor(NOTE_DRAG), 400.0, "built-in drag");
  requireEffectFrequency(builtIn.bankFor(NOTE_FLICK), 500.0, "built-in flick");
  require(builtIn.statusFor(NOTE_TAP).source == SoundEffectSource::BuiltIn,
      "built-in source is reported");
  require(builtIn.statusFor(NOTE_TAP).label.rfind("built-in:", 0) == 0,
      "built-in label names the asset");

  // Hold reads the tap bank rather than carrying its own samples.
  require(soundEffectBankFor(NOTE_HOLD) == soundEffectBankFor(NOTE_TAP), "hold maps to tap");
  require(&builtIn.bankFor(NOTE_HOLD) == &builtIn.bankFor(NOTE_TAP),
      "hold shares the tap samples");
  require(builtIn.statusFor(NOTE_HOLD).label == builtIn.statusFor(NOTE_TAP).label,
      "hold shares the tap label");

  // A full skin overrides every bank with its own files.
  const auto skin = freshEffectDirectory("skin-full");
  writeEffectWav(skin / "click.wav", 900.0F, 0.2F);
  writeEffectWav(skin / "click.ogg", 700.0F, 0.2F);
  writeEffectWav(skin / "drag.wav", 950.0F, 0.2F);
  writeEffectWav(skin / "flick.wav", 1000.0F, 0.2F);
  const auto skinned = resolveSoundEffects(assetRoot, skin, kEffectRate);
  requireEffectFrequency(skinned.bankFor(NOTE_TAP), 900.0, "skin click overrides tap");
  requireEffectFrequency(skinned.bankFor(NOTE_DRAG), 950.0, "skin drag overrides drag");
  requireEffectFrequency(skinned.bankFor(NOTE_FLICK), 1000.0, "skin flick overrides flick");
  requireEffectFrequency(skinned.bankFor(NOTE_HOLD), 900.0, "hold follows the skin click");
  require(skinned.statusFor(NOTE_TAP).source == SoundEffectSource::Skin, "skin source is reported");
  require(skinned.statusFor(NOTE_TAP).label.rfind("skin:", 0) == 0,
      "skin label names the file");
  require(skinned.statusFor(NOTE_TAP).label.find("click.wav") != std::string::npos,
      "skin label names the chosen extension");

  // Resolving again without a skin returns to the built-in set: no skin survives a reload.
  const auto restored = resolveSoundEffects(assetRoot, {}, kEffectRate);
  requireEffectFrequency(restored.bankFor(NOTE_TAP), 300.0, "reload restores the built-in tap");
  require(restored.statusFor(NOTE_DRAG).source == SoundEffectSource::BuiltIn,
      "reload restores every built-in source");

  // A damaged click falls through to the next extension while a missing drag falls back to
  // the built-in bank, so one bad skin file cannot silence a whole run.
  const auto partial = freshEffectDirectory("skin-partial");
  writeDamagedEffect(partial / "click.wav");
  writeEffectWav(partial / "click.ogg", 1200.0F, 0.2F);
  writeEffectWav(partial / "flick.wav", 1300.0F, 0.2F);
  const auto partialSet = resolveSoundEffects(assetRoot, partial, kEffectRate);
  requireEffectFrequency(partialSet.bankFor(NOTE_TAP), 1200.0,
      "damaged wav falls through to ogg");
  requireEffectFrequency(partialSet.bankFor(NOTE_DRAG), 400.0, "missing skin drag falls back");
  requireEffectFrequency(partialSet.bankFor(NOTE_FLICK), 1300.0, "skin flick still applies");

  // With no decodable built-in file either, a synthesised tone keeps every bank audible.
  const auto emptyAssets = freshEffectDirectory("assets-empty");
  std::filesystem::create_directories(emptyAssets / "audio");
  writeDamagedEffect(emptyAssets / "audio" / "tap.wav");
  writeDamagedEffect(emptyAssets / "audio" / "drag.wav");
  writeDamagedEffect(emptyAssets / "audio" / "flick.wav");
  const auto synthesized = resolveSoundEffects(emptyAssets, {}, kEffectRate);
  require(synthesized.statusFor(NOTE_TAP).source == SoundEffectSource::Synthesized,
      "damaged built-in falls back to a synthesised tone");
  require(synthesized.statusFor(NOTE_TAP).label.rfind("synthesized:", 0) == 0,
      "synthesised label reports the tone");
  require(!synthesized.bankFor(NOTE_TAP).empty(), "synthesised tap has samples");
}

void testSoundEffectDiagnostics() {
  using namespace rechigros::audio;
  using namespace rechigros::game;

  AudioEngine engine;
  require(engine.soundEffectSource(NOTE_TAP) == SoundEffectSource::None,
      "unloaded tap source is none");
  require(engine.soundEffectSource(NOTE_HOLD) == engine.soundEffectSource(NOTE_TAP),
      "unloaded hold reports the tap source");
  require(engine.soundEffectSourceLabel(NOTE_TAP) == "none", "unloaded tap label");
  require(engine.soundEffectSourcesLabel() == "tap=none drag=none flick=none",
      "unloaded batch label lists every bank");
}
} // namespace

int main() {
  testWavDecoding();
  testWavResampling();
  testMixerSumAndClamp();
  testGainAndSpeedClamping();
  testPlayheadAdvance();
  testLoopWrap();
  testPlayheadInterpolation();
  testLowPass();
  testSynthesizedTone();
  testDecoderRejectsInvalidInput();
  testResampleStereo();
  testSkinSoundEffects();
  testSoundEffectDiagnostics();

  std::cout << "All ReChigros audio tests passed\n";
  return 0;
}
