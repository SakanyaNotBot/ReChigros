#include "rechigros/app/VideoEncoder.hpp"

#include "rechigros/app/VideoEncoderPlatform.hpp"
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

bool VideoEncoder::usesPlatformEncoder() noexcept { return platformenc::available(); }

std::filesystem::path VideoEncoder::findEncoder() {
  // A build that encodes in-process has nothing to locate; reporting an empty path is what tells
  // the interface the external tool is not needed here.
  if (usesPlatformEncoder()) return {};
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
  if (usesPlatformEncoder()) return openPlatform(settings, output, error);
  executable_ = findEncoder();
  std::error_code fsError;
  if (const auto parent = output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent, fsError);
  }

  const std::string encoder = settings.codec == Codec::H265 ? "libx265"
      : settings.codec == Codec::Vp9                            ? "libvpx-vp9"
                                                                : "libx264";
  // The frame stream is raw video at a fixed size and rate, so every parameter the tool needs is
  // known before the first frame. Frames are already in top-down order, which is what the raw
  // video reader accepts, so no flip filter is applied. When the caller supplies the planar colour
  // layout the tool is told so, which saves it converting every frame on its own.
  inputIsPlanar_ = settings.planarInput;
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
      "-pix_fmt", settings.planarInput ? "yuv420p" : "rgba",
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
  // The effort preset is what decides how much of the frame budget encoding takes. Passing it
  // explicitly also pins it: the encoder's own default is slower and would change between builds.
  if (!settings.preset.empty() && settings.codec != Codec::Vp9) {
    arguments.insert(arguments.end(), {"-preset", settings.preset});
  }
  if (withAudio) {
    // The delay is expressed with `adelay` so the track starts at the frame the intro ends on,
    // which is when the chart clock begins.
    const int delayMs = static_cast<int>(std::max(0.0, settings.audioDelaySec) * 1000.0);
    arguments.insert(arguments.end(), {"-c:a", "aac", "-b:a", "192k"});
    if (delayMs > 0) {
      // A single number delays only the first channel; the delay has to be given once per
      // channel or one side of the stereo track starts late.
      const std::string channels = "adelay=" + std::to_string(delayMs) + "|" +
          std::to_string(delayMs);
      arguments.insert(arguments.end(), {"-af", channels});
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
  // A fresh run clears the teardown request left by a previous one.
  closing_.store(false, std::memory_order_release);
  active_ = true;
  return true;
}

bool VideoEncoder::writeFrame(const std::vector<std::uint8_t>& pixels, std::string& error) {
  if (platform_) {
    if (!active_) {
      error = "no encoder is running";
      return false;
    }
    return writePlatformFrame(pixels.data(), pixels.size(), error);
  }
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
  // Closing the stream from another thread invalidates it underneath this loop, so the whole write
  // holds the pipe lock and re-reads the stream each pass.
  std::lock_guard<std::mutex> pipeGuard(pipeMutex_);
  auto* stream = static_cast<SDL_IOStream*>(input_);
  if (stream == nullptr) {
    error = "the video encoder is no longer accepting frames";
    return false;
  }
  // The whole frame is handed over in one call. Splitting it into fixed-size pieces turned every
  // frame into a run of partial writes, and each of those costs the same regardless of its size.
  while (remaining > 0) {
    // A teardown request stops the loop at a frame boundary instead of letting the writer keep
    // pushing into a stream the caller is about to close.
    if (closing_.load(std::memory_order_acquire)) {
      error = "the video encoder was closed";
      return false;
    }
    const std::size_t written = SDL_WriteIO(stream, data, remaining);
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
  std::lock_guard<std::mutex> pipeGuard(pipeMutex_);
  if (!active_) return true;
  if (platform_) {
    const bool ok = finishPlatform(error);
    active_ = false;
    platform_ = false;
    return ok;
  }
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
  // The writer can be parked inside a blocking write while the child is gone. Waiting for the pipe
  // lock here would keep the caller from ever joining that writer, so the lock is only taken when
  // it is free; a write in flight then finishes on its own and the streams are dropped after it.
  closing_.store(true, std::memory_order_release);
  std::unique_lock<std::mutex> pipeGuard(pipeMutex_);
  if (platform_) {
    platformenc::abort();
    platform_ = false;
    active_ = false;
    return;
  }
  if (!active_) {
    // This run already failed, so the child is gone or going. It is collected first: closing the
    // input stream while the reader still exists flushes into a pipe nobody drains, and closing it
    // while another thread is inside a write is what corrupted the stream. Once the child is reaped
    // there is no reader left, so the close below only releases the handle.
    if (process_ != nullptr) {
      auto* process = static_cast<SDL_Process*>(process_);
      SDL_KillProcess(process, true);
      int exitCode = -1;
      SDL_WaitProcess(process, true, &exitCode);
    }
    if (input_ != nullptr) {
      SDL_CloseIO(static_cast<SDL_IOStream*>(input_));
      input_ = nullptr;
    }
    if (process_ != nullptr) {
      SDL_DestroyProcess(static_cast<SDL_Process*>(process_));
      process_ = nullptr;
    }
    return;
  }
  auto* process = static_cast<SDL_Process*>(process_);
  // The child is stopped before its pipe is closed, in that order: closing first would drain the
  // pipe into a reader that is still alive, and the writer may be parked in it. Killing shortens
  // that wait to the drain the encoder no longer needs to finish.
  SDL_KillProcess(process, true);
  int exitCode = -1;
  SDL_WaitProcess(process, true, &exitCode);
  if (input_ != nullptr) {
    SDL_CloseIO(static_cast<SDL_IOStream*>(input_));
    input_ = nullptr;
  }
  SDL_DestroyProcess(process);
  process_ = nullptr;
  active_ = false;
}

bool VideoEncoder::openPlatform(const Settings& settings, const std::filesystem::path& output,
    std::string& error) {
  // The audio track is handed over before the first frame, because the platform muxer needs to
  // know about it when it writes the container header.
  std::error_code fsError;
  if (const auto parent = output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent, fsError);
  }
  const std::string outputPath = platform::pathToUtf8(output);
  if (!platformenc::open(settings.width, settings.height, settings.fps, settings.quality,
          outputPath, error)) {
    return false;
  }
  const bool withAudio = !settings.audioTrack.empty() && settings.audioSampleRate > 0
      && std::filesystem::is_regular_file(settings.audioTrack, fsError);
  if (withAudio) {
    const int delayMs = static_cast<int>(std::max(0.0, settings.audioDelaySec) * 1000.0);
    std::string audioError;
    if (!platformenc::addAudioTrack(platform::pathToUtf8(settings.audioTrack),
            static_cast<int>(settings.audioSampleRate), delayMs, audioError)) {
      // The run still produces a picture without its music, which is what the external path does
      // when the track cannot be written, so this is not treated as a failed recording.
      (void)audioError;
    }
  }
  widthForPlatform_ = settings.width;
  heightForPlatform_ = settings.height;
  // The platform encoder takes the planar layout as-is, so it is told which one to expect for the
  // same reason the external tool is: it lets the caller do the conversion where it is cheapest.
  inputIsPlanar_ = settings.planarInput;
  platform_ = true;
  active_ = true;
  return true;
}

VideoEncoder::AudioTrackState VideoEncoder::audioTrackState() const noexcept {
  if (!platform_) return AudioTrackState::Ready;
  switch (platformenc::audioTrackState()) {
    case platformenc::AudioTrackState::Preparing: return AudioTrackState::Preparing;
    case platformenc::AudioTrackState::Ready: return AudioTrackState::Ready;
    case platformenc::AudioTrackState::Failed: return AudioTrackState::Failed;
    case platformenc::AudioTrackState::None:
    default: return AudioTrackState::None;
  }
}

bool VideoEncoder::writePlatformFrame(const std::uint8_t* pixels, std::size_t bytes,
    std::string& error) {
  return platformenc::writeFrame(widthForPlatform_, heightForPlatform_, pixels, bytes, error);
}

bool VideoEncoder::finishPlatform(std::string& error) { return platformenc::finish(error); }

void VideoEncoder::abortPlatform() noexcept { platformenc::abort(); }

} // namespace rechigros::app
