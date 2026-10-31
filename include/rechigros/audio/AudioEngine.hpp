#pragma once

#include "rechigros/audio/Decoder.hpp"
#include "rechigros/audio/SkinSoundEffects.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rechigros::audio {

/// Software mixing engine. Music and a pool of effects are mixed in a single
/// device callback; playback speed, looping, a music low-pass and gains are
/// applied while mixing.
///
/// Threading: every public method is safe to call from any thread. Loading music
/// or effects decodes files and is therefore not real-time safe. Triggering an
/// effect is lock-free and does not allocate.
class AudioEngine final {
public:
  AudioEngine();
  ~AudioEngine();
  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;
  AudioEngine(AudioEngine&&) noexcept;
  AudioEngine& operator=(AudioEngine&&) noexcept;

  /// Opens the output device. Safe to call repeatedly; returns true when a device
  /// is usable. Decoding music or effects also initialises the device implicitly.
  bool initialize();
  /// Releases the device and every decoded buffer.
  void shutdown();

  /// Decodes the effect assets below `assetRoot`, preferring files in `skinDirectory` when it
  /// names a directory. For every bank the skin's click/drag/flick file replaces the built-in
  /// asset; a missing or undecodable file falls back to the built-in asset and then to a
  /// synthesised tone. Each call replaces every bank, so a skin choice never survives into the
  /// next load. Missing assets fall back to synthesised tones so effects still trigger.
  bool loadSoundEffects(const std::filesystem::path& assetRoot,
      const std::filesystem::path& skinDirectory = {});

  /// Decodes the effect bank for `skinDirectory` on a worker thread and keeps the result.
  /// The decode is the expensive half of loadSoundEffects; a loading screen calls this when
  /// the run starts and polls soundEffectsPrefetchDone() while it animates, so the frame
  /// that installs the bank only swaps buffers. A request for a different skin while the
  /// previous worker is still decoding is deferred to a later poll, never waited on here.
  void beginSoundEffectPrefetch(const std::filesystem::path& assetRoot,
      const std::filesystem::path& skinDirectory);
  /// True when no prefetch is running. A completed or failed prefetch both count as done.
  [[nodiscard]] bool soundEffectsPrefetchDone() const;

  /// Installs hit-sound buffers decoded by a chart-load worker. Each path is installed once;
  /// a buffer already present is left alone. The swap takes the same exclusive window as
  /// loadSoundEffects, so it is real-time safe.
  void installHitSounds(std::vector<std::pair<std::string, AudioBuffer>> buffers);

  /// Decodes `path` as the current music track. Replaces any previous track and
  /// rewinds the playhead.
  bool loadMusic(const std::filesystem::path& path);

  /// Installs music already decoded with `Decoder::decodeFile` for this engine's output
  /// rate. Decoding a track takes hundreds of milliseconds, so a caller that wants the
  /// screen to keep drawing decodes on its own thread and hands the buffer over here;
  /// the swap takes an exclusive window and is real-time safe. An empty buffer is
  /// rejected and leaves the current track alone.
  bool installDecodedMusic(AudioBuffer decoded);

  /// Starts or resumes playback from the current playhead.
  void play();
  /// Freezes the playhead and silences output. `play()` resumes from that position.
  void pause();
  /// Stops output and rewinds the playhead.
  void stop();
  /// Releases the device while keeping the playhead, for application suspension.
  void suspend();
  /// Reopens the device after `suspend()` and resumes playback when it was running.
  void resume();

  /// Moves the playhead to `second`. Works while playing and while paused.
  void seek(double second);
  /// Rewinds to the start and plays.
  void restart();
  /// Rewinds to the start but stays paused until `play()` or `resume()`.
  void restartPaused();

  /// Music gain, clamped to `[0, 5]`.
  void setVolume(float volume);
  /// Effect gain, clamped to `[0, 5]`.
  void setSoundEffectVolume(float volume);
  /// Alias of `setVolume`.
  void setMusicVolume(float volume);
  /// Alias of `setSoundEffectVolume`.
  void setSfxVolume(float volume);

  /// Playback rate, clamped to `[0.05, 4]`. 1.0 is unmodified speed.
  void setPlaybackSpeed(float speed);
  /// Restarts music playback at the end of the track instead of stopping.
  void setMusicLooping(bool looping);
  /// Enables the low-pass filter that muffles the music.
  void setLowPassEnabled(bool enabled);

  /// Queues one effect. `noteType` uses the note constants; hold notes reuse tap.
  /// Lock-free and allocation-free on the calling thread.
  void triggerSoundEffect(int noteType);
  /// Loads one additional effect from the chart package. An empty path or a failed decode
  /// leaves the ordinary type bank available as a fallback.
  bool loadHitSound(const std::filesystem::path& path);
  void triggerSoundEffect(int noteType, const std::string& hitSound);

  /// Where the bank a note type reads came from. Hold notes report the tap bank; an unknown
  /// type reports `SoundEffectSource::None`. Read-only and safe from any thread.
  [[nodiscard]] SoundEffectSource soundEffectSource(int noteType) const;
  /// Diagnostic label of that bank: `skin:<path>`, `built-in:<relative path>`,
  /// `synthesized:<frequency>Hz` or `none`. Suitable for one start-up log entry.
  [[nodiscard]] std::string soundEffectSourceLabel(int noteType) const;
  /// Every bank's label on one line, for logging a run's effect setup once.
  [[nodiscard]] std::string soundEffectSourcesLabel() const;

  /// Interpolated music position in seconds, for animation timing.
  [[nodiscard]] double cursorSeconds() const;
  /// Same as `cursorSeconds`.
  [[nodiscard]] double getPlayheadSeconds() const;
  /// Duration of the loaded track in seconds, or 0 when nothing is loaded.
  [[nodiscard]] double getMusicDurationSeconds() const;

  [[nodiscard]] bool isPlaying() const;
  [[nodiscard]] bool isSuspended() const;
  [[nodiscard]] bool hasMusic() const;
  [[nodiscard]] bool musicLooping() const;

  /// Device sample rate, or 0 before the device is opened.
  [[nodiscard]] std::uint32_t outputSampleRate() const;
  /// Frames rendered per callback period.
  [[nodiscard]] std::uint32_t outputPeriodFrames() const;
  /// Periods buffered between the mixer and the speaker.
  [[nodiscard]] std::uint32_t outputPeriodCount() const;
  /// Nominal latency of the requested buffering, in seconds.
  [[nodiscard]] double outputLatencySeconds() const;
  /// Maximum number of effects that can sound simultaneously.
  [[nodiscard]] static std::uint32_t voiceCapacity() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rechigros::audio
