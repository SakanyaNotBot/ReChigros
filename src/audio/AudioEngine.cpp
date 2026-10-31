#include "rechigros/audio/AudioEngine.hpp"

#include "rechigros/audio/Decoder.hpp"
#include "rechigros/audio/Mixer.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace rechigros::audio {
namespace {
constexpr std::uint32_t kOutputChannels = 2;
/// Requested frames per device callback. Small periods keep hit sounds tight.
constexpr std::uint32_t kPeriodFrames = 256;
/// Requested number of periods buffered between the mixer and the speaker.
constexpr std::uint32_t kPeriodCount = 2;
/// Upper bound on effects sounding at the same time.
constexpr std::uint32_t kMaxVoices = 32;
/// Depth of the lock-free effect queue. Triggers are dropped when it is full. The index
/// registers are 32-bit, so the capacity matches them and the ring arithmetic stays in one
/// type.
constexpr std::uint32_t kSfxQueueCapacity = 1024;
/// Sentinel for "no seek is pending".
constexpr double kNoSeek = -1.0;
/// How long buffer replacement waits for the audio thread to stand down.
constexpr std::int64_t kQuiesceTimeoutNs = 200'000'000;

std::int64_t nowNanoseconds() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Single-producer single-consumer queue of effect triggers.
class SfxQueue final {
public:
  struct Event {
    int noteType{};
    const AudioBuffer* customBuffer{};
    std::int64_t targetOutputFrame{};
  };

  /// Returns false when the queue is full and the trigger has to be dropped.
  bool push(Event event) noexcept {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % kSfxQueueCapacity;
    if (next == read_.load(std::memory_order_acquire)) return false;
    events_[write] = event;
    write_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(Event& event) noexcept {
    const auto read = read_.load(std::memory_order_relaxed);
    if (read == write_.load(std::memory_order_acquire)) return false;
    event = events_[read];
    read_.store((read + 1) % kSfxQueueCapacity, std::memory_order_release);
    return true;
  }

  void clear() noexcept {
    read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
  }

private:
  std::atomic<std::uint32_t> write_{0};
  std::atomic<std::uint32_t> read_{0};
  std::array<Event, kSfxQueueCapacity> events_{};
};

/// One sounding effect. Owned by the audio thread.
struct Voice {
  const float* frames{};
  std::int64_t frameCount{};
  std::int64_t frameIndex{};
  std::int64_t startOffsetFrames{};
  bool active{};
};

void mixSilence(float* output, std::uint32_t frameCount) noexcept {
  std::memset(output, 0, static_cast<std::size_t>(frameCount) * kOutputChannels * sizeof(float));
}
} // namespace

/// Implementation notes
/// --------------------
/// The device callback owns mixing, the voice pool and the low-pass state. Threads
/// that want to replace decoded buffers take an exclusive window: they publish a
/// request, the callback observes it, drops every voice and then emits silence
/// until the window closes. That keeps decoded memory alive only while nothing is
/// reading it, without blocking the callback on a mutex.
struct AudioEngine::Impl final {
  Impl() = default;
  ~Impl() { closeDevice(); }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  // -- device lifecycle ------------------------------------------------

  bool openDevice() {
    std::lock_guard<std::mutex> lock{deviceMutex_};
    if (deviceOpen_) return true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kOutputChannels;
    config.sampleRate = 0; // Follow the device rate; decoding converts at load time.
    config.periodSizeInFrames = kPeriodFrames;
    config.periods = kPeriodCount;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = &Impl::dataCallback;
    config.notificationCallback = &Impl::notificationCallback;
    config.pUserData = this;
    config.noClip = MA_TRUE; // Every mixed frame is clamped here.
    config.aaudio.usage = ma_aaudio_usage_game;
    config.aaudio.contentType = ma_aaudio_content_type_music;
    config.opensl.streamType = ma_opensl_stream_type_media;

    if (ma_device_init(nullptr, &config, &device_) != MA_SUCCESS) return false;

    deviceOpen_ = true;
    {
      std::lock_guard<std::mutex> state{stateMutex_};
      const auto previousRate = sampleRate_.load(std::memory_order_relaxed);
      const auto newRate = device_.sampleRate != 0 ? device_.sampleRate : 48000U;
      sampleRate_.store(newRate, std::memory_order_release);
      periodFrames_.store(device_.playback.internalPeriodSizeInFrames != 0
          ? device_.playback.internalPeriodSizeInFrames : kPeriodFrames, std::memory_order_release);
      periodCount_.store(device_.playback.internalPeriods != 0
          ? device_.playback.internalPeriods : kPeriodCount, std::memory_order_release);
      lowPass_.setSampleRate(static_cast<double>(newRate));
      if (previousRate != 0 && previousRate != newRate) {
        retimeBuffers(previousRate, newRate);
      }
    }

    running_.store(false, std::memory_order_release);
    deviceLost_.store(false, std::memory_order_release);
    return true;
  }

  void closeDevice() {
    std::lock_guard<std::mutex> lock{deviceMutex_};
    if (!deviceOpen_) return;
    ma_device_uninit(&device_);
    deviceOpen_ = false;
    running_.store(false, std::memory_order_release);
  }

  bool startDevice() {
    if (!openDevice()) return false;
    std::lock_guard<std::mutex> lock{deviceMutex_};
    if (!deviceOpen_) return false;
    if (!ma_device_is_started(&device_) && ma_device_start(&device_) != MA_SUCCESS) return false;
    running_.store(true, std::memory_order_release);
    return true;
  }

  void stopDevice() {
    std::lock_guard<std::mutex> lock{deviceMutex_};
    if (!deviceOpen_) return;
    if (ma_device_is_started(&device_)) ma_device_stop(&device_);
    running_.store(false, std::memory_order_release);
  }

  /// Reopens the device after it was lost, restoring the previous run state.
  void repairDevice() {
    if (!deviceLost_.exchange(false, std::memory_order_acq_rel)) return;
    const bool wasRunning = running_.load(std::memory_order_acquire);
    closeDevice();
    if (openDevice() && wasRunning) startDevice();
  }

  // -- exclusive buffer access -----------------------------------------

  /// Requests an exclusive window for replacing decoded buffers. Returns once the
  /// audio thread has dropped every reference to them.
  void beginExclusive() {
    if (!running_.load(std::memory_order_acquire)) {
      flushVoices();
      queue_.clear();
      return;
    }
    const auto request = exclusiveCounter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    exclusiveRequest_.store(request, std::memory_order_release);

    const auto deadline = nowNanoseconds() + kQuiesceTimeoutNs;
    while (exclusiveAck_.load(std::memory_order_acquire) != request) {
      if (nowNanoseconds() > deadline) {
        // The callback stopped answering; fall back to a synchronous device stop.
        stopDevice();
        flushVoices();
        queue_.clear();
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds{200});
    }
  }

  /// Ends the window opened by beginExclusive.
  void endExclusive() { exclusiveRequest_.store(0, std::memory_order_release); }

  /// Holds an exclusive window for the lifetime of the object.
  class ExclusiveScope final {
  public:
    explicit ExclusiveScope(Impl& impl) : impl_(impl) { impl_.beginExclusive(); }
    ~ExclusiveScope() { impl_.endExclusive(); }
    ExclusiveScope(const ExclusiveScope&) = delete;
    ExclusiveScope& operator=(const ExclusiveScope&) = delete;

  private:
    Impl& impl_;
  };

  /// Re-encodes decoded buffers for a new device rate and keeps the playhead on
  /// the same musical position.
  void retimeBuffers(std::uint32_t fromRate, std::uint32_t toRate) {
    if (fromRate == 0 || toRate == 0 || fromRate == toRate) return;
    const double scale = static_cast<double>(toRate) / static_cast<double>(fromRate);

    auto retime = [&](AudioBuffer& buffer) {
      if (buffer.empty()) return;
      auto frames = mixer::resampleStereo(buffer.data(), buffer.frameCount(), fromRate, toRate);
      buffer.assign(std::move(frames), toRate);
    };
    retime(music_);
    for (auto& bank : banks_) retime(bank);
    for (auto& [path, bank] : customBanks_) retime(*bank);

    const double playhead = playheadFrames_.load(std::memory_order_acquire);
    if (playhead >= 0.0) playheadFrames_.store(playhead * scale, std::memory_order_release);
  }

  // -- audio thread ----------------------------------------------------

  static void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    static_cast<Impl*>(device->pUserData)->mix(static_cast<float*>(output), frameCount);
  }

  static void notificationCallback(const ma_device_notification* notification) {
    if (notification == nullptr || notification->pDevice == nullptr) return;
    auto* self = static_cast<Impl*>(notification->pDevice->pUserData);
    if (self == nullptr) return;
    switch (notification->type) {
      case ma_device_notification_type_started:
        self->running_.store(true, std::memory_order_release);
        break;
      case ma_device_notification_type_stopped:
        // A deliberate ma_device_stop is not a device loss: the same device can be started
        // again. Rebuilding it here would turn every pause/resume into a risky reinit.
        self->running_.store(false, std::memory_order_release);
        break;
      case ma_device_notification_type_rerouted:
      case ma_device_notification_type_interruption_began:
        self->running_.store(false, std::memory_order_release);
        self->deviceLost_.store(true, std::memory_order_release);
        break;
      default:
        break;
    }
  }

  void flushVoices() {
    for (auto& voice : voices_) voice = Voice{};
  }

  Voice* claimVoice() {
    for (auto& voice : voices_) {
      if (!voice.active) return &voice;
    }
    // Every slot is busy: reuse the voice that is furthest through its playback.
    Voice* oldest = &voices_[0];
    for (auto& voice : voices_) {
      if (voice.frameIndex - voice.startOffsetFrames
          > oldest->frameIndex - oldest->startOffsetFrames) {
        oldest = &voice;
      }
    }
    return oldest;
  }

  AudioBuffer* bankFor(int noteType) noexcept {
    if (noteType <= 0 || static_cast<std::size_t>(noteType) >= banks_.size()) return nullptr;
    return &banks_[static_cast<std::size_t>(noteType)];
  }

  void spawnVoice(const SfxQueue::Event& event, std::int64_t callbackStartOutputFrames) {
    auto* bank = event.customBuffer != nullptr
        ? const_cast<AudioBuffer*>(event.customBuffer) : bankFor(event.noteType);
    if (bank == nullptr || bank->empty()) return;

    Voice* voice = claimVoice();
    voice->frames = bank->data();
    voice->frameCount = bank->frameCount();
    voice->frameIndex = 0;
    voice->startOffsetFrames = std::max<std::int64_t>(
        0, event.targetOutputFrame - callbackStartOutputFrames);
    voice->active = true;
  }

  /// Number of output frames the device is estimated to have consumed by now,
  /// used to place a trigger on the frame the caller is hearing.
  std::int64_t estimateGeneratedOutputFrameNow() const noexcept {
    const auto startNs = callbackStartNanos_.load(std::memory_order_acquire);
    const auto startOutput = callbackStartOutputFrames_.load(std::memory_order_acquire);
    const auto endOutput = generatedOutputFrames_.load(std::memory_order_acquire);
    const auto callbackFrames = callbackFrameCount_.load(std::memory_order_acquire);
    const auto rate = sampleRate_.load(std::memory_order_acquire);
    if (startNs == 0 || callbackFrames <= 0 || rate == 0) return endOutput;

    const double elapsed = static_cast<double>(nowNanoseconds() - startNs) * 1.0e-9;
    double generated = static_cast<double>(startOutput) + elapsed * static_cast<double>(rate);
    const double maximum = std::min(
        static_cast<double>(startOutput + callbackFrames), static_cast<double>(endOutput));
    if (generated > maximum) generated = maximum;
    if (generated < static_cast<double>(startOutput)) generated = static_cast<double>(startOutput);
    return static_cast<std::int64_t>(std::llround(generated));
  }

  void mix(float* output, std::uint32_t frameCount) {
    if (output == nullptr || frameCount == 0) return;

    // Honour a pending exclusive window before touching any decoded buffer.
    const auto exclusive = exclusiveRequest_.load(std::memory_order_acquire);
    if (exclusive != 0) {
      flushVoices();
      queue_.clear();
      lowPass_.reset();
      playheadResetPending_.store(false, std::memory_order_relaxed);
      exclusiveAck_.store(exclusive, std::memory_order_release);
      mixSilence(output, frameCount);
      return;
    }
    if (playheadResetPending_.exchange(false, std::memory_order_relaxed)) lowPass_.reset();

    const auto callbackStartOutputFrames = generatedOutputFrames_.load(std::memory_order_relaxed);

    SfxQueue::Event event;
    while (queue_.pop(event)) spawnVoice(event, callbackStartOutputFrames);

    const auto rate = sampleRate_.load(std::memory_order_relaxed);
    const bool audible = playing_.load(std::memory_order_relaxed)
        && !paused_.load(std::memory_order_relaxed) && rate != 0;
    if (!audible) {
      // Effects must not pile up while the mixer is silent.
      flushVoices();
      queue_.clear();
      mixSilence(output, frameCount);
      return;
    }

    const std::int64_t musicFrames = music_.frameCount();
    double position = playheadFrames_.load(std::memory_order_relaxed);
    const double target = seekTargetFrames_.exchange(kNoSeek, std::memory_order_acq_rel);
    if (target >= 0.0) {
      position = target;
      flushVoices();
      queue_.clear();
      lowPass_.reset();
    }

    const float speed = mixer::clampSpeed(playbackSpeed_.load(std::memory_order_relaxed));
    const float musicGain = mixer::clampGain(musicVolume_.load(std::memory_order_relaxed));
    const float sfxGain = mixer::clampGain(sfxVolume_.load(std::memory_order_relaxed));
    const bool looping = musicLooping_.load(std::memory_order_relaxed);

    // Whole-number speeds at integral positions skip interpolation entirely.
    const double roundedSpeed = std::round(static_cast<double>(speed));
    const bool integerStep = speed > 1.0001F
        && std::abs(static_cast<double>(speed) - roundedSpeed) < 1.0e-6
        && std::abs(position - std::round(position)) < 1.0e-9;

    const float filterDelta = static_cast<float>(1.0 / static_cast<double>(rate)) * speed;
    const float targetCutoff = lowPassEnabled_.load(std::memory_order_relaxed)
        ? LowPassFilter::kClosedCutoffHz : LowPassFilter::kOpenCutoffHz;

    callbackStartFrames_.store(position, std::memory_order_release);
    callbackStartNanos_.store(nowNanoseconds(), std::memory_order_release);
    callbackFrameCount_.store(static_cast<std::int32_t>(frameCount), std::memory_order_release);
    callbackSpeed_.store(speed, std::memory_order_release);
    callbackStartOutputFrames_.store(callbackStartOutputFrames, std::memory_order_release);

    const float* music = musicFrames > 0 ? music_.data() : nullptr;
    std::size_t outputIndex = 0;

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
      StereoSample mixed;

      if (music != nullptr && position >= 0.0
          && (looping || position < static_cast<double>(musicFrames))) {
        if (integerStep) {
          const float* source = music + static_cast<std::int64_t>(position) * 2;
          mixed.left += source[0] * musicGain;
          mixed.right += source[1] * musicGain;
        } else {
          const auto sample = mixer::sampleMusic(music, musicFrames, position, looping);
          mixed.left += sample.left * musicGain;
          mixed.right += sample.right * musicGain;
        }
      }

      lowPass_.setTargetCutoff(targetCutoff);
      lowPass_.advance(filterDelta);
      lowPass_.process(mixed.left, mixed.right);

      for (auto& voice : voices_) {
        if (!voice.active) continue;
        if (voice.startOffsetFrames > 0) {
          --voice.startOffsetFrames;
          continue;
        }
        mixer::accumulate(mixed, voice.frames, voice.frameCount, voice.frameIndex, sfxGain);
        ++voice.frameIndex;
      }

      position = mixer::advancePosition(position, speed);

      output[outputIndex++] = mixer::clampSample(mixed.left);
      output[outputIndex++] = mixer::clampSample(mixed.right);
    }

    for (auto& voice : voices_) {
      if (voice.active && voice.frameIndex >= voice.frameCount) voice = Voice{};
    }

    playheadFrames_.store(position, std::memory_order_release);
    generatedOutputFrames_.store(callbackStartOutputFrames + static_cast<std::int64_t>(frameCount),
        std::memory_order_release);
  }

  // -- control ---------------------------------------------------------

  void seekTo(double seconds) {
    const auto rate = sampleRate_.load(std::memory_order_acquire);
    if (rate == 0) {
      playheadFrames_.store(0.0, std::memory_order_release);
      return;
    }

    double frames = seconds * static_cast<double>(rate);
    if (!std::isfinite(frames) || frames < 0.0) frames = 0.0;

    const auto musicFrames = music_.frameCount();
    if (musicFrames > 0 && !musicLooping_.load(std::memory_order_relaxed)
        && frames > static_cast<double>(musicFrames)) {
      frames = static_cast<double>(musicFrames);
    }

    seekTargetFrames_.store(frames, std::memory_order_release);
    playheadFrames_.store(frames, std::memory_order_release);
    callbackStartFrames_.store(frames, std::memory_order_release);
    callbackStartNanos_.store(nowNanoseconds(), std::memory_order_release);
    callbackFrameCount_.store(0, std::memory_order_release);
    callbackStartOutputFrames_.store(
        generatedOutputFrames_.load(std::memory_order_relaxed), std::memory_order_release);
    playheadResetPending_.store(true, std::memory_order_release);
  }

  double playheadSeconds() const {
    const auto rate = sampleRate_.load(std::memory_order_acquire);
    if (rate == 0) return 0.0;

    const double endFrames = playheadFrames_.load(std::memory_order_acquire);
    const bool audible = playing_.load(std::memory_order_relaxed)
        && !paused_.load(std::memory_order_relaxed);
    if (!audible) return endFrames / static_cast<double>(rate);

    const auto startNs = callbackStartNanos_.load(std::memory_order_acquire);
    const auto callbackFrames = callbackFrameCount_.load(std::memory_order_acquire);
    if (startNs == 0 || callbackFrames <= 0) return endFrames / static_cast<double>(rate);

    const double elapsed = static_cast<double>(nowNanoseconds() - startNs) * 1.0e-9;
    const double estimate = mixer::estimatePlayheadFrames(
        callbackStartFrames_.load(std::memory_order_acquire), callbackFrames,
        callbackSpeed_.load(std::memory_order_acquire), elapsed,
        static_cast<double>(rate), endFrames);
    return estimate / static_cast<double>(rate);
  }

  // -- members ---------------------------------------------------------

  mutable std::mutex deviceMutex_;
  mutable std::mutex stateMutex_;
  ma_device device_{};
  bool deviceOpen_{};

  std::atomic<bool> running_{false};
  std::atomic<bool> deviceLost_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> paused_{true};
  std::atomic<bool> suspended_{false};

  std::atomic<std::uint32_t> sampleRate_{0};
  std::atomic<std::uint32_t> periodFrames_{kPeriodFrames};
  std::atomic<std::uint32_t> periodCount_{kPeriodCount};

  std::atomic<std::uint32_t> exclusiveCounter_{0};
  std::atomic<std::uint32_t> exclusiveRequest_{0};
  std::atomic<std::uint32_t> exclusiveAck_{0};
  std::atomic<bool> playheadResetPending_{false};
  SfxQueue queue_;

  // Decoded audio, replaced only while the audio thread is standing down.
  AudioBuffer music_;
  std::array<AudioBuffer, kSoundEffectBankCount> banks_;
  std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> customBanks_;
  std::mutex customMutex_;

  // Provenance of each bank, read by diagnostic queries from any thread.
  std::array<SoundEffectStatus, kSoundEffectBankCount> effectStatus_;
  std::mutex effectStatusMutex_;
  // Key of the currently installed hit-sound bank set. Loading the same set again is a no-op,
  // which lets a background warm-up and the first run share one decode.
  std::mutex soundEffectsMutex_;
  std::string soundEffectsKey_;
  bool soundEffectsLoaded_{false};

  // Owned by the audio thread.
  std::array<Voice, kMaxVoices> voices_{};
  LowPassFilter lowPass_;

  // Playhead and callback anchors shared with caller threads.
  std::atomic<double> playheadFrames_{0.0};
  std::atomic<double> seekTargetFrames_{kNoSeek};
  std::atomic<double> callbackStartFrames_{0.0};
  std::atomic<std::int64_t> callbackStartNanos_{0};
  std::atomic<std::int64_t> callbackStartOutputFrames_{0};
  std::atomic<std::int64_t> generatedOutputFrames_{0};
  std::atomic<std::int32_t> callbackFrameCount_{0};
  std::atomic<float> callbackSpeed_{1.0F};

  std::atomic<float> playbackSpeed_{1.0F};
  std::atomic<float> musicVolume_{1.0F};
  std::atomic<float> sfxVolume_{1.0F};
  std::atomic<bool> musicLooping_{false};
  std::atomic<bool> lowPassEnabled_{false};
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() = default;
AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

bool AudioEngine::initialize() {
  impl_->repairDevice();
  return impl_->openDevice();
}

void AudioEngine::shutdown() {
  impl_->playing_.store(false, std::memory_order_release);
  impl_->paused_.store(true, std::memory_order_release);
  impl_->stopDevice();
  impl_->music_.clear();
  for (auto& bank : impl_->banks_) bank.clear();
  impl_->customBanks_.clear();
  {
    std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
    impl_->effectStatus_.fill(SoundEffectStatus{});
  }
  impl_->flushVoices();
  impl_->queue_.clear();
  {
    std::lock_guard<std::mutex> lock{impl_->soundEffectsMutex_};
    impl_->soundEffectsLoaded_ = false;
    impl_->soundEffectsKey_.clear();
  }
  impl_->playheadFrames_.store(0.0, std::memory_order_release);
  impl_->seekTargetFrames_.store(kNoSeek, std::memory_order_release);
  impl_->callbackStartFrames_.store(0.0, std::memory_order_release);
  impl_->callbackStartNanos_.store(0, std::memory_order_release);
  impl_->callbackFrameCount_.store(0, std::memory_order_release);
  impl_->callbackSpeed_.store(1.0F, std::memory_order_release);
  impl_->generatedOutputFrames_.store(0, std::memory_order_release);
  impl_->closeDevice();
}

bool AudioEngine::loadSoundEffects(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory) {
  const std::string key = assetRoot.generic_string() + "|" + skinDirectory.generic_string();
  std::lock_guard<std::mutex> keyLock{impl_->soundEffectsMutex_};
  if (impl_->soundEffectsLoaded_ && impl_->soundEffectsKey_ == key) return true;
  if (!initialize()) return false;

  const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
  auto resolved = resolveSoundEffects(assetRoot, skinDirectory, rate);
  const bool loaded = resolved.anyLoaded();

  {
    // The whole bank set is swapped inside one window: every call replaces the previous skin
    // completely, and the audio thread never sees a bank from two different loads.
    Impl::ExclusiveScope scope{*impl_};
    impl_->banks_ = std::move(resolved.banks);
    std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
    impl_->effectStatus_ = std::move(resolved.status);
  }
  if (loaded) {
    impl_->soundEffectsKey_ = key;
    impl_->soundEffectsLoaded_ = true;
  }
  return loaded;
}

bool AudioEngine::loadHitSound(const std::filesystem::path& path) {
  if (path.empty() || !initialize()) return false;
  const std::string key = path.generic_string();
  {
    std::lock_guard<std::mutex> lock{impl_->customMutex_};
    if (impl_->customBanks_.contains(key)) return true;
  }
  const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
  auto decoded = Decoder::decodeFile(path, rate);
  if (decoded.empty()) return false;
  {
    Impl::ExclusiveScope scope{*impl_};
    std::lock_guard<std::mutex> lock{impl_->customMutex_};
    impl_->customBanks_[key] = std::make_shared<AudioBuffer>(std::move(decoded));
  }
  return true;
}

bool AudioEngine::loadMusic(const std::filesystem::path& path) {
  if (!initialize()) return false;

  const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
  return installDecodedMusic(Decoder::decodeFile(path, rate));
}

bool AudioEngine::installDecodedMusic(AudioBuffer decoded) {
  if (!initialize()) return false;
  if (decoded.empty()) return false;

  impl_->paused_.store(true, std::memory_order_release);
  {
    Impl::ExclusiveScope scope{*impl_};
    impl_->music_ = std::move(decoded);
  }
  impl_->playheadFrames_.store(0.0, std::memory_order_release);
  impl_->seekTargetFrames_.store(kNoSeek, std::memory_order_release);
  impl_->callbackStartFrames_.store(0.0, std::memory_order_release);
  impl_->callbackStartNanos_.store(0, std::memory_order_release);
  impl_->callbackFrameCount_.store(0, std::memory_order_release);
  impl_->callbackSpeed_.store(
      mixer::clampSpeed(impl_->playbackSpeed_.load(std::memory_order_relaxed)),
      std::memory_order_release);
  impl_->musicLooping_.store(false, std::memory_order_release);
  impl_->playheadResetPending_.store(true, std::memory_order_release);
  return true;
}

void AudioEngine::play() {
  impl_->repairDevice();
  if (impl_->suspended_.load(std::memory_order_acquire)) return;
  impl_->playing_.store(true, std::memory_order_release);
  impl_->paused_.store(false, std::memory_order_release);
  impl_->startDevice();
}

void AudioEngine::pause() {
  // The mixer already outputs silence while paused, so the device itself can stay alive.
  // Stopping it here would force a full device rebuild on the next play().
  impl_->paused_.store(true, std::memory_order_release);
}

void AudioEngine::stop() {
  impl_->playing_.store(false, std::memory_order_release);
  impl_->paused_.store(true, std::memory_order_release);
  impl_->seekTo(0.0);
}

void AudioEngine::suspend() {
  if (impl_->suspended_.exchange(true, std::memory_order_acq_rel)) return;
  impl_->stopDevice();
}

void AudioEngine::resume() {
  if (!impl_->suspended_.exchange(false, std::memory_order_acq_rel)) return;
  impl_->repairDevice();
  if (!impl_->playing_.load(std::memory_order_acquire)) return;
  // Keep the paused flag as it was before the suspension: a game that is still in its pause
  // menu must not start playing music just because the app came back to the foreground.
  impl_->startDevice();
}

void AudioEngine::seek(double second) {
  impl_->repairDevice();
  impl_->seekTo(second);
}

void AudioEngine::restart() {
  impl_->repairDevice();
  impl_->playing_.store(true, std::memory_order_release);
  impl_->paused_.store(false, std::memory_order_release);
  impl_->seekTo(0.0);
  impl_->startDevice();
}

void AudioEngine::restartPaused() {
  impl_->repairDevice();
  impl_->playing_.store(true, std::memory_order_release);
  impl_->paused_.store(true, std::memory_order_release);
  impl_->seekTo(0.0);
}

void AudioEngine::setVolume(float volume) { setMusicVolume(volume); }

void AudioEngine::setSoundEffectVolume(float volume) { setSfxVolume(volume); }

void AudioEngine::setMusicVolume(float volume) {
  impl_->musicVolume_.store(mixer::clampGain(volume), std::memory_order_release);
}

void AudioEngine::setSfxVolume(float volume) {
  impl_->sfxVolume_.store(mixer::clampGain(volume), std::memory_order_release);
}

void AudioEngine::setPlaybackSpeed(float speed) {
  impl_->playbackSpeed_.store(mixer::clampSpeed(speed), std::memory_order_release);
}

void AudioEngine::setMusicLooping(bool looping) {
  impl_->musicLooping_.store(looping, std::memory_order_release);
}

void AudioEngine::setLowPassEnabled(bool enabled) {
  impl_->lowPassEnabled_.store(enabled, std::memory_order_release);
}

void AudioEngine::triggerSoundEffect(int noteType) {
  if (impl_->suspended_.load(std::memory_order_acquire)) return;
  if (!impl_->running_.load(std::memory_order_acquire)) return;
  if (noteType == game::NOTE_HOLD) noteType = game::NOTE_TAP;

  SfxQueue::Event event;
  event.noteType = noteType;
  event.targetOutputFrame = impl_->estimateGeneratedOutputFrameNow();
  impl_->queue_.push(event);
}

void AudioEngine::triggerSoundEffect(int noteType, const std::string& hitSound) {
  if (hitSound.empty()) {
    triggerSoundEffect(noteType);
    return;
  }
  if (impl_->suspended_.load(std::memory_order_acquire)) return;
  if (!impl_->running_.load(std::memory_order_acquire)) return;
  if (noteType == game::NOTE_HOLD) noteType = game::NOTE_TAP;
  SfxQueue::Event event;
  event.noteType = noteType;
  {
    std::lock_guard<std::mutex> lock{impl_->customMutex_};
    const auto found = impl_->customBanks_.find(hitSound);
    if (found != impl_->customBanks_.end()) event.customBuffer = found->second.get();
  }
  event.targetOutputFrame = impl_->estimateGeneratedOutputFrameNow();
  impl_->queue_.push(event);
}

SoundEffectSource AudioEngine::soundEffectSource(int noteType) const {
  const auto slot = static_cast<std::size_t>(soundEffectBankFor(noteType));
  std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
  return impl_->effectStatus_[slot].source;
}

std::string AudioEngine::soundEffectSourceLabel(int noteType) const {
  const auto slot = static_cast<std::size_t>(soundEffectBankFor(noteType));
  std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
  const auto& label = impl_->effectStatus_[slot].label;
  return label.empty() ? "none" : label;
}

std::string AudioEngine::soundEffectSourcesLabel() const {
  std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
  const auto& status = impl_->effectStatus_;
  const auto name = [&status](int noteType) {
    const auto& text = status[static_cast<std::size_t>(noteType)].label;
    return text.empty() ? std::string{"none"} : text;
  };
  return "tap=" + name(game::NOTE_TAP) + " drag=" + name(game::NOTE_DRAG)
      + " flick=" + name(game::NOTE_FLICK);
}

double AudioEngine::cursorSeconds() const { return impl_->playheadSeconds(); }

double AudioEngine::getPlayheadSeconds() const { return impl_->playheadSeconds(); }

double AudioEngine::getMusicDurationSeconds() const { return impl_->music_.durationSeconds(); }

bool AudioEngine::isPlaying() const {
  return impl_->playing_.load(std::memory_order_acquire)
      && !impl_->paused_.load(std::memory_order_acquire)
      && !impl_->suspended_.load(std::memory_order_acquire);
}

bool AudioEngine::isSuspended() const { return impl_->suspended_.load(std::memory_order_acquire); }

bool AudioEngine::hasMusic() const { return !impl_->music_.empty(); }

bool AudioEngine::musicLooping() const {
  return impl_->musicLooping_.load(std::memory_order_acquire);
}

std::uint32_t AudioEngine::outputSampleRate() const {
  return impl_->sampleRate_.load(std::memory_order_acquire);
}

std::uint32_t AudioEngine::outputPeriodFrames() const {
  return impl_->periodFrames_.load(std::memory_order_acquire);
}

std::uint32_t AudioEngine::outputPeriodCount() const {
  return impl_->periodCount_.load(std::memory_order_acquire);
}

double AudioEngine::outputLatencySeconds() const {
  const auto rate = outputSampleRate();
  if (rate == 0) return 0.0;
  const auto frames = static_cast<double>(outputPeriodFrames())
      * static_cast<double>(outputPeriodCount());
  return frames / static_cast<double>(rate);
}

std::uint32_t AudioEngine::voiceCapacity() noexcept { return kMaxVoices; }

} // namespace rechigros::audio
