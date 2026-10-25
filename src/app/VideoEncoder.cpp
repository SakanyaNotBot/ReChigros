#include "rechigros/app/VideoEncoder.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

// The encoder is an external tool, so recording depends on it being installed. Shipping one is
// not possible for every platform this program targets, and the interface is expected to say so
// rather than fail silently mid-recording.
constexpr const char* kEncoderName = "ffmpeg";

std::string qualityFlag(VideoEncoder::Codec codec) {
  // Constant-quality coding: the encoder spends bits where the picture needs them, which suits
  // gameplay footage far better than a fixed bitrate.
  switch (codec) {
    case VideoEncoder::Codec::Vp9: return "-crf";
    case VideoEncoder::Codec::H265:
    case VideoEncoder::Codec::H264:
    default: return "-crf";
  }
}

} // namespace

VideoEncoder::VideoEncoder() = default;

VideoEncoder::~VideoEncoder() { abort(); }

std::filesystem::path VideoEncoder::findEncoder() {
  // An explicit copy next to the program wins, so a portable install can carry its own encoder;
  // otherwise the one on the search path is used.
  std::error_code error;
  const auto self = platform::executablePath();
  if (!self.empty()) {
    const auto sibling = self.parent_path() / (std::string(kEncoderName) + ".exe");
    if (std::filesystem::is_regular_file(sibling, error)) return sibling;
    const auto plain = self.parent_path() / kEncoderName;
    if (std::filesystem::is_regular_file(plain, error)) return plain;
  }
  // Nothing else can be verified without starting a process, so the bare name is reported and
  // the process launch decides whether it exists.
  return std::filesystem::path(kEncoderName);
}

std::string VideoEncoder::codecName(Codec codec) {
  switch (codec) {
    case Codec::H265: return "H.265";
    case Codec::Vp9: return "VP9";
    case Codec::H264:
    default: return "H.264";
  }
}

bool VideoEncoder::open(const Settings& settings, const std::filesystem::path& output,
    std::string& error) {
  if (active_) {
    error = "an encoder is already running";
    return false;
  }
  if (settings.width <= 0 || settings.height <= 0 || settings.fps <= 0) {
    error = "the video size or frame rate is not usable";
    return false;
  }
  executable_ = findEncoder();
  std::error_code fsError;
  if (const auto parent = output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent, fsError);
  }

  const std::string encoder = settings.codec == Codec::H265 ? "libx265"
      : settings.codec == Codec::Vp9                            ? "libvpx-vp9"
                                                                : "libx264";
  // The frame stream is raw RGBA at a fixed size and rate, so every parameter the tool needs is
  // known before the first frame. Frames are already in top-down order, which is what the raw
  // video reader expects, so no flip filter is applied.
  //
  // When a track is supplied it arrives as a second input of interleaved stereo float samples. It
  // is delayed rather than padded so its first sample coincides with the frame the chart starts on,
  // which keeps the music aligned with the notes without decoding audio into the video stream.
  const bool withAudio = !settings.audioTrack.empty() && settings.audioSampleRate > 0
      && std::filesystem::is_regular_file(settings.audioTrack, fsError);
  std::vector<std::string> arguments = {
      // The encoder re-parses the whole command line in its own process, so every argument has to
      // be UTF-8. A path narrowed through the host's ANSI code page reaches it as mojibake, the
      // output directory stops resolving, and the encoder closes its input instead of writing.
      platform::pathToUtf8(executable_),
      "-hide_banner",
      "-loglevel", "error",
      "-y",
      "-f", "rawvideo",
      "-pix_fmt", "rgba",
      "-s", std::to_string(settings.width) + "x" + std::to_string(settings.height),
      "-r", std::to_string(settings.fps),
      "-i", "pipe:0",
  };
  if (withAudio) {
    arguments.insert(arguments.end(),
        {"-f", "f32le", "-ar", std::to_string(settings.audioSampleRate), "-ac", "2", "-i",
            platform::pathToUtf8(settings.audioTrack)});
  }
  arguments.insert(arguments.end(),
      {"-c:v", encoder, qualityFlag(settings.codec),
          std::to_string(std::clamp(settings.quality, 0, 51)), "-pix_fmt", "yuv420p"});
  if (withAudio) {
    // The delay is expressed with `adelay` so the track starts at the frame the intro ends on,
    // which is when the chart clock begins.
    const int delayMs = static_cast<int>(std::max(0.0, settings.audioDelaySec) * 1000.0);
    arguments.insert(arguments.end(), {"-c:a", "aac", "-b:a", "192k"});
    if (delayMs > 0) {
      arguments.insert(arguments.end(), {"-af", "adelay=" + std::to_string(delayMs)});
    }
    // The audio is not held to the video's length: the picture runs past the music during the exit
    // animation, and stopping there would cut the animation off. The track simply ends.
  } else {
    arguments.insert(arguments.end(), {"-an"});
  }
  arguments.push_back(platform::pathToUtf8(output));
  std::vector<const char*> raw;
  raw.reserve(arguments.size() + 1);
  for (const auto& argument : arguments) raw.push_back(argument.c_str());
  raw.push_back(nullptr);

  SDL_PropertiesID properties = SDL_CreateProperties();
  if (properties == 0) {
    error = SDL_GetError();
    return false;
  }
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, raw.data());
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
      SDL_PROCESS_STDIO_APP);
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
      SDL_PROCESS_STDIO_NULL);
  // The encoder's diagnostics are written to a file rather than to a pipe: while frames are being
  // pushed there is nobody reading a pipe, so a chatty encoder would block on its own error output
  // and stop accepting frames. The file is read only when the run ends or fails.
  diagnosticsPath_ = output;
  diagnosticsPath_ += ".log";
  SDL_IOStream* diagnosticsStream =
      SDL_IOFromFile(platform::pathToUtf8(diagnosticsPath_).c_str(), "wb");
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
      diagnosticsStream != nullptr ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL);
  if (diagnosticsStream != nullptr) {
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_POINTER,
        diagnosticsStream);
  }
  SDL_Process* process = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  // The process holds its own handle on the file; the caller's handle is no longer needed once the
  // process exists, and closing it here flushes anything already written.
  if (diagnosticsStream != nullptr) SDL_CloseIO(diagnosticsStream);
  if (process == nullptr) {
    error = std::string("the video encoder could not be started: ") + SDL_GetError();
    return false;
  }
  SDL_IOStream* input = SDL_GetProcessInput(process);
  if (input == nullptr) {
    SDL_DestroyProcess(process);
    error = "the video encoder has no input stream";
    return false;
  }
  process_ = process;
  input_ = input;
  active_ = true;
  return true;
}

bool VideoEncoder::writeFrame(const std::vector<std::uint8_t>& pixels, std::string& error) {
  if (!active_ || input_ == nullptr) {
    error = "no encoder is running";
    return false;
  }
  if (pixels.empty()) {
    error = "the frame is empty";
    return false;
  }
  const auto* data = pixels.data();
  std::size_t remaining = pixels.size();
  auto* stream = static_cast<SDL_IOStream*>(input_);
  while (remaining > 0) {
    const std::size_t written =
        SDL_WriteIO(stream, data, std::min<std::size_t>(remaining, 1U << 20));
    data += written;
    remaining -= written;
    if (written != 0) continue;
    // A zero-byte write is only fatal when the stream reports an error; a stream whose buffer is
    // momentarily full reports NOT_READY and accepts the data once the reader catches up.
    const SDL_IOStatus status = SDL_GetIOStatus(stream);
    if (status == SDL_IO_STATUS_NOT_READY) {
      SDL_Delay(1);
      continue;
    }
    error = std::string("the video encoder stopped accepting frames: ") + SDL_GetError();
    return false;
  }
  return true;
}

bool VideoEncoder::finish(std::string& error) {
  if (!active_) return true;
  auto* process = static_cast<SDL_Process*>(process_);
  auto* stream = static_cast<SDL_IOStream*>(input_);
  // Closing the input is what tells the encoder there are no more frames; the wait then covers
  // writing the remaining packets and the file trailer.
  if (stream != nullptr) SDL_CloseIO(stream);
  input_ = nullptr;
  // Whatever the encoder reported is read once the process is gone, so a failure explains itself
  // instead of leaving the caller with a bare exit code.
  const auto diagnostics = readDiagnostics();
  int exitCode = -1;
  const bool exited = SDL_WaitProcess(process, true, &exitCode);
  SDL_DestroyProcess(process);
  process_ = nullptr;
  active_ = false;
  if (!exited || exitCode != 0) {
    error = "the video encoder did not finish successfully";
    if (!diagnostics.empty()) error += ": " + diagnostics;
    return false;
  }
  // A finished recording needs no log, so it is removed unless it carries something to say.
  std::error_code removalError;
  if (diagnostics.empty()) std::filesystem::remove(diagnosticsPath_, removalError);
  return true;
}

std::string VideoEncoder::readDiagnostics() const {
  const auto text = platform::readTextFile(diagnosticsPath_);
  if (text.empty()) return {};
  // The last line is the useful one for an encoder failure, and the tail is all that fits a
  // readable message.
  const auto start = text.size() > 400 ? text.size() - 400 : 0U;
  return text.substr(start);
}

void VideoEncoder::abort() noexcept {
  if (!active_) {
    if (process_ != nullptr) {
      SDL_DestroyProcess(static_cast<SDL_Process*>(process_));
      process_ = nullptr;
    }
    input_ = nullptr;
    return;
  }
  auto* process = static_cast<SDL_Process*>(process_);
  if (input_ != nullptr) {
    SDL_CloseIO(static_cast<SDL_IOStream*>(input_));
    input_ = nullptr;
  }
  // Killing the process discards the partial file; waiting would block for as long as the
  // encoder needs to drain frames the user no longer wants.
  SDL_KillProcess(process, true);
  int exitCode = -1;
  SDL_WaitProcess(process, true, &exitCode);
  SDL_DestroyProcess(process);
  process_ = nullptr;
  active_ = false;
}

} // namespace rechigros::app
