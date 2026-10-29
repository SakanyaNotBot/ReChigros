#include "rechigros/app/RecordSession.hpp"

#include <SDL3/SDL_log.h>

#include <chrono>

#include "rechigros/app/AppContext.hpp"
#include "rechigros/audio/Decoder.hpp"
#include "rechigros/audio/Mixer.hpp"
#include "rechigros/audio/SkinSoundEffects.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
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
#include <unordered_map>

namespace rechigros::app {
namespace {

// The opening transition runs for its own duration before the chart clock starts, and the exit
// animation runs after the music ends. A recording covers both, because they are part of what the
// run looks like; the settlement page is deliberately excluded.
constexpr double kIntroLeadSec = 1.083;
constexpr double kOutroTailSec = 1.7;
// The exit keeps drawing after the chart clock has passed the last note: the play field blurs,
// the HUD holds its layout and then fades out over its own window. A video that stopped at the
// chart's end cut that away, so the tail covers the HUD's whole exit.
constexpr double kHudExitSec = 0.4;

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

float recordingMusicSpeed(const AppContext& context) {
  const float speed = context.settings.musicSpeed;
  return std::isfinite(speed) && speed > 0.0F ? speed : 1.0F;
}

float recordingGain(float gain) {
  return std::isfinite(gain) ? audio::mixer::clampGain(gain) : 0.0F;
}

double recordingOffsetSec(const AppContext& context) {
  return context.game.chartOffsetSec + context.game.userOffsetSec;
}

double recordingEndPlayhead(const AppContext& context) {
  // The run ends where the interactive run ends: when the music does. Using the later of the
  // music and the last note would keep drawing an outro for a chart whose notes stop early,
  // which is why a recording of a short chart came out seconds longer than its own audio.
  if (context.game.musicLoaded && context.game.musicDurationSec > 0.0) {
    // The music is what ends the run, and the offsets only shift the chart inside it, so they
    // must not stretch the recording: a calibrated offset is not extra song.
    return context.game.musicDurationSec;
  }
  // Without decodable music there is no playhead to stop on, so the last note is the only
  // reference the run has left, shifted by the same offsets the picture applies.
  const double chartEnd = context.game.chart != nullptr ? context.game.chart->chartEndTimeSec : 0.0;
  return std::max(0.0, chartEnd + recordingOffsetSec(context));
}

double recordingContentSeconds(const AppContext& context) {
  return recordingEndPlayhead(context) / static_cast<double>(recordingMusicSpeed(context));
}

std::filesystem::path recordingSkinDirectory(const AppContext& context) {
  if (context.settings.selectedSkin.empty()) return {};
  const auto directory = context.dataRoot / "skins" / context.settings.selectedSkin;
  std::error_code error;
  return std::filesystem::is_directory(directory, error) ? directory : std::filesystem::path{};
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
  return platform::recordingWorkspace();
}

std::filesystem::path RecordSession::displayOutputDirectory(const Settings& settings) {
  const auto displayed = platform::publicVideoDisplayDirectory();
  if (settings.outputDirectory.empty() || settings.outputDirectory == displayed) return displayed;
#if defined(__ANDROID__)
  if (settings.outputDirectory == platform::userDataRoot() / "staging" ||
      settings.outputDirectory == platform::userDataRoot() / "recordings") {
    return displayed;
  }
#else
  if (settings.outputDirectory == defaultOutputDirectory()) return displayed;
#endif
  return settings.outputDirectory;
}

std::string RecordSession::defaultFileName() { return timestampName(); }

float RecordSession::progress() const noexcept {
  if (totalFrames_ <= 0) return 0.0F;
  return std::clamp(static_cast<float>(renderedFrames_) / static_cast<float>(totalFrames_), 0.0F,
      1.0F);
}

bool RecordSession::preparingAudio() const noexcept {
  return state_ == State::Running
      && encoder_.audioTrackState() == VideoEncoder::AudioTrackState::Preparing;
}

float RecordSession::audioProgress() const noexcept { return encoder_.audioTrackProgress(); }

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

  const double contentSeconds = recordingContentSeconds(context);
  // The recording covers the opening, everything up to the end of the music, and the exit that
  // follows it. Both transitions are wall time and are not scaled by the music speed.
  const double duration = std::max(
      0.1, kIntroLeadSec + contentSeconds + kOutroTailSec + kHudExitSec);
  totalFrames_ = std::max(1, static_cast<int>(std::ceil(duration * settings_.fps)));
  renderedFrames_ = 0;

  // The audio track is the chart's music, decoded once and written as raw samples for the encoder.
  // It starts after the opening transition, so the first beat lands on the first note.
  audioTrackPath_.clear();
  audioTrackPath_ = writeAudioTrack(context, file);

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
  // The renderer converts frames to the encoder's layout on the graphics device, which is cheaper
  // than letting the encoder convert every frame on the way in.
  encoderSettings.planarInput = true;
  if (!encoder_.open(encoderSettings, file, error)) {
    state_ = State::Failed;
    error_ = error;
    std::error_code cleanup;
    if (!audioTrackPath_.empty()) std::filesystem::remove(audioTrackPath_, cleanup);
    audioTrackPath_.clear();
    return false;
  }
  // Frames are handed to the encoder thread from the first one, so rendering and encoding run at
  // the same time rather than one after the other.
  encoderUsesPlanar_ = encoder_.inputIsPlanar();
  startEncoderThread();

  context.renderer->setSkinDirectory(recordingSkinDirectory(context));
  context.renderer->setBackgroundImage(context.play.bgFile);
  render::RenderFrameInput probe;
  // The recording always runs the chart in autoplay, which is what makes it reproducible: no
  // input is injected, and the judged result comes from the chart itself.
  context.game.autoplay = true;
  context.game.challenge = context.settings.challengeMode;
  context.game.mirrorX = context.settings.mirrorX;
  context.game.musicSpeed = recordingMusicSpeed(context);
  context.game.scrollSpeed = context.settings.scrollSpeed;
  context.game.session.setChart(context.game.chart.get(), context.game.challenge);
  context.game.session.setAutoplay(true);
  context.game.session.setMusicSpeed(context.game.musicSpeed);
  context.game.session.judgement().setJudgementDetailDistribution(
      context.settings.judgementDetailDistribution);
  game::GameplaySession::Viewport viewport;
  viewport.width = static_cast<float>(resolution.width);
  viewport.height = static_cast<float>(resolution.height);
  viewport.targetAspect = context.settings.aspectRatio;
  viewport.scrollSpeed = context.settings.scrollSpeed;
  viewport.dpi = context.displayDpi;
  viewport.keyScale = context.settings.keyScale;
  context.game.session.setViewport(viewport);
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
  // The container has to know both tracks before it starts, and it starts as soon as the first
  // video frame arrives. Staging the music is handed to a background thread; until it lands there
  // is nothing to render into, and reporting "more work remains" keeps the interface drawing its
  // progress overlay in the meantime.
  switch (encoder_.audioTrackState()) {
    case VideoEncoder::AudioTrackState::Preparing:
      return true;
    case VideoEncoder::AudioTrackState::Failed:
      // A run without music is still a complete run: the picture is what the rendering is for.
      break;
    case VideoEncoder::AudioTrackState::None:
      // Nothing was handed over, so there is no track to wait for. A silent run is a complete run.
      break;
    case VideoEncoder::AudioTrackState::Ready:
    default:
      break;
  }
  const double chartEnd = context.game.chart->chartEndTimeSec;
  // The exit starts where the run actually ends: when the music stops. A chart whose last note
  // comes before its song would otherwise spend the rest of the song in a finished state that
  // keeps drawing the exit over and over.
  const double outroAt =
      context.game.musicDurationSec > 0.05 ? context.game.musicDurationSec : chartEnd;

  for (int index = 0; index < kFramesPerStep && renderedFrames_ < totalFrames_; ++index) {
    const int frame = renderedFrames_;
    // The recording covers the opening transition, the chart and the exit animation. Time is
    // derived from the frame number, not accumulated, so the video cannot drift.
    const double elapsed = static_cast<double>(frame) * frameDuration;
    const bool inIntro = elapsed < kIntroLeadSec;
    // The playhead advances at the user's music speed: at 2x, one second of video covers two
    // seconds of music. The opening transition is wall time and is not scaled. Chart time is the
    // playhead with the chart and user offsets removed, which is what the session judges against.
    const double speed = static_cast<double>(recordingMusicSpeed(context));
    const double playhead = inIntro ? 0.0 : (elapsed - kIntroLeadSec) * speed;
    const double chartTime = playhead - recordingOffsetSec(context);

    render::RenderFrameInput input;
    input.chartTimeSec = chartTime;
    input.playheadSec = playhead;
    input.visualTimeSec = elapsed;
    input.userOffsetSec = context.game.userOffsetSec;
    input.dpi = context.displayDpi;
    input.scrollSpeed = context.settings.scrollSpeed;
    input.keyScale = context.settings.keyScale;
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
    // A play setting like the rest: the debug overlay is drawn when the player asked for it.
    input.showChartDebug = context.settings.showDebugInfo;
    input.transition.inIntro = inIntro;
    input.transition.introLineScale = inIntro
        ? render::scene::introLineScale(
              static_cast<float>(std::clamp(elapsed / (55.0 / 60.0), 0.0, 1.0)))
        : 1.0F;
    const bool inOutro = playhead > outroAt;
    input.transition.inOutro = inOutro;
    input.transition.outroElapsedSec = inOutro
        ? static_cast<float>(playhead - outroAt)
        : 0.0F;

    // The session advances with the chart clock so the judgement state matches the picture.
    // The session advances only while the chart clock is inside the chart. Past its end the picture
    // keeps drawing the exit, but nothing is left to judge: advancing there would only spend work on
    // a chart that cannot change any more.
    if (!inIntro && chartTime >= 0.0 && chartTime <= chartEnd) {
      // The session integrates in chart time, so the frame delta is scaled the same way.
      context.game.session.update(chartTime, static_cast<float>(frameDuration * speed));
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
    // The offset indicator follows the play setting, but stays suppressed for the same reason it
    // is suppressed during an autoplay run: there is no hit offset to report when nothing is
    // played by hand.
    input.hud.hitOffsetIndicatorMode =
        std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2);
    input.hud.hitOffsetSuppressed = true;
    // The frame counter is a play setting; forcing it off made the video disagree with the screen
    // the player configured. A recording renders at a fixed rate, so that rate is what the counter
    // reports -- the live counter measures draw speed, which an offscreen run does not have.
    input.hud.showFps = context.settings.showFps;
    input.hud.fps = static_cast<float>(settings_.fps);
    input.hud.visualTimeSec = elapsed;
    // The reveal camera shrinks the HUD along with the play field; the renderer resolves the
    // stage itself, so the values it needs for the HUD are computed here the same way.
    if (context.settings.chartReveal && !inIntro && !inOutro && chartTime >= 0.0) {
      const double revealTotal =
          context.game.musicDurationSec > 0.05 ? context.game.musicDurationSec : chartEnd;
      constexpr double kRevealTransitionSec = 1.0;
      constexpr double kRevealTargetScale = 0.30;
      const auto smooth = [](double value) {
        value = std::clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
      };
      input.hud.revealCameraActive = true;
      if (revealTotal > 0.0 && playhead >= revealTotal - kRevealTransitionSec) {
        const double start = std::max(0.0, revealTotal - kRevealTransitionSec);
        input.hud.revealScale = static_cast<float>(
            kRevealTargetScale + (1.0 - kRevealTargetScale) * smooth((playhead - start) /
                                    kRevealTransitionSec));
      } else {
        input.hud.revealScale = static_cast<float>(
            1.0 + (kRevealTargetScale - 1.0) * smooth(playhead / kRevealTransitionSec));
      }
      input.hud.revealScale = std::clamp(input.hud.revealScale, 0.30F, 1.0F);
    }
    input.hud.introProgress = inIntro
        ? render::scene::introUiEase(
              static_cast<float>(std::clamp(elapsed / (40.0 / 60.0), 0.0, 1.0)))
        : 1.0F;
    // The exit animation starts with the outro itself, not after a delay: the HUD travels off the
    // screen over its own window, which is what the recorded picture has to end on. The interactive
    // path feeds the same progress from the same elapsed time.
    // The exit animation runs over its own window from the moment the run leaves play: the HUD
    // travels off the screen by growing about the play field centre, and only the combo label loses
    // opacity. The interactive path feeds this same progress from the same elapsed time, so a
    // recording shows what the player saw.
    const double outroElapsed = inOutro ? playhead - outroAt : 0.0;
    input.hud.outroProgress = inOutro
        ? static_cast<float>(std::clamp(outroElapsed / (40.0 / 60.0), 0.0, 1.0))
        : 0.0F;
    input.hud.alpha = 1.0F;
    input.songName = context.play.songName;
    input.levelName = context.play.difficulty;
    // Hit feedback is part of what a run looks like; the interface can turn the particles off
    // while keeping the sound.
    input.showHitParticles = settings_.noteHitParticles;
    // The recording runs autoplay; the renderer selects its burst source from this flag.
    input.practice.enabled = false;
    input.practice.speed = static_cast<float>(context.game.musicSpeed);
    input.practice.autoPlay = true;
    // Hold bursts are produced by the session while it advances and drawn by the hit-effect pass.
    // They are consumed once per frame, so a recording that skipped them would show every hold
    // without its effect -- the one thing a viewer notices first in a hold-heavy chart.
    input.holdBodyBursts = context.game.session.takeHoldBodyBursts();
    input.holdHeadBursts = context.game.session.takeHoldHeadBursts();
    (void)resolution;

    const auto captureStart = std::chrono::steady_clock::now();
    // The planar layout is produced on the graphics device, which spares both the readback and the
    // encoder a conversion per frame. The coloured path stays as the fallback for a context that
    // cannot run the conversion.
    bool frameReady = true;
    const bool converted = encoderUsesPlanar_
        && context.renderer->captureFrameYuvAsync(input, context.game.chart.get(), &judgement,
            framePixels_, frameReady);
    if (!converted
        && !context.renderer->captureFrame(input, context.game.chart.get(), &judgement,
            framePixels_)) {
      state_ = State::Failed;
      error_ = "a frame could not be rendered";
      failAndRelease(context);
      return false;
    }
    const auto captureEnd = std::chrono::steady_clock::now();
    captureMs_ += std::chrono::duration<double, std::milli>(captureEnd - captureStart).count();
    // The frame goes to the encoder thread and the next one is drawn straight away, so the two
    // stages overlap instead of alternating. A deferred conversion reports ready only from its
    // second call, so the first frame of a run arrives one draw later and the final one is
    // collected below.
    if (!converted || frameReady) {
      if (!enqueueTimedFrame(framePixels_)) {
        // A failed handoff leaves the encoder thread running. Returning with it still joinable
        // would destroy a running thread together with the session, which aborts the process, so
        // the thread is stopped and the unfinished output removed here.
        failAndRelease(context);
        return false;
      }
    }
    ++renderedFrames_;
  }

  if (renderedFrames_ >= totalFrames_) {
    // The last frame is still in its transfer; collect it before the encoder is told the stream
    // is over, otherwise the tail of the video is short by the frames the pipeline holds.
    if (encoderUsesPlanar_) {
      while (context.renderer->takePendingCaptureFrame(framePixels_)) {
        if (!enqueueTimedFrame(framePixels_)) return false;
      }
      if (context.renderer->hasPendingCaptureFrame()) {
        state_ = State::Failed;
        error_ = "a converted frame could not be collected";
        failAndRelease(context);
        return false;
      }
    }
    finish(context);
    return false;
  }
  return true;
}

bool RecordSession::enqueueTimedFrame(const std::vector<std::uint8_t>& pixels) {
  const auto submitStart = std::chrono::steady_clock::now();
  const bool queued = submitFrame(pixels);
  const auto submitEnd = std::chrono::steady_clock::now();
  encodeWaitMs_ += std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
  ++timedFrames_;
  if (!queued) {
    {
      std::lock_guard<std::mutex> guard(queueMutex_);
      error_ = encoderError_.empty() ? "the encoder stopped accepting frames" : encoderError_;
    }
    state_ = State::Failed;
  }
  return queued;
}

void RecordSession::reportTimings() const {
  if (timedFrames_ == 0) return;
  const double frames = static_cast<double>(timedFrames_);
  // Logged through the platform channel as well as to the console: a packaged build has no console,
  // and these numbers are the only way to attribute a slow recording on a device.
  const char* message = "[perf] frames=%d capture=%.2fms/frame encodeWait=%.2fms/frame "
                        "encoderWrite=%.2fms/frame";
  std::printf(message, timedFrames_, captureMs_ / frames, encodeWaitMs_ / frames,
      encoderWriteMs_ / frames);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, message, timedFrames_, captureMs_ / frames,
      encodeWaitMs_ / frames, encoderWriteMs_ / frames);
}

void RecordSession::cancel(AppContext& context) {
  // The reader is stopped first: it owns the queue the encoder thread is draining, and stopping
  // the encoder underneath it would leave the thread writing to a closed stream.
  stopEncoderThread();
  const bool wasRunning = state_ == State::Running;
  if (wasRunning && encoder_.active()) encoder_.abort();
  releaseResources(context);
  // A cancelled encode leaves an unfinished container: the encoder was killed before it wrote
  // the index, so the file cannot be opened by any player. Keeping it only leaves the user with
  // a broken video next to their real ones, so the partial output goes away with the run.
  if (wasRunning) discardIncompleteOutput();
  state_ = State::Cancelled;
}

void RecordSession::finish(AppContext& context) {
  if (state_ != State::Running) return;
  // Every queued frame has to be written before the container is closed, or the tail of the
  // recording would be missing.
  std::string error;
  if (!drainEncoder(error)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] drain failed: %s", error.c_str());
    state_ = State::Failed;
    error_ = error;
    failAndRelease(context);
    return;
  }
  if (!encoder_.finish(error)) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] finish failed: %s", error.c_str());
    state_ = State::Failed;
    error_ = error;
    failAndRelease(context);
    return;
  }
  // The shared video folder is where the player looks for the file, but a recent release does not
  // allow writing it by path. A recording that landed in the staging folder is handed to the
  // platform media service here, which also deletes the private copy; a published path becomes the
  // reported one so the interface says where the video actually is.
  const auto published = platform::publishMedia(outputPath_, "Movies");
  if (!published.empty() && published != outputPath_) {
    outputPath_ = published;
  }
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[record] finished %s frames=%d",
      platform::pathToUtf8(outputPath_).c_str(), renderedFrames_);
  releaseResources(context);
  state_ = State::Finished;
  reportTimings();
}

void RecordSession::failAndRelease(AppContext& context) noexcept {
  // The writer is stopped before the thread is collected. The encoder may already be a dead child
  // whose pipe is gone, and a writer still parked inside that pipe holds state the encoder tears
  // down; joining first would collect the thread while it is still in there, which is a race the
  // process loses intermittently.
  {
    std::lock_guard<std::mutex> guard(queueMutex_);
    encoderStop_ = true;
  }
  queueFilled_.notify_all();
  queueSpace_.notify_all();
  // Joining first is what makes this safe: the writer notices the stop before its next write, so
  // the encoder's streams are already untouched when they are closed.
  if (encoderThread_.joinable()) encoderThread_.join();
  if (encoder_.active()) encoder_.abort();
  {
    std::lock_guard<std::mutex> guard(queueMutex_);
    frameQueue_.clear();
    encoderRunning_ = false;
  }
  releaseResources(context);
  discardIncompleteOutput();
}

void RecordSession::discardIncompleteOutput() noexcept {
  if (outputPath_.empty()) return;
  // A packaged build has no console, and a deleted output is the one failure a player sees as
  // "the recording vanished", so the reason is reported through the platform log as well.
  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[record] discarding %s (%s)",
      platform::pathToUtf8(outputPath_).c_str(), error_.c_str());
  std::error_code cleanup;
  std::filesystem::remove(outputPath_, cleanup);
}


void RecordSession::startEncoderThread() {
  stopEncoderThread();
  {
    std::lock_guard<std::mutex> guard(queueMutex_);
    encoderStop_ = false;
    encoderDraining_ = false;
    encoderRunning_ = true;
    encoderError_.clear();
    frameQueue_.clear();
  }
  encoderThread_ = std::thread([this] { encoderLoop(); });
}

void RecordSession::encoderLoop() {
  std::string error;
  for (;;) {
    std::vector<std::uint8_t> frame;
    {
      std::unique_lock<std::mutex> lock(queueMutex_);
      queueFilled_.wait(lock, [this] {
        return encoderStop_ || encoderDraining_ || !frameQueue_.empty();
      });
      if (frameQueue_.empty()) {
        // A graceful finish exits once every queued frame has been written; cancellation and
        // failure still stop immediately.
        if (encoderStop_ || encoderDraining_) break;
        continue;
      }
      frame = std::move(frameQueue_.front());
      frameQueue_.pop_front();
      // Cancellation may have signalled a stop while this thread was waking up; writing after
      // that point would touch a stream the main thread is already tearing down.
      if (encoderStop_) break;
    }
    queueSpace_.notify_one();
    const auto writeStart = std::chrono::steady_clock::now();
    const bool written = encoder_.writeFrame(frame, error);
    const auto writeEnd = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> guard(queueMutex_);
      encoderWriteMs_ +=
          std::chrono::duration<double, std::milli>(writeEnd - writeStart).count();
    }
    if (!written) {
      std::lock_guard<std::mutex> guard(queueMutex_);
      if (encoderError_.empty()) encoderError_ = error;
      encoderStop_ = true;
      break;
    }
  }
  {
    std::lock_guard<std::mutex> guard(queueMutex_);
    encoderRunning_ = false;
  }
  queueFilled_.notify_all();
}

bool RecordSession::submitFrame(const std::vector<std::uint8_t>& pixels) {
  {
    std::unique_lock<std::mutex> lock(queueMutex_);
    // The wait is bounded. The encoder thread can park inside the platform encoder, which on some
    // devices never returns: the platform's writer stalls while holding the bridge lock, the frame
    // hand-off then blocks on that same lock, and the queue never drains. Without a bound the
    // recorder waits for ever with the interface frozen behind it; with one it reports the failure
    // and the player keeps their session.
    if (!queueSpace_.wait_for(lock, std::chrono::seconds(20), [this] {
          return encoderStop_ || frameQueue_.size() < kMaxQueuedFrames;
        })) {
      error_ = "the encoder stopped accepting frames";
      return false;
    }
    if (encoderStop_) return false;
    // The queue owns its pixels: the renderer reuses its own buffer for the next frame, and the
    // encoder thread must not read it while that is happening.
    frameQueue_.push_back(pixels);
  }
  queueFilled_.notify_one();
  return true;
}

bool RecordSession::drainEncoder(std::string& error) {
  {
    std::unique_lock<std::mutex> lock(queueMutex_);
    encoderDraining_ = true;
  }
  queueFilled_.notify_all();
  queueSpace_.notify_all();
  if (encoderThread_.joinable()) encoderThread_.join();
  std::lock_guard<std::mutex> guard(queueMutex_);
  frameQueue_.clear();
  if (!encoderError_.empty()) {
    error = encoderError_;
    return false;
  }
  return true;
}

void RecordSession::stopEncoderThread() {
  {
    std::lock_guard<std::mutex> guard(queueMutex_);
    encoderStop_ = true;
  }
  queueFilled_.notify_all();
  queueSpace_.notify_all();
  if (encoderThread_.joinable()) encoderThread_.join();
  std::lock_guard<std::mutex> guard(queueMutex_);
  frameQueue_.clear();
  encoderRunning_ = false;
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

void RecordSession::mixHitSounds(AppContext& context, std::vector<float>& mix,
    std::int64_t frames, double speed) const {
  if (context.game.chart == nullptr) return;
  // Every hit in an autoplay run lands at the note's own time, so the schedule comes from the
  // chart rather than from the frame that noticed it: the picture may catch up later, but a hit
  // is heard where the note is.
  const auto totalFrames = static_cast<std::int64_t>(mix.size() / 2U);
  if (totalFrames <= 0) return;
  const float gain = recordingGain(context.settings.soundEffectVolume);
  if (gain <= 0.0F) return;
  // The same bank resolution the interactive mixer uses, so a skin effect is heard in a
  // recording exactly as it is heard while playing.
  const auto sounds = audio::resolveSoundEffects(
      platform::assetRoot(), recordingSkinDirectory(context), kAudioSampleRate);
  std::unordered_map<std::string, audio::AudioBuffer> customBanks;
  for (const auto* note : context.game.chart->allNotesSorted) {
    if (note == nullptr) continue;
    if (note->isFake) continue;
    // Hold notes and any type without its own bank reuse the tap sound.
    const int type = note->type == game::NOTE_DRAG || note->type == game::NOTE_FLICK
        ? note->type
        : game::NOTE_TAP;
    const audio::AudioBuffer* sound = &sounds.bankFor(type);
    if (!note->hitSound.empty()) {
      auto [entry, inserted] = customBanks.try_emplace(note->hitSound);
      if (inserted) entry->second = audio::Decoder::decodeFile(note->hitSound, kAudioSampleRate);
      if (!entry->second.empty()) sound = &entry->second;
    }
    if (sound->empty()) continue;
    // The picture shows a note when the playhead reaches its chart time plus the offsets, and the
    // track advances at `speed`, so that playhead maps to this sample position. Omitting the
    // offsets would sound every hit before or after the note it belongs to whenever the player
    // has calibrated a non-zero offset.
    const double playheadAtNote = note->sect + recordingOffsetSec(context);
    const auto startFrame = static_cast<std::int64_t>(
        std::llround(std::max(0.0, playheadAtNote) / std::max(0.001, speed) * kAudioSampleRate));
    if (startFrame < 0 || startFrame >= totalFrames) continue;
    const auto available = totalFrames - startFrame;
    const auto count = std::min<std::int64_t>(sound->frameCount(), available);
    const float* samples = sound->data();
    for (std::int64_t offset = 0; offset < count; ++offset) {
      const auto frame = static_cast<std::size_t>(startFrame + offset) * 2U;
      mix[frame] += samples[static_cast<std::size_t>(offset) * 2U] * gain;
      mix[frame + 1U] += samples[static_cast<std::size_t>(offset) * 2U + 1U] * gain;
    }
  }
  (void)frames;
}

// Builds the audio track: the chart's music with every hit the run produced mixed on top, written as
// raw interleaved stereo float samples beside the video. The encoder takes it as its second input.
// Returns an empty path when there is nothing audible, in which case the video is silent.
std::filesystem::path RecordSession::writeAudioTrack(AppContext& context,
    const std::filesystem::path& videoPath) {
  audio::AudioBuffer buffer;
  if (!context.play.musicFile.empty()) {
    buffer = audio::Decoder::decodeFile(context.play.musicFile, kAudioSampleRate);
  }
  // The music and the hit sounds are independent: turning the hit sound off must not silence the
  // song, and a chart without decodable music still carries its hits.
  if (buffer.empty() && !settings_.noteHitsound) return {};
  // The track has to cover exactly the picture, so it uses the same content length the video
  // does. Sizing it from the chart alone left the tail of a recording silent whenever the music
  // outlasted the last note, which is the normal case for a song with an outro.
  const double speed = std::max(0.001, static_cast<double>(recordingMusicSpeed(context)));
  const double contentSeconds = std::max(0.1, recordingContentSeconds(context));
  const double trackSeconds =
      std::max(0.1, contentSeconds + kOutroTailSec + kHudExitSec);
  const std::int64_t frames = static_cast<std::int64_t>(std::ceil(trackSeconds * kAudioSampleRate));
  if (frames <= 0) return {};
  std::vector<float> mix(static_cast<std::size_t>(frames) * 2U, 0.0F);
  // Music is read through the playback speed: at 2x every output frame advances two input frames,
  // with linear interpolation, which is what the interactive player does. Its gain is the user's
  // music volume and is independent of the hit-sound switch.
  const float musicGain = recordingGain(context.settings.musicVolume);
  const std::int64_t musicFrames = buffer.frameCount();
  if (musicFrames > 0 && musicGain > 0.0F) {
    const float* music = buffer.data();
    for (std::int64_t index = 0; index < frames; ++index) {
      const double position = static_cast<double>(index) * speed;
      if (position >= static_cast<double>(musicFrames)) break;
      const auto first = static_cast<std::int64_t>(position);
      const auto second = std::min<std::int64_t>(first + 1, musicFrames - 1);
      const auto fraction = static_cast<float>(position - static_cast<double>(first));
      for (std::size_t channel = 0; channel < 2U; ++channel) {
        const float a = music[static_cast<std::size_t>(first) * 2U + channel];
        const float b = music[static_cast<std::size_t>(second) * 2U + channel];
        mix[static_cast<std::size_t>(index) * 2U + channel] =
            (a + (b - a) * fraction) * musicGain;
      }
    }
  }
  // Hits sit on the chart clock, which advances at the same speed, so their position in this
  // track is their chart time divided by the speed.
  if (settings_.noteHitsound) mixHitSounds(context, mix, frames, speed);
  // The interactive mixer soft-clips every sample as it leaves the callback; a recording that
  // summed music and hits without doing the same would clip on loud passages.
  for (float& sample : mix) sample = audio::mixer::clampSample(sample);
  std::filesystem::path track = videoPath;
  track += ".pcm";
  // A name holding characters outside the ANSI code page cannot be opened through a narrow path, so
  // the wide form is used where the C library offers one. UTF-8 bytes would not work here: fopen
  // decodes narrow names with the host code page, not with UTF-8.
#ifdef _WIN32
  std::FILE* file = _wfopen(track.wstring().c_str(), L"wb");
#else
  std::FILE* file = std::fopen(track.string().c_str(), "wb");
#endif
  if (file == nullptr) return {};
  const auto written = std::fwrite(mix.data(), sizeof(float) * 2U,
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
