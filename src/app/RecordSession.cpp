#include "rechigros/app/RecordSession.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/audio/Decoder.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/scene/Transition.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace rechigros::app {
namespace {

// The opening transition runs for its own duration before the chart clock starts, and the exit
// animation runs after the music ends. A recording covers both, because they are part of what the
// run looks like; the settlement page is deliberately excluded.
constexpr double kIntroLeadSec = 1.083;
constexpr double kOutroTailSec = 1.7;

// Frames rendered per call to step(). Each frame costs a full offscreen render plus an encode
// write, so the batch is sized to keep a 60 Hz interface responsive while still making progress.
constexpr int kFramesPerStep = 2;

// Sample rate of the rendered audio track. It is the rate the decoder resamples to, so the encoder
// never has to convert, and it is high enough that a re-encode at this rate is transparent.
constexpr std::uint32_t kAudioSampleRate = 48000;

std::string timestampName() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  std::ostringstream text;
  text << std::put_time(&local, "%Y%m%d-%H%M%S");
  return text.str();
}

} // namespace
const std::vector<RecordSession::Resolution>& RecordSession::resolutions() {
  // The first entry is the standard 16:9 landscape size and is the default, so a recording made
  // without touching the setting matches what the game looks like on a typical display.
  static const std::vector<Resolution> kResolutions = {
      {"1080p (1920x1080)", 1920, 1080},
      {"720p (1280x720)", 1280, 720},
      {"1440p (2560x1440)", 2560, 1440},
      {"2160p (3840x2160)", 3840, 2160},
      {"Square (1080x1080)", 1080, 1080},
      {"Portrait (1080x1920)", 1080, 1920},
  };
  return kResolutions;
}

std::filesystem::path RecordSession::defaultOutputDirectory() {
  // The device's public video directory is where a recorded video is expected to appear, so the
  // gallery and any media application can find it without the user moving the file.
  return platform::publicVideoDirectory();
}

std::string RecordSession::defaultFileName() { return timestampName(); }

float RecordSession::progress() const noexcept {
  if (totalFrames_ <= 0) return 0.0F;
  return std::clamp(static_cast<float>(renderedFrames_) / static_cast<float>(totalFrames_), 0.0F,
      1.0F);
}

bool RecordSession::start(AppContext& context, const Settings& settings, std::string& error) {
  if (state_ == State::Running) {
    error = "a recording is already running";
    return false;
  }
  if (context.renderer == nullptr) {
    error = "the renderer is not available";
    return false;
  }
  if (!context.game.loaded || !context.game.chart) {
    error = "no chart is loaded";
    return false;
  }
  const auto& resolutions = RecordSession::resolutions();
  const int index = std::clamp(settings.resolutionIndex, 0,
      static_cast<int>(resolutions.size()) - 1);
  const auto& resolution = resolutions[static_cast<std::size_t>(index)];
  settings_ = settings;
  settings_.resolutionIndex = index;
  settings_.fps = std::clamp(settings.fps, 15, 240);
  if (settings_.fileName.empty()) settings_.fileName = defaultFileName();
  if (settings_.outputDirectory.empty()) settings_.outputDirectory = defaultOutputDirectory();

  // The file name is used as given but never allowed to escape the chosen directory.
  std::string name = settings_.fileName;
  std::replace(name.begin(), name.end(), '/', '_');
  std::replace(name.begin(), name.end(), '\\', '_');
  std::filesystem::path file = settings_.outputDirectory / (name + ".mp4");
  outputPath_ = file;

  const double chartEnd = context.game.chart->chartEndTimeSec;
  const double duration = std::max(0.1, chartEnd + kOutroTailSec);
  totalFrames_ = std::max(1, static_cast<int>(std::ceil(duration * settings_.fps)));
  renderedFrames_ = 0;

  // The audio track is the chart's music, decoded once and written as raw samples for the encoder.
  // It starts after the opening transition, so the first beat lands on the first note.
  audioTrackPath_.clear();
  if (settings_.noteHitsound) {
    audioTrackPath_ = writeAudioTrack(context, file);
  }

  VideoEncoder::Settings encoderSettings;
  encoderSettings.width = resolution.width;
  encoderSettings.height = resolution.height;
  encoderSettings.fps = settings_.fps;
  encoderSettings.codec = settings_.codec;
  encoderSettings.audioTrack = audioTrackPath_;
  encoderSettings.audioSampleRate = kAudioSampleRate;
  // The encoder places the track at the frame the chart clock starts on, so the samples keep their
  // original timing instead of being shifted by padding.
  encoderSettings.audioDelaySec = kIntroLeadSec;
  if (!encoder_.open(encoderSettings, file, error)) {
    state_ = State::Failed;
    error_ = error;
    std::error_code cleanup;
    if (!audioTrackPath_.empty()) std::filesystem::remove(audioTrackPath_, cleanup);
    audioTrackPath_.clear();
    return false;
  }

  render::RenderFrameInput probe;
  // The recording always runs the chart in autoplay, which is what makes it reproducible: no
  // input is injected, and the judged result comes from the chart itself.
  context.game.autoplay = true;
  context.game.session.setChart(context.game.chart.get(), context.game.challenge);
  context.game.session.setAutoplay(true);
  context.game.session.setMusicSpeed(context.game.musicSpeed);
  if (!context.renderer->beginCapture(probe, context.game.chart.get(), resolution.width,
          resolution.height)) {
    encoder_.abort();
    state_ = State::Failed;
    error_ = "the offscreen render target could not be allocated";
    error = error_;
    return false;
  }
  framePixels_.clear();
  state_ = State::Running;
  error_.clear();
  return true;
}

bool RecordSession::step(AppContext& context) {
  if (state_ != State::Running) return false;
  if (!context.game.chart || context.renderer == nullptr) {
    state_ = State::Failed;
    error_ = "the chart went away while recording";
    return false;
  }
  const auto& resolutions = RecordSession::resolutions();
  const auto& resolution =
      resolutions[static_cast<std::size_t>(settings_.resolutionIndex)];
  const double frameDuration = 1.0 / static_cast<double>(settings_.fps);
  const double chartEnd = context.game.chart->chartEndTimeSec;

  for (int index = 0; index < kFramesPerStep && renderedFrames_ < totalFrames_; ++index) {
    const int frame = renderedFrames_;
    // The recording covers the opening transition, the chart and the exit animation. Time is
    // derived from the frame number, not accumulated, so the video cannot drift.
    const double elapsed = static_cast<double>(frame) * frameDuration;
    const bool inIntro = elapsed < kIntroLeadSec;
    const double chartTime = inIntro ? 0.0 : elapsed - kIntroLeadSec;

    render::RenderFrameInput input;
    input.chartTimeSec = chartTime;
    input.playheadSec = chartTime;
    input.visualTimeSec = elapsed;
    input.userOffsetSec = context.game.userOffsetSec;
    input.dpi = context.displayDpi;
    input.scrollSpeed = context.settings.scrollSpeed;
    input.mirrorX = context.settings.mirrorX;
    input.backgroundDim = context.settings.backgroundDim;
    input.aspectRatio = context.settings.aspectRatio;
    input.challengeMode = context.game.challenge;
    input.apfcIndicator = context.settings.apfcIndicator;
    input.judgeLineWidth = context.settings.judgeLineWidth;
    input.lowResolutionMode = context.settings.lowResolutionMode;
    input.antialias = context.settings.antialias;
    input.disableChartShader = context.settings.disableChartShader;
    input.multiPressHighlight = context.settings.multiPressHighlight;
    input.storyMode = context.settings.storyMode;
    input.chartReveal = context.settings.chartReveal;
    input.showChartDebug = false;
    input.transition.inIntro = inIntro;
    input.transition.introLineScale = inIntro
        ? render::scene::introLineScale(
              static_cast<float>(std::clamp(elapsed / (55.0 / 60.0), 0.0, 1.0)))
        : 1.0F;
    const bool inOutro = chartTime > chartEnd;
    input.transition.inOutro = inOutro;
    input.transition.outroElapsedSec = inOutro
        ? static_cast<float>(chartTime - chartEnd)
        : 0.0F;

    // The session advances with the chart clock so the judgement state matches the picture.
    if (!inIntro) {
      context.game.session.update(chartTime, static_cast<float>(frameDuration));
      (void)context.game.session.takeSoundEvents();
    }
    const auto& judgement = context.game.session.judgement();

    input.hud.visible = true;
    input.hud.combo = judgement.combo();
    input.hud.score = judgement.score();
    input.hud.accuracy = context.settings.realtimeAccuracy ? judgement.realtimeAccuracy()
                                                           : judgement.accuracy();
    input.hud.maxCombo = judgement.maxCombo();
    input.hud.perfect = judgement.counts()[0];
    input.hud.good = judgement.counts()[1];
    input.hud.bad = judgement.counts()[2];
    input.hud.miss = judgement.counts()[3];
    input.hud.playheadSec = chartTime;
    input.hud.totalDurationSec = chartEnd;
    input.hud.showAccuracy = context.settings.realtimeAccuracy;
    input.hud.scoreJumpEffect = context.settings.scoreJumpEffect;
    input.hud.comboLabel = settings_.comboLabel.empty() ? "RECORD" : settings_.comboLabel;
    input.hud.hitOffsetIndicatorMode = 0;
    input.hud.hitOffsetSuppressed = true;
    input.hud.showFps = false;
    input.hud.visualTimeSec = elapsed;
    input.hud.introProgress = inIntro
        ? render::scene::introUiEase(
              static_cast<float>(std::clamp(elapsed / (40.0 / 60.0), 0.0, 1.0)))
        : 1.0F;
    input.songName = context.play.songName;
    input.levelName = context.play.difficulty;
    // Hit feedback is part of what a run looks like; the interface can turn the particles off
    // while keeping the sound.
    input.showHitParticles = settings_.noteHitParticles;
    (void)resolution;

    if (!context.renderer->captureFrame(input, context.game.chart.get(), &judgement,
            framePixels_)) {
      state_ = State::Failed;
      error_ = "a frame could not be rendered";
      return false;
    }
    std::string writeError;
    if (!encoder_.writeFrame(framePixels_, writeError)) {
      state_ = State::Failed;
      error_ = writeError;
      return false;
    }
    ++renderedFrames_;
  }

  if (renderedFrames_ >= totalFrames_) {
    finish(context);
    return false;
  }
  return true;
}

void RecordSession::cancel(AppContext& context) {
  if (state_ == State::Running && encoder_.active()) encoder_.abort();
  releaseResources(context);
  state_ = State::Cancelled;
}

void RecordSession::finish(AppContext& context) {
  if (state_ != State::Running) return;
  std::string error;
  if (!encoder_.finish(error)) {
    state_ = State::Failed;
    error_ = error;
    releaseResources(context);
    return;
  }
  releaseResources(context);
  state_ = State::Finished;
}

void RecordSession::releaseResources(AppContext& context) noexcept {
  if (context.renderer != nullptr) context.renderer->endCapture();
  framePixels_.clear();
  // The raw sample file is only an input to the encoder; once the encode is over it would just be a
  // stray copy of the music next to the video.
  if (!audioTrackPath_.empty()) {
    std::error_code cleanup;
    std::filesystem::remove(audioTrackPath_, cleanup);
    audioTrackPath_.clear();
  }
}

// Decodes the chart's music and writes it as raw interleaved stereo float samples beside the video,
// which is what the encoder consumes as its second input. Returns an empty path when the chart has no
// music or it cannot be decoded, in which case the recording is simply video-only.
std::filesystem::path RecordSession::writeAudioTrack(AppContext& context,
    const std::filesystem::path& videoPath) {
  if (context.play.musicFile.empty()) return {};
  audio::AudioBuffer buffer =
      audio::Decoder::decodeFile(context.play.musicFile, kAudioSampleRate);
  if (buffer.empty()) return {};
  // The track is trimmed to the chart's own length so the file does not end on a silent tail; the
  // encoder repeats nothing, so a shorter track simply finishes before the video does.
  const double chartEnd = context.game.chart != nullptr
      ? context.game.chart->chartEndTimeSec
      : buffer.durationSeconds();
  const std::int64_t frames = std::min<std::int64_t>(buffer.frameCount(),
      static_cast<std::int64_t>(std::ceil(std::max(0.0, chartEnd) * kAudioSampleRate)));
  if (frames <= 0) return {};
  std::filesystem::path track = videoPath;
  track += ".pcm";
  // The track is opened through the platform's own wide-capable path: a name holding characters
  // outside the ANSI code page cannot be opened through the narrow conversion at all.
  std::FILE* file = std::fopen(platform::pathToUtf8(track).c_str(), "wb");
  if (file == nullptr) return {};
  const auto written = std::fwrite(buffer.data(), sizeof(float) * 2U,
      static_cast<std::size_t>(frames), file);
  std::fclose(file);
  if (written != static_cast<std::size_t>(frames)) {
    std::error_code cleanup;
    std::filesystem::remove(track, cleanup);
    return {};
  }
  return track;
}

} // namespace rechigros::app
