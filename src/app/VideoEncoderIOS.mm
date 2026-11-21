// iOS video export through VideoToolbox.
//
// The recorder hands over the same planar I420 frames the GPU conversion produces (and the raw
// interleaved stereo float music track it writes beside the video). VideoToolbox compresses the
// picture with the hardware H.264 encoder; AVFoundation muxes it together with an AAC music track
// into the MPEG-4 container the .mp4 name promises. The session mirrors the Android bridge: one
// process-wide encoder, the music track registered before the first frame starts the container,
// lock-free state polling for the interface, and an abort that tears everything down at once.

#include <TargetConditionals.h>
#if defined(__APPLE__) && TARGET_OS_IPHONE

#include "rechigros/app/VideoEncoderPlatform.hpp"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rechigros::app::platformenc {
namespace {

// In-flight frames the compression session may hold before writeFrame waits. The encoder thread
// upstream already bounds its own queue; this keeps the hardware pipeline from growing without
// limit while still letting several frames overlap.
constexpr int kMaxInFlight = 6;
// Frames of music fed to the AAC encoder per sample buffer.
constexpr int kAudioChunkFrames = 4096;

int clampFps(int fps) { return std::clamp(fps, 15, 240); }

// The same quality-to-bitrate hint the Android bridge uses, so one setting yields a comparable
// file on both platforms: a bits-per-pixel budget scaled by resolution and rate, floored at 1 Mbps.
int bitrateFor(int width, int height, int fps, int quality) {
  const double bitsPerPixel = quality <= 18 ? 0.16 : (quality <= 26 ? 0.10 : 0.06);
  const double bits = static_cast<double>(width) * height * fps * bitsPerPixel;
  return static_cast<int>(std::max(1000000.0, bits));
}

// One recording session. Every field is touched from at most one of: the encoder thread
// (open/writeFrame/finish/abort), the VideoToolbox callback thread, or the music thread. The mutex
// serialises the container and the start handshake; the atomics carry the polled state.
struct Session {
  std::mutex mutex;
  std::condition_variable cv;

  AVAssetWriter* writer = nil;
  AVAssetWriterInput* videoInput = nil;
  AVAssetWriterInput* audioInput = nil;
  VTCompressionSessionRef videoSession = nullptr;
  CVPixelBufferPoolRef pixelPool = nullptr;

  int width = 0;
  int height = 0;
  int fps = 60;
  std::int64_t frameIndex = 0;
  int inFlight = 0;
  bool writingStarted = false;
  std::string error;

  std::atomic<bool> aborted{false};
  // Releases the music thread when the container closes without ever starting (no video frame),
  // so a finish or abort never waits on a track that is still parked at the start handshake.
  std::atomic<bool> audioStop{false};

  // Music track: the PCM is ingested synchronously (the caller deletes the file right after), then
  // fed to the AAC input on its own thread once the container has started.
  std::vector<std::int16_t> pcm;
  int sampleRate = 48000;
  std::int64_t delayFrames = 0;
  std::thread audioThread;
  std::atomic<int> audioState{0}; // 0 none, 1 preparing, 2 ready, 3 failed
  std::atomic<int> audioProgress{0};
  std::atomic<std::uint64_t> audioGeneration{0};
};

Session gSession;

void setError(Session& s, const std::string& message) {
  std::lock_guard<std::mutex> guard(s.mutex);
  if (s.error.empty()) s.error = message;
}

// Starts the container exactly once, after both tracks are known. The first compressed video frame
// triggers it; the music thread waits on the same handshake before feeding samples.
bool ensureWritingStarted(Session& s) {
  if (s.writingStarted) return true;
  if (s.writer == nil) return false;
  if (![s.writer startWriting]) {
    setError(s, "the video container could not be opened: "
        + std::string(s.writer.error.localizedDescription.UTF8String ?: "unknown"));
    return false;
  }
  [s.writer startSessionAtSourceTime:kCMTimeZero];
  s.writingStarted = true;
  s.cv.notify_all();
  return true;
}

// VideoToolbox delivers each compressed frame here, on its own thread. The sample buffer already
// carries the presentation time writeFrame assigned, so it is appended to the container as-is.
void encoderOutputCallback(void* refCon, void* /*sourceFrameRefCon*/, OSStatus status,
    VTEncodeInfoFlags /*infoFlags*/, CMSampleBufferRef sampleBuffer) {
  Session& s = *static_cast<Session*>(refCon);
  if (s.aborted.load()) {
    std::lock_guard<std::mutex> guard(s.mutex);
    s.inFlight = std::max(0, s.inFlight - 1);
    s.cv.notify_all();
    return;
  }
  if (status != noErr || sampleBuffer == nullptr || !CMSampleBufferDataIsReady(sampleBuffer)) {
    if (status != noErr) {
      setError(s, "the hardware encoder rejected a frame (" + std::to_string(status) + ")");
    }
    std::lock_guard<std::mutex> guard(s.mutex);
    s.inFlight = std::max(0, s.inFlight - 1);
    s.cv.notify_all();
    return;
  }
  std::lock_guard<std::mutex> guard(s.mutex);
  if (!ensureWritingStarted(s)) {
    s.inFlight = std::max(0, s.inFlight - 1);
    s.cv.notify_all();
    return;
  }
  // VideoToolbox delivers compressed frames in presentation order on a serial callback, and the
  // append happens under the session lock, so the container sees strictly increasing timestamps.
  // The writer buffers ahead (expectsMediaDataInRealTime is off); writeFrame's in-flight bound is
  // what keeps that buffer from growing without limit, so no wait is needed here.
  if (!s.aborted.load() && s.videoInput != nil) {
    if (![s.videoInput appendSampleBuffer:sampleBuffer]) {
      setError(s, "the video container refused a frame: "
          + std::string(s.writer.error.localizedDescription.UTF8String ?: "unknown"));
    }
  }
  s.inFlight = std::max(0, s.inFlight - 1);
  s.cv.notify_all();
}

// Creates the NV12 pixel-buffer pool the compression session draws from. Limited-range video NV12
// matches the BT.709 limited-range planes the GPU conversion produces.
bool createPixelPool(Session& s) {
  const int w = s.width;
  const int h = s.height;
  NSDictionary* attrs = @{
    (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
        @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
    (__bridge NSString*)kCVPixelBufferWidthKey : @(w),
    (__bridge NSString*)kCVPixelBufferHeightKey : @(h),
    (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
  };
  NSDictionary* poolAttrs = @{
    (__bridge NSString*)kCVPixelBufferPoolAllocationThresholdKey : @(kMaxInFlight + 2),
  };
  CVReturn rc = CVPixelBufferPoolCreate(kCFAllocatorDefault,
      (__bridge CFDictionaryRef)poolAttrs, (__bridge CFDictionaryRef)attrs, &s.pixelPool);
  return rc == kCVReturnSuccess && s.pixelPool != nullptr;
}

// Copies the luma plane and interleaves the chroma into the NV12 buffer, honouring the row strides
// CoreVideo reports. `i420` is the tightly packed Y+U+V layout the renderer produces.
void fillNv12FromI420(CVPixelBufferRef pb, const std::uint8_t* i420, int w, int h) {
  CVPixelBufferLockBaseAddress(pb, 0);
  auto* yDst = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 0));
  auto* uvDst = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 1));
  const std::size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
  const std::size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
  const std::uint8_t* ySrc = i420;
  const std::uint8_t* uSrc = i420 + static_cast<std::size_t>(w) * h;
  const std::uint8_t* vSrc = uSrc + static_cast<std::size_t>(w / 2) * (h / 2);
  for (int row = 0; row < h; ++row) {
    std::memcpy(yDst + static_cast<std::size_t>(row) * yStride,
        ySrc + static_cast<std::size_t>(row) * w, static_cast<std::size_t>(w));
  }
  const int cw = w / 2;
  const int ch = h / 2;
  for (int row = 0; row < ch; ++row) {
    std::uint8_t* dst = uvDst + static_cast<std::size_t>(row) * uvStride;
    const std::uint8_t* u = uSrc + static_cast<std::size_t>(row) * cw;
    const std::uint8_t* v = vSrc + static_cast<std::size_t>(row) * cw;
    for (int col = 0; col < cw; ++col) {
      dst[col * 2] = u[col];
      dst[col * 2 + 1] = v[col];
    }
  }
  CVPixelBufferUnlockBaseAddress(pb, 0);
}

// RGBA fallback: the recorder falls back to a colour readback when the GPU conversion is
// unavailable, so the same BT.709 limited-range math the shader uses is applied here.
void fillNv12FromRgba(CVPixelBufferRef pb, const std::uint8_t* rgba, int w, int h) {
  CVPixelBufferLockBaseAddress(pb, 0);
  auto* yDst = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 0));
  auto* uvDst = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pb, 1));
  const std::size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
  const std::size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
  const auto luma = [](int r, int g, int b) {
    return static_cast<int>(16.0 + (0.2126 * r + 0.7152 * g + 0.0722 * b) * (219.0 / 255.0));
  };
  for (int row = 0; row < h; ++row) {
    const std::uint8_t* src = rgba + static_cast<std::size_t>(row) * w * 4;
    std::uint8_t* dst = yDst + static_cast<std::size_t>(row) * yStride;
    for (int col = 0; col < w; ++col) {
      dst[col] = static_cast<std::uint8_t>(std::clamp(
          luma(src[col * 4], src[col * 4 + 1], src[col * 4 + 2]), 0, 255));
    }
  }
  const int cw = w / 2;
  const int ch = h / 2;
  for (int row = 0; row < ch; ++row) {
    std::uint8_t* dst = uvDst + static_cast<std::size_t>(row) * uvStride;
    for (int col = 0; col < cw; ++col) {
      int r = 0, g = 0, b = 0;
      for (int dy = 0; dy < 2; ++dy) {
        const std::uint8_t* src =
            rgba + static_cast<std::size_t>(row * 2 + dy) * w * 4 + (col * 2) * 4;
        for (int dx = 0; dx < 2; ++dx) {
          r += src[dx * 4];
          g += src[dx * 4 + 1];
          b += src[dx * 4 + 2];
        }
      }
      r /= 4; g /= 4; b /= 4;
      const int cb = static_cast<int>(128.0 + (-0.114572 * r - 0.385428 * g + 0.5 * b) * (224.0 / 255.0));
      const int cr = static_cast<int>(128.0 + (0.5 * r - 0.454153 * g - 0.045847 * b) * (224.0 / 255.0));
      dst[col * 2] = static_cast<std::uint8_t>(std::clamp(cb, 0, 255));
      dst[col * 2 + 1] = static_cast<std::uint8_t>(std::clamp(cr, 0, 255));
    }
  }
  CVPixelBufferUnlockBaseAddress(pb, 0);
}

void tagColorSpace(CVPixelBufferRef pb) {
  CVBufferSetAttachment(pb, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2,
      kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2,
      kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(pb, kCVImageBufferTransferFunctionKey,
      kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
}

// Builds a linear-PCM format description matching the int16 stereo samples fed to the AAC input.
CMAudioFormatDescriptionRef makePcmFormat(int sampleRate) {
  AudioStreamBasicDescription asbd{};
  asbd.mSampleRate = static_cast<Float64>(sampleRate);
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  asbd.mBytesPerPacket = 4;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerFrame = 4;
  asbd.mChannelsPerFrame = 2;
  asbd.mBitsPerChannel = 16;
  CMAudioFormatDescriptionRef format = nullptr;
  CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr,
      &format);
  return format;
}

// Wraps `frames` interleaved int16 samples (starting at `data`) into a sample buffer timed at
// `startFrame` of the stream. The caller owns the memory until the buffer is released.
CMSampleBufferRef makeAudioBuffer(const std::int16_t* data, int frames, std::int64_t startFrame,
    int sampleRate, CMAudioFormatDescriptionRef format) {
  const std::size_t bytes = static_cast<std::size_t>(frames) * 4;
  CMBlockBufferRef block = nullptr;
  if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, const_cast<std::int16_t*>(data),
          bytes, kCFAllocatorNull, nullptr, 0, bytes, 0, &block) != kCMBlockBufferNoErr) {
    return nullptr;
  }
  CMSampleBufferRef buffer = nullptr;
  const CMSampleTimingInfo timing{
      CMTimeMake(1, sampleRate), CMTimeMake(startFrame, sampleRate), kCMTimeInvalid};
  const std::size_t sampleSize = 4;
  OSStatus rc = CMSampleBufferCreate(kCFAllocatorDefault, block, true, nullptr, nullptr, format,
      frames, 1, &timing, 1, &sampleSize, &buffer);
  CFRelease(block);
  if (rc != noErr) return nullptr;
  return buffer;
}

// Feeds the music track: leading silence implements the chart-clock delay, then the song itself.
// Runs on its own thread once the container has started; the generation guard stops a cancelled
// run's track from feeding the next one.
void audioFeedLoop(Session& s, std::uint64_t generation) {
  CMAudioFormatDescriptionRef format = makePcmFormat(s.sampleRate);
  if (format == nullptr) {
    s.audioState.store(3);
    return;
  }
  {
    std::unique_lock<std::mutex> guard(s.mutex);
    s.cv.wait(guard, [&] {
      return s.writingStarted || s.aborted.load() || s.audioStop.load();
    });
  }
  const std::int64_t contentFrames = static_cast<std::int64_t>(s.pcm.size() / 2);
  const std::int64_t totalFrames = s.delayFrames + contentFrames;
  std::int64_t presented = 0;
  const std::vector<std::int16_t> silence(static_cast<std::size_t>(kAudioChunkFrames) * 2, 0);
  const auto stop = [&] { return s.aborted.load() || s.audioStop.load(); };
  const auto feed = [&](const std::int16_t* data, int frames) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!stop() && s.audioGeneration.load() == generation
        && std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> guard(s.mutex);
        if (s.audioInput != nil && [s.audioInput isReadyForMoreMediaData]) {
          CMSampleBufferRef buffer = makeAudioBuffer(data, frames, presented, s.sampleRate, format);
          if (buffer != nullptr) {
            const bool ok = [s.audioInput appendSampleBuffer:buffer];
            CFRelease(buffer);
            if (!ok) {
              setError(s, "the audio container refused a sample buffer");
              return false;
            }
          }
          presented += frames;
          const int permille = totalFrames > 0
              ? static_cast<int>(std::min<std::int64_t>(999, presented * 1000 / totalFrames))
              : 1000;
          s.audioProgress.store(permille);
          return true;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  };
  // Leading silence: the music starts where the chart clock does, not at the container's zero.
  std::int64_t remainingDelay = s.delayFrames;
  while (remainingDelay > 0 && !stop() && s.audioGeneration.load() == generation) {
    const int chunk = static_cast<int>(std::min<std::int64_t>(kAudioChunkFrames, remainingDelay));
    if (!feed(silence.data(), chunk)) break;
    remainingDelay -= chunk;
  }
  // The song itself, in chunks.
  std::int64_t offset = 0;
  while (offset < contentFrames && !stop() && s.audioGeneration.load() == generation) {
    const int chunk = static_cast<int>(std::min<std::int64_t>(kAudioChunkFrames, contentFrames - offset));
    if (!feed(s.pcm.data() + static_cast<std::size_t>(offset) * 2, chunk)) break;
    offset += chunk;
  }
  if (format != nullptr) CFRelease(format);
  s.audioProgress.store(1000);
  s.cv.notify_all();
}

void releaseSession(Session& s) {
  if (s.videoSession != nullptr) {
    VTCompressionSessionInvalidate(s.videoSession);
    CFRelease(s.videoSession);
    s.videoSession = nullptr;
  }
  if (s.pixelPool != nullptr) {
    CVPixelBufferPoolRelease(s.pixelPool);
    s.pixelPool = nullptr;
  }
  if (s.writer != nil) {
    if (s.writer.status == AVAssetWriterStatusWriting) [s.writer cancelWriting];
    s.writer = nil;
  }
  s.videoInput = nil;
  s.audioInput = nil;
}

} // namespace

bool available() noexcept { return true; }

bool open(int width, int height, int fps, int quality, const std::string& outputPath,
    std::string& error) {
  Session& s = gSession;
  abort();
  std::lock_guard<std::mutex> guard(s.mutex);
  s.error.clear();
  s.aborted.store(false);
  s.frameIndex = 0;
  s.inFlight = 0;
  s.writingStarted = false;
  s.width = width;
  s.height = height;
  s.fps = clampFps(fps);
  s.audioState.store(0);
  s.audioProgress.store(0);
  s.audioStop.store(false);
  s.pcm.clear();
  s.audioGeneration.fetch_add(1);

  if (width <= 0 || height <= 0) {
    error = "the frame size is invalid";
    return false;
  }
  NSString* path = [NSString stringWithUTF8String:outputPath.c_str()];
  NSURL* url = [NSURL fileURLWithPath:path];
  [[NSFileManager defaultManager] createDirectoryAtURL:[url URLByDeletingLastPathComponent]
      withIntermediateDirectories:YES attributes:nil error:nil];
  s.writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:nil];
  if (s.writer == nil) {
    error = "the video container could not be created";
    return false;
  }
  // The picture arrives already compressed by VideoToolbox, so the container input passes the
  // samples through unchanged; its format is taken from the first compressed frame.
  s.videoInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:nil];
  s.videoInput.expectsMediaDataInRealTime = NO;
  if (![s.writer canAddInput:s.videoInput]) {
    error = "the video container refused the video track";
    releaseSession(s);
    return false;
  }
  [s.writer addInput:s.videoInput];

  const int bitrate = bitrateFor(width, height, s.fps, quality);
  OSStatus rc = VTCompressionSessionCreate(kCFAllocatorDefault, width, height,
      kCMVideoCodecType_H264, nullptr, nullptr, nullptr, encoderOutputCallback, &s, &s.videoSession);
  if (rc != noErr || s.videoSession == nullptr) {
    error = "the hardware video encoder could not be created (" + std::to_string(rc) + ")";
    releaseSession(s);
    return false;
  }
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_ProfileLevel,
      kVTProfileLevel_H264_High_AutoLevel);
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_AverageBitRate,
      (__bridge CFTypeRef)@(bitrate));
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_MaxKeyFrameInterval,
      (__bridge CFTypeRef)@(s.fps));
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_ExpectedFrameRate,
      (__bridge CFTypeRef)@(s.fps));
  // No frame reordering keeps presentation and decode order identical, which suits a recording
  // whose frames are produced strictly in sequence.
  VTSessionSetProperty(s.videoSession, kVTCompressionPropertyKey_AllowFrameReordering,
      kCFBooleanFalse);
  VTCompressionSessionPrepareToEncodeFrames(s.videoSession);
  if (!createPixelPool(s)) {
    error = "the video frame pool could not be allocated";
    releaseSession(s);
    return false;
  }
  return true;
}

bool writeFrame(int width, int height, const std::uint8_t* pixels, std::size_t bytes,
    std::string& error) {
  Session& s = gSession;
  if (s.aborted.load() || s.videoSession == nullptr) {
    error = "the video encoder is not running";
    return false;
  }
  const std::size_t planarBytes = static_cast<std::size_t>(width) * height * 3 / 2;
  const std::size_t rgbaBytes = static_cast<std::size_t>(width) * height * 4;
  const bool planar = bytes == planarBytes;
  if (!planar && bytes != rgbaBytes) {
    error = "the frame layout does not match the encoder";
    return false;
  }
  // Backpressure: let the hardware pipeline drain before queueing more, bounded so a stalled
  // encoder reports a failure instead of freezing the recorder.
  {
    std::unique_lock<std::mutex> guard(s.mutex);
    if (!s.cv.wait_for(guard, std::chrono::seconds(20),
            [&] { return s.aborted.load() || s.inFlight < kMaxInFlight; })) {
      error = "the video encoder stopped accepting frames";
      return false;
    }
    if (s.aborted.load()) {
      error = "the video encoder was cancelled";
      return false;
    }
  }
  CVPixelBufferRef pb = nullptr;
  if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, s.pixelPool, &pb) != kCVReturnSuccess
      || pb == nullptr) {
    error = "a video frame buffer could not be allocated";
    return false;
  }
  if (planar) {
    fillNv12FromI420(pb, pixels, width, height);
  } else {
    fillNv12FromRgba(pb, pixels, width, height);
  }
  tagColorSpace(pb);
  const CMTime pts = CMTimeMake(s.frameIndex, s.fps);
  const CMTime duration = CMTimeMake(1, s.fps);
  {
    std::lock_guard<std::mutex> guard(s.mutex);
    ++s.inFlight;
    ++s.frameIndex;
  }
  const OSStatus rc = VTCompressionSessionEncodeFrame(s.videoSession, pb, pts, duration, nullptr,
      nullptr, nullptr);
  CVPixelBufferRelease(pb);
  if (rc != noErr) {
    std::lock_guard<std::mutex> guard(s.mutex);
    s.inFlight = std::max(0, s.inFlight - 1);
    error = "the hardware encoder refused a frame (" + std::to_string(rc) + ")";
    return false;
  }
  return true;
}

bool addAudioTrack(const std::string& pcmPath, int sampleRate, int delayMs, std::string& error) {
  Session& s = gSession;
  if (s.aborted.load() || s.writer == nil) {
    error = "the video encoder is not running";
    return false;
  }
  s.audioState.store(1);
  s.audioProgress.store(0);
  // The caller deletes the PCM file as soon as this returns, so the samples are read here.
  std::FILE* file = std::fopen(pcmPath.c_str(), "rb");
  if (file == nullptr) {
    s.audioState.store(3);
    error = "the music track could not be read";
    return false;
  }
  std::vector<float> raw;
  float buffer[8192];
  std::size_t got = 0;
  while ((got = std::fread(buffer, sizeof(float), 8192, file)) > 0) {
    raw.insert(raw.end(), buffer, buffer + got);
  }
  std::fclose(file);
  s.sampleRate = std::max(8000, sampleRate);
  s.delayFrames = static_cast<std::int64_t>(std::max(0, delayMs)) * s.sampleRate / 1000;
  s.pcm.resize(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const float clamped = std::clamp(raw[i], -1.0F, 1.0F);
    s.pcm[i] = static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
  }
  // The AAC track is registered before the first video frame starts the container, exactly as the
  // muxer requires; the samples themselves are fed once writing has begun.
  NSDictionary* settings = @{
    AVFormatIDKey : @(kAudioFormatMPEG4AAC),
    AVSampleRateKey : @(s.sampleRate),
    AVNumberOfChannelsKey : @2,
    AVEncoderBitRateKey : @192000,
  };
  {
    std::lock_guard<std::mutex> guard(s.mutex);
    s.audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
        outputSettings:settings];
    s.audioInput.expectsMediaDataInRealTime = NO;
    if ([s.writer canAddInput:s.audioInput]) {
      [s.writer addInput:s.audioInput];
    } else {
      s.audioInput = nil;
    }
  }
  if (s.audioInput == nil) {
    s.audioState.store(3);
    error = "the video container refused the audio track";
    return false;
  }
  const std::uint64_t generation = s.audioGeneration.load();
  if (s.audioThread.joinable()) s.audioThread.join();
  s.audioThread = std::thread(audioFeedLoop, std::ref(s), generation);
  s.audioState.store(2);
  s.audioProgress.store(0);
  return true;
}

bool finish(std::string& error) {
  Session& s = gSession;
  {
    std::lock_guard<std::mutex> guard(s.mutex);
    if (s.writer == nil || s.videoSession == nullptr) {
      error = "the video encoder is not running";
      return false;
    }
    if (s.aborted.load()) {
      error = "the video encoder was cancelled";
      return false;
    }
  }
  // Drain every queued frame through the callback before closing the picture track. The callback
  // takes the same mutex, so the lock must be released across this call or the drain deadlocks.
  VTCompressionSessionCompleteFrames(s.videoSession, kCMTimeInvalid);
  {
    std::unique_lock<std::mutex> guard(s.mutex);
    if (!s.cv.wait_for(guard, std::chrono::seconds(30),
            [&] { return s.aborted.load() || s.inFlight == 0; })) {
      error = "the video encoder did not finish";
      return false;
    }
  }
  // The music thread parks at the start handshake until the first frame; release it whether or
  // not writing ever began, then collect it before the container is closed.
  s.audioStop.store(true);
  s.cv.notify_all();
  if (s.audioThread.joinable()) s.audioThread.join();
  std::unique_lock<std::mutex> guard(s.mutex);
  if (s.aborted.load()) {
    error = "the video encoder was cancelled";
    return false;
  }
  if (s.videoInput != nil) [s.videoInput markAsFinished];
  if (s.audioInput != nil) [s.audioInput markAsFinished];
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [s.writer finishWritingWithCompletionHandler:^{
    dispatch_semaphore_signal(done);
  }];
  guard.unlock();
  const long waited = dispatch_semaphore_wait(done,
      dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(30) * NSEC_PER_SEC));
  guard.lock();
  const bool completed = waited == 0 && s.writer.status == AVAssetWriterStatusCompleted;
  if (!completed) {
    error = "the video container did not close: "
        + std::string(s.writer.error.localizedDescription.UTF8String ?: "unknown");
  }
  releaseSession(s);
  return completed;
}

void abort() noexcept {
  Session& s = gSession;
  s.aborted.store(true);
  s.audioStop.store(true);
  s.audioGeneration.fetch_add(1);
  s.cv.notify_all();
  if (s.audioThread.joinable()) s.audioThread.join();
  std::lock_guard<std::mutex> guard(s.mutex);
  releaseSession(s);
  s.inFlight = 0;
  s.writingStarted = false;
}

AudioTrackState audioTrackState() {
  switch (gSession.audioState.load()) {
    case 1: return AudioTrackState::Preparing;
    case 2: return AudioTrackState::Ready;
    case 3: return AudioTrackState::Failed;
    default: return AudioTrackState::None;
  }
}

int audioTrackProgressPermille() { return gSession.audioProgress.load(); }

} // namespace rechigros::app::platformenc

#endif // __APPLE__ && TARGET_OS_IPHONE
