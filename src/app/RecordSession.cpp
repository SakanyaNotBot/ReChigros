#include "rechigros/app/RecordSession.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/audio/Decoder.hpp"
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
  // The picture has to run past the last note: the HUD keeps its layout while the play field
  // blurs and then fades out over its own window, and the exit transition follows. Stopping at
  // the chart's end cut the video off exactly when the song ended, so the recording covers the
  // HUD's exit as well.
  // The chart clock only starts after the opening lead, so the picture is that lead longer than the
  // chart. Leaving it out of the total ended the video early by exactly the intro, which is why the
  // HUD exit was still on screen in the last frame.
  const double duration =
      std::max(0.1, kIntroLeadSec + chartEnd + kOutroTailSec + kHudExitSec);
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
    // The session advances only while the chart clock is inside the chart. Past its end the picture
    // keeps drawing the exit, but nothing is left to judge: advancing there would only spend work on
    // a chart that cannot change any more.
    if (!inIntro && chartTime <= chartEnd) {
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
    // The HUD fades out over the last part of the exit, the same window the interactive path
    // uses, so the recorded picture ends on the interface disappearing rather than on a frozen
    // HUD over a blurred field.
    input.hud.alpha = 1.0F;
    if (inOutro) {
      const double outroElapsed = chartTime - chartEnd;
      if (outroElapsed > kOutroTailSec) {
        const double fade = (outroElapsed - kOutroTailSec) / kHudExitSec;
        input.hud.alpha = static_cast<float>(std::clamp(1.0 - fade, 0.0, 1.0));
      }
    }
    input.songName = context.play.songName;
    input.levelName = context.play.difficulty;
    // Hit feedback is part of what a run looks like; the interface can turn the particles off
    // while keeping the sound.
    input.showHitParticles = settings_.noteHitParticles;
    // Hold bursts are produced by the session while it advances and drawn by the hit-effect pass.
    // They are consumed once per frame, so a recording that skipped them would show every hold
    // without its effect -- the one thing a viewer notices first in a hold-heavy chart.
    input.holdBodyBursts = context.game.session.takeHoldBodyBursts();
    input.holdHeadBursts = context.game.session.takeHoldHeadBursts();
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

void RecordSession::mixHitSounds(AppContext& context, std::vector<float>& mix,
    std::int64_t frames) const {
  if (context.game.chart == nullptr) return;
  // An autoplay run hits every note at the note's own time, so the hit times come straight from the
  // chart rather than from the judged frame: the picture may notice a note on a later frame, but the
  // sound of a hit belongs where the note is. This is what makes a recorded run sound like the run
  // it shows.
  constexpr float kHitSeconds = 0.045F;
  constexpr float kHitAmplitude = 0.35F;
  const auto hitFrames = static_cast<std::int64_t>(std::ceil(kHitSeconds * kAudioSampleRate));
  if (hitFrames <= 0) return;
  const auto totalFrames = static_cast<std::int64_t>(mix.size() / 2U);
  using game::NOTE_DRAG;
  using game::NOTE_FLICK;
  using game::NOTE_HOLD;
  using game::NOTE_TAP;
  for (const auto* note : context.game.chart->allNotesSorted) {
    if (note == nullptr || note->type == NOTE_HOLD) continue;
    const auto startFrame =
        static_cast<std::int64_t>(std::llround(note->sect * kAudioSampleRate));
    if (startFrame < 0 || startFrame >= totalFrames) continue;
    // A short decaying tone: without the shipped samples the recording keeps the rhythm of the
    // hits, which is the part that carries the feel of the run.
    const float frequency = note->type == NOTE_FLICK ? 1100.0F
        : note->type == NOTE_DRAG                 ? 660.0F
                                                  : 880.0F;
    for (std::int64_t offset = 0; offset < hitFrames; ++offset) {
      const auto frame = startFrame + offset;
      if (frame >= totalFrames) break;
      const float progress = static_cast<float>(offset) / static_cast<float>(hitFrames);
      const float envelope = (1.0F - progress) * (1.0F - progress);
      const float phase = 6.2831853F * frequency * static_cast<float>(offset)
          / static_cast<float>(kAudioSampleRate);
      const float sample = std::sin(phase) * envelope * kHitAmplitude;
      mix[static_cast<std::size_t>(frame) * 2U] += sample;
      mix[static_cast<std::size_t>(frame) * 2U + 1U] += sample;
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
  // A chart with no decodable music still gets a track carrying its hit sounds; only a run with no
  // music and no hits stays silent.
  if (buffer.empty() && !settings_.noteHitsound) return {};
  // The track covers the chart plus the exit: the music ends with the song, while the hit sounds
  // of the final notes and the fade still have to be audible, so the track is sized from the
  // picture, not from the music file.
  const double chartEnd = context.game.chart != nullptr
      ? context.game.chart->chartEndTimeSec
      : buffer.durationSeconds();
  // The encoder delays this track by the opening lead, so it starts where the chart clock does and
  // the lead must not be added here; the track only has to cover the chart and its exit.
  const double trackSeconds = std::max(0.1, chartEnd + kOutroTailSec + kHudExitSec);
  const std::int64_t frames = static_cast<std::int64_t>(std::ceil(trackSeconds * kAudioSampleRate));
  if (frames <= 0) return {};
  // The music is decoded at the sample rate the encoder is told to expect, so no conversion is
  // needed; it is copied in and the hit sounds are summed on top of it.
  std::vector<float> mix(static_cast<std::size_t>(frames) * 2U, 0.0F);
  const std::int64_t musicFrames = std::min<std::int64_t>(buffer.frameCount(), frames);
  if (musicFrames > 0) {
    std::copy_n(buffer.data(), static_cast<std::size_t>(musicFrames) * 2U, mix.begin());
  }
  if (settings_.noteHitsound) mixHitSounds(context, mix, frames);
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
