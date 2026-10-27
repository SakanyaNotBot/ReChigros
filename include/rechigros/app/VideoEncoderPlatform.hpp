#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Bridge to the platform's own video encoder.
//
// The packaged mobile build cannot depend on an external encoder binary: nothing ships one, and
// current mobile systems refuse to execute a bundled executable anyway. This bridge lets the
// recorder use the platform's hardware encoder instead, behind the same four operations the
// external-process path has. Builds that encode through an external tool get a stub that reports
// itself unavailable, so there is one call site for every platform.
namespace rechigros::app::platformenc {

// True when the platform encoder can be used at all on this build.
[[nodiscard]] bool available() noexcept;

// Creates the encoder for the requested output. `quality` is interpreted by the platform as a
// bitrate hint, so the same setting produces a comparable file on both paths.
bool open(int width, int height, int fps, int quality, const std::string& outputPath,
    std::string& error);

// Queues one top-down RGBA frame. Blocks while the encoder is saturated, so the caller never
// loses a frame to backpressure.
bool writeFrame(int width, int height, const std::uint8_t* pixels, std::size_t bytes,
    std::string& error);

// Drains the encoder and writes the container. Must be called before the output is complete.
bool finish(std::string& error);

// Stops immediately, discarding output.
void abort() noexcept;

// Supplies the raw interleaved stereo float track the recorder already writes, with a delay in
// milliseconds so the audio starts where the chart clock does.
bool addAudioTrack(const std::string& pcmPath, int sampleRate, int delayMs, std::string& error);

// How far the supplied music track has got.
enum class AudioTrackState { None, Preparing, Ready, Failed };

// Reports the music encode's progress. A caller that needs the track in the container before the
// first frame polls this instead of blocking the thread that draws the interface.
[[nodiscard]] AudioTrackState audioTrackState();

// Fraction of the input samples accepted by the audio encoder, in thousandths.
[[nodiscard]] int audioTrackProgressPermille();

} // namespace rechigros::app::platformenc
