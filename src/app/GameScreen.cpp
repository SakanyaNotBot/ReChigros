#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"

#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/render/GameRenderer.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <SDL_log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

using game::GameplaySession;

constexpr double kIntroAudioStartSec = 0.35;
constexpr double kIntroEndSec = 0.45;
constexpr double kOutroDurationSec = 1.7;

std::int64_t nowMilliseconds() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

struct StageRect {
  float left{};
  float top{};
  float width{};
  float height{};
};

// Mirrors the touch coordinate space used by GameplaySession.
StageRect touchStage(const AppContext& context) {
  const float aspect = context.settings.aspectRatio > 0.0F ? context.settings.aspectRatio
                                                          : 16.0F / 9.0F;
  StageRect rect;
  rect.width = static_cast<float>(context.windowWidth);
  rect.height = rect.width / std::max(0.01F, aspect);
  if (rect.height > static_cast<float>(context.windowHeight)) {
    rect.height = static_cast<float>(context.windowHeight);
    rect.width = rect.height * aspect;
  }
  rect.left = (static_cast<float>(context.windowWidth) - rect.width) * 0.5F;
  rect.top = (static_cast<float>(context.windowHeight) - rect.height) * 0.5F;
  return rect;
}

render::scene::StageLayout renderStage(const AppContext& context) {
  return render::scene::computeStageLayout(static_cast<float>(context.drawableWidth),
      static_cast<float>(context.drawableHeight), 16.0F / 9.0F, false);
}

bool isPauseButtonHit(const AppContext& context, float windowX, float windowY) {
  const auto stage = renderStage(context);
  const float unit = stage.stage.height > 1.0e-6F ? stage.stage.height / 1000.0F : 1.0F;
  const float centerDrawableX = stage.stage.x + 50.5887985F * unit;
  const float centerDrawableY = stage.centerY() - 444.2999878F * unit;
  const float scaleX = context.drawableWidth > 0
      ? static_cast<float>(context.windowWidth) / static_cast<float>(context.drawableWidth)
      : 1.0F;
  const float scaleY = context.drawableHeight > 0
      ? static_cast<float>(context.windowHeight) / static_cast<float>(context.drawableHeight)
      : 1.0F;
  const float centerX = centerDrawableX * scaleX;
  const float centerY = centerDrawableY * scaleY;
  const float radiusX = std::max(56.0F * unit * scaleX, 40.0F);
  const float radiusY = std::max(56.0F * unit * scaleY, 40.0F);
  return std::abs(windowX - centerX) <= radiusX && std::abs(windowY - centerY) <= radiusY;
}

void syncGameplayViewport(AppContext& context) {
  GameplaySession::Viewport viewport;
  viewport.width = static_cast<float>(std::max(1, context.windowWidth));
  viewport.height = static_cast<float>(std::max(1, context.windowHeight));
  viewport.targetAspect = context.settings.aspectRatio > 0.0F ? context.settings.aspectRatio
                                                             : 16.0F / 9.0F;
  viewport.scrollSpeed = context.settings.scrollSpeed;
  viewport.dpi = context.displayDpi;
  viewport.keyScale = context.settings.keyScale;
  context.game.session.setViewport(viewport);
}

void startAudio(AppContext& context, bool beginPlayback) {
  if (context.audio == nullptr) return;
  context.audio->setMusicVolume(context.settings.musicVolume);
  context.audio->setSoundEffectVolume(context.settings.soundEffectVolume);
  context.audio->setPlaybackSpeed(context.play.musicSpeed);
  context.audio->setLowPassEnabled(false);
  context.audio->setMusicLooping(false);
  if (beginPlayback) context.audio->play();
}

void applyPracticeSeek(AppContext& context, double targetChartTime) {
  auto& game = context.game;
  if (!game.chart) return;
  const double target = std::clamp(targetChartTime, 0.0,
      std::max(0.0, game.chart->chartEndTimeSec - 0.1));
  // Rebuild the judgement state and skip every note before the seek point.
  game.session.setChart(game.chart.get(), game.challenge);
  game.session.setAutoplay(game.autoplay);
  syncGameplayViewport(context);
  for (auto* note : game.chart->allNotesSorted) {
    if (note->sect < target) {
      note->judgeResult = static_cast<int>(game::JudgementEngine::Judgement::Miss);
      note->isJudged = true;
      note->clicked = true;
    }
  }
  game.playheadSec = target + game.chartOffsetSec + game.userOffsetSec;
  if (context.audio != nullptr && game.musicLoaded) context.audio->seek(game.playheadSec);
  game.chartTimeSec = target;
  game.lastCounts[0] = game.lastCounts[1] = game.lastCounts[2] = game.lastCounts[3] = 0;
  game.popupJudgement = -1;
}

void updateJudgementPopup(GameRuntime& game) {
  const auto& counts = game.session.judgement().counts();
  for (int index = 0; index < 4; ++index) {
    if (counts[index] > game.lastCounts[index]) {
      game.popupJudgement = index;
      game.popupAgeSec = 0.0;
    }
    game.lastCounts[index] = counts[index];
  }
  if (game.popupJudgement >= 0) game.popupAgeSec += game.frameDeltaSec;
}

void dispatchRecordedTouches(AppContext& context) {
  auto& game = context.game;
  if (!game.replayPlayback) return;
  const auto stage = touchStage(context);
  while (game.replayCursor < game.playback.entries.size()) {
    const auto& entry = game.playback.entries[game.replayCursor];
    if (entry.timeSec > game.chartTimeSec + 0.0005) break;
    const float x = stage.left + entry.x * stage.width;
    const float y = stage.top + entry.y * stage.height;
    switch (entry.kind) {
      case ReplayTouchEvent::Kind::Down: game.session.touchDown(entry.pointerId, x, y); break;
      case ReplayTouchEvent::Kind::Move: game.session.touchMove(entry.pointerId, x, y); break;
      case ReplayTouchEvent::Kind::Up: game.session.touchUp(entry.pointerId); break;
    }
    ++game.replayCursor;
  }
}

void recordTouch(AppContext& context, ReplayTouchEvent::Kind kind, std::int64_t pointerId, float x,
    float y) {
  auto& game = context.game;
  if (!game.recording) return;
  const auto stage = touchStage(context);
  if (stage.width <= 1.0F || stage.height <= 1.0F) return;
  ReplayTouchEvent event;
  event.kind = kind;
  event.pointerId = pointerId;
  event.x = std::clamp((x - stage.left) / stage.width, 0.0F, 1.0F);
  event.y = std::clamp((y - stage.top) / stage.height, 0.0F, 1.0F);
  event.timeSec = game.chartTimeSec;
  game.recorded.entries.push_back(event);
}

void finishGame(AppContext& context) {
  auto& game = context.game;
  if (game.finished) return;
  game.finished = true;
  const auto& judgement = game.session.judgement();
  context.lastResult = judgement.result();
  context.lastRunWasReplay = context.play.replayMode;
  context.lastRunHasReplay = game.recording;
  if (game.recording) {
    auto& meta = game.recorded.meta;
    meta.chartId = context.play.chartId;
    meta.songName = context.play.songName;
    meta.difficulty = context.play.difficulty;
    meta.score = context.lastResult.score;
    meta.accuracy = context.lastResult.accuracy;
    meta.maxCombo = context.lastResult.maxCombo;
    meta.totalNotes = context.lastResult.totalNotes;
    meta.perfect = context.lastResult.perfect;
    meta.good = context.lastResult.good;
    meta.bad = context.lastResult.bad;
    meta.miss = context.lastResult.miss;
    meta.timestampMs = nowMilliseconds();
    meta.autoplay = game.autoplay;
    meta.challenge = game.challenge;
    meta.mirrorX = game.mirrorX;
    meta.practice = game.practice;
    meta.musicSpeed = game.musicSpeed;
    meta.scrollSpeed = game.scrollSpeed;
    meta.chartOffsetMs = context.play.chartOffsetMs;
    meta.audioOffsetMs = context.play.audioOffsetMs;
    context.lastReplayMeta = meta;
  }
  if (!context.play.replayMode) {
    if (!context.play.chartId.empty()) {
      context.library.recordPlayTime(context.play.chartId, nowMilliseconds());
      if (!game.autoplay && !game.challenge && !game.practice) {
        context.library.recordBestResult(context.play.chartId, context.lastResult);
      }
    }
  }
  context.savedReplayId.clear();
  game.session.cancelTouches();
  // Release the chart and stop the mixer; the result screen only needs the
  // copied result and the recorded replay, both stored on the context.
  gameLeave(context);
  context.screen = Screen::Result;
}

void drawDebugOverlay(AppContext& context, ImDrawList* drawList) {
  const auto& game = context.game;
  const auto& judgement = game.session.judgement();
  const float scale = layoutScale();
  char buffer[256];
  int line = 0;
  const auto draw = [&](const char* format, auto... arguments) {
    std::snprintf(buffer, sizeof(buffer), format, arguments...);
    ui::drawText(drawList, ImVec2(14.0F * scale, (14.0F + static_cast<float>(line) * 22.0F) * scale),
        15.0F * scale, ui::rgba(0xFFFFFFFFU), buffer);
    ++line;
  };
  draw("FPS %.1f", static_cast<double>(context.fps));
  draw("Chart %.3f s   Playhead %.3f s", game.chartTimeSec, game.playheadSec);
  draw("Combo %d   Max %d   Score %d", judgement.combo(), judgement.maxCombo(),
      judgement.score());
  draw("P/G/B/M %d/%d/%d/%d", judgement.counts()[0], judgement.counts()[1],
      judgement.counts()[2], judgement.counts()[3]);
  draw("Touches recorded %d", static_cast<int>(game.recorded.entries.size()));
  if (context.audio != nullptr) {
    draw("Audio latency %.1f ms", context.audio->outputLatencySeconds() * 1000.0);
  }
}

void drawPauseMenu(AppContext& context) {
  auto& game = context.game;
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##game_overlay");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      IM_COL32(0, 0, 0, 150));

  const ImVec2 panelSize(360.0F * scale, 0.0F);
  const float panelLeft = (size.width - panelSize.x) * 0.5F;
  float cursorY = size.height * 0.24F;
  ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, cursorY), 34.0F * scale,
      ui::rgba(ui::kTextPrimary), "暂停");
  cursorY += 56.0F * scale;

  const auto button = [&](const char* id, const char* label, float height) {
    const bool pressed = ui::skewButton(id, label, ImVec2(panelLeft, cursorY),
        ImVec2(panelSize.x, height * scale), 22.0F * scale, -1.0F, ui::rgba(ui::kPanelFill),
        IM_COL32(255, 255, 255, 140), true, false);
    cursorY += (height + 12.0F) * scale;
    return pressed;
  };

  if (button("##pause_resume", "继续", 48.0F)) {
    game.pause = GameRuntime::Pause::Countdown;
    game.countdown = 3;
    game.countdownElapsedSec = 0.0;
    game.pauseElapsedSec = 0.0;
  }
  if (button("##pause_restart", "重新开始", 48.0F)) {
    gameRestart(context);
    ui::endFullscreenWindow();
    return;
  }
  if (button("##pause_quit", "退出", 48.0F)) {
    gameQuitToMenu(context);
    ui::endFullscreenWindow();
    return;
  }

  // Practice controls.
  if (game.practice) {
    cursorY += 6.0F * scale;
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    ImGui::SetWindowFontScale(std::clamp(scale, 0.8F, 1.4F));
    ImGui::TextUnformatted("练习模式");
    cursorY += 28.0F * scale;
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    ImGui::SetNextItemWidth((panelSize.x - 16.0F * scale));
    float speed = game.musicSpeed;
    if (ImGui::SliderFloat("##practice_speed", &speed, 0.1F, 2.0F, "速度 %.2f")) {
      game.musicSpeed = speed;
      context.play.musicSpeed = speed;
      if (context.audio != nullptr) context.audio->setPlaybackSpeed(speed);
    }
    cursorY += 40.0F * scale;
    const double duration = game.musicDurationSec > 0.0 ? game.musicDurationSec
                                                        : game.chart->chartEndTimeSec;
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    ImGui::SetNextItemWidth((panelSize.x - 16.0F * scale));
    float position = static_cast<float>(std::max(0.0, game.chartTimeSec));
    char positionLabel[48];
    std::snprintf(positionLabel, sizeof(positionLabel), "跳转 %.2f / %.2f",
        static_cast<double>(position), duration);
    if (ImGui::SliderFloat("##practice_seek", &position, 0.0F,
            static_cast<float>(std::max(1.0, duration)), positionLabel)) {
      game.seekRequestSec = position;
    }
    cursorY += 40.0F * scale;

    // Per-attempt judgement windows. A tap or hold is judged with these while the
    // override is armed; drag and flick windows always come from the chart.
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    ImGui::SetNextItemWidth((panelSize.x - 16.0F * scale));
    float perfectMs = static_cast<float>(game.practiceWindows.perfectMs());
    if (ImGui::SliderFloat("##practice_perfect_window", &perfectMs,
            static_cast<float>(game::PracticeJudgeWindows::PERFECT_MIN_MS),
            static_cast<float>(game::PracticeJudgeWindows::PERFECT_MAX_MS),
            "Perfect 窗口 %.1f ms")) {
      game.practiceWindows.setPerfectMs(static_cast<double>(perfectMs));
    }
    cursorY += 40.0F * scale;
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    ImGui::SetNextItemWidth((panelSize.x - 16.0F * scale));
    float goodMs = static_cast<float>(game.practiceWindows.goodMs());
    if (ImGui::SliderFloat("##practice_good_window", &goodMs,
            static_cast<float>(game::PracticeJudgeWindows::GOOD_MIN_MS),
            static_cast<float>(game::PracticeJudgeWindows::GOOD_MAX_MS),
            "Good 窗口 %.1f ms")) {
      game.practiceWindows.setGoodMs(static_cast<double>(goodMs));
    }
    cursorY += 40.0F * scale;
    ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
    if (ImGui::Button("恢复默认窗口")) game.practiceWindows.reset();
    cursorY += 34.0F * scale;
  }

  // Runtime toggles.
  ImGui::SetCursorScreenPos(ImVec2(panelLeft + 8.0F * scale, cursorY));
  bool autoplay = game.autoplay;
  if (ImGui::Checkbox("自动演示", &autoplay)) gameToggleAutoplay(context);
  ImGui::SameLine(panelLeft + 180.0F * scale);
  bool debug = game.debugOverlay;
  if (ImGui::Checkbox("调试信息", &debug)) game.debugOverlay = debug;

  ui::endFullscreenWindow();
}

void drawCountdown(AppContext& context, ImDrawList* drawList) {
  auto& game = context.game;
  const auto size = viewportSize();
  const float scale = layoutScale();
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%d", std::max(1, game.countdown));
  ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, size.height * 0.45F), 90.0F * scale,
      IM_COL32(255, 255, 255, 235), buffer);
}

} // namespace

bool gameEnter(AppContext& context) {
  gameLeave(context);
  std::unique_ptr<game::Chart> chart;
  try {
    chart = chart::ChartLoader::loadFromFile(context.play.chartFile, platform::assetRoot(),
        context.play.songName);
  } catch (const std::exception& exception) {
    context.loadError = exception.what();
    return false;
  } catch (...) {
    context.loadError = "未知错误";
    return false;
  }
  if (!chart) {
    context.loadError = "谱面为空";
    return false;
  }
  context.loadError.clear();

  // Resolve the selected skin to a directory and hand it to the renderer; a skin only
  // overrides the material settings it actually specifies.
  if (context.renderer != nullptr) {
    std::filesystem::path skinDirectory;
    if (!context.settings.selectedSkin.empty()) {
      const auto candidate = context.dataRoot / "skins" / context.settings.selectedSkin;
      std::error_code error;
      if (std::filesystem::is_directory(candidate, error)) skinDirectory = candidate;
    }
    context.renderer->setSkinDirectory(skinDirectory);
  }
  context.game = GameRuntime{};
  auto& game = context.game;
  game.chart = std::move(chart);
  game.loaded = true;
  game.started = true;
  game.autoplay = context.play.autoplay;
  game.challenge = context.play.challenge;
  game.practice = context.play.practice;
  game.mirrorX = context.play.mirrorX;
  // The practice window override starts neutral on every attempt; the player arms it by
  // moving a slider, so an untouched practice run judges exactly like a normal one.
  game.practiceWindows.reset();
  game.session.judgement().setPracticeWindows(&game.practiceWindows);
  game.session.judgement().setPracticeEnabled(game.practice);
  game.musicSpeed = context.play.musicSpeed > 0.0F ? context.play.musicSpeed : 1.0F;
  game.scrollSpeed = context.play.scrollSpeed > 0.0F ? context.play.scrollSpeed : 1.0F;
  game.userOffsetSec =
      static_cast<double>(context.play.chartOffsetMs + context.play.audioOffsetMs) / 1000.0;
  game.chartOffsetSec = game.chart->offset;
  game.session.setChart(game.chart.get(), game.challenge);
  game.session.setAutoplay(game.autoplay);
  game.session.judgement().setJudgementDetailDistribution(
      context.settings.judgementDetailDistribution);
  game.session.judgement().setReplayPlayback(context.play.replayMode);
  syncGameplayViewport(context);

  if (context.audio != nullptr) {
    context.audio->stop();
    context.audio->setLowPassEnabled(false);
    context.audio->setMusicLooping(false);
    context.audio->setPlaybackSpeed(game.musicSpeed);
    context.audio->setMusicVolume(context.settings.musicVolume);
    context.audio->setSoundEffectVolume(context.settings.soundEffectVolume);
    if (!context.play.musicFile.empty() && context.audio->loadMusic(context.play.musicFile)) {
      game.musicLoaded = true;
      game.musicDurationSec = context.audio->getMusicDurationSeconds();
      (void)0;
    }
  }

  if (context.play.replayMode) {
    const auto replay = readReplayFile(context.play.replayFile);
    if (!replay) {
      context.loadError = "回放文件损坏";
      game = GameRuntime{};
      return false;
    }
    game.playback = *replay;
    game.replayPlayback = true;
    game.recording = false;
  } else {
    game.replayPlayback = false;
    game.recording = context.settings.replayEnabled && !game.autoplay && !game.practice;
    if (game.recording) {
      game.recorded.meta.chartId = context.play.chartId;
      game.recorded.meta.songName = context.play.songName;
      game.recorded.meta.difficulty = context.play.difficulty;
      game.recorded.meta.timestampMs = nowMilliseconds();
    }
  }
  game.inIntro = true;
  game.inOutro = false;
  game.finished = false;
  game.introElapsedSec = 0.0;
  game.pause = GameRuntime::Pause::None;
  game.chartTimeSec = 0.0;
  game.visualTimeSec = 0.0;
  return true;
}

void gameLeave(AppContext& context) {
  auto& game = context.game;
  game.session.cancelTouches();
  if (context.audio != nullptr) {
    context.audio->stop();
    context.audio->setLowPassEnabled(false);
    context.audio->setMusicLooping(false);
    context.audio->setPlaybackSpeed(1.0F);
  }
  game.chart.reset();
  game.loaded = false;
  game.pause = GameRuntime::Pause::None;
  game.recording = false;
  game.replayPlayback = false;
}

void gameUpdate(AppContext& context, float frameDeltaSec) {
  auto& game = context.game;
  if (!game.loaded || !game.chart) return;
  const float delta = std::clamp(frameDeltaSec, 0.0F, 0.1F);
  game.frameDeltaSec = delta;
  syncGameplayViewport(context);

  if (game.pause != GameRuntime::Pause::None) {
    game.pauseElapsedSec += delta;
    if (game.pause == GameRuntime::Pause::Countdown) {
      game.countdownElapsedSec += delta;
      if (game.countdownElapsedSec >= 1.0) {
        game.countdownElapsedSec -= 1.0;
        --game.countdown;
        if (game.countdown <= 0) {
          game.pause = GameRuntime::Pause::None;
          game.pauseElapsedSec = 0.0;
          if (context.audio != nullptr && game.musicLoaded) context.audio->play();
        }
      }
    }
    if (game.seekRequestSec >= 0.0) {
      const double target = game.seekRequestSec;
      game.seekRequestSec = -1.0;
      applyPracticeSeek(context, target);
    }
    return;
  }

  // Intro: draw the opening line, start the music, then hand over to gameplay.
  if (game.inIntro) {
    game.introElapsedSec += delta;
    if (!game.inOutro && context.audio != nullptr && game.musicLoaded
        && !context.audio->isPlaying() && game.introElapsedSec >= kIntroAudioStartSec) {
      startAudio(context, true);
    }
    if (game.introElapsedSec >= kIntroEndSec) {
      game.inIntro = false;
      if (context.audio == nullptr || !game.musicLoaded) {
        game.playheadSec = 0.0;
      }
    } else {
      return;
    }
  }

  if (context.audio != nullptr && game.musicLoaded) {
    // The chart advances with the music. A stream that has stopped or stalled would freeze
    // the chart at one position, so the frame clock takes over whenever the music is not
    // actually running, which keeps the chart moving at the right pace.
    if (context.audio->isPlaying()) {
      game.playheadSec = context.audio->cursorSeconds();
    } else {
      game.playheadSec += delta;
    }
  } else {
    game.playheadSec += delta;
  }
  game.chartTimeSec = game.playheadSec - game.chartOffsetSec - game.userOffsetSec;
  game.visualTimeSec += delta;

  if (game.inOutro) {
    game.outroElapsedSec += delta;
    if (game.outroElapsedSec >= kOutroDurationSec) finishGame(context);
    return;
  }

  // Recorded touches are injected for this chart time before the session judges,
  // so replay playback observes the same timing a live player did.
  dispatchRecordedTouches(context);
  game.session.update(game.chartTimeSec, delta);
  for (const int type : game.session.takeSoundEvents()) {
    if (context.audio != nullptr) context.audio->triggerSoundEffect(type);
  }
  updateJudgementPopup(game);

  bool ended = false;
  if (context.audio != nullptr && game.musicLoaded) {
    // The mixer clock can keep advancing after the decoded track ran out, so the
    // end of the song is detected from the playhead against the known duration.
    ended = game.musicDurationSec > 0.0
        && game.playheadSec >= game.musicDurationSec - 0.02;
  }
  if (!ended) {
    ended = game.session.finished(game.chartTimeSec)
        || game.chartTimeSec >= game.chart->chartEndTimeSec + 0.4;
  }
  if (ended) {
    game.inOutro = true;
    game.outroElapsedSec = 0.0;
    game.session.cancelTouches();
    if (context.audio != nullptr) context.audio->pause();
  }
}

void gameRender(AppContext& context) {
  auto& game = context.game;
  if (!game.loaded || !game.chart || context.renderer == nullptr) return;
  render::RenderFrameInput input;
  input.chartTimeSec = game.chartTimeSec;
  input.playheadSec = game.playheadSec;
  input.visualTimeSec = game.visualTimeSec;
  input.userOffsetSec = game.userOffsetSec;
  input.dpi = context.displayDpi;
  input.scrollSpeed = game.scrollSpeed;
  input.mirrorX = game.mirrorX;
  input.backgroundDim = context.settings.backgroundDim;
  input.challengeMode = game.challenge;
  input.apfcIndicator = context.settings.apfcIndicator;
  switch (game.pause) {
    case GameRuntime::Pause::Menu:
      input.pause.mode = render::PauseMode::Menu;
      input.pause.elapsedSec = static_cast<float>(game.pauseElapsedSec);
      break;
    case GameRuntime::Pause::Countdown:
      input.pause.mode = render::PauseMode::Countdown;
      input.pause.elapsedSec = static_cast<float>(game.pauseElapsedSec);
      break;
    default: input.pause.mode = render::PauseMode::None; break;
  }
  input.transition.inIntro = game.inIntro;
  input.transition.introLineScale = game.inIntro
      ? static_cast<float>(std::clamp(game.introElapsedSec / kIntroEndSec, 0.0, 1.0))
      : 1.0F;
  input.transition.inOutro = game.inOutro;
  input.transition.outroElapsedSec = static_cast<float>(game.outroElapsedSec);

  const auto& judgement = game.session.judgement();
  input.hud.visible = game.hudVisible;
  input.hud.combo = judgement.combo();
  input.hud.score = judgement.score();
  input.hud.accuracy = context.settings.realtimeAccuracy ? judgement.realtimeAccuracy()
                                                         : judgement.accuracy();
  input.hud.maxCombo = judgement.maxCombo();
  input.hud.perfect = judgement.counts()[0];
  input.hud.good = judgement.counts()[1];
  input.hud.bad = judgement.counts()[2];
  input.hud.miss = judgement.counts()[3];
  const double duration = game.chart->chartEndTimeSec > 0.01 ? game.chart->chartEndTimeSec : 1.0;
  input.hud.progress = std::clamp(game.chartTimeSec / duration, 0.0, 1.0);
  input.hud.alpha = game.inOutro
      ? static_cast<float>(std::clamp(1.0 - game.outroElapsedSec / kOutroDurationSec, 0.0, 1.0))
      : 1.0F;
  input.practice.enabled = game.practice;
  input.practice.speed = game.musicSpeed;
  input.practice.autoPlay = game.autoplay;
  input.songName = context.play.songName;
  input.levelName = context.play.difficulty;
  context.renderer->render(input, game.chart.get(), &judgement);
}

void drawGameOverlay(AppContext& context) {
  auto& game = context.game;
  if (!game.loaded) return;
  const auto size = viewportSize();
  const float scale = layoutScale();
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();

  if (game.popupJudgement >= 0 && game.popupAgeSec < 0.7) {
    static const char* kLabels[4] = {"PERFECT", "GOOD", "BAD", "MISS"};
    static const ImU32 kColors[4] = {IM_COL32(254, 255, 169, 255), IM_COL32(162, 238, 255, 255),
        IM_COL32(224, 162, 255, 255), IM_COL32(255, 142, 142, 255)};
    const float alpha = static_cast<float>(std::clamp(1.0 - game.popupAgeSec / 0.7, 0.0, 1.0));
    const int index = std::clamp(game.popupJudgement, 0, 3);
    const ImU32 color = (kColors[index] & 0x00FFFFFFU)
        | (static_cast<ImU32>(alpha * 255.0F) << IM_COL32_A_SHIFT);
    ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, size.height * 0.40F),
        40.0F * scale, color, kLabels[index]);
  }

  // Hit offset indicator text.
  const auto& judgement = game.session.judgement();
  const float indicatorAlpha =
      judgement.indicator().latestTextAlpha(game.visualTimeSec);
  if (indicatorAlpha > 0.01F && context.settings.hitOffsetIndicatorMode != 0) {
    const auto& text = judgement.indicator().latestText();
    if (!text.empty()) {
      const ImU32 color = IM_COL32(255, 255, 255,
          static_cast<int>(std::clamp(indicatorAlpha, 0.0F, 1.0F) * 235.0F));
      ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, size.height * 0.86F),
          22.0F * scale, color, text.c_str());
    }
  }

  if (game.debugOverlay || context.settings.showDebugInfo) drawDebugOverlay(context, drawList);
  if (context.settings.showFps) {
    char fpsBuffer[32];
    std::snprintf(fpsBuffer, sizeof(fpsBuffer), "FPS %.1f", static_cast<double>(context.fps));
    drawTextRight(drawList, ImVec2(size.width - 16.0F * scale, 20.0F * scale), 17.0F * scale,
        ui::rgba(0xFFFFFFFFU), fpsBuffer);
  }

  if (game.pause == GameRuntime::Pause::Menu) {
    drawPauseMenu(context);
  } else if (game.pause == GameRuntime::Pause::Countdown) {
    drawCountdown(context, drawList);
  }
}

void gameTogglePause(AppContext& context) {
  auto& game = context.game;
  if (!game.loaded || game.finished || game.inOutro) return;
  if (game.pause == GameRuntime::Pause::None) {
    game.pause = GameRuntime::Pause::Menu;
    game.pauseElapsedSec = 0.0;
    game.pausePlayheadSec = game.playheadSec;
    game.session.cancelTouches();
    if (context.audio != nullptr && game.musicLoaded) context.audio->pause();
  } else if (game.pause == GameRuntime::Pause::Menu) {
    game.pause = GameRuntime::Pause::Countdown;
    game.countdown = 3;
    game.countdownElapsedSec = 0.0;
    game.pauseElapsedSec = 0.0;
  }
}

// Demo mode mirrors the play field horizontally, so the input has to be mirrored to match:
// a touch inside the stage is reflected around its centre, and a touch outside is left
// alone (it belongs to the surrounding chrome, not the play field).
float mirrorTouchX(const AppContext& context, float x) {
  if (!context.game.mirrorX) return x;
  const auto stage = touchStage(context);
  if (x < stage.left || x > stage.left + stage.width) return x;
  return stage.left + (stage.left + stage.width - x);
}

bool gamePointerDown(AppContext& context, std::int64_t pointerId, float x, float y) {
  auto& game = context.game;
  if (!game.loaded || game.finished || game.inOutro) return false;
  if (game.pause != GameRuntime::Pause::None) return false;
  if (game.inIntro) return false;
  if (isPauseButtonHit(context, x, y)) {
    gameTogglePause(context);
    return true;
  }
  if (game.replayPlayback) return false;
  // Mirroring is applied before recording so a replay reproduces the same gameplay.
  x = mirrorTouchX(context, x);
  recordTouch(context, ReplayTouchEvent::Kind::Down, pointerId, x, y);
  game.session.touchDown(pointerId, x, y);
  ++context.forwardedPointerEvents;
  return true;
}

void gamePointerMove(AppContext& context, std::int64_t pointerId, float x, float y) {
  auto& game = context.game;
  if (!game.loaded || game.finished || game.inOutro) return;
  if (game.pause != GameRuntime::Pause::None || game.inIntro) return;
  if (game.replayPlayback) return;
  x = mirrorTouchX(context, x);
  recordTouch(context, ReplayTouchEvent::Kind::Move, pointerId, x, y);
  game.session.touchMove(pointerId, x, y);
  ++context.forwardedPointerEvents;
}

void gamePointerUp(AppContext& context, std::int64_t pointerId) {
  auto& game = context.game;
  if (!game.loaded) return;
  if (game.pause != GameRuntime::Pause::None) return;
  if (game.replayPlayback) return;
  recordTouch(context, ReplayTouchEvent::Kind::Up, pointerId, 0.0F, 0.0F);
  game.session.touchUp(pointerId);
  ++context.forwardedPointerEvents;
}

void gameCancelTouches(AppContext& context) {
  context.game.session.cancelTouches();
}

void gameFocusLost(AppContext& context) {
  auto& game = context.game;
  if (game.loaded && game.pause == GameRuntime::Pause::None && !game.finished) {
    gameTogglePause(context);
  }
  game.session.settleTouches();
}

void gameToggleAutoplay(AppContext& context) {
  auto& game = context.game;
  if (!game.loaded) return;
  game.autoplay = !game.autoplay;
  game.session.setAutoplay(game.autoplay);
}

void gameRestart(AppContext& context) {
  if (!gameEnter(context)) {
    notify(context, context.loadError.empty() ? "谱面加载失败" : context.loadError);
    return;
  }
  context.screen = Screen::Game;
}

void gameQuitToMenu(AppContext& context) {
  navigateTo(context, Screen::Library);
}

} // namespace rechigros::app
