#pragma once

#include "rechigros/app/VideoEncoder.hpp"
#include "rechigros/render/GameRenderer.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rechigros::app {

struct AppContext;

// Recording of an autoplay run to a video file.
//
// A recording renders the run offscreen one frame at a time and encodes the frames as they are
// produced. Nothing about it depends on the wall clock: each frame states the chart time it
// shows, so a slow machine produces the same video as a fast one. Every drawing setting comes
// from the player's current configuration, so what is recorded is what the game looks like.
class RecordSession final {
public:
  // Video sizes the interface offers, named by the conventional label for their shape.
  struct Resolution {
    const char* label;
    int width;
    int height;
  };

  // The sizes offered for recording, in the order the interface lists them.
  [[nodiscard]] static const std::vector<Resolution>& resolutions();

  struct Settings {
    int resolutionIndex{0};
    int fps{60};
    VideoEncoder::Codec codec{VideoEncoder::Codec::H264};
    std::string fileName;
    std::filesystem::path outputDirectory;
    // Text shown in the combo label position instead of the usual word.
    std::string comboLabel{"RECORD"};
    bool noteHitsound{true};
    bool noteHitParticles{true};
  };

  // The public video directory of the device, where a recording goes by default.
  [[nodiscard]] static std::filesystem::path defaultOutputDirectory();

  // A default file name for the moment the interface is opened: the wall-clock time, so two
  // recordings of the same chart do not overwrite each other.
  [[nodiscard]] static std::string defaultFileName();

  enum class State { Idle, Running, Finished, Failed, Cancelled };

  // Begins a recording for the chart currently loaded in `context`. Returns false and fills
  // `error` when the recording cannot start, leaving the session idle.
  bool start(AppContext& context, const Settings& settings, std::string& error);

  // Advances the recording by a bounded amount of work so the interface stays responsive.
  // Returns true while more work remains.
  bool step(AppContext& context);

  void cancel(AppContext& context);
  void finish(AppContext& context);

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] const std::string& error() const noexcept { return error_; }
  [[nodiscard]] const std::filesystem::path& outputPath() const noexcept { return outputPath_; }
  // Progress in the range 0..1, for the progress bar.
  [[nodiscard]] float progress() const noexcept;
  [[nodiscard]] int renderedFrames() const noexcept { return renderedFrames_; }
  [[nodiscard]] int totalFrames() const noexcept { return totalFrames_; }
  [[nodiscard]] bool preparingAudio() const noexcept;
  [[nodiscard]] float audioProgress() const noexcept;

private:
  void releaseResources(AppContext& context) noexcept;
  // Deletes the output file of a run that did not finish. An encoder killed before it writes the
  // container index leaves a file no player can open, so it is removed rather than left beside
  // real recordings.
  // Prints the accumulated stage timings once, so a slow recording can be attributed.
  void reportTimings() const;
  void discardIncompleteOutput() noexcept;
  // Stops the encoder thread and releases the capture target after the run failed. A failure part
  // way through leaves a live thread behind, and letting the session be destroyed with it still
  // running terminates the process instead of reporting the error.
  void failAndRelease(AppContext& context) noexcept;
  // Frame handoff to the encoder thread. Encoding a frame costs more than drawing it, so the work
  // is done on its own thread: the renderer hands over a finished frame and immediately starts the
  // next one. Without this the recording advances at the encoder's pace, which is what makes
  // rendering a chart take far longer than playing it.
  void startEncoderThread();
  void stopEncoderThread();
  // Hands one frame to the encoder thread, blocking only while the queue is full.
  bool submitFrame(const std::vector<std::uint8_t>& pixels);
  // Timing wrapper for a produced frame: queue handoff plus the wait it cost.
  bool enqueueTimedFrame(const std::vector<std::uint8_t>& pixels);
  // Blocks until every queued frame has been written.
  bool drainEncoder(std::string& error);
  void encoderLoop();
  // Sums one short percussive sample per hit into `mix` at the note's own time, so a recorded run
  // sounds like the run it shows.
  void mixHitSounds(AppContext& context, std::vector<float>& mix, std::int64_t frames,
      double speed) const;
  // Writes the decoded music plus every judged hit sound beside `videoPath` as raw samples for the
  // encoder. Empty on failure.
  std::filesystem::path writeAudioTrack(AppContext& context,
      const std::filesystem::path& videoPath);

  Settings settings_;
  State state_{State::Idle};
  std::string error_;
  std::filesystem::path outputPath_;
  // Raw sample file handed to the encoder, removed once the recording ends.
  std::filesystem::path audioTrackPath_;
  VideoEncoder encoder_;
  int renderedFrames_{};
  int totalFrames_{};
  std::vector<std::uint8_t> framePixels_;

  // Encoder thread. A bound on the queue keeps a fast renderer from growing it without limit; when
  // it is reached the renderer waits, which is the only backpressure the handoff needs.
  std::thread encoderThread_;
  std::mutex queueMutex_;
  std::condition_variable queueSpace_;
  std::condition_variable queueFilled_;
  std::deque<std::vector<std::uint8_t>> frameQueue_;
  bool encoderStop_{};
  bool encoderRunning_{};
  std::string encoderError_;
  // Deep enough that the renderer never waits for the encoder in steady state. Each slot costs one
  // frame of pixels, so this trades a few megabytes of memory for a recording that runs at the
  // speed of its slower stage instead of the sum of both.
  static constexpr std::size_t kMaxQueuedFrames = 16;

  // Stage timings, reported once per recording so a slow run can be attributed instead of guessed.
  int timedFrames_{};
  double captureMs_{};
  double encodeWaitMs_{};
  // Time the encoder thread spends inside a single frame write, which is the encoding cost itself.
  double encoderWriteMs_{};
  // True once the encoder has been opened for planar input, which is the fast path.
  bool encoderUsesPlanar_{};

};

} // namespace rechigros::app
