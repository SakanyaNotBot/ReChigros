#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiText.hpp"

#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/render/GameRenderer.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/scene/StageLayout.hpp"
#include "rechigros/render/scene/Transition.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <unordered_map>

namespace rechigros::app {
namespace {

using game::GameplaySession;

constexpr double kIntroLineDurationSec = 55.0 / 60.0;
// The music is released when the opening buffer ends (reference INTRO_AUDIO_DELAY_MS).
constexpr double kIntroEndSec = 1.083;
constexpr double kOutroDurationSec = 1.7;
// The HUD start and exit tweens are 40 frames long, like the play field's.
constexpr double kHudIntroDurationSec = 40.0 / 60.0;
constexpr double kHudOutroDurationSec = 40.0 / 60.0;

// Screen-local feedback state for the pause button's tap ring and double-tap gesture.
double gPauseRingStartSec{-1.0};
double gLastPauseTapMs{0.0};
float gLastPauseTapX{};
float gLastPauseTapY{};

// Wall-clock length of the hover disc's grow and shrink. The disc is not toggled: it eases in
// while the finger is swallowed and eases out once the press ends, so a fast tap does not flash.
constexpr double kHoverEnterSec = 0.1;

// One hover disc. A finger a zone has swallowed owns one until the press ends, which is what
// keeps the glow alive while the finger drags out of the zone.
struct HoverSlot {
  std::int64_t id{-1};
  // Last position the finger was seen at, in view pixels. Kept after the press ends so the
  // disc can shrink where it was instead of jumping to the origin.
  float x{};
  float y{};
  double startSec{};
  double startScale{};
  double endScale{};
  double scale{};
  bool held{};
};
std::array<HoverSlot, 10> gHoverSlots{};

// Whether the music is currently ducked by a zone. The audio filter and the level mod are only
// told about changes, because both act on the transition rather than on every frame.
bool gBlockAudioFilterOn{false};
// Bad+miss and miss totals the level mod was last told about. A challenge fails on the frame a
// counted note lands, so the counters are the trigger.
int gLevelEffectBadMiss{};
int gLevelEffectMiss{};

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

// Chart-reveal camera state for the current gameplay frame. The renderer resolves the same
// curves from its own frame input; the app needs them for HUD texture transforms and for
// the pause-button hit area.
bool chartRevealCameraActive(const AppContext& context) {
  if (!context.settings.chartReveal || !context.game.loaded || context.game.chart == nullptr) {
    return false;
  }
  const auto& game = context.game;
  return !game.inIntro && !game.inOutro && game.chartTimeSec >= 0.0;
}

float chartRevealScale(const AppContext& context) {
  if (!chartRevealCameraActive(context)) return 1.0F;
  const auto& game = context.game;
  const double totalSec = game.musicDurationSec > 0.05 ? game.musicDurationSec
                                                       : game.chart->chartEndTimeSec;
  if (!(totalSec > 0.0)) return 1.0F;
  constexpr float kRevealTransitionSec = 1.0F;
  constexpr float kRevealTargetScale = 0.30F;
  const float playhead = static_cast<float>(game.playheadSec);
  const float total = static_cast<float>(totalSec);
  const auto smooth = [](float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - 2.0F * value);
  };
  float scale = 1.0F;
  if (playhead >= std::max(0.0F, total - kRevealTransitionSec)) {
    const float start = std::max(0.0F, total - kRevealTransitionSec);
    scale = kRevealTargetScale +
        (1.0F - kRevealTargetScale) * smooth((playhead - start) / kRevealTransitionSec);
  } else {
    scale = 1.0F + (kRevealTargetScale - 1.0F) * smooth(playhead / kRevealTransitionSec);
  }
  return std::clamp(scale, kRevealTargetScale, 1.0F);
}

bool isPauseButtonHit(const AppContext& context, float windowX, float windowY) {
  // The pause control's hit area is its rect expanded by a padded radius, sized from the
  // play field's 1000-unit canvas exactly like the reference layout. During a chart reveal
  // the rect and the padded radius shrink with the revealed play field.
  const StageRect stage = touchStage(context);
  if (stage.width <= 1.0F || stage.height <= 1.0F) return false;
  const float unit = stage.height / 1000.0F;
  const float halfUiW = stage.width / stage.height * 500.0F;
  const float baseCenterX = stage.left + stage.width * 0.5F + (50.5887985F - halfUiW) * unit;
  const float baseCenterY = stage.top + stage.height * 0.5F - 444.2999878F * unit;
  const float pauseWidth = 34.262F * unit;
  const float pauseHeight = 37.966F * unit;
  const float dp = 1.75F * std::max(0.2F, stage.height / 1080.0F);
  const float pad = std::clamp(std::max(pauseWidth, pauseHeight) * 1.1F, dp * 20.0F, dp * 80.0F);
  const float revealScale = chartRevealScale(context);
  const float stageCenterX = stage.left + stage.width * 0.5F;
  const float stageCenterY = stage.top + stage.height * 0.5F;
  const float centerX = stageCenterX + (baseCenterX - stageCenterX) * revealScale;
  const float centerY = stageCenterY + (baseCenterY - stageCenterY) * revealScale;
  const float radiusX = (pauseWidth * 0.5F + pad) * revealScale;
  const float radiusY = (pauseHeight * 0.5F + pad) * revealScale;
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
  // A seek replaces the position outright, so the drawing clock restarts from the new one
  // instead of correcting towards it. Its own discontinuity detection only catches large jumps;
  // a small forward seek would otherwise be treated as normal advance and the extrapolated
  // position would briefly run ahead of where the run actually is.
  game.smoothClock.reset(game.playheadSec, context.nowSec);
  game.smoothClockValid = true;
  game.smoothedPlayheadSec = game.playheadSec;
  game.lastCounts[0] = game.lastCounts[1] = game.lastCounts[2] = game.lastCounts[3] = 0;
  // The animated readout follows the position; a seek replaces it, so it restarts there.
  if (context.renderer != nullptr) context.renderer->resetHudDisplay();
}

void dispatchRecordedTouches(AppContext& context) {
  auto& game = context.game;
  if (!game.replayPlayback) return;
  // Judgement-entry replays are applied by the session, not through synthetic touches.
  if (game.playback.usesJudgementModel()) return;
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
  // A replay playback shows the recorded settlement, with the live offset histogram when it
  // collected samples; every other run shows its own judgement result.
  context.lastResult = game.replayPlayback && game.playback.usesJudgementModel()
      ? mergeReplayPlaybackResult(game.playback.meta, judgement.result())
      : judgement.result();
  context.lastRunWasReplay = context.play.replayMode;
  context.lastRunHasReplay = game.recording;
  if (game.recording) {
    // The run's judgements are the replay; touch samples are only kept for the legacy
    // format and are dropped once a judgement recording exists.
    game.recorded.judgements = game.session.recordedReplay();
    game.recorded.entries.clear();
    game.recorded.version = 2;
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

// ---------------------------------------------------------------------------
// Pause menu, resume countdown and practice overlay.
//
// The play field belongs to the renderer; these controls are layered above it the same
// way the reference screen places its pause views over the frozen scene. Geometry is in
// screen pixels and mirrors the reference's density metrics and stage fractions.
// ---------------------------------------------------------------------------

struct GameStagePixels {
  float left{};
  float top{};
  float width{};
  float height{};
};

GameStagePixels gameStagePixels(const AppContext& context) {
  const StageRect stage = touchStage(context);
  GameStagePixels pixels;
  pixels.left = stage.left;
  pixels.top = stage.top;
  pixels.width = stage.width;
  pixels.height = stage.height;
  return pixels;
}

// Reference density: one dp is 1.75 px at the 1080-tall capture resolution.
constexpr float kReferenceDensity = 1.75F;

float gameDp(const GameStagePixels& stage) {
  return kReferenceDensity * std::max(0.2F, stage.height / 1080.0F);
}

// Scale of the practice panels: both shrink together on short stages so nothing runs
// past the play field edge.
float practicePanelScale(const GameStagePixels& stage) {
  const float dp = gameDp(stage);
  const float byHeight = stage.height / (500.0F * dp);
  const float byWidth = stage.width / (720.0F * dp);
  return std::clamp(std::min(byHeight, byWidth), 0.5F, 1.0F);
}

void drawRoundedRect(ImDrawList* drawList, float x, float y, float width, float height,
    float radius, ImU32 color) {
  if (width <= 0.0F || height <= 0.0F) return;
  drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), color, radius);
}

void drawCard(ImDrawList* drawList, float x, float y, float width, float height, float dp) {
  const float radius = dp * 16.0F;
  drawRoundedRect(drawList, x, y, width, height, radius, IM_COL32(255, 255, 255, 23));
  drawRoundedRect(drawList, x + dp, y + dp, width - dp * 2.0F, height - dp * 2.0F,
      std::max(dp * 2.0F, radius - dp), IM_COL32(11, 13, 17, 224));
}

// One rasterised line measured on the pixel grid. Glyph offsets are relative to the
// baseline as reported by the interface font cache.
struct TextRun {
  struct Glyph {
    std::uint32_t codepoint{};
    const ui::UiFontCache::Glyph* glyph{};
  };
  std::vector<Glyph> glyphs;
  float inkLeft{};
  float inkTop{};
  float inkRight{};
  float inkBottom{};
  float sizePx{};
  bool bold{};
  bool valid{};
};

TextRun measureTextRun(std::string_view text, float pixelSize, bool bold) {
  TextRun run;
  auto& cache = ui::fontCache();
  run.sizePx = pixelSize;
  run.bold = bold;
  float penX = 0.0F;
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool first = true;
  std::uint32_t previous = 0;
  for (std::size_t index = 0; index < text.size();) {
    std::size_t length = 0;
    const std::uint32_t codepoint = ui::UiFontCache::decode(text, index, length);
    if (length == 0) break;
    index += length;
    const auto* glyph = cache.glyph(codepoint, pixelSize, bold);
    if (glyph == nullptr) continue;
    if (previous != 0) penX += cache.kerning(previous, codepoint, pixelSize, bold);
    if (glyph->width > 0.0F && glyph->height > 0.0F) {
      const float glyphLeft = penX + glyph->offsetX;
      const float glyphRight = glyphLeft + glyph->width;
      const float glyphTop = glyph->offsetY;
      const float glyphBottom = glyphTop + glyph->height;
      if (first) {
        minX = glyphLeft;
        maxX = glyphRight;
        minY = glyphTop;
        maxY = glyphBottom;
        first = false;
      } else {
        minX = std::min(minX, glyphLeft);
        maxX = std::max(maxX, glyphRight);
        minY = std::min(minY, glyphTop);
        maxY = std::max(maxY, glyphBottom);
      }
    }
    run.glyphs.push_back({codepoint, glyph});
    penX += glyph->advance;
    previous = codepoint;
  }
  if (run.glyphs.empty()) return run;
  run.inkLeft = minX;
  run.inkTop = minY;
  run.inkRight = maxX;
  run.inkBottom = maxY;
  run.valid = true;
  return run;
}

// Draws a measured run with its ink top-left at (inkX, inkY), scaled by `scale`.
void drawTextRun(ImDrawList* drawList, const TextRun& run, float inkX, float inkY, float scale,
    ImU32 color) {
  if (!run.valid || scale <= 0.0F) return;
  auto& cache = ui::fontCache();
  const auto texture = static_cast<ImTextureID>(cache.textureId());
  float penX = 0.0F;
  std::uint32_t previous = 0;
  for (const auto& item : run.glyphs) {
    const auto* glyph = item.glyph;
    if (glyph == nullptr) continue;
    if (previous != 0) penX += cache.kerning(previous, item.codepoint, run.sizePx, run.bold);
    if (glyph->width > 0.0F && glyph->height > 0.0F) {
      const float x = inkX + (penX + glyph->offsetX - run.inkLeft) * scale;
      const float y = inkY + (glyph->offsetY - run.inkTop) * scale;
      drawList->AddImage(texture, ImVec2(x, y),
          ImVec2(x + glyph->width * scale, y + glyph->height * scale), ImVec2(glyph->u0, glyph->v0),
          ImVec2(glyph->u1, glyph->v1), color);
    }
    penX += glyph->advance;
    previous = item.codepoint;
  }
}

// Reference panel text: rasterised at a stage-relative base size with 25% padding, then
// scaled so the whole texture is exactly `heightPx` tall.
void drawPanelText(ImDrawList* drawList, std::string_view text, const GameStagePixels& stage,
    float heightPx, int align, float x, float centerY, ImU32 color) {
  if (heightPx <= 0.0F || text.empty()) return;
  const float baseSize = std::max(16.0F, std::round(stage.width * (50.0F / 1350.0F)));
  const float pad = std::max(4.0F, std::round(baseSize * 0.25F));
  const TextRun run = measureTextRun(text, baseSize, false);
  if (!run.valid) return;
  const float inkWidth = std::ceil(run.inkRight) - std::floor(run.inkLeft);
  const float inkHeight = std::ceil(run.inkBottom) - std::floor(run.inkTop);
  const float textureWidth = inkWidth + pad * 2.0F;
  const float textureHeight = inkHeight + pad * 2.0F;
  const float scale = heightPx / std::max(1.0F, textureHeight);
  float left = x;
  if (align == 1) left = x - textureWidth * scale * 0.5F;
  if (align == 2) left = x - textureWidth * scale;
  const float top = centerY - heightPx * 0.5F;
  // The glyph run starts at the texture's padding inset, like the reference canvas draw
  // that places the text origin one pad step inside the bitmap.
  drawTextRun(drawList, run, left + pad * scale, top + pad * scale, scale, color);
}

// Countdown digit: drawn at its natural pixel size, centred on the given point.
void drawCountdownDigit(ImDrawList* drawList, std::string_view text, float textPx, float centerX,
    float centerY, float alpha) {
  if (alpha <= 0.001F || text.empty()) return;
  const TextRun run = measureTextRun(text, textPx, true);
  if (!run.valid) return;
  const float inkWidth = run.inkRight - run.inkLeft;
  const float inkHeight = run.inkBottom - run.inkTop;
  const float inkX = centerX - inkWidth * 0.5F;
  const float inkY = centerY - inkHeight * 0.5F;
  const int baseAlpha = static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F);
  // Soft drop shadow, approximated with two translucent copies because the overlay draw
  // list has no blur pass.
  const float shadowRadius = std::max(1.0F, textPx * 0.12F);
  const float shadowY = std::max(1.0F, textPx * 0.06F);
  drawTextRun(drawList, run, inkX, inkY + shadowY + shadowRadius * 0.5F, 1.0F,
      IM_COL32(0, 0, 0, static_cast<int>(baseAlpha * 0.26F)));
  drawTextRun(drawList, run, inkX, inkY + shadowY, 1.0F,
      IM_COL32(0, 0, 0, static_cast<int>(baseAlpha * 0.4F)));
  drawTextRun(drawList, run, inkX, inkY, 1.0F, IM_COL32(255, 255, 255, baseAlpha));
}

// The pause icon artwork stores the shape in its alpha channel with a dark RGB payload, so
// it has to be recoloured to pure white before it is tinted. The screen keeps its own
// uploads for that: the shared image cache preserves the source pixels.
std::uint64_t whiteIconTexture(const char* relativePath) {
  static std::unordered_map<std::string, std::unique_ptr<rechigros::render::gl::Texture>> cache;
  static std::unordered_map<std::string, bool> failed;
  const std::string key = relativePath;
  if (const auto found = cache.find(key); found != cache.end()) {
    return found->second->id();
  }
  if (failed.find(key) != failed.end()) return 0;
  if (!rechigros::render::gl::contextAvailable()) return 0;
  const auto bytes = platform::AssetManager::readBinary(platform::assetRoot(), relativePath);
  if (bytes.empty()) {
    failed[key] = true;
    return 0;
  }
  auto image = rechigros::render::asset::decodeImageMemory(bytes.data(), bytes.size());
  if (!image.valid()) {
    failed[key] = true;
    return 0;
  }
  rechigros::render::asset::forceWhite(image);
  auto texture = std::make_unique<rechigros::render::gl::Texture>();
  if (!texture->uploadRgba(image.width, image.height, image.pixels.data())) {
    failed[key] = true;
    return 0;
  }
  texture->setFilter(true);
  const auto id = texture->id();
  cache.emplace(key, std::move(texture));
  return id;
}

// Pause button icon plate: the reference draws its icon artwork stretched to the button
// rect with the pixels forced to white. A missing texture falls back to a plain geometric
// glyph.
void drawPauseIcon(AppContext& context, ImDrawList* drawList, const char* asset,
    float x, float y, float width, float height, float alpha, int fallbackKind) {
  const ImU32 tint = IM_COL32(255, 255, 255,
      static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
  if (const std::uint64_t texture = whiteIconTexture(asset); texture != 0) {
    drawList->AddImage(static_cast<ImTextureID>(texture), ImVec2(x, y), ImVec2(x + width, y + height),
        ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F), tint);
    return;
  }
  (void)context;
  const float pad = width * 0.25F;
  const float left = x + pad;
  const float right = x + width - pad;
  const float top = y + pad;
  const float bottom = y + height - pad;
  const float centerX = x + width * 0.5F;
  const float centerY = y + height * 0.5F;
  const float thickness = std::max(2.0F, width * 0.075F);
  switch (fallbackKind) {
    case 0: {
      drawList->AddLine(ImVec2(right, top), ImVec2(left, centerY), tint, thickness);
      drawList->AddLine(ImVec2(left, centerY), ImVec2(right, bottom), tint, thickness);
      break;
    }
    case 1: {
      drawList->PathArcTo(ImVec2(centerX, centerY), (right - left) * 0.42F,
          std::numbers::pi_v<float> * 0.15F, std::numbers::pi_v<float> * 1.75F, 24);
      drawList->PathStroke(tint, 0, thickness);
      drawList->AddTriangleFilled(ImVec2(right, centerY - (right - left) * 0.18F),
          ImVec2(right + (right - left) * 0.10F, centerY),
          ImVec2(right, centerY + (right - left) * 0.18F), tint);
      break;
    }
    default: {
      drawList->AddTriangleFilled(ImVec2(left, top), ImVec2(left, bottom), ImVec2(right, centerY),
          tint);
      break;
    }
  }
}

// Pause button geometry, mirroring the reference's stage-fraction layout.
struct PauseButtonRects {
  float size{};
  ImVec2 exit{};
  ImVec2 restart{};
  ImVec2 continueButton{};
};

PauseButtonRects computePauseButtonRects(const GameStagePixels& stage) {
  PauseButtonRects rects;
  rects.size = stage.height * 0.1F;
  float gap = rects.size * 1.35F;
  const float maxGap = std::max(0.0F, (stage.width - rects.size * 3.0F) * 0.5F);
  gap = std::min(gap, maxGap * 0.95F);
  const float top = stage.top + stage.height * 0.5F - rects.size * 0.5F;
  const float centerX = stage.left + stage.width * 0.5F;
  rects.exit = ImVec2(centerX - gap - rects.size, top);
  rects.restart = ImVec2(centerX - rects.size * 0.5F, top);
  rects.continueButton = ImVec2(centerX + gap, top);
  return rects;
}

// Hermite curve keys of the pause button fade-in (0.5 s, 31 samples).
constexpr float kPauseButtonAlphaTime[] = {
    0.0F, 0.016666668F, 0.033333335F, 0.05F, 0.06666667F, 0.083333336F, 0.1F, 0.11666667F,
    0.13333334F, 0.15F, 0.16666667F, 0.18333334F, 0.2F, 0.21666667F, 0.23333333F, 0.25F,
    0.26666668F, 0.28333333F, 0.3F, 0.31666666F, 0.33333334F, 0.35F, 0.36666667F, 0.38333333F,
    0.4F, 0.41666666F, 0.43333334F, 0.45F, 0.46666667F, 0.48333332F, 0.5F};
constexpr float kPauseButtonAlphaValue[] = {
    0.0F, 0.037041754F, 0.0834299F, 0.14347078F, 0.22434197F, 0.33479667F, 0.46590355F,
    0.57978284F, 0.6635525F, 0.7254899F, 0.7731407F, 0.8110696F, 0.84202385F, 0.86774313F,
    0.8893877F, 0.9077667F, 0.92346704F, 0.9369268F, 0.94848245F, 0.958398F, 0.9668845F,
    0.97411364F, 0.9802265F, 0.985341F, 0.9895567F, 0.99295855F, 0.9956194F, 0.9976021F,
    0.99896187F, 0.999747F, 1.0F};
constexpr float kPauseButtonAlphaInSlope[] = {
    0.0F, 2.222505F, 2.7832887F, 3.6024528F, 4.852271F, 6.627284F, 7.8664136F, 6.832758F,
    5.026179F, 3.7162437F, 2.859049F, 2.275733F, 1.8572546F, 1.5431572F, 1.2986757F, 1.1027385F,
    0.94201964F, 0.8075853F, 0.6933397F, 0.59493256F, 0.50919014F, 0.43374938F, 0.36677212F,
    0.30687004F, 0.25294283F, 0.20410915F, 0.1596521F, 0.118961446F, 0.08158558F, 0.047106788F,
    0.01518129F};
constexpr float kPauseButtonAlphaOutSlope[] = {
    2.222505F, 2.7832887F, 3.6024528F, 4.852271F, 6.627282F, 7.8664136F, 6.832758F, 5.026179F,
    3.7162437F, 2.859049F, 2.275733F, 1.8572546F, 1.5431572F, 1.2986757F, 1.1027385F, 0.94201964F,
    0.8075853F, 0.6933397F, 0.59493244F, 0.50919014F, 0.43374938F, 0.36677212F, 0.30687004F,
    0.25294283F, 0.20410915F, 0.1596521F, 0.118961446F, 0.08158558F, 0.047106788F, 0.01518129F,
    0.0F};

float evaluateHermite(const float* times, const float* values, const float* inSlopes,
    const float* outSlopes, int count, float timeSec) {
  if (count <= 0) return 0.0F;
  if (timeSec <= times[0]) return values[0];
  if (timeSec >= times[count - 1]) return values[count - 1];
  for (int index = 0; index + 1 < count; ++index) {
    const float t0 = times[index];
    const float t1 = times[index + 1];
    if (timeSec < t0 || timeSec > t1) continue;
    const float span = t1 - t0;
    if (std::abs(span) < 1.0e-6F) return values[index + 1];
    const float p = std::clamp((timeSec - t0) / span, 0.0F, 1.0F);
    const float p2 = p * p;
    const float p3 = p2 * p;
    const float h00 = 2.0F * p3 - 3.0F * p2 + 1.0F;
    const float h10 = p3 - 2.0F * p2 + p;
    const float h01 = -2.0F * p3 + 3.0F * p2;
    const float h11 = p3 - p2;
    return std::clamp(h00 * values[index] + h10 * span * outSlopes[index] + h01 * values[index + 1] +
            h11 * span * inSlopes[index + 1],
        0.0F, 1.0F);
  }
  return values[count - 1];
}

constexpr int kPauseButtonAlphaCount =
    static_cast<int>(sizeof(kPauseButtonAlphaTime) / sizeof(float));

float pauseButtonIntroAlpha(double elapsedSec) {
  return evaluateHermite(kPauseButtonAlphaTime, kPauseButtonAlphaValue,
      kPauseButtonAlphaInSlope, kPauseButtonAlphaOutSlope, kPauseButtonAlphaCount,
      static_cast<float>(elapsedSec));
}

// The three pause plates fade out over the first tenth of a second of the resume.
float continueButtonAlpha(double elapsedSec) {
  if (elapsedSec <= 0.0) return 1.0F;
  constexpr double fadeOutSec = 0.1;
  if (elapsedSec >= fadeOutSec) return 0.0F;
  const float p = static_cast<float>(elapsedSec / fadeOutSec);
  return 1.0F - p * p * (3.0F - 2.0F * p);
}

// Practice state the reference keeps in the renderer. The port keeps the same values in
// the screen and feeds seeks through GameRuntime::seekRequestSec.
struct PracticeUiState {
  double repeatFromSec{0.0};
  double repeatToSec{-1.0};
  bool repeatActive{false};
  int dragMode{0}; // 1 speed, 2 scrub, 3 perfect window, 4 good window
  bool scrubFine{false};
  bool scrubFineBlocked{false};
  double scrubDownSec{0.0};
  double scrubLastMoveSec{0.0};
  double scrubFineCenterSec{0.0};
  float scrubDownX{0.0F};
  float scrubLastX{0.0F};
  float scrubMaxDrift{0.0F};
  float scrubFineAnchorX{0.0F};
};
PracticeUiState gPractice;
// The chart the practice state belongs to and the playback speed it last used; a restart
// of the same run keeps both, a different chart resets them.
std::string gPracticeChartId;
float gPracticeSpeed{1.0F};

constexpr double kPracticeRepeatMinLengthSec = 1.0;
constexpr double kPracticeJumpSec = 5.0;
constexpr float kPracticeSpeedMin = 0.10F;
constexpr float kPracticeSpeedMax = 2.00F;
constexpr float kPracticeSpeedStep = 0.05F;
constexpr double kPracticeFineSpanSec = 4.0;
constexpr double kPracticeFineHoldSec = 0.45;
constexpr double kPracticeFineStillSec = 0.25;
constexpr float kPracticeFineHoldSlopDp = 8.0F;
// The judgement zone bar spans the BAD window plus a fixed miss margin on both sides.
constexpr double kPracticeBarSpanSec = 0.220 + 0.080;
constexpr double kPracticePerfectMinMs = 20.0;
constexpr double kPracticePerfectMaxMs = 80.0;
constexpr double kPracticeGoodMinMs = 25.0;
constexpr double kPracticeGoodMaxMs = 180.0;

std::string practiceFormatTime(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
  const int total = static_cast<int>(std::floor(seconds + 0.5));
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
  return buffer;
}

std::string practiceFormatTimePrecise(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
  int total = static_cast<int>(std::floor(seconds));
  int hundredths = static_cast<int>(std::floor((seconds - static_cast<double>(total)) * 100.0 + 0.5));
  if (hundredths >= 100) {
    hundredths -= 100;
    ++total;
  }
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d.%02d", total / 60, total % 60, hundredths);
  return buffer;
}

double practiceDurationSec(const AppContext& context) {
  const auto& game = context.game;
  if (game.musicDurationSec > 0.05) {
    return std::max(0.001, game.musicDurationSec - game.chartOffsetSec - game.userOffsetSec);
  }
  return std::max(0.001, game.chart ? game.chart->chartEndTimeSec : 1.0);
}

double practicePlayheadSec(const AppContext& context) {
  return std::max(0.0, context.game.chartTimeSec);
}

// Layout of both practice panels, in screen pixels.
struct PracticeLayout {
  float dp{};
  float panelLeft{};
  float panelTop{};
  float panelWidth{};
  float panelHeight{};
  float pad{};
  float timesHeight{};
  float trackHeight{};
  float buttonHeight{};
  float rowGap{};
  float timelineLeft{};
  float timelineTop{};
  float timelineRight{};
  float timelineBottom{};
  float speedLeft{};
  float speedTop{};
  float speedRight{};
  float speedBottom{};
  float speedTrackLeft{};
  float buttonsTop{};
  float buttonsLeft{};
  float buttonWidth{};
  float judgeLeft{};
  float judgeTop{};
  float judgeHeight{};
  float judgeBarLeft{};
  float judgeBarTop{};
  float judgeBarRight{};
  float judgeBarBottom{};
  float judgePerfectLeft{};
  float judgePerfectTop{};
  float judgePerfectRight{};
  float judgePerfectBottom{};
  float judgeGoodTop{};
  float judgeGoodBottom{};
};

PracticeLayout computePracticeLayout(const AppContext& context, const GameStagePixels& stage) {
  PracticeLayout layout;
  const float uiScale = practicePanelScale(stage);
  const float dp = gameDp(stage) * uiScale;
  layout.dp = dp;
  layout.panelWidth = std::min(stage.width * 0.66F, dp * 620.0F);
  layout.pad = dp * 20.0F;
  layout.timesHeight = dp * 20.0F;
  layout.trackHeight = dp * 10.0F;
  layout.buttonHeight = dp * 42.0F;
  layout.rowGap = dp * 16.0F;
  layout.panelHeight = layout.pad * 2.0F + layout.timesHeight + dp * 12.0F + layout.trackHeight +
      dp * 18.0F + layout.buttonHeight + layout.rowGap + layout.buttonHeight;
  layout.panelLeft = stage.left + (stage.width - layout.panelWidth) * 0.5F;
  const PauseButtonRects buttonRects = computePauseButtonRects(stage);
  layout.panelTop = buttonRects.exit.y - layout.panelHeight - dp * 24.0F;

  float x = layout.panelLeft + layout.pad;
  const float right = layout.panelLeft + layout.panelWidth - layout.pad;
  float y = layout.panelTop + layout.pad + layout.timesHeight + dp * 12.0F;
  layout.timelineLeft = x;
  layout.timelineTop = y;
  layout.timelineRight = right;
  layout.timelineBottom = y + layout.trackHeight;
  y += layout.trackHeight + dp * 18.0F + layout.buttonHeight + layout.rowGap;
  layout.speedLeft = x;
  layout.speedTop = y;
  layout.speedRight = right;
  layout.speedBottom = y + layout.buttonHeight;
  layout.speedTrackLeft = x + dp * 86.0F;

  y -= layout.rowGap;
  const float gap = dp * 10.0F;
  layout.buttonWidth = (right - x - gap * 4.0F) / 5.0F;
  layout.buttonsTop = y - layout.buttonHeight;
  layout.buttonsLeft = x;

  // Judgement panel, below the pause button row.
  const float judgeTop = std::min(buttonRects.exit.y + buttonRects.size + dp * 24.0F,
      stage.top + stage.height - dp * 210.0F);
  const float judgePad = dp * 20.0F;
  const float judgeBarHeight = dp * 10.0F;
  const float judgeRowHeight = dp * 34.0F;
  layout.judgeLeft = layout.panelLeft;
  layout.judgeTop = judgeTop;
  layout.judgeHeight =
      judgePad * 2.0F + judgeBarHeight + dp * 16.0F + judgeRowHeight + dp * 10.0F + judgeRowHeight;
  const float judgeX = layout.panelLeft + judgePad;
  const float judgeRight = layout.panelLeft + layout.panelWidth - judgePad;
  layout.judgeBarLeft = judgeX;
  layout.judgeBarTop = judgeTop + judgePad;
  layout.judgeBarRight = judgeRight;
  layout.judgeBarBottom = layout.judgeBarTop + judgeBarHeight;
  const float rowY = layout.judgeBarBottom + dp * 16.0F;
  layout.judgePerfectLeft = judgeX + dp * 78.0F;
  layout.judgePerfectRight = judgeRight - dp * 64.0F;
  layout.judgePerfectTop = rowY;
  layout.judgePerfectBottom = rowY + judgeRowHeight;
  layout.judgeGoodTop = layout.judgePerfectBottom + dp * 10.0F;
  layout.judgeGoodBottom = layout.judgeGoodTop + judgeRowHeight;
  return layout;
}

void applyPracticeSeekRequest(AppContext& context, double targetSec) {
  const double duration = practiceDurationSec(context);
  context.game.seekRequestSec = std::clamp(targetSec, 0.0, std::max(0.0, duration - 0.05));
}

void setPracticeRepeatPoint(AppContext& context, bool isFrom) {
  auto& game = context.game;
  const double time = practicePlayheadSec(context);
  const double duration = practiceDurationSec(context);
  if (isFrom) {
    gPractice.repeatFromSec = time;
    if (!std::isfinite(gPractice.repeatToSec) || gPractice.repeatToSec <= gPractice.repeatFromSec) {
      gPractice.repeatToSec = gPractice.repeatFromSec + kPracticeRepeatMinLengthSec;
    }
  } else {
    gPractice.repeatToSec = time;
    if (gPractice.repeatToSec <= gPractice.repeatFromSec) {
      gPractice.repeatFromSec =
          std::max(0.0, gPractice.repeatToSec - kPracticeRepeatMinLengthSec);
    }
  }
  gPractice.repeatToSec = std::min(gPractice.repeatToSec, duration);
  if (gPractice.repeatFromSec > gPractice.repeatToSec - kPracticeRepeatMinLengthSec) {
    gPractice.repeatFromSec =
        std::max(0.0, gPractice.repeatToSec - kPracticeRepeatMinLengthSec);
  }
  (void)game;
}

void togglePracticeRepeat(AppContext& context) {
  gPractice.repeatActive = !gPractice.repeatActive;
  if (gPractice.repeatActive &&
      (!std::isfinite(gPractice.repeatToSec) ||
          gPractice.repeatToSec <= gPractice.repeatFromSec)) {
    setPracticeRepeatPoint(context, true);
    setPracticeRepeatPoint(context, false);
  }
}

void setPracticeSpeed(AppContext& context, float speed) {
  auto& game = context.game;
  game.musicSpeed = std::clamp(speed, kPracticeSpeedMin, kPracticeSpeedMax);
  gPracticeSpeed = game.musicSpeed;
  if (context.audio != nullptr) context.audio->setPlaybackSpeed(game.musicSpeed);
}

void updatePracticeFineScrub(AppContext& context) {
  if (gPractice.dragMode != 2 || gPractice.scrubFine || gPractice.scrubFineBlocked) return;
  const float slop = gameDp(gameStagePixels(context)) * kPracticeFineHoldSlopDp;
  const double now = context.nowSec;
  if (gPractice.scrubMaxDrift > slop) {
    gPractice.scrubFineBlocked = true;
    return;
  }
  if (gPractice.scrubDownSec <= 0.0 || now - gPractice.scrubDownSec < kPracticeFineHoldSec) return;
  const double lastMove = gPractice.scrubLastMoveSec > 0.0 ? gPractice.scrubLastMoveSec
                                                           : gPractice.scrubDownSec;
  if (now - lastMove < kPracticeFineStillSec) return;
  gPractice.scrubFine = true;
  gPractice.scrubFineCenterSec = practicePlayheadSec(context);
  gPractice.scrubFineAnchorX = gPractice.scrubLastX;
}

void applyPracticeScrubFromTouch(AppContext& context, const PracticeLayout& layout, float rawX) {
  const float width = std::max(1.0F, layout.timelineRight - layout.timelineLeft);
  const float fraction = std::clamp((rawX - layout.timelineLeft) / width, 0.0F, 1.0F);
  applyPracticeSeekRequest(context, practiceDurationSec(context) * fraction);
}

void applyPracticeSpeedFromTouch(AppContext& context, const PracticeLayout& layout, float rawX) {
  const float width = std::max(1.0F, layout.speedRight - layout.speedTrackLeft);
  const float fraction = std::clamp((rawX - layout.speedTrackLeft) / width, 0.0F, 1.0F);
  float speed = kPracticeSpeedMin + (kPracticeSpeedMax - kPracticeSpeedMin) * fraction;
  speed = std::round(speed / kPracticeSpeedStep) * kPracticeSpeedStep;
  setPracticeSpeed(context, speed);
}

void applyPracticeJudgeWindowFromTouch(
    AppContext& context, const PracticeLayout& layout, float rawX, bool perfect) {
  const float left = layout.judgePerfectLeft;
  const float width = std::max(1.0F, layout.judgePerfectRight - layout.judgePerfectLeft);
  const float fraction = std::clamp((rawX - left) / width, 0.0F, 1.0F);
  if (perfect) {
    const double value = kPracticePerfectMinMs + (kPracticePerfectMaxMs - kPracticePerfectMinMs) *
        static_cast<double>(fraction);
    context.game.practiceWindows.setPerfectMs(value);
  } else {
    const double value = kPracticeGoodMinMs + (kPracticeGoodMaxMs - kPracticeGoodMinMs) *
        static_cast<double>(fraction);
    context.game.practiceWindows.setGoodMs(value);
  }
}

// Draws one practice button plate.
void drawPracticeButton(ImDrawList* drawList, const GameStagePixels& stage,
    const PracticeLayout& layout, float x, float y, float width, float height,
    std::string_view label, bool accent) {
  const float dp = layout.dp;
  const float radius = dp * 11.0F;
  if (accent) {
    drawRoundedRect(drawList, x, y, width, height, radius, IM_COL32(255, 199, 71, 230));
    drawRoundedRect(drawList, x + dp, y + dp, width - dp * 2.0F, height - dp * 2.0F,
        std::max(dp * 2.0F, radius - dp), IM_COL32(255, 199, 71, 36));
    drawPanelText(drawList, label, stage, dp * 18.0F, 1, x + width * 0.5F, y + height * 0.5F,
        IM_COL32(255, 219, 242, 255));
  } else {
    drawRoundedRect(drawList, x, y, width, height, radius, IM_COL32(255, 255, 255, 41));
    drawRoundedRect(drawList, x + dp, y + dp, width - dp * 2.0F, height - dp * 2.0F,
        std::max(dp * 2.0F, radius - dp), IM_COL32(255, 255, 255, 11));
    drawPanelText(drawList, label, stage, dp * 18.0F, 1, x + width * 0.5F, y + height * 0.5F,
        IM_COL32(224, 224, 224, 235));
  }
}

// Drives the pointer interactions of the pause menu. Returns true when the press was
// consumed by a control so the caller stops forwarding it.
bool updatePracticeInteractions(AppContext& context, const PracticeLayout& layout) {
  auto& io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;
  bool consumed = false;

  // Buttons are placed on a grid and hit-tested directly, matching the reference's
  // touch-down activation instead of ImGui's press/release cycle.
  const auto pressButton = [&](float x, float y, float width, float height) {
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= x && mouse.x <= x + width &&
        mouse.y >= y && mouse.y <= y + height;
  };
  const float gap = layout.dp * 10.0F;
  const float buttonY = layout.buttonsTop;
  const float buttonWidth = layout.buttonWidth;
  const float buttonHeight = layout.buttonHeight;
  if (pressButton(layout.buttonsLeft, buttonY, buttonWidth, buttonHeight)) {
    setPracticeRepeatPoint(context, true);
    return true;
  }
  if (pressButton(layout.buttonsLeft + (buttonWidth + gap), buttonY, buttonWidth, buttonHeight)) {
    setPracticeRepeatPoint(context, false);
    return true;
  }
  if (pressButton(layout.buttonsLeft + (buttonWidth + gap) * 2.0F, buttonY, buttonWidth,
          buttonHeight)) {
    togglePracticeRepeat(context);
    return true;
  }
  if (pressButton(layout.buttonsLeft + (buttonWidth + gap) * 3.0F, buttonY, buttonWidth,
          buttonHeight)) {
    applyPracticeSeekRequest(context, practicePlayheadSec(context) -
            kPracticeJumpSec * std::max(0.05F, context.game.musicSpeed));
    return true;
  }
  if (pressButton(layout.buttonsLeft + (buttonWidth + gap) * 4.0F, buttonY, buttonWidth,
          buttonHeight)) {
    applyPracticeSeekRequest(context, practicePlayheadSec(context) +
            kPracticeJumpSec * std::max(0.05F, context.game.musicSpeed));
    return true;
  }

  // The three sliders activate on press and keep following the pointer while held.
  if (gPractice.dragMode == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (mouse.x >= layout.speedTrackLeft && mouse.x <= layout.speedRight &&
        mouse.y >= layout.speedTop && mouse.y <= layout.speedBottom) {
      gPractice.dragMode = 1;
      applyPracticeSpeedFromTouch(context, layout, mouse.x);
      return true;
    }
    if (context.game.practice && !context.game.challenge) {
      if (mouse.x >= layout.judgePerfectLeft && mouse.x <= layout.judgePerfectRight &&
          mouse.y >= layout.judgePerfectTop && mouse.y <= layout.judgePerfectBottom) {
        gPractice.dragMode = 3;
        applyPracticeJudgeWindowFromTouch(context, layout, mouse.x, true);
        return true;
      }
      if (mouse.x >= layout.judgePerfectLeft && mouse.x <= layout.judgePerfectRight &&
          mouse.y >= layout.judgeGoodTop && mouse.y <= layout.judgeGoodBottom) {
        gPractice.dragMode = 4;
        applyPracticeJudgeWindowFromTouch(context, layout, mouse.x, false);
        return true;
      }
    }
    if (mouse.x >= layout.timelineLeft && mouse.x <= layout.timelineRight &&
        mouse.y >= layout.timelineTop - layout.dp * 12.0F &&
        mouse.y <= layout.timelineBottom + layout.dp * 12.0F) {
      gPractice.dragMode = 2;
      gPractice.scrubDownSec = context.nowSec;
      gPractice.scrubDownX = mouse.x;
      gPractice.scrubLastX = mouse.x;
      gPractice.scrubFine = false;
      gPractice.scrubFineBlocked = false;
      gPractice.scrubMaxDrift = 0.0F;
      gPractice.scrubLastMoveSec = context.nowSec;
      applyPracticeScrubFromTouch(context, layout, mouse.x);
      return true;
    }
  }

  if (gPractice.dragMode != 0) {
    consumed = true;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      switch (gPractice.dragMode) {
        case 1: applyPracticeSpeedFromTouch(context, layout, mouse.x); break;
        case 2: {
          const float previousX = gPractice.scrubLastX;
          gPractice.scrubLastX = mouse.x;
          gPractice.scrubMaxDrift =
              std::max(gPractice.scrubMaxDrift, std::abs(mouse.x - gPractice.scrubDownX));
          if (std::abs(mouse.x - previousX) > 0.5F) gPractice.scrubLastMoveSec = context.nowSec;
          if (gPractice.scrubFine) {
            // Fine scrub: the whole track spans a few seconds around the press point.
            const float width = std::max(1.0F, layout.timelineRight - layout.timelineLeft);
            const double delta = static_cast<double>(mouse.x - gPractice.scrubFineAnchorX) /
                static_cast<double>(width) * kPracticeFineSpanSec;
            applyPracticeSeekRequest(context, gPractice.scrubFineCenterSec + delta);
          } else {
            applyPracticeScrubFromTouch(context, layout, mouse.x);
          }
          break;
        }
        case 3: applyPracticeJudgeWindowFromTouch(context, layout, mouse.x, true); break;
        case 4: applyPracticeJudgeWindowFromTouch(context, layout, mouse.x, false); break;
        default: break;
      }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      gPractice.dragMode = 0;
      gPractice.scrubFine = false;
      gPractice.scrubFineBlocked = false;
      gPractice.scrubMaxDrift = 0.0F;
      gPractice.scrubDownSec = 0.0;
      gPractice.scrubLastMoveSec = 0.0;
    }
    return consumed;
  }

  updatePracticeFineScrub(context);
  return false;
}

// Draws the practice timeline panel (reference drawPracticeMenu).
void drawPracticePanel(AppContext& context, ImDrawList* drawList, const GameStagePixels& stage,
    const PracticeLayout& layout) {
  const float dp = layout.dp;
  drawCard(drawList, layout.panelLeft, layout.panelTop, layout.panelWidth, layout.panelHeight, dp);

  const double playhead = practicePlayheadSec(context);
  const double duration = practiceDurationSec(context);
  const float timesCenterY = layout.panelTop + layout.pad + layout.timesHeight * 0.5F;
  const bool scrubbing = gPractice.dragMode == 2;
  const std::string elapsedText = scrubbing ? practiceFormatTimePrecise(playhead)
                                            : practiceFormatTime(playhead);
  drawPanelText(drawList, elapsedText, stage, dp * 15.0F, 0, layout.panelLeft + layout.pad,
      timesCenterY, IM_COL32(235, 235, 235, 235));
  drawPanelText(drawList, "/  " + practiceFormatTime(duration), stage, dp * 15.0F, 2,
      layout.panelLeft + layout.panelWidth - layout.pad, timesCenterY, IM_COL32(235, 235, 235, 115));

  // Timeline track, fill, loop markers and playhead knob.
  const float trackLeft = layout.timelineLeft;
  const float trackRight = layout.timelineRight;
  const float trackCenterY = (layout.timelineTop + layout.timelineBottom) * 0.5F;
  const float trackHeight = layout.trackHeight;
  drawRoundedRect(drawList, trackLeft, trackCenterY - trackHeight * 0.5F, trackRight - trackLeft,
      trackHeight, trackHeight * 0.5F, IM_COL32(255, 255, 255, 36));
  const float fraction = static_cast<float>(std::clamp(playhead / duration, 0.0, 1.0));
  const float playX = trackLeft + (trackRight - trackLeft) * fraction;
  if (playX > trackLeft + dp) {
    const ImU32 fillColor = gPractice.scrubFine ? IM_COL32(255, 199, 71, 242)
                                                : IM_COL32(255, 255, 255, 217);
    drawRoundedRect(drawList, trackLeft, trackCenterY - trackHeight * 0.5F, playX - trackLeft,
        trackHeight, trackHeight * 0.5F, fillColor);
  }
  const bool hasRange = gPractice.repeatActive && std::isfinite(gPractice.repeatFromSec) &&
      std::isfinite(gPractice.repeatToSec);
  float fromX = 0.0F;
  float toX = 0.0F;
  if (hasRange) {
    fromX = trackLeft + (trackRight - trackLeft) *
        static_cast<float>(std::clamp(gPractice.repeatFromSec / duration, 0.0, 1.0));
    toX = trackLeft + (trackRight - trackLeft) *
        static_cast<float>(std::clamp(gPractice.repeatToSec / duration, 0.0, 1.0));
    if (toX - fromX > dp) {
      drawRoundedRect(drawList, fromX, trackCenterY - trackHeight * 0.5F, toX - fromX, trackHeight,
          trackHeight * 0.5F, IM_COL32(255, 204, 77, 140));
    }
    drawRoundedRect(drawList, fromX - dp * 1.5F, trackCenterY - (trackHeight + dp * 10.0F) * 0.5F,
        dp * 3.0F, trackHeight + dp * 10.0F, dp * 1.5F, IM_COL32(255, 204, 77, 255));
    drawRoundedRect(drawList, toX - dp * 1.5F, trackCenterY - (trackHeight + dp * 10.0F) * 0.5F,
        dp * 3.0F, trackHeight + dp * 10.0F, dp * 1.5F, IM_COL32(255, 204, 77, 255));
  }
  const float knobRadius = (gPractice.dragMode == 2 ? dp * 9.0F : dp * 7.5F);
  if (gPractice.scrubFine) {
    drawList->AddCircleFilled(ImVec2(playX, trackCenterY), knobRadius + dp * 3.0F,
        IM_COL32(255, 199, 71, 242));
  }
  drawList->AddCircleFilled(ImVec2(playX, trackCenterY), knobRadius + dp * 1.5F,
      IM_COL32(15, 18, 23, 217));
  drawList->AddCircleFilled(ImVec2(playX, trackCenterY), knobRadius, IM_COL32(255, 255, 255, 255));
  if (hasRange) {
    const float letterGap = dp * 22.0F;
    float leftLetterX = fromX;
    float rightLetterX = toX;
    if (rightLetterX - leftLetterX < letterGap) {
      const float middle = (leftLetterX + rightLetterX) * 0.5F;
      leftLetterX = middle - letterGap * 0.5F;
      rightLetterX = middle + letterGap * 0.5F;
    }
    const float minX = layout.panelLeft + layout.pad + dp * 8.0F;
    const float maxX = layout.panelLeft + layout.panelWidth - layout.pad - dp * 8.0F;
    leftLetterX = std::clamp(leftLetterX, minX, maxX);
    rightLetterX = std::clamp(rightLetterX, minX, maxX);
    const float letterY = layout.timelineTop - dp * 10.0F;
    drawPanelText(drawList, "A", stage, dp * 14.0F, 1, leftLetterX, letterY,
        IM_COL32(229, 229, 229, 229));
    drawPanelText(drawList, "B", stage, dp * 14.0F, 1, rightLetterX, letterY,
        IM_COL32(229, 229, 229, 229));
  }

  // Five action buttons.
  const float gap = dp * 10.0F;
  const float buttonY = layout.buttonsTop;
  const float buttonWidth = layout.buttonWidth;
  const float buttonHeight = layout.buttonHeight;
  drawPracticeButton(drawList, stage, layout, layout.buttonsLeft, buttonY, buttonWidth, buttonHeight,
      "A点", false);
  drawPracticeButton(drawList, stage, layout, layout.buttonsLeft + (buttonWidth + gap), buttonY,
      buttonWidth, buttonHeight, "B点", false);
  drawPracticeButton(drawList, stage, layout,
      layout.buttonsLeft + (buttonWidth + gap) * 2.0F, buttonY, buttonWidth, buttonHeight, "循环",
      gPractice.repeatActive);
  drawPracticeButton(drawList, stage, layout,
      layout.buttonsLeft + (buttonWidth + gap) * 3.0F, buttonY, buttonWidth, buttonHeight, "-5s",
      false);
  drawPracticeButton(drawList, stage, layout,
      layout.buttonsLeft + (buttonWidth + gap) * 4.0F, buttonY, buttonWidth, buttonHeight, "+5s",
      false);

  // Speed slider.
  const float speedCenterY = (layout.speedTop + layout.speedBottom) * 0.5F;
  char speedLabel[32];
  std::snprintf(speedLabel, sizeof(speedLabel), "×%.2f",
      static_cast<double>(context.game.musicSpeed));
  drawPanelText(drawList, speedLabel, stage, dp * 18.0F, 2, layout.speedTrackLeft - dp * 12.0F,
      speedCenterY, IM_COL32(235, 235, 235, 184));
  const float speedWidth = layout.speedRight - layout.speedTrackLeft;
  const float speedFraction = std::clamp(
      (context.game.musicSpeed - kPracticeSpeedMin) / (kPracticeSpeedMax - kPracticeSpeedMin), 0.0F,
      1.0F);
  const float thumbX = layout.speedTrackLeft + speedWidth * speedFraction;
  drawRoundedRect(drawList, layout.speedTrackLeft, speedCenterY - dp * 3.0F, speedWidth, dp * 6.0F,
      dp * 3.0F, IM_COL32(255, 255, 255, 33));
  if (thumbX > layout.speedTrackLeft + dp) {
    drawRoundedRect(drawList, layout.speedTrackLeft, speedCenterY - dp * 3.0F,
        thumbX - layout.speedTrackLeft, dp * 6.0F, dp * 3.0F, IM_COL32(255, 255, 255, 204));
  }
  drawList->AddCircleFilled(ImVec2(thumbX, speedCenterY), dp * 8.5F, IM_COL32(13, 14, 18, 217));
  drawList->AddCircleFilled(ImVec2(thumbX, speedCenterY), dp * 7.0F, IM_COL32(255, 255, 255, 255));
}

// Draws the judgement-window panel (reference drawJudgeMenu).
void drawJudgePanel(AppContext& context, ImDrawList* drawList, const GameStagePixels& stage,
    const PracticeLayout& layout) {
  if (!context.game.practice || context.game.challenge) return;
  const float dp = layout.dp;
  drawCard(drawList, layout.judgeLeft, layout.judgeTop, layout.panelWidth, layout.judgeHeight, dp);

  const auto& windows = context.game.practiceWindows;
  const double perfectSec = windows.perfectSec();
  const double goodSec = windows.goodSec();
  const double badSec = std::max(0.001, 0.220);

  const float barLeft = layout.judgeBarLeft;
  const float barRight = layout.judgeBarRight;
  const float barCenterY = (layout.judgeBarTop + layout.judgeBarBottom) * 0.5F;
  const float barHeight = layout.judgeBarBottom - layout.judgeBarTop;
  const float barWidth = barRight - barLeft;
  const float pixelsPerSecond = barWidth / static_cast<float>(2.0 * kPracticeBarSpanSec);
  const float centerX = (barLeft + barRight) * 0.5F;
  const float goldHalf = std::min(static_cast<float>(perfectSec) * pixelsPerSecond, centerX - barLeft);
  const float blueEach = std::max(0.0F, static_cast<float>(goodSec - perfectSec) * pixelsPerSecond);
  const float redEach = std::max(0.0F, static_cast<float>(badSec - goodSec) * pixelsPerSecond);
  const float goldLeft = centerX - goldHalf;
  const float goldRight = centerX + goldHalf;
  const float zoneLeft = goldLeft - blueEach;
  const float zoneRight = goldRight + blueEach;
  const float redLeft = zoneLeft - redEach;
  const float redRight = zoneRight + redEach;
  const float cap = barHeight * 0.5F;
  drawRoundedRect(drawList, barLeft, barCenterY - barHeight * 0.5F,
      redLeft + cap - barLeft, barHeight, cap, IM_COL32(224, 227, 235, 140));
  drawRoundedRect(drawList, redRight - cap, barCenterY - barHeight * 0.5F,
      barRight - (redRight - cap), barHeight, cap, IM_COL32(224, 227, 235, 140));
  if (zoneLeft - redLeft > 0.5F) {
    drawList->AddRectFilled(ImVec2(redLeft, layout.judgeBarTop),
        ImVec2(zoneLeft, layout.judgeBarBottom), IM_COL32(255, 92, 97, 242));
  }
  if (redRight - zoneRight > 0.5F) {
    drawList->AddRectFilled(ImVec2(zoneRight, layout.judgeBarTop),
        ImVec2(redRight, layout.judgeBarBottom), IM_COL32(255, 92, 97, 242));
  }
  if (goldLeft - zoneLeft > 0.5F) {
    drawList->AddRectFilled(ImVec2(zoneLeft, layout.judgeBarTop),
        ImVec2(goldLeft, layout.judgeBarBottom), IM_COL32(107, 173, 255, 242));
  }
  if (zoneRight - goldRight > 0.5F) {
    drawList->AddRectFilled(ImVec2(goldRight, layout.judgeBarTop),
        ImVec2(zoneRight, layout.judgeBarBottom), IM_COL32(107, 173, 255, 242));
  }
  drawList->AddRectFilled(ImVec2(goldLeft, layout.judgeBarTop),
      ImVec2(goldRight, layout.judgeBarBottom), IM_COL32(255, 199, 71, 242));
  drawList->AddRectFilled(ImVec2(centerX - dp, layout.judgeBarTop - dp * 3.0F),
      ImVec2(centerX + dp, layout.judgeBarBottom + dp * 3.0F), IM_COL32(255, 255, 255, 217));

  // One slider row per adjustable window.
  const auto sliderRow = [&](float top, float bottom, const char* label, double valueMs,
                             double minMs, double maxMs, ImU32 color, float valueX) {
    const float centerY = (top + bottom) * 0.5F;
    drawPanelText(drawList, label, stage, dp * 14.0F, 0, layout.judgePerfectLeft - dp * 78.0F,
        centerY, IM_COL32(255, 255, 255, 209));
    char value[32];
    std::snprintf(value, sizeof(value), "±%dms", static_cast<int>(std::lround(valueMs)));
    drawPanelText(drawList, value, stage, dp * 14.0F, 2, valueX, centerY, IM_COL32(255, 255, 255, 184));
    const float fraction =
        static_cast<float>(std::clamp((valueMs - minMs) / (maxMs - minMs), 0.0, 1.0));
    const float thumb = layout.judgePerfectLeft +
        (layout.judgePerfectRight - layout.judgePerfectLeft) * fraction;
    drawRoundedRect(drawList, layout.judgePerfectLeft, centerY - dp * 3.0F,
        layout.judgePerfectRight - layout.judgePerfectLeft, dp * 6.0F, dp * 3.0F,
        IM_COL32(255, 255, 255, 33));
    if (thumb > layout.judgePerfectLeft + dp) {
      drawRoundedRect(drawList, layout.judgePerfectLeft, centerY - dp * 3.0F,
          thumb - layout.judgePerfectLeft, dp * 6.0F, dp * 3.0F, color);
    }
    drawList->AddCircleFilled(ImVec2(thumb, centerY), dp * 8.5F, IM_COL32(13, 14, 18, 217));
    drawList->AddCircleFilled(ImVec2(thumb, centerY), dp * 7.0F, color);
  };
  const float valueX = layout.judgePerfectRight + dp * 62.0F;
  sliderRow(layout.judgePerfectTop, layout.judgePerfectBottom, "PERFECT", windows.perfectMs(),
      kPracticePerfectMinMs, kPracticePerfectMaxMs, IM_COL32(255, 199, 71, 255), valueX);
  sliderRow(layout.judgeGoodTop, layout.judgeGoodBottom, "GOOD", windows.goodMs(),
      kPracticeGoodMinMs, kPracticeGoodMaxMs, IM_COL32(107, 173, 255, 255), valueX);
}

// Resume countdown: three digits slide in from the right, one per second.
float computeContinueBarX(float elapsed);

void drawCountdownDigits(AppContext& context, ImDrawList* drawList, const GameStagePixels& stage) {
  const double elapsed = context.game.pauseElapsedSec;
  constexpr double durationSec = 3.0;
  if (elapsed >= durationSec) return;
  const float uiScale = std::max(0.001F, stage.height / 1080.0F);
  // The deployed reference build draws the countdown digit at an 80 px em size; the
  // current source constant (120) does not match the shipped frames (measured ink 56 px
  // tall, i.e. 80 px em), so the shipped size is used here.
  const float textPx = std::max(1.0F, 80.0F * uiScale);
  const float centerX = stage.left + stage.width * 0.5F;
  const float centerY = stage.top + stage.height * 0.5F;
  constexpr float barStep = 216.0F;
  const float barX = computeContinueBarX(static_cast<float>(elapsed)) * uiScale;
  const auto alpha = [&](double fadeInStart, double fadeInEnd, double holdEnd, double fadeOutEnd) {
    if (elapsed < fadeInStart || elapsed >= fadeOutEnd) return 0.0F;
    if (elapsed < fadeInEnd) {
      const float p = static_cast<float>((elapsed - fadeInStart) / (fadeInEnd - fadeInStart));
      return p * p * (3.0F - 2.0F * p);
    }
    if (elapsed <= holdEnd) return 1.0F;
    const float p = static_cast<float>((elapsed - holdEnd) / (fadeOutEnd - holdEnd));
    return 1.0F - p * p * (3.0F - 2.0F * p);
  };
  drawCountdownDigit(drawList, "3", textPx, centerX + barX + barStep * uiScale, centerY,
      alpha(0.0, 0.16666667, 0.96666664, 1.1666666));
  drawCountdownDigit(drawList, "2", textPx, centerX + barX + barStep * 2.0F * uiScale, centerY,
      alpha(0.96666664, 1.1666666, 2.0, 2.1666667));
  drawCountdownDigit(drawList, "1", textPx, centerX + barX + barStep * 3.0F * uiScale, centerY,
      alpha(1.9666667, 2.1666667, 2.8333333, 3.0));
}

// Horizontal offset of the sliding countdown strip (reference computeOfficialContinueBarX).
float computeContinueBarX(float elapsed) {
  constexpr float barStep = 216.0F;
  constexpr float moveSec = 0.18333334F;
  constexpr float stepSec = 1.0F;
  if (!std::isfinite(elapsed) || elapsed <= 0.0F) return 0.0F;
  if (elapsed >= 3.0F) return -barStep * 3.0F;
  int step = static_cast<int>(std::floor(elapsed / stepSec));
  step = std::clamp(step, 0, 2);
  const float local = elapsed - static_cast<float>(step) * stepSec;
  const float start = -barStep * static_cast<float>(step);
  const float end = -barStep * static_cast<float>(step + 1);
  if (local >= moveSec) return end;
  const float p = std::clamp(local / moveSec, 0.0F, 1.0F);
  const float t = p - 1.0F;
  const float eased = 1.0F + t * t * (2.70158F * t + 1.70158F);
  return start + (end - start) * eased;
}

void drawPauseOverlay(AppContext& context) {
  auto& game = context.game;
  const GameStagePixels stage = gameStagePixels(context);
  const GameStagePixels viewport{0.0F, 0.0F, static_cast<float>(context.windowWidth),
      static_cast<float>(context.windowHeight)};
  const PauseButtonRects buttons = computePauseButtonRects(stage);
  const bool menu = game.pause == GameRuntime::Pause::Menu;

  ui::beginFullscreenWindow("##game_pause_overlay");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const auto buttonLayout = [&](const char* id) {
    ImGui::SetCursorScreenPos(ImVec2(0.0F, 0.0F));
    ImGui::InvisibleButton(id, ImVec2(viewport.width, viewport.height));
  };
  buttonLayout("##pause_fullscreen");

  float buttonAlpha = 0.0F;
  if (menu) {
    buttonAlpha = pauseButtonIntroAlpha(game.pauseElapsedSec);
    const PracticeLayout layout = computePracticeLayout(context, stage);
    if (game.practice) {
      updatePracticeInteractions(context, layout);
      drawPracticePanel(context, drawList, stage, layout);
      drawJudgePanel(context, drawList, stage, layout);
    }
    // The three plates sit centred over the frozen field, below the practice panels.
    drawPauseIcon(context, drawList, "res/exit.png", buttons.exit.x, buttons.exit.y, buttons.size,
        buttons.size, buttonAlpha, 0);
    drawPauseIcon(context, drawList, "res/restart.png", buttons.restart.x, buttons.restart.y,
        buttons.size, buttons.size, buttonAlpha, 1);
    drawPauseIcon(context, drawList, "res/continue.png", buttons.continueButton.x,
        buttons.continueButton.y, buttons.size, buttons.size, buttonAlpha, 2);

    // Pause actions activate on the press, like the reference's touch handling.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const auto inside = [&](const ImVec2& origin) {
      return mouse.x >= origin.x && mouse.x <= origin.x + buttons.size && mouse.y >= origin.y &&
          mouse.y <= origin.y + buttons.size;
    };
    if (clicked && inside(buttons.exit)) {
      gameQuitToMenu(context);
    } else if (clicked && inside(buttons.restart)) {
      gameRestart(context);
    } else if (clicked && inside(buttons.continueButton)) {
      gameTogglePause(context);
    }
  } else if (game.pause == GameRuntime::Pause::Countdown) {
    buttonAlpha = continueButtonAlpha(game.pauseElapsedSec);
    drawPauseIcon(context, drawList, "res/exit.png", buttons.exit.x, buttons.exit.y, buttons.size,
        buttons.size, buttonAlpha, 0);
    drawPauseIcon(context, drawList, "res/restart.png", buttons.restart.x, buttons.restart.y,
        buttons.size, buttons.size, buttonAlpha, 1);
    drawPauseIcon(context, drawList, "res/continue.png", buttons.continueButton.x,
        buttons.continueButton.y, buttons.size, buttons.size, buttonAlpha, 2);
    drawCountdownDigits(context, drawList, stage);
  }

  ui::endFullscreenWindow();
}

// Advances the hover slots and fills the renderer's touch list. A slot is claimed when a zone
// swallows a finger and released only when that finger lifts: the disc keeps following the
// touch even after it leaves the zone, so the glow does not blink out mid-drag.
void updateHoverSlots(const AppContext& context, double nowSec,
    render::RenderFrameInput::TouchState& out) {
  const auto& blocked = context.game.session.blockedTouches();
  for (auto& slot : gHoverSlots) slot.held = false;
  for (const auto& touch : blocked) {
    HoverSlot* slot = nullptr;
    for (auto& candidate : gHoverSlots) {
      if (candidate.id == touch.id) {
        slot = &candidate;
        break;
      }
    }
    if (slot == nullptr) {
      for (auto& candidate : gHoverSlots) {
        if (candidate.id != -1) continue;
        slot = &candidate;
        break;
      }
    }
    if (slot == nullptr) continue;
    if (slot->id != touch.id) {
      // A fresh slot always grows in from nothing, whatever the previous owner left behind.
      slot->id = touch.id;
      slot->startSec = nowSec;
      slot->startScale = 0.0;
      slot->endScale = 1.0;
      slot->scale = 0.0;
    }
    slot->held = true;
    slot->x = touch.x;
    slot->y = touch.y;
  }

  out.count = 0;
  for (auto& slot : gHoverSlots) {
    if (slot.id == -1) continue;
    if (!slot.held && slot.endScale != 0.0) {
      // Released: shrink from wherever the fade had got to instead of restarting at full size.
      slot.startSec = nowSec;
      slot.startScale = slot.scale;
      slot.endScale = 0.0;
    }
    const double elapsed = nowSec - slot.startSec;
    const double progress = std::clamp(elapsed / kHoverEnterSec, 0.0, 1.0);
    slot.scale = slot.startScale + (slot.endScale - slot.startScale) * progress;
    if (slot.endScale == 0.0 && progress >= 1.0) {
      slot.id = -1;
      continue;
    }
    if (!(slot.scale > 0.0) || out.count >= 10) continue;
    auto& point = out.points[out.count++];
    point.x = slot.x;
    point.y = slot.y;
    point.scale = static_cast<float>(std::clamp(slot.scale, 0.0, 1.0));
  }
}

// Drops the per-run feedback state: the hover discs of a previous run must not reappear on the
// next one's first frame, and the audio duck and the challenge counters belong to one attempt.
void resetRunFeedbackState(AppContext& context) {
  gHoverSlots = {};
  gBlockAudioFilterOn = false;
  gLevelEffectBadMiss = 0;
  gLevelEffectMiss = 0;
  if (context.audio != nullptr) context.audio->setLowPassEnabled(false);
  if (context.game.chart != nullptr && context.game.chart->levelEffects != nullptr) {
    context.game.chart->levelEffects->onBlockStateChanged(false);
  }
}

// Ducks the music while a zone holds a finger and forwards the state to the level mod. The
// session reports the transition, so the two listeners hear every edge -- including the one
// that ends a latch, which a caller-side comparison against the finger list cannot see when no
// zone is authored at all.
void applyBlockTouchState(AppContext& context, bool touching) {
  auto& game = context.game;
  gBlockAudioFilterOn = touching;
  if (context.audio != nullptr) context.audio->setLowPassEnabled(touching);
  if (game.chart != nullptr && game.chart->levelEffects != nullptr) {
    game.chart->levelEffects->onBlockStateChanged(touching);
  }
}

// Reports newly counted bad and miss notes to the level mod. The challenge fails on the frame
// its counted note lands, so the counters are compared rather than polled per frame.
void syncLevelEffectChallenge(AppContext& context) {
  auto& game = context.game;
  if (game.chart == nullptr || game.chart->levelEffects == nullptr) return;
  const auto& counts = game.session.judgement().counts();
  const int badMiss = counts[2] + counts[3];
  if (badMiss > gLevelEffectBadMiss) {
    game.chart->levelEffects->onBadOrMiss(static_cast<float>(game.chartTimeSec),
        counts[3] > gLevelEffectMiss);
  }
  gLevelEffectBadMiss = badMiss;
  gLevelEffectMiss = counts[3];
}

} // namespace

// Everything a run needs once its chart has been parsed. Split from the load itself so the
// loading screen can hand the chart over when its worker finishes.
bool beginRun(AppContext& context, std::unique_ptr<game::Chart> chart,
    audio::AudioBuffer* decodedMusic = nullptr) {
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
    // The play field uses the chart's own illustration as its (blurred) backdrop.
    context.renderer->setBackgroundImage(context.play.bgFile);
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
  // A fresh chart starts with neutral practice state; restarting the same practice run
  // keeps the A/B points and the playback speed the player had set.
  if (context.play.chartId != gPracticeChartId) {
    gPractice = PracticeUiState{};
    gPracticeChartId = context.play.chartId;
    gPracticeSpeed = 1.0F;
  }
  if (game.practice) game.musicSpeed = gPracticeSpeed;
  gPauseRingStartSec = -1.0;
  gLastPauseTapMs = 0.0;
  gLastPauseTapX = 0.0F;
  gLastPauseTapY = 0.0F;
  game.userOffsetSec =
      static_cast<double>(context.play.chartOffsetMs + context.play.audioOffsetMs) / 1000.0;
  game.chartOffsetSec = game.chart->offset;
  game.session.setChart(game.chart.get(), game.challenge);
  game.session.setAutoplay(game.autoplay);
  // The duck and the level-mod latch follow the session's own swallowed-finger edge. The
  // context outlives the session, so capturing it is safe for the whole run.
  game.session.setBlockTouchObserver(
      [&context](bool touching) { applyBlockTouchState(context, touching); });
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
    for (const auto* note : game.chart->allNotesSorted) {
      if (!note->hitSound.empty()) context.audio->loadHitSound(note->hitSound);
    }
    // A run started through the loading screen already has its music decoded, so the frame
    // that starts it only swaps the buffer in. The synchronous path still decodes here.
    const bool musicReady = decodedMusic != nullptr
        ? context.audio->installDecodedMusic(std::move(*decodedMusic))
        : (!context.play.musicFile.empty() && context.audio->loadMusic(context.play.musicFile));
    if (musicReady) {
      game.musicLoaded = true;
      game.musicDurationSec = context.audio->getMusicDurationSeconds();
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
    // A judgement-entry replay is re-applied by the session itself; only a legacy
    // touch-sample file still drives the touch pipeline.
    if (game.playback.usesJudgementModel()) {
      game.session.setReplayPlayback(game.playback.judgements);
    }
  } else {
    game.replayPlayback = false;
    game.recording = (context.settings.replayEnabled || context.recordRequested)
        && !game.autoplay && !game.practice;
    game.session.setReplayRecording(game.recording);
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
  resetRunFeedbackState(context);
  game.chartTimeSec = 0.0;
  game.visualTimeSec = 0.0;
  // A new run starts from an unprimed drawing clock: its first frame takes the reported playhead
  // as the reference instead of correcting against the previous run's position.
  game.smoothClockValid = false;
  game.smoothedPlayheadSec = 0.0;
  return true;
}

bool gameEnter(AppContext& context) {
  // Timed because the run start is the one frame the player waits on: everything below runs
  // before the first gameplay frame is drawn.
  const auto startMark = std::chrono::steady_clock::now();
  gameLeave(context);
  std::unique_ptr<game::Chart> chart;
  try {
    // The story-mode setting decides whether a chart carries a level-effect chain at all: with
    // it off, every chart except the one that always plays its effects is loaded as a plain
    // chart. Passing it here rather than filtering at draw time is what keeps the chain, the
    // HUD suppression and the challenge state from leaking into a plain run.
    chart = chart::ChartLoader::loadFromFile(context.play.chartFile, platform::assetRoot(),
        context.play.songName, context.settings.storyMode);
  } catch (const std::exception& exception) {
    context.loadError = exception.what();
    return false;
  } catch (...) {
    context.loadError = "未知错误";
    return false;
  }
  const bool entered = beginRun(context, std::move(chart));
  const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - startMark).count();
  std::printf("run-start: blocking=%.1f ms entered=%d\n", elapsed, entered ? 1 : 0);
  return entered;
}

// Why the last background load failed, kept by the worker because the frame that consumes the
// future must be able to report a reason rather than a bare null.
std::shared_ptr<std::string> gLoadingFailure;

void gameEnterWithLoading(AppContext& context) {
  gameLeave(context);
  auto& game = context.game;
  game = GameRuntime{};
  game.loading = true;
  game.loadingElapsedSec = 0.0;
  game.loadingPlay = context.play;
  game.loadingError.clear();
  // The file and the name are copied into the worker: the context's play setup may be replaced
  // by another navigation while the parse is still running.
  const auto chartFile = context.play.chartFile;
  const auto songName = context.play.songName;
  const auto musicFile = context.play.musicFile;
  const bool storyMode = context.settings.storyMode;
  // The device rate has to be read before the worker starts: the worker resamples the track to
  // it, and opening the device is the engine's business, not the worker's.
  std::uint32_t sampleRate = 0;
  if (context.audio != nullptr) sampleRate = context.audio->outputSampleRate();
  auto failure = std::make_shared<std::string>();
  game.chartLoad = std::async(std::launch::async,
      [chartFile, songName, musicFile, sampleRate, storyMode, failure]() -> GameRuntime::LoadedRun {
        GameRuntime::LoadedRun loaded;
        // The audio decode is the larger half of a run start, so it rides along with the parse
        // instead of blocking the frame that installs the result.
        if (!musicFile.empty() && sampleRate > 0) {
          if (auto music = audio::Decoder::decodeFile(musicFile, sampleRate); !music.empty()) {
            loaded.musicDecoded = true;
            loaded.music = std::move(music);
          }
        }
        try {
          // Sampled when the load starts, like the rest of the run's settings.
          loaded.chart = chart::ChartLoader::loadFromFile(chartFile, platform::assetRoot(),
              songName, storyMode);
        } catch (const std::exception& exception) {
          *failure = exception.what();
        } catch (...) {
          *failure = "未知错误";
        }
        return loaded;
      });
  // The worker keeps the reason; the frame that consumes the future reads it here.
  gLoadingFailure = failure;
}

void pollChartLoad(AppContext& context) {
  auto& game = context.game;
  if (!game.loading || !game.chartLoad.valid()) return;
  if (game.chartLoad.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  GameRuntime::LoadedRun loaded = game.chartLoad.get();
  // The future is spent; keep the runtime from treating it as still running.
  game.loading = false;
  if (!loaded.chart) {
    context.loadError = gLoadingFailure != nullptr && !gLoadingFailure->empty()
        ? *gLoadingFailure
        : std::string("谱面加载失败");
    gLoadingFailure.reset();
    game.loadingError = context.loadError;
    game = GameRuntime{};
    notify(context, context.loadError);
    navigateTo(context, Screen::Library);
    return;
  }
  gLoadingFailure.reset();
  // Only the part that cannot leave the main thread is timed here: the parse already happened
  // on the worker, so this is what a press still costs the frame that consumes it.
  const auto startMark = std::chrono::steady_clock::now();
  const bool entered = beginRun(context, std::move(loaded.chart),
      loaded.musicDecoded ? &loaded.music : nullptr);
  const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - startMark).count();
  std::printf("run-start: deferred=%.1f ms entered=%d\n", elapsed, entered ? 1 : 0);
  if (!entered) {
    notify(context, context.loadError.empty() ? "谱面加载失败" : context.loadError);
    navigateTo(context, Screen::Library);
  }
}

void gameLeave(AppContext& context) {
  auto& game = context.game;
  game.session.cancelTouches();
  resetRunFeedbackState(context);
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

// Advances the drawing clock for this frame. The correction itself lives in SmoothingClock so it
// can be tested without a renderer or an audio device.
void updateSmoothedClock(AppContext& context) {
  auto& game = context.game;
  if (!game.musicLoaded) {
    // No stream to follow: the frame clock already produced the playhead.
    game.smoothedPlayheadSec = game.playheadSec;
    game.smoothClockValid = false;
    return;
  }
  if (!game.smoothClockValid) {
    game.smoothClockValid = true;
    game.smoothClock.reset(game.playheadSec, context.nowSec);
    game.smoothedPlayheadSec = game.playheadSec;
    return;
  }
  game.smoothedPlayheadSec = game.smoothClock.advance(game.playheadSec, context.nowSec,
      static_cast<double>(game.musicSpeed));
}

// Restores a run that was paused before gameplay became stable. The music never reached a
// playhead worth returning to, so the run starts over: reloading rebuilds the session and
// playback state, and the fresh entry replays the opening transition from its first frame.
void restartRunFromZero(AppContext& context) {
  if (!gameEnter(context)) {
    notify(context, context.loadError.empty() ? "谱面加载失败" : context.loadError);
    return;
  }
  context.screen = Screen::Game;
}

void gameUpdate(AppContext& context, float frameDeltaSec) {
  auto& game = context.game;
  // A run whose chart is still being parsed has no gameplay clock to advance. The load is
  // polled here so the frame that completes it starts the run on a normal boundary rather than
  // inside the loader's own thread.
  if (game.loading) {
    game.loadingElapsedSec += static_cast<double>(frameDeltaSec);
    pollChartLoad(context);
    return;
  }
  if (!game.loaded || !game.chart) return;
  // The chart advance follows the reference: a non-positive or overlong frame interval
  // falls back to one 60 Hz frame instead of stalling the playhead or jumping it. The
  // reference uses 0.25 as the overlong bound; clamping only the upper end here would
  // still leave a zero interval advancing nothing, which the reference never does.
  float delta = frameDeltaSec;
  if (!(delta > 0.0F) || delta > 0.25F) delta = 1.0F / 60.0F;
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
          // The countdown ends the pause outright: play resumes on the frame the third step
          // finishes, with no intermediate state between the two. There is deliberately no
          // fade-out window here -- adding one re-applies the paused backdrop after the field
          // has already cleared, which shows up as the picture dimming and blurring again for
          // a moment before it settles.
          game.pause = GameRuntime::Pause::None;
          game.pauseElapsedSec = 0.0;
          game.countdown = 0;
          game.countdownElapsedSec = 0.0;
          if (game.pauseResumesFromZero) {
            game.pauseResumesFromZero = false;
            game.pausePlayheadValid = false;
            restartRunFromZero(context);
            return;
          }
          // Resuming rewinds to the playhead captured when the pause began. The audio stream
          // was stopped for the whole pause, so without this seek the run would continue from
          // wherever the stream happened to be rather than where the player paused.
          if (game.pausePlayheadValid && context.audio != nullptr && game.musicLoaded) {
            game.playheadSec = game.pausePlayheadSec;
            game.chartTimeSec = game.playheadSec - game.chartOffsetSec - game.userOffsetSec;
            context.audio->seek(game.playheadSec);
            // The prediction ran against a frozen position for the whole pause, so the drawing
            // clock restarts from the resumed playhead instead of correcting across the gap.
            game.smoothClock.reset(game.playheadSec, context.nowSec);
            game.smoothClockValid = true;
            game.smoothedPlayheadSec = game.playheadSec;
          }
          game.pausePlayheadValid = false;
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

  // Intro: the decorative judge line expands for 55 frames and holds for 10; the music is
  // released exactly at the end of that buffer so gameplay starts at chart time zero.
  if (game.inIntro) {
    game.introElapsedSec += delta;
    if (game.introElapsedSec >= kIntroEndSec) {
      game.inIntro = false;
      if (context.audio != nullptr && game.musicLoaded) {
        startAudio(context, true);
      } else {
        game.playheadSec = 0.0;
      }
      game.visualTimeSec = 0.0;
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
  updateSmoothedClock(context);
  game.visualTimeSec += delta;

  if (game.inOutro) {
    // The settlement page draws the artwork as a heavily blurred backdrop, which needs the image
    // decoded and blurred. Doing that on its first frame stalls visibly, so it is prepared here
    // on the frame the exit starts, where the one-off cost is hidden by the transition.
    if (!game.outroBlurPrepared) {
      game.outroBlurPrepared = true;
      const auto record = context.library.find(context.play.chartId);
      const auto& art = record ? record->bgFile : context.play.bgFile;
      if (!art.empty()) (void)context.textures.blurredBackdropFor(art);
    }
    game.outroElapsedSec += delta;
    if (game.outroElapsedSec >= kOutroDurationSec) finishGame(context);
    return;
  }

  // Practice seeks requested by the pause panel are applied on the next gameplay frame.
  if (game.practice && game.seekRequestSec >= 0.0) {
    const double target = game.seekRequestSec;
    game.seekRequestSec = -1.0;
    applyPracticeSeek(context, target);
  }

  // Practice loop: keep the playhead inside the A..B range while the music runs. The wrap
  // starts slightly before A (scaled by the playback speed) so the loop seam is not heard.
  if (game.practice && gPractice.repeatActive && std::isfinite(gPractice.repeatFromSec) &&
      std::isfinite(gPractice.repeatToSec) && gPractice.repeatToSec > gPractice.repeatFromSec) {
    const double lead = 0.2 * static_cast<double>(game.musicSpeed);
    const double duration = practiceDurationSec(context);
    const bool playing = !game.inIntro;
    const bool outsideRange = game.chartTimeSec < gPractice.repeatFromSec - lead ||
        game.chartTimeSec > gPractice.repeatToSec;
    const bool audioEnd = game.chartTimeSec >= duration - 0.1 &&
        gPractice.repeatToSec >= duration - 0.1;
    if (playing && (outsideRange || audioEnd)) {
      game.seekRequestSec = gPractice.repeatFromSec;
    }
  }

  // Recorded touches are injected for this chart time before the session judges,
  // so replay playback observes the same timing a live player did.
  dispatchRecordedTouches(context);
  game.session.update(game.chartTimeSec, delta);
  for (const auto& event : game.session.takeDetailedSoundEvents()) {
    if (context.audio != nullptr) context.audio->triggerSoundEffect(event.type, event.hitSound);
  }
  // The audio duck and the level-mod latch were already driven by the session during the update
  // above; the challenge counter still depends on this frame's judgement.
  syncLevelEffectChallenge(context);

  // The run ends when the music reaches the end of the track, and nothing else decides it: the
  // playhead is compared with the duration. A run whose music failed to load has no counterpart
  // in the reference, which cannot begin gameplay without decoded audio, so the chart's own end
  // time terminates it rather than leaving it running forever.
  bool ended = false;
  if (game.musicDurationSec > 0.0) {
    ended = game.playheadSec >= game.musicDurationSec;
  } else {
    ended = game.chartTimeSec >= game.chart->chartEndTimeSec;
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
  // The play field is drawn from the smoothed clock, which hides the audio callback's reporting
  // interval. Judgement already ran this frame against the unsmoothed playhead, so a note's drawn
  // position never changes when it will be judged.
  input.chartTimeSec = game.musicLoaded
      ? game.smoothedPlayheadSec - game.chartOffsetSec - game.userOffsetSec
      : game.chartTimeSec;
  input.playheadSec = game.musicLoaded ? game.smoothedPlayheadSec : game.playheadSec;
  input.visualTimeSec = game.visualTimeSec;
  input.userOffsetSec = game.userOffsetSec;
  input.dpi = context.displayDpi;
  input.scrollSpeed = game.scrollSpeed;
  input.mirrorX = game.mirrorX;
  input.backgroundDim = context.settings.backgroundDim;
  input.aspectRatio = context.settings.aspectRatio;
  input.challengeMode = game.challenge;
  input.apfcIndicator = context.settings.apfcIndicator;
  // Display settings are read when the run starts; the settings screen is not reachable
  // while gameplay is active.
  input.judgeLineWidth = context.settings.judgeLineWidth;
  input.lowResolutionMode = context.settings.lowResolutionMode;
  input.antialias = context.settings.antialias;
  input.disableChartShader = context.settings.disableChartShader;
  input.multiPressHighlight = context.settings.multiPressHighlight;
  input.storyMode = context.settings.storyMode;
  input.chartReveal = context.settings.chartReveal;
  // The chart-debug overlay marks judge lines and notes on the play field, which is what the
  // reference's debug switch shows.
  input.showChartDebug = context.settings.showDebugInfo;
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
  // The decorative line follows the reference's 55-frame expansion curve; it stays
  // fully expanded through the hold and disappears when gameplay starts.
  input.transition.introLineScale = game.inIntro
      ? render::scene::introLineScale(static_cast<float>(
            std::clamp(game.introElapsedSec / kIntroLineDurationSec, 0.0, 1.0)))
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
  input.hud.playheadSec = game.playheadSec;
  input.hud.totalDurationSec = game.musicDurationSec > 0.05 ? game.musicDurationSec
                                                            : game.chart->chartEndTimeSec;
  input.hud.showAccuracy = context.settings.realtimeAccuracy;
  input.hud.scoreJumpEffect = context.settings.scoreJumpEffect;
  input.hud.comboLabel = game.replayPlayback ? "REPLAY"
      : (game.practice ? "PRACTICE" : (game.autoplay ? "AUTOPLAY" : "COMBO"));
  // The judgement offset indicator is a manual-play readout; autoplay runs and a replay
  // without recorded judgements never show it (the reference suppresses them the same way).
  input.hud.hitOffsetIndicatorMode = std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2);
  input.hud.hitOffsetSuppressed = game.autoplay && !game.replayPlayback;
  input.hud.showFps = context.settings.showFps;
  input.hud.fps = context.fps;
  input.hud.visualTimeSec = game.visualTimeSec;
  if (gPauseRingStartSec >= 0.0 && context.nowSec - gPauseRingStartSec >= 1.0) {
    gPauseRingStartSec = -1.0;
  }
  input.hud.pauseRingAgeSec =
      gPauseRingStartSec >= 0.0 ? context.nowSec - gPauseRingStartSec : -1.0;
  input.hud.revealCameraActive = chartRevealCameraActive(context);
  input.hud.revealScale = chartRevealScale(context);
  // The HUD start tween runs over the first 40 frames of the opening and is finished
  // before gameplay starts.
  input.hud.introProgress = game.inIntro
      ? static_cast<float>(std::clamp(game.introElapsedSec / kHudIntroDurationSec, 0.0, 1.0))
      : 1.0F;
  // The exit moves the HUD off the screen rather than fading it. Every element rides a vertical
  // scale about the stage centre, so the ones above the centre travel up and the ones below it
  // travel down; the combo label alone also loses its opacity, on a slightly shorter window than
  // the travel. The HUD itself keeps full opacity throughout.
  // The exit moves the HUD off the screen rather than fading it. Every element rides a vertical
  // scale about the stage centre, so the ones above the centre travel up and the ones below it
  // travel down; the combo label alone also loses its opacity, on a slightly shorter window than
  // the travel. The HUD itself keeps full opacity throughout.
  input.hud.outroProgress = game.inOutro
      ? static_cast<float>(std::clamp(game.outroElapsedSec / kHudOutroDurationSec, 0.0, 1.0))
      : 0.0F;
  input.hud.alpha = 1.0F;
  input.practice.enabled = game.practice;
  input.practice.speed = game.musicSpeed;
  input.practice.autoPlay = game.autoplay;
  input.songName = context.play.songName;
  input.levelName = context.play.difficulty;
  // Hold-body bursts are produced by the gameplay update; the hit-effect pass draws them.
  input.holdBodyBursts = game.session.takeHoldBodyBursts();
  input.holdHeadBursts = game.session.takeHoldHeadBursts();
  // Swallowed fingers: zone pulses are drawn where they are, and the composite lights the same
  // points. The session reports both, so the two never disagree about which touch is held.
  input.blockedTouchPositions = game.session.blockedTouchPositions();
  input.blockedTouchPulseSec = game.session.blockedTouchPulseSec();
  // The hover discs run on the wall clock so a paused chart does not freeze their fade, and a
  // finger that has been swallowed keeps its slot until it lifts.
  updateHoverSlots(context, context.nowSec, input.touches);
  context.renderer->render(input, game.chart.get(), &judgement);
}

// The loading panel shown while a run's chart is being parsed. The reference puts a small
// translucent card with an indeterminate spinner over a dark gradient and keeps it up until the
// first gameplay frame is ready, so starting a run never shows an empty screen.
void drawLoadingPanel(const AppContext& context) {
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();
  const ImVec2 size(static_cast<float>(context.windowWidth),
      static_cast<float>(context.windowHeight));
  // Three-stop vertical gradient, the same tones the reference's loading root uses.
  const ImU32 top = IM_COL32(0x10, 0x13, 0x1A, 0xFF);
  const ImU32 middle = IM_COL32(0x15, 0x1A, 0x23, 0xFF);
  const ImU32 bottom = IM_COL32(0x0D, 0x10, 0x16, 0xFF);
  const float half = size.y * 0.5F;
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(size.x, half), top, top, middle,
      middle);
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, half), ImVec2(size.x, size.y), middle, middle,
      bottom, bottom);

  const float density = std::max(1.0F, context.displayDpi / 96.0F);
  const float panelWidth = 280.0F * density;
  const float panelHeight = 132.0F * density;
  const ImVec2 centre(size.x * 0.5F, size.y * 0.5F);
  const ImVec2 minimum(centre.x - panelWidth * 0.5F, centre.y - panelHeight * 0.5F);
  const ImVec2 maximum(minimum.x + panelWidth, minimum.y + panelHeight);
  const float rounding = 8.0F * density;
  drawList->AddRectFilled(minimum, maximum, IM_COL32(0x1B, 0x22, 0x2D, 0xE6), rounding);
  drawList->AddRect(minimum, maximum, IM_COL32(0x2F, 0x3B, 0x4A, 0xFF), rounding, 0,
      std::max(1.0F, density));

  // Indeterminate spinner: an arc that sweeps while the load runs. Its angle follows the wall
  // clock the frame already carries, so the panel keeps turning even though gameplay time is
  // standing still.
  const float spinnerRadius = 16.0F * density;
  const float spinnerThickness = std::max(1.5F, 3.0F * density);
  const ImVec2 spinnerCentre(centre.x, minimum.y + 44.0F * density);
  const float phase = static_cast<float>(
      std::fmod(context.nowSec * 2.4, 2.0 * std::numbers::pi));
  const ImU32 track = IM_COL32(0x2F, 0x3B, 0x4A, 0xFF);
  const ImU32 sweep = IM_COL32(0x56, 0xD6, 0xC9, 0xFF);
  drawList->PathClear();
  drawList->PathArcTo(spinnerCentre, spinnerRadius, 0.0F, 2.0F * std::numbers::pi, 48);
  drawList->PathStroke(track, 0, spinnerThickness);
  drawList->PathClear();
  drawList->PathArcTo(spinnerCentre, spinnerRadius, phase, phase + 1.7F, 32);
  drawList->PathStroke(sweep, 0, spinnerThickness);

  // Two lines, matching the reference's loading caption.
  const char* caption = "加载中...";
  const char* detail = "(正在解析谱面与解码音频)";
  const float captionSize = 15.0F * density;
  const float detailSize = 12.0F * density;
  const ImVec2 captionExtent = ImGui::GetFont()->CalcTextSizeA(captionSize, FLT_MAX, 0.0F, caption);
  const ImVec2 detailExtent = ImGui::GetFont()->CalcTextSizeA(detailSize, FLT_MAX, 0.0F, detail);
  const float textTop = spinnerCentre.y + spinnerRadius + 12.0F * density;
  drawList->AddText(ImGui::GetFont(), captionSize,
      ImVec2(centre.x - captionExtent.x * 0.5F, textTop), IM_COL32(0xF3, 0xF7, 0xFB, 0xFF),
      caption);
  drawList->AddText(ImGui::GetFont(), detailSize,
      ImVec2(centre.x - detailExtent.x * 0.5F, textTop + captionExtent.y + 4.0F * density),
      IM_COL32(0xA9, 0xB4, 0xC2, 0xFF), detail);
}

void drawGameOverlay(AppContext& context) {
  auto& game = context.game;
  // The loading panel replaces the gameplay overlay: there is no HUD to draw yet and the pause
  // control has nothing to act on.
  if (game.loading) {
    drawLoadingPanel(context);
    return;
  }
  if (!game.loaded) return;
  ImDrawList* drawList = ImGui::GetBackgroundDrawList();

  // The reference gameplay screen has no centre judgement label and no raw offset text:
  // the judgement offset indicator drawn by the renderer HUD is the only feedback.
  if (game.debugOverlay || context.settings.showDebugInfo) drawDebugOverlay(context, drawList);

  if (game.pause != GameRuntime::Pause::None) {
    drawPauseOverlay(context);
  }
}

void gameTogglePause(AppContext& context) {
  auto& game = context.game;
  if (!game.loaded || game.finished || game.inOutro) return;
  // A pause taken before gameplay starts, or while the opening transition is still running,
  // cannot resume from where it stopped: the music has no stable playhead yet. Such a run
  // restarts from the beginning when it resumes, so the frozen playhead is forced to zero here
  // instead of being captured from the audio engine.
  const bool beforeGameplay = game.inIntro || game.chartTimeSec < 0.0;
  if (game.pause == GameRuntime::Pause::None) {
    game.pause = GameRuntime::Pause::Menu;
    game.pauseElapsedSec = 0.0;
    game.pausePlayheadSec = beforeGameplay ? 0.0 : game.playheadSec;
    game.pausePlayheadValid = true;
    game.pauseResumesFromZero = beforeGameplay;
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

  // The pause plates are drawn immediately but their hit test has to happen on the real
  // touch path: on Android a finger never reaches ImGui's mouse state, so the overlay's own
  // `IsMouseClicked` check can never fire and the plates would be dead. These are the same
  // rectangles the overlay draws, from the same function.
  if (game.pause == GameRuntime::Pause::Menu) {
    const GameStagePixels stage = gameStagePixels(context);
    const PauseButtonRects buttons = computePauseButtonRects(stage);
    const auto inside = [&](const ImVec2& origin) {
      return x >= origin.x && x <= origin.x + buttons.size && y >= origin.y &&
          y <= origin.y + buttons.size;
    };
    if (inside(buttons.exit)) {
      gameQuitToMenu(context);
      return true;
    }
    if (inside(buttons.restart)) {
      gameRestart(context);
      return true;
    }
    if (inside(buttons.continueButton)) {
      gameTogglePause(context);
      return true;
    }
    return true;  // The menu owns every press while it is up.
  }
  if (game.pause != GameRuntime::Pause::None) return false;
  if (game.inIntro) return false;

  // The pause control reacts to a double tap inside its expanded hit area; a single tap
  // only flashes the ring feedback (the reference keeps the ring even when the tap lands
  // on the play field, and the same press is still forwarded to the judgement below).
  if (isPauseButtonHit(context, x, y)) {
    gPauseRingStartSec = context.nowSec;
    const double nowMs = static_cast<double>(nowMilliseconds());
    const float maxDistance = 40.0F * gameDp(gameStagePixels(context));
    const float deltaX = x - gLastPauseTapX;
    const float deltaY = y - gLastPauseTapY;
    if (gLastPauseTapMs > 0.0 && nowMs - gLastPauseTapMs <= 1000.0 &&
        deltaX * deltaX + deltaY * deltaY <= maxDistance * maxDistance) {
      gLastPauseTapMs = 0.0;
      gameTogglePause(context);
      return true;
    }
    gLastPauseTapMs = nowMs;
    gLastPauseTapX = x;
    gLastPauseTapY = y;
  } else {
    gLastPauseTapMs = 0.0;
  }

  if (game.replayPlayback) return false;
  // An autoplay run plays itself. The press is dropped before it reaches the judgement, so a
  // finger can neither claim a note nor be latched inside a zone; the pause control above is
  // handled before this point and keeps working. Replay playback already returned above.
  if (game.autoplay) return false;
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
  if (game.replayPlayback || game.autoplay) return;
  x = mirrorTouchX(context, x);
  recordTouch(context, ReplayTouchEvent::Kind::Move, pointerId, x, y);
  game.session.touchMove(pointerId, x, y);
  ++context.forwardedPointerEvents;
}

void gamePointerUp(AppContext& context, std::int64_t pointerId) {
  auto& game = context.game;
  if (!game.loaded) return;
  if (game.pause != GameRuntime::Pause::None) return;
  if (game.replayPlayback || game.autoplay) return;
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
