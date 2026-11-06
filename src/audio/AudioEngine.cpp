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
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace rechigros::audio {
namespace {
constexpr std::uint32_t kOutputChannels = 2;
/// Requested frames per device callback. Small periods keep hit sounds tight. WASAPI shared
/// mode may round this up to its 10 ms engine period; the exclusive path below honors it.
constexpr std::uint32_t kPeriodFrames = 128;
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

std::string musicFileKey(const std::filesystem::path& path, std::uint32_t sampleRate) {
  if (path.empty() || sampleRate == 0) return {};
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) return {};
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error) return {};
  return path.generic_string() + "|" + std::to_string(size) + "|"
      + std::to_string(static_cast<long long>(modified.time_since_epoch().count())) + "|"
      + std::to_string(sampleRate);
}

/// Single-producer single-consumer queue of effect triggers.
class SfxQueue final {
public:
  struct Event {
    int noteType{};
    const AudioBuffer* customBuffer{};
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
  bool active{};
};

/// One audio callback window, published as a unit so a reader can dead-reckon the music
/// position without ever combining fields from two different callbacks.
struct CallbackAnchor {
  double startFrames{};
  std::int64_t startNanos{};
  std::int64_t startOutputFrames{};
  std::int32_t frameCount{};
  float speed{1.0F};
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
  ~Impl() {
    if (effectPrefetchThread_.joinable()) effectPrefetchThread_.join();
    closeDevice();
  }

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
#if defined(_WIN32)
    // Shared WASAPI commonly runs with a 10 ms engine period, so even a 128-frame hint
    // becomes a 480-frame callback. Exclusive mode lets the endpoint use the requested
    // short period, which removes the dominant part of the hit-sound delay.
    config.playback.shareMode = ma_share_mode_exclusive;
    config.wasapi.usage = ma_wasapi_usage_games;
#endif

    // The default iOS category is play-and-record, which routes playback through the earpiece,
    // keeps the input side of the session alive and can raise a microphone prompt the first
    // time a chart starts. A game shipping its own music belongs in the playback category.
    if (!contextOpen_) {
      ma_context_config contextConfig = ma_context_config_init();
#if defined(__APPLE__)
      contextConfig.coreaudio.sessionCategory = ma_ios_session_category_playback;
#endif
      if (ma_context_init(nullptr, 0, &contextConfig, &context_) != MA_SUCCESS) return false;
      contextOpen_ = true;
    }

    ma_result deviceResult = ma_device_init(&context_, &config, &device_);
#if defined(_WIN32)
    if (deviceResult != MA_SUCCESS) {
      // Exclusive access is not always available: another application may own the endpoint,
      // or the device may reject the stream format. Shared mode is the safe fallback; one
      // period instead of two still removes a full buffer from the reported latency.
      config.playback.shareMode = ma_share_mode_shared;
      config.periods = 1;
      deviceResult = ma_device_init(&context_, &config, &device_);
      if (deviceResult == MA_SUCCESS) {
        std::fprintf(stderr, "[audio] exclusive WASAPI unavailable; using shared mode%c", 10);
      }
    }
#endif
    if (deviceResult != MA_SUCCESS) return false;

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
    if (deviceOpen_) {
      ma_device_uninit(&device_);
      deviceOpen_ = false;
    }
    running_.store(false, std::memory_order_release);
    if (contextOpen_) {
      ma_context_uninit(&context_);
      contextOpen_ = false;
    }
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
    const double display = seekDisplayFrames_.load(std::memory_order_acquire);
    if (display >= 0.0) seekDisplayFrames_.store(display * scale, std::memory_order_release);
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

  // -- callback anchor -------------------------------------------------

  /// Publishes the callback window. Only the audio thread writes it, so the odd/even
  /// sequence has a single writer; readers retry when they land mid-write.
  void publishAnchor(const CallbackAnchor& anchor) noexcept {
    anchorSequence_.fetch_add(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    anchor_ = anchor;
    std::atomic_thread_fence(std::memory_order_release);
    anchorSequence_.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] bool readAnchor(CallbackAnchor& out) const noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
      const auto before = anchorSequence_.load(std::memory_order_acquire);
      if ((before & 1U) != 0) continue;
      out = anchor_;
      std::atomic_thread_fence(std::memory_order_acquire);
      if (anchorSequence_.load(std::memory_order_relaxed) == before) return true;
    }
    return false;
  }

  Voice* claimVoice() {
    for (auto& voice : voices_) {
      if (!voice.active) return &voice;
    }
    // Every slot is busy: reuse the voice that is furthest through its playback.
    Voice* oldest = &voices_[0];
    for (auto& voice : voices_) {
      if (voice.frameIndex > oldest->frameIndex) oldest = &voice;
    }
    return oldest;
  }

  AudioBuffer* bankFor(int noteType) noexcept {
    if (noteType <= 0 || static_cast<std::size_t>(noteType) >= banks_.size()) return nullptr;
    return &banks_[static_cast<std::size_t>(noteType)];
  }

  void spawnVoice(const SfxQueue::Event& event) {
    auto* bank = event.customBuffer != nullptr
        ? const_cast<AudioBuffer*>(event.customBuffer) : bankFor(event.noteType);
    if (bank == nullptr || bank->empty()) return;

    Voice* voice = claimVoice();
    voice->frames = bank->data();
    voice->frameCount = bank->frameCount();
    voice->frameIndex = 0;
    voice->active = true;
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
    // A trigger becomes the first sample of the next callback. Do not add a dead-reckoned
    // start offset on top of that: the callback is already the shortest scheduling unit, and
    // an offset computed from the generated position can only delay the effect further.
    while (queue_.pop(event)) spawnVoice(event);

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

    publishAnchor({position, nowNanoseconds(), callbackStartOutputFrames,
        static_cast<std::int32_t>(frameCount), speed});
    // The anchor now describes the audible window, so a seek no longer needs its own
    // display position.
    seekDisplayFrames_.store(kNoSeek, std::memory_order_release);

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
      seekDisplayFrames_.store(0.0, std::memory_order_release);
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
    seekDisplayFrames_.store(frames, std::memory_order_release);
    playheadResetPending_.store(true, std::memory_order_release);
  }

  double playheadSeconds() const {
    const auto rate = sampleRate_.load(std::memory_order_acquire);
    if (rate == 0) return 0.0;

    // A seek is reflected immediately, before the next callback publishes an anchor.
    const double seekFrames = seekDisplayFrames_.load(std::memory_order_acquire);
    if (seekFrames >= 0.0) return seekFrames / static_cast<double>(rate);

    const double endFrames = playheadFrames_.load(std::memory_order_acquire);
    const bool audible = playing_.load(std::memory_order_relaxed)
        && !paused_.load(std::memory_order_relaxed);
    if (!audible) return endFrames / static_cast<double>(rate);

    CallbackAnchor anchor;
    if (!readAnchor(anchor) || anchor.startNanos == 0 || anchor.frameCount <= 0) {
      return endFrames / static_cast<double>(rate);
    }

    const double elapsed = static_cast<double>(nowNanoseconds() - anchor.startNanos) * 1.0e-9;
    const double estimate = mixer::estimatePlayheadFrames(anchor.startFrames, anchor.frameCount,
        anchor.speed, elapsed, static_cast<double>(rate));
    return estimate / static_cast<double>(rate);
  }

  // -- members ---------------------------------------------------------

  mutable std::mutex deviceMutex_;
  mutable std::mutex stateMutex_;
  ma_device device_{};
  ma_context context_{};
  bool deviceOpen_{};
  bool contextOpen_{};

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
  std::string musicKey_;
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

  // Effect-bank prefetch: the decode runs on its own worker while a loading screen is up,
  // and loadSoundEffects then installs the finished set without reading the files again.
  std::mutex effectPrefetchMutex_;
  std::thread effectPrefetchThread_;
  std::atomic<bool> effectPrefetchRunning_{false};
  std::string effectPrefetchKey_;
  std::string prefetchedEffectsKey_;
  SoundEffectSet prefetchedEffects_;

  // Owned by the audio thread.
  std::array<Voice, kMaxVoices> voices_{};
  LowPassFilter lowPass_;

  // Playhead shared with caller threads. The callback window is published as one unit by the
  // audio thread; `seekDisplayFrames_` covers the interval between a seek and the callback that
  // adopts it.
  std::atomic<double> playheadFrames_{0.0};
  std::atomic<double> seekTargetFrames_{kNoSeek};
  std::atomic<double> seekDisplayFrames_{kNoSeek};
  std::atomic<std::uint32_t> anchorSequence_{0};
  CallbackAnchor anchor_{};
  std::atomic<std::int64_t> generatedOutputFrames_{0};

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
  // Stop the prefetch worker before the buffers it can touch are cleared.
  {
    std::thread prefetch;
    {
      std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
      prefetch = std::move(impl_->effectPrefetchThread_);
    }
    if (prefetch.joinable()) prefetch.join();
  }
  impl_->playing_.store(false, std::memory_order_release);
  impl_->paused_.store(true, std::memory_order_release);
  impl_->stopDevice();
  impl_->music_.clear();
  impl_->musicKey_.clear();
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
  impl_->seekDisplayFrames_.store(kNoSeek, std::memory_order_release);
  impl_->generatedOutputFrames_.store(0, std::memory_order_release);
  impl_->closeDevice();
}

void AudioEngine::beginSoundEffectPrefetch(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory) {
  const std::string key = assetRoot.generic_string() + "|" + skinDirectory.generic_string();
  {
    std::lock_guard<std::mutex> lock{impl_->soundEffectsMutex_};
    if (impl_->soundEffectsLoaded_ && impl_->soundEffectsKey_ == key) return;
  }
  std::thread previous;
  {
    std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
    // A finished prefetch for the same key is as good as a running one: the load will take
    // it from prefetchedEffects_ without reading the files again.
    if ((impl_->effectPrefetchRunning_.load(std::memory_order_acquire)
            && impl_->effectPrefetchKey_ == key)
        || impl_->prefetchedEffectsKey_ == key) {
      return;
    }
    // A worker for another skin is still decoding. Let it finish and start this one on a
    // later poll instead of joining it on the frame that asked; the waiting is what the
    // loading panel is for.
    if (impl_->effectPrefetchRunning_.load(std::memory_order_acquire)) return;
    previous = std::move(impl_->effectPrefetchThread_);
  }
  // A worker for another key (the player changed skins) is collected outside the lock; the
  // prefetch only ever runs on the loading screen's frames.
  if (previous.joinable()) previous.join();
  {
    std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
    impl_->effectPrefetchKey_ = key;
    impl_->prefetchedEffectsKey_.clear();
    impl_->effectPrefetchRunning_.store(true, std::memory_order_release);
    impl_->effectPrefetchThread_ = std::thread([this, assetRoot, skinDirectory, key] {
      SoundEffectSet resolved;
      // Opening the device is the other one-time cost of the first run, so it happens on the
      // worker too and needs no graphics context.
      const auto started = std::chrono::steady_clock::now();
      if (initialize()) {
        const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
        resolved = resolveSoundEffects(assetRoot, skinDirectory, rate);
      }
      const double elapsedMs = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count();
      std::fprintf(stderr, "[audio] effect bank prefetched in %.1f ms (skin %s, loaded=%d)\n",
          elapsedMs, skinDirectory.filename().string().c_str(), resolved.anyLoaded() ? 1 : 0);
      std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
      if (impl_->effectPrefetchKey_ == key) {
        impl_->prefetchedEffects_ = std::move(resolved);
        impl_->prefetchedEffectsKey_ = key;
      }
      impl_->effectPrefetchRunning_.store(false, std::memory_order_release);
    });
  }
}

bool AudioEngine::soundEffectsPrefetchDone() const {
  return !impl_->effectPrefetchRunning_.load(std::memory_order_acquire);
}

bool AudioEngine::loadSoundEffects(const std::filesystem::path& assetRoot,
    const std::filesystem::path& skinDirectory) {
  const std::string key = assetRoot.generic_string() + "|" + skinDirectory.generic_string();
  {
    std::lock_guard<std::mutex> keyLock{impl_->soundEffectsMutex_};
    if (impl_->soundEffectsLoaded_ && impl_->soundEffectsKey_ == key) return true;
  }
  // The loading screen waits for the prefetch, so this join only collects a finished worker;
  // a run that starts without one still takes the synchronous path below.
  {
    std::thread prefetch;
    {
      std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
      prefetch = std::move(impl_->effectPrefetchThread_);
    }
    if (prefetch.joinable()) prefetch.join();
  }
  SoundEffectSet resolved;
  bool havePrefetched = false;
  {
    std::lock_guard<std::mutex> lock{impl_->effectPrefetchMutex_};
    if (impl_->prefetchedEffectsKey_ == key && impl_->prefetchedEffects_.anyLoaded()) {
      resolved = std::move(impl_->prefetchedEffects_);
      impl_->prefetchedEffectsKey_.clear();
      havePrefetched = true;
    }
  }
  if (!havePrefetched) {
    if (!initialize()) return false;
    const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
    resolved = resolveSoundEffects(assetRoot, skinDirectory, rate);
  }
  const bool loaded = resolved.anyLoaded();

  {
    std::lock_guard<std::mutex> keyLock{impl_->soundEffectsMutex_};
    if (impl_->soundEffectsLoaded_ && impl_->soundEffectsKey_ == key) return true;
    // The whole bank set is swapped inside one window: every call replaces the previous skin
    // completely, and the audio thread never sees a bank from two different loads.
    Impl::ExclusiveScope scope{*impl_};
    impl_->banks_ = std::move(resolved.banks);
    std::lock_guard<std::mutex> lock{impl_->effectStatusMutex_};
    impl_->effectStatus_ = std::move(resolved.status);
    if (loaded) {
      impl_->soundEffectsKey_ = key;
      impl_->soundEffectsLoaded_ = true;
    }
  }
  return loaded;
}

void AudioEngine::installHitSounds(
    std::vector<std::pair<std::string, AudioBuffer>> buffers) {
  if (buffers.empty() || !initialize()) return;
  // One exclusive window for the whole set; the frame that installs them does no decoding.
  Impl::ExclusiveScope scope{*impl_};
  std::lock_guard<std::mutex> lock{impl_->customMutex_};
  for (auto& entry : buffers) {
    if (entry.first.empty() || entry.second.empty()) continue;
    if (impl_->customBanks_.contains(entry.first)) continue;
    impl_->customBanks_[entry.first] =
        std::make_shared<AudioBuffer>(std::move(entry.second));
  }
}

bool AudioEngine::loadHitSound(const std::filesystem::path& path) {
  if (path.empty() || !initialize()) return false;
  const auto utf8 = path.generic_u8string();
  const std::string key(utf8.begin(), utf8.end());
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

bool AudioEngine::hasMusicFile(const std::filesystem::path& path) const {
  const auto key = musicFileKey(path, impl_->sampleRate_.load(std::memory_order_acquire));
  std::lock_guard<std::mutex> lock{impl_->stateMutex_};
  return !key.empty() && impl_->musicKey_ == key && !impl_->music_.empty();
}

bool AudioEngine::loadMusic(const std::filesystem::path& path) {
  if (!initialize()) return false;
  if (hasMusicFile(path)) {
    restartPaused();
    return true;
  }

  const auto rate = impl_->sampleRate_.load(std::memory_order_acquire);
  return installDecodedMusic(Decoder::decodeFile(path, rate), path);
}

bool AudioEngine::installDecodedMusic(AudioBuffer decoded, const std::filesystem::path& source) {
  if (!initialize()) return false;
  if (decoded.empty()) return false;

  impl_->paused_.store(true, std::memory_order_release);
  {
    Impl::ExclusiveScope scope{*impl_};
    impl_->music_ = std::move(decoded);
    std::lock_guard<std::mutex> lock{impl_->stateMutex_};
    impl_->musicKey_ = musicFileKey(source, impl_->sampleRate_.load(std::memory_order_acquire));
  }
  impl_->playheadFrames_.store(0.0, std::memory_order_release);
  // A seek target of zero makes the first callback start at the top even if it raced the
  // buffer swap, and the display position follows immediately.
  impl_->seekTargetFrames_.store(0.0, std::memory_order_release);
  impl_->seekDisplayFrames_.store(0.0, std::memory_order_release);
  impl_->musicLooping_.store(false, std::memory_order_release);
  impl_->playheadResetPending_.store(true, std::memory_order_release);
  return true;
}

void AudioEngine::play() {
  impl_->repairDevice();
  if (impl_->suspended_.load(std::memory_order_acquire)) return;
  // The callback anchor still describes the window that was playing when the engine paused.
  // Reporting the position playback stopped at until the next callback replaces it avoids a
  // one-buffer jump forward followed by a snap back when the stream restarts.
  impl_->seekDisplayFrames_.store(impl_->playheadFrames_.load(std::memory_order_acquire),
      std::memory_order_release);
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
