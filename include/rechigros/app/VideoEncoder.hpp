#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rechigros::app {

// Encodes raw frames into a video file.
//
// Frames arrive already rendered and in top-down RGBA order. Two back ends exist: desktop builds
// pipe the frames to an external encoder process, while the packaged mobile build asks the
// platform's own encoder through a bridge, because a shipped build cannot rely on an external
// tool being installed (and recent mobile systems refuse to execute a bundled binary anyway).
// Failures are reported instead of thrown: a recording is a long operation whose errors have to
// reach the interface rather than terminate the program.
class VideoEncoder final {
public:
  // Video codecs the recorder offers. The value names the encoder the external tool is asked
  // for, so the label and the actual encoding stay in one place.
  enum class Codec { H264, H265, Vp9 };

  struct Settings {
    int width{};
    int height{};
    int fps{60};
    Codec codec{Codec::H264};
    // Quantiser for the constant-quality mode. Lower is better quality and a larger file.
    int quality{20};
    // Encoder effort preset, named the way the external encoder spells it. The default keeps
    // encoding well below the frame budget while staying visually close to the slower presets,
    // which matters because encoding runs on the recording's critical path.
    std::string preset{"veryfast"};
    // Audio track written next to the video. An empty path produces a video-only file. The track
    // is supplied as raw interleaved stereo float samples, so the caller decides what it contains
    // and the encoder never has to understand the game's audio.
    std::filesystem::path audioTrack;
    std::uint32_t audioSampleRate{};
    // Seconds of silence to place before the audio, so it starts where the chart starts.
    double audioDelaySec{};
    // True when the caller produces frames in the encoder's planar layout instead of RGBA. The
    // caller has to make that layout anyway to avoid a conversion here, so it says so up front.
    bool planarInput{};
  };

  VideoEncoder();
  ~VideoEncoder();
  VideoEncoder(const VideoEncoder&) = delete;
  VideoEncoder& operator=(const VideoEncoder&) = delete;

  // True when this build encodes through the platform instead of an external process. A build
  // that encodes in-process never has a missing-tool precondition, so the interface can offer the
  // action unconditionally.
  [[nodiscard]] static bool usesPlatformEncoder() noexcept;
  // Locates the external encoder. Recording needs it; an empty result means it is not installed
  // and the interface reports that rather than starting a capture that cannot finish. Always
  // empty for a platform build, which has nothing to locate.
  [[nodiscard]] static std::filesystem::path findEncoder();

  [[nodiscard]] static std::string codecName(Codec codec);

  // Starts the encoder writing to `output`. Returns false with `error` set when the process
  // cannot be started.
  bool open(const Settings& settings, const std::filesystem::path& output, std::string& error);

  // Appends one frame. `pixels` must hold width*height*4 bytes.
  bool writeFrame(const std::vector<std::uint8_t>& pixels, std::string& error);

  // Closes the input and waits for the encoder to finish writing. Returns false with `error` set
  // when the encoder reported a failure.
  bool finish(std::string& error);

  // Stops the encoder immediately, discarding whatever it has not written. Used when a recording
  // is cancelled, so the caller does not wait for the process to drain.
  void abort() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }
  // True when this encoder was opened for planar frames, so the caller knows which layout to send.
  [[nodiscard]] bool inputIsPlanar() const noexcept { return inputIsPlanar_; }

private:
  void closePipes() noexcept;
  // Reads the tail of the encoder's error output, or an empty string when it said nothing.
  [[nodiscard]] std::string readDiagnostics() const;
  // Platform back end: creates the encoder for the requested format, or returns false when the
  // platform cannot provide one. Kept out of line so a build without a bridge still links.
  bool openPlatform(const Settings& settings, const std::filesystem::path& output,
      std::string& error);
  bool writePlatformFrame(const std::uint8_t* pixels, std::size_t bytes, std::string& error);
  bool finishPlatform(std::string& error);
  void abortPlatform() noexcept;

  // Set when the frame stream goes through the platform bridge rather than a child process.
  bool platform_{};
  // Set when this encoder takes planar frames instead of RGBA.
  bool inputIsPlanar_{};
  // Frame size the platform bridge was opened with, restated on every frame.
  int widthForPlatform_{};
  int heightForPlatform_{};

  std::filesystem::path executable_;
  void* process_{};
  void* input_{};
  // Error output is collected in a file: a pipe would need a reader while frames are streaming, and
  // an encoder that fills its own error pipe stops accepting input.
  std::filesystem::path diagnosticsPath_;
  bool active_{};
};

} // namespace rechigros::app
