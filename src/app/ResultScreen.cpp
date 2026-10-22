#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

// Every position below is a design unit on the 960x540 settlement canvas.
constexpr float kCanvasWidth = 960.0F;
constexpr float kCanvasHeight = 540.0F;
constexpr float kIllustrationLeft = 53.0F;
constexpr float kIllustrationTop = 114.0F;
constexpr float kIllustrationWidth = 500.0F;
constexpr float kIllustrationHeight = 312.0F;
constexpr float kUpperLeft = 526.0F;
constexpr float kUpperTop = 114.0F;
constexpr float kUpperWidth = 389.0F;
constexpr float kUpperHeight = 156.0F;
constexpr float kMiddleLeft = 505.0F;
constexpr float kMiddleTop = 292.0F;
constexpr float kMiddleWidth = 362.0F;
constexpr float kMiddleHeight = 64.0F;
constexpr float kLowerLeft = 485.0F;
constexpr float kLowerTop = 372.0F;
constexpr float kLowerWidth = 362.0F;
constexpr float kLowerHeight = 64.0F;
constexpr float kModeTagsLeft = 559.0F;
constexpr float kModeTagsTop = 34.0F;
constexpr float kCornerWidth = 102.0F;
constexpr float kCornerHeight = 78.0F;
constexpr float kCornerMargin = 14.0F;
constexpr float kSmallButtonWidth = 70.0F;
constexpr float kSmallButtonHeight = 40.0F;
constexpr float kHistogramWidth = 500.0F;
constexpr float kHistogramHeight = 170.0F;

std::uint64_t backgroundTexture(AppContext& context, const ChartRecord* record, int maxSize) {
  if (record != nullptr) return coverTexture(context, *record, maxSize);
  if (!context.play.bgFile.empty()) return context.textures.textureFor(context.play.bgFile, maxSize);
  return 0;
}

void saveReplay(AppContext& context) {
  if (!context.lastRunHasReplay) {
    notify(context, "本次游玩没有可保存的回放");
    return;
  }
  if (!context.savedReplayId.empty()) {
    notify(context, "回放已保存");
    return;
  }
  const std::string id = context.replays.save(context.game.recorded);
  if (id.empty()) {
    notify(context, "回放保存失败");
    return;
  }
  context.savedReplayId = id;
  notify(context, "回放已保存");
}

void playReplay(AppContext& context) {
  if (!context.lastRunHasReplay) {
    notify(context, "本次游玩没有可播放的回放");
    return;
  }
  if (context.savedReplayId.empty()) saveReplay(context);
  if (context.savedReplayId.empty()) return;
  startSavedReplay(context, context.savedReplayId);
}

// Judgement distribution histogram, drawn on the same design canvas as the boards.
void drawHistogram(AppContext& context, ImDrawList* drawList, ImVec2 topLeft, ImVec2 size,
    float unit) {
  const auto& bins = context.lastResult.judgementOffsetBins;
  const int rangeMs = context.lastRunWasReplay
      ? (context.lastReplayMeta.challenge ? game::PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                                          : game::PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS)
      : (context.play.challenge ? game::PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                                : game::PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS);
  drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
      IM_COL32(8, 11, 16, 216), 6.0F * unit);
  drawList->AddRect(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
      IM_COL32(255, 255, 255, 60), 6.0F * unit);
  const float graphLeft = topLeft.x + 26.0F * unit;
  const float graphRight = topLeft.x + size.x - 26.0F * unit;
  const float graphTop = topLeft.y + 42.0F * unit;
  const float baseline = topLeft.y + size.y - 34.0F * unit;
  const float labelHeight = 8.0F;
  int maximum = 1;
  for (const int count : bins) maximum = std::max(maximum, count);
  const int count = static_cast<int>(bins.size());
  if (count <= 0) return;
  const float slot = (graphRight - graphLeft) / static_cast<float>(count);
  drawList->AddLine(ImVec2(graphLeft, baseline), ImVec2(graphRight, baseline),
      IM_COL32(255, 255, 255, 60), 1.0F);
  for (int index = 0; index < count; ++index) {
    const float barWidth = slot * 0.62F;
    const float barHeight = bins[index] == 0
        ? labelHeight * unit
        : (static_cast<float>(bins[index]) / static_cast<float>(maximum)) * (baseline - graphTop)
            + labelHeight * unit;
    const float left = graphLeft + static_cast<float>(index) * slot + (slot - barWidth) * 0.5F;
    const ImU32 color = index == count / 2 ? IM_COL32(255, 255, 255, 255)
                                           : IM_COL32(201, 211, 230, 230);
    drawList->AddRectFilled(ImVec2(left, baseline - barHeight),
        ImVec2(left + barWidth, baseline), color, 2.0F * unit);
  }
  const int binWidthMs = std::max(1, (rangeMs * 2) / std::max(1, count - 1));
  const int peak = static_cast<int>(std::max_element(bins.begin(), bins.end()) - bins.begin());
  const int peakMs = -rangeMs + peak * binWidthMs;
  char title[96];
  std::snprintf(title, sizeof(title), "判定分布  (峰值 %+d ms)", peakMs);
  ui::drawText(drawList, ImVec2(topLeft.x + 14.0F * unit, topLeft.y + 12.0F * unit),
      17.0F * unit, ui::rgba(ui::kTextPrimary), title);
  ui::drawText(drawList, ImVec2(graphLeft, baseline + 8.0F * unit), 13.0F * unit,
      ui::rgba(ui::kTextMuted), "早");
  drawTextRight(drawList, ImVec2(graphRight, baseline + 14.0F * unit), 13.0F * unit,
      ui::rgba(ui::kTextMuted), "晚");
}

} // namespace

void drawResultScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##result");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  const auto record = context.library.find(context.play.chartId);
  const std::uint64_t background = backgroundTexture(context, record ? &*record : nullptr, 1024);
  if (background != 0) {
    drawList->AddImage(static_cast<ImTextureID>(background), ImVec2(0.0F, 0.0F),
        ImVec2(size.width, size.height), ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F),
        IM_COL32(255, 255, 255, 70));
  }
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      IM_COL32(8, 10, 14, 190));

  const ui::DesignCanvas canvas = ui::designCanvas(ImVec2(size.width, size.height),
      kCanvasWidth, kCanvasHeight);
  const float unit = canvas.scale;
  const auto point = [&](float x, float y) { return ui::canvasPoint(canvas, x, y); };
  const auto length = [&](float value) { return value * unit; };
  const game::PlayResult& result = context.lastResult;
  const float skew = ui::kSkewRatio;

  // Illustration parallelogram on the left, holding the song and difficulty.
  {
    const ImVec2 topLeft = point(kIllustrationLeft, kIllustrationTop);
    const float width = length(kIllustrationWidth);
    const float height = length(kIllustrationHeight);
    ui::drawPanel(drawList, topLeft, ImVec2(topLeft.x + width, topLeft.y + height), height * skew,
        IM_COL32(16, 20, 26, 216), IM_COL32(255, 255, 255, 90), 1.0F);
    if (background != 0) {
      // The dimmed backdrop already shows the artwork; a clipped copy keeps the
      // parallelogram reading as a picture frame.
      const ImVec2 clipMin = topLeft;
      const ImVec2 clipMax(topLeft.x + width, topLeft.y + height);
      drawList->PushClipRect(clipMin, clipMax, true);
      drawList->AddImage(static_cast<ImTextureID>(background), clipMin, clipMax, ImVec2(0.0F, 0.0F),
          ImVec2(1.0F, 1.0F), IM_COL32(255, 255, 255, 150));
      drawList->PopClipRect();
      drawList->AddRectFilled(clipMin, clipMax, IM_COL32(0, 0, 0, 60));
    }
    char songBuffer[160];
    std::snprintf(songBuffer, sizeof(songBuffer), "%.48s", context.play.songName.c_str());
    ui::drawClippedText(drawList,
        ImVec2(topLeft.x + length(26.0F), topLeft.y + height - length(52.0F)),
        topLeft, ImVec2(topLeft.x + width, topLeft.y + height), 30.0F * unit,
        IM_COL32(255, 255, 255, 255),
        ui::ellipsize(songBuffer, 30.0F * unit, width - length(50.0F)));
    const std::string difficulty = context.play.difficulty;
    const ImVec2 extent = ui::measureText(difficulty.c_str(), 18.0F * unit);
    ui::drawText(drawList, ImVec2(topLeft.x + width - length(26.0F) - extent.x,
                            topLeft.y + height - length(40.0F)),
        18.0F * unit, IM_COL32(255, 255, 255, 255), difficulty.c_str());
  }

  // Upper board: score and grade.
  {
    const ImVec2 topLeft = point(kUpperLeft, kUpperTop);
    const float width = length(kUpperWidth);
    const float height = length(kUpperHeight);
    ui::drawPanel(drawList, topLeft, ImVec2(topLeft.x + width, topLeft.y + height), height * skew,
        ui::rgba(ui::kDetailTitlePanel), IM_COL32(255, 255, 255, 90), 1.0F);
    const std::string scoreText = formatScore(result.score);
    ui::drawText(drawList, ImVec2(topLeft.x + length(34.0F), topLeft.y + length(56.0F)),
        52.0F * unit, IM_COL32(255, 255, 255, 255), scoreText.c_str());
    drawList->AddRectFilled(ImVec2(topLeft.x + length(36.0F), topLeft.y + length(128.0F)),
        ImVec2(topLeft.x + length(186.0F), topLeft.y + length(130.0F)),
        IM_COL32(255, 255, 255, 140));
    const int grade = gradeIndexForScore(result.score, result.isFullCombo());
    drawGradeImage(context, drawList,
        ImVec2(topLeft.x + length(243.0F + 54.0F), topLeft.y + length(22.0F + 54.0F)),
        length(108.0F), grade);
  }

  // Middle board: max combo, standard deviation, accuracy.
  {
    const ImVec2 topLeft = point(kMiddleLeft, kMiddleTop);
    const float width = length(kMiddleWidth);
    const float height = length(kMiddleHeight);
    ui::drawPanel(drawList, topLeft, ImVec2(topLeft.x + width, topLeft.y + height), height * skew,
        ui::rgba(ui::kDetailInfoPanel), IM_COL32(255, 255, 255, 90), 1.0F);

    char comboBuffer[32];
    std::snprintf(comboBuffer, sizeof(comboBuffer), "%d", result.maxCombo);
    ui::drawText(drawList, ImVec2(topLeft.x + length(26.0F), topLeft.y + length(6.0F)),
        28.0F * unit, IM_COL32(255, 255, 255, 255), comboBuffer);
    ui::drawText(drawList, ImVec2(topLeft.x + length(26.0F), topLeft.y + length(42.0F)),
        11.0F * unit, IM_COL32(204, 255, 255, 255), "MAX COMBO");

    char deviationBuffer[48];
    std::snprintf(deviationBuffer, sizeof(deviationBuffer), "%.3f ms", result.stdDevMs);
    const ImVec2 devExtent = ui::measureText(deviationBuffer, 24.0F * unit);
    ui::drawText(drawList, ImVec2(topLeft.x + length(122.0F + 62.0F) - devExtent.x * 0.5F,
                            topLeft.y + length(7.0F)),
        24.0F * unit, IM_COL32(255, 255, 255, 255), deviationBuffer);
    ui::drawTextCentered(drawList, ImVec2(topLeft.x + length(122.0F + 62.0F),
                                    topLeft.y + length(42.0F + 7.0F)),
        11.0F * unit, IM_COL32(204, 255, 255, 255), "STD DEVIATION");

    const std::string accuracyText = formatAccuracy(result.accuracy);
    const ImVec2 accExtent = ui::measureText(accuracyText.c_str(), 28.0F * unit);
    ui::drawText(drawList, ImVec2(topLeft.x + width - length(26.0F) - accExtent.x,
                            topLeft.y + length(6.0F)),
        28.0F * unit, IM_COL32(255, 255, 255, 255), accuracyText.c_str());
    ui::drawText(drawList, ImVec2(topLeft.x + width - length(26.0F) - length(64.0F),
                            topLeft.y + length(42.0F)),
        11.0F * unit, IM_COL32(204, 255, 255, 255), "ACCURACY");
  }

  // Lower board: judgement counts and early/late split.
  {
    const ImVec2 topLeft = point(kLowerLeft, kLowerTop);
    const float width = length(kLowerWidth);
    const float height = length(kLowerHeight);
    ui::drawPanel(drawList, topLeft, ImVec2(topLeft.x + width, topLeft.y + height), height * skew,
        ui::rgba(ui::kDetailInfoPanel), IM_COL32(255, 255, 255, 90), 1.0F);

    struct CountEntry {
      const char* label;
      int value;
    };
    const CountEntry counts[] = {
        {"PERFECT", result.perfect},
        {"GOOD", result.good},
        {"BAD", result.bad},
        {"MISS", result.miss},
    };
    const float centers[] = {10.0F + 36.0F, 86.0F + 28.0F, 140.0F + 25.0F, 191.0F + 25.0F};
    for (int index = 0; index < 4; ++index) {
      const std::string value = std::to_string(counts[index].value);
      ui::drawTextCentered(drawList, ImVec2(topLeft.x + length(centers[index]),
                                      topLeft.y + length(7.0F + 17.0F)),
          24.0F * unit, IM_COL32(255, 255, 255, 255), value.c_str());
      ui::drawTextCentered(drawList, ImVec2(topLeft.x + length(centers[index]),
                                      topLeft.y + length(42.0F + 8.0F)),
          12.0F * unit, IM_COL32(204, 255, 255, 255), counts[index].label);
    }
    // Divider before the early/late column.
    drawList->AddRectFilled(ImVec2(topLeft.x + length(252.0F), topLeft.y + length(14.0F)),
        ImVec2(topLeft.x + length(253.0F), topLeft.y + length(50.0F)),
        IM_COL32(255, 255, 255, 90));
    ui::drawText(drawList, ImVec2(topLeft.x + length(262.0F), topLeft.y + length(14.0F)),
        11.0F * unit, IM_COL32(204, 255, 255, 255), "EARLY");
    const std::string early = std::to_string(result.goodEarly);
    const ImVec2 earlyExtent = ui::measureText(early.c_str(), 14.0F * unit);
    ui::drawText(drawList, ImVec2(topLeft.x + length(304.0F + 36.0F) - earlyExtent.x,
                            topLeft.y + length(13.0F)),
        14.0F * unit, IM_COL32(255, 255, 255, 255), early.c_str());
    ui::drawText(drawList, ImVec2(topLeft.x + length(262.0F), topLeft.y + length(33.0F)),
        11.0F * unit, IM_COL32(204, 255, 255, 255), "LATE");
    const std::string late = std::to_string(result.goodLate);
    const ImVec2 lateExtent = ui::measureText(late.c_str(), 14.0F * unit);
    ui::drawText(drawList, ImVec2(topLeft.x + length(300.0F + 36.0F) - lateExtent.x,
                            topLeft.y + length(32.0F)),
        14.0F * unit, IM_COL32(255, 255, 255, 255), late.c_str());
  }

  // Mode tags along the top-right.
  {
    float tagLeft = kModeTagsLeft;
    const float tagTop = kModeTagsTop;
    const auto drawTag = [&](const char* label) {
      const ImVec2 extent = ui::measureText(label, 13.0F * unit);
      const ImVec2 topLeft = point(tagLeft, tagTop);
      const float width = extent.x + length(20.0F);
      const float height = length(26.0F);
      ui::skewQuad(drawList, topLeft.x, topLeft.y, topLeft.x + width, topLeft.y + height,
          height * skew, ui::rgba(ui::kChip));
      ui::drawTextCentered(drawList, ImVec2(topLeft.x + width * 0.5F - height * skew * 0.5F,
                                      topLeft.y + height * 0.5F),
          13.0F * unit, ui::rgba(ui::kTextSecondary), label);
      tagLeft += (extent.x / std::max(0.01F, unit) + 28.0F);
    };
    if (context.play.autoplay) drawTag("AUTOPLAY");
    if (context.play.challenge) drawTag("挑战");
    if (context.play.practice) drawTag("练习");
    if (context.play.mirrorX) drawTag("镜像");
    if (context.play.replayMode) drawTag("回放");
    if (std::abs(context.play.musicSpeed - 1.0F) > 0.001F) {
      char speed[32];
      std::snprintf(speed, sizeof(speed), "倍速 %.2fx", static_cast<double>(context.play.musicSpeed));
      drawTag(speed);
    }
  }

  // Histogram toggle and overlay.
  static bool histogramVisible = false;
  {
    const ImVec2 topLeft = point(kCanvasWidth - kCornerWidth - 130.0F, 20.0F);
    const char* label = histogramVisible ? "隐藏分布" : "判定分布";
    if (ui::cornerButton("##result_histogram", topLeft, ImVec2(126.0F * unit, 34.0F * unit), false,
            0, label, 15.0F * unit, nullptr)) {
      histogramVisible = !histogramVisible;
    }
  }
  if (histogramVisible) {
    drawHistogram(context, drawList, point(60.0F, 330.0F),
        ImVec2(length(kHistogramWidth), length(kHistogramHeight)), unit);
  }

  // Corner controls: retry top-left, replay/save and exit bottom-right.
  const ImVec2 cornerSize(kCornerWidth * unit, kCornerHeight * unit);
  if (!context.play.replayMode) {
    if (ui::cornerButton("##result_retry", ImVec2(0.0F, kCornerMargin * unit), cornerSize, true,
            assetTexture(context, "res/retry.png", 96), nullptr, 18.0F * unit, "重新开始")) {
      const PlaySetup setup = context.play;
      if (!setup.chartId.empty()) {
        startChart(context, setup.practice);
      } else {
        context.play = setup;
        if (gameEnter(context)) context.screen = Screen::Game;
      }
    }
  }
  if (ui::cornerButton("##result_exit",
          ImVec2(size.width - cornerSize.x, size.height - cornerSize.y - kCornerMargin * unit),
          cornerSize, false, assetTexture(context, "res/continue.png", 96), nullptr,
          18.0F * unit, "返回")) {
    navigateTo(context, Screen::Library);
  }

  // Replay controls sit just left of the exit button.
  if (context.lastRunHasReplay) {
    const ImVec2 smallSize(kSmallButtonWidth * unit, kSmallButtonHeight * unit);
    const float rowY = size.height - smallSize.y - 26.0F * unit;
    const float rightEdge = size.width - cornerSize.x - 12.0F * unit;
    if (!context.play.replayMode) {
      if (ui::cornerButton("##result_play_replay",
              ImVec2(rightEdge - smallSize.x * 2.0F - 6.0F * unit, rowY), smallSize, false,
              assetTexture(context, "res/replay.png", 64), nullptr, 14.0F * unit, "播放回放")) {
        playReplay(context);
      }
    }
    if (ui::cornerButton("##result_save_replay", ImVec2(rightEdge - smallSize.x, rowY), smallSize,
            false, assetTexture(context, "res/save.png", 64), nullptr, 14.0F * unit,
            "保存回放")) {
      saveReplay(context);
    }
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
