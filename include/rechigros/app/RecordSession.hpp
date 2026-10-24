#pragma once

#include "rechigros/app/VideoEncoder.hpp"
#include "rechigros/render/GameRenderer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
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

private:
  void releaseResources(AppContext& context) noexcept;
  // Writes the decoded music beside `videoPath` as raw samples for the encoder. Empty on failure.
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
};

} // namespace rechigros::app
