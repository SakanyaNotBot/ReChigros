#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

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

void drawHistogram(AppContext& context, ImDrawList* drawList, ImVec2 topLeft, ImVec2 size) {
  const auto& bins = context.lastResult.judgementOffsetBins;
  const int rangeMs = context.lastRunWasReplay
      ? (context.lastReplayMeta.challenge ? game::PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                                          : game::PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS)
      : (context.play.challenge ? game::PlayResult::JUDGEMENT_DETAIL_CHALLENGE_RANGE_MS
                                : game::PlayResult::JUDGEMENT_DETAIL_NORMAL_RANGE_MS);
  const float scale = layoutScale();
  drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
      IM_COL32(8, 11, 16, 216), 6.0F * scale);
  drawList->AddRect(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
      IM_COL32(255, 255, 255, 60), 6.0F * scale);
  const float graphLeft = topLeft.x + 26.0F * scale;
  const float graphRight = topLeft.x + size.x - 26.0F * scale;
  const float graphTop = topLeft.y + 42.0F * scale;
  const float baseline = topLeft.y + size.y - 34.0F * scale;
  const float labelHeight = 8.0F;
  int maximum = 1;
  for (const int count : bins) maximum = std::max(maximum, count);
  const int count = static_cast<int>(bins.size());
  const float slot = (graphRight - graphLeft) / static_cast<float>(count);
  drawList->AddLine(ImVec2(graphLeft, baseline), ImVec2(graphRight, baseline),
      IM_COL32(255, 255, 255, 60), 1.0F);
  for (int index = 0; index < count; ++index) {
    const float barWidth = slot * 0.62F;
    const float barHeight = bins[index] == 0
        ? labelHeight * scale
        : (static_cast<float>(bins[index]) / static_cast<float>(maximum)) * (baseline - graphTop)
            + labelHeight * scale;
    const float left = graphLeft + static_cast<float>(index) * slot + (slot - barWidth) * 0.5F;
    const ImU32 color = index == count / 2 ? IM_COL32(255, 255, 255, 255)
                                           : IM_COL32(201, 211, 230, 230);
    drawList->AddRectFilled(ImVec2(left, baseline - barHeight),
        ImVec2(left + barWidth, baseline), color, 2.0F * scale);
  }
  const int binWidthMs = std::max(1, (rangeMs * 2) / std::max(1, count - 1));
  const int peak = static_cast<int>(std::max_element(bins.begin(), bins.end()) - bins.begin());
  const int peakMs = -rangeMs + peak * binWidthMs;
  char title[96];
  std::snprintf(title, sizeof(title), "判定分布  (峰值 %+d ms)", peakMs);
  ui::drawText(drawList, ImVec2(topLeft.x + 14.0F * scale, topLeft.y + 12.0F * scale),
      17.0F * scale, ui::rgba(ui::kTextPrimary), title);
  ui::drawText(drawList, ImVec2(graphLeft, baseline + 8.0F * scale), 13.0F * scale,
      ui::rgba(ui::kTextMuted), "早");
  drawTextRight(drawList, ImVec2(graphRight, baseline + 14.0F * scale), 13.0F * scale,
      ui::rgba(ui::kTextMuted), "晚");
  char leftLabel[24];
  std::snprintf(leftLabel, sizeof(leftLabel), "-%d ms", rangeMs);
  char rightLabel[24];
  std::snprintf(rightLabel, sizeof(rightLabel), "+%d ms", rangeMs);
  ui::drawText(drawList, ImVec2(graphLeft, baseline + 22.0F * scale), 12.0F * scale,
      ui::rgba(ui::kTextMuted), leftLabel);
  drawTextRight(drawList, ImVec2(graphRight, baseline + 28.0F * scale), 12.0F * scale,
      ui::rgba(ui::kTextMuted), rightLabel);
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

  const auto point = [&](float x, float y) {
    return designPosition(ImVec2(size.width, size.height), scale, x, y);
  };
  const game::PlayResult& result = context.lastResult;

  // Header.
  const ImVec2 titleCenter = point(365.0F, 44.0F);
  char songBuffer[160];
  std::snprintf(songBuffer, sizeof(songBuffer), "%.48s", context.play.songName.c_str());
  ui::drawTextCentered(drawList, titleCenter, 26.0F * scale, ui::rgba(ui::kTextPrimary), songBuffer);
  ui::drawTextCentered(drawList, point(365.0F, 72.0F), 17.0F * scale,
      ui::rgba(ui::kTextSecondary), context.play.difficulty.c_str());

  // Rank and score.
  const int grade = gradeIndexForScore(result.score, result.isFullCombo());
  drawGradeImage(context, drawList, point(150.0F, 168.0F), 132.0F * scale, grade);
  const std::string scoreText = formatScore(result.score);
  ui::drawText(drawList, point(238.0F, 128.0F), 42.0F * scale, ui::rgba(ui::kTextPrimary),
      scoreText.c_str());
  const std::string accuracyText = formatAccuracy(result.accuracy);
  ui::drawText(drawList, point(240.0F, 180.0F), 22.0F * scale, ui::rgba(ui::kTextSecondary),
      accuracyText.c_str());
  char comboBuffer[48];
  std::snprintf(comboBuffer, sizeof(comboBuffer), "最大连击 %d", result.maxCombo);
  ui::drawText(drawList, point(240.0F, 210.0F), 17.0F * scale, ui::rgba(ui::kTextMuted),
      comboBuffer);
  char deviationBuffer[64];
  std::snprintf(deviationBuffer, sizeof(deviationBuffer), "标准差 %.3f ms", result.stdDevMs);
  ui::drawText(drawList, point(240.0F, 234.0F), 17.0F * scale, ui::rgba(ui::kTextMuted),
      deviationBuffer);

  // Mode tags.
  float tagX = 240.0F;
  const auto drawTag = [&](const char* label) {
    const ImVec2 extent = ui::measureText(label, 14.0F * scale);
    const ImVec2 tagTopLeft = point(tagX, 260.0F);
    drawList->AddRectFilled(tagTopLeft,
        ImVec2(tagTopLeft.x + extent.x + 16.0F * scale, tagTopLeft.y + 22.0F * scale),
        IM_COL32(255, 255, 255, 34), 3.0F);
    ui::drawText(drawList, ImVec2(tagTopLeft.x + 8.0F * scale, tagTopLeft.y + 3.0F * scale),
        14.0F * scale, ui::rgba(ui::kTextSecondary), label);
    tagX += extent.x / scale + 26.0F;
  };
  if (context.play.autoplay) drawTag("自动演示");
  if (context.play.challenge) drawTag("挑战模式");
  if (context.play.practice) drawTag("练习");
  if (context.play.mirrorX) drawTag("镜像");
  if (context.play.replayMode) drawTag("回放");
  if (std::abs(context.play.musicSpeed - 1.0F) > 0.001F) {
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%.2fx", static_cast<double>(context.play.musicSpeed));
    drawTag(speed);
  }

  // Judgement counts.
  struct CountEntry {
    const char* label;
    int value;
    std::uint32_t color;
  };
  const CountEntry counts[] = {
      {"PERFECT", result.perfect, 0xFFFEFFA9U},
      {"GOOD", result.good, 0xFFA2EEFFU},
      {"BAD", result.bad, 0xFFE0A2FFU},
      {"MISS", result.miss, 0xFFFF8E8EU},
  };
  const float boxWidth = 300.0F * scale;
  const float boxHeight = 54.0F * scale;
  for (int index = 0; index < 4; ++index) {
    const ImVec2 boxTopLeft = point(420.0F, 108.0F + static_cast<float>(index) * 66.0F);
    drawList->AddRectFilled(boxTopLeft,
        ImVec2(boxTopLeft.x + boxWidth, boxTopLeft.y + boxHeight), IM_COL32(0, 0, 0, 120),
        4.0F);
    ui::drawText(drawList, ImVec2(boxTopLeft.x + 16.0F * scale, boxTopLeft.y + 16.0F * scale),
        18.0F * scale, ui::rgba(counts[index].color), counts[index].label);
    drawTextRight(drawList, ImVec2(boxTopLeft.x + boxWidth - 16.0F * scale,
                             boxTopLeft.y + boxHeight * 0.5F),
        24.0F * scale, ui::rgba(ui::kTextPrimary), std::to_string(counts[index].value).c_str());
  }
  char earlyLateBuffer[64];
  std::snprintf(earlyLateBuffer, sizeof(earlyLateBuffer), "提前 %d  /  延后 %d", result.goodEarly,
      result.goodLate);
  ui::drawText(drawList, point(420.0F, 372.0F), 15.0F * scale, ui::rgba(ui::kTextMuted),
      earlyLateBuffer);

  // Histogram toggle.
  static bool histogramVisible = false;
  const ImVec2 histogramButtonTopLeft = point(596.0F, 26.0F);
  if (ui::skewButton("##result_histogram", histogramVisible ? "隐藏判定分布" : "判定分布",
          histogramButtonTopLeft, ImVec2(126.0F * scale, 36.0F * scale), 15.0F * scale, -1.0F,
          ui::rgba(ui::kPanelFill), IM_COL32(255, 255, 255, 120), false, true)) {
    histogramVisible = !histogramVisible;
  }
  if (histogramVisible) {
    drawHistogram(context, drawList, point(120.0F, 290.0F), ImVec2(500.0F * scale, 170.0F * scale));
  }

  // Action buttons.
  const float buttonWidth = 78.0F * scale;
  const float buttonHeight = 48.0F * scale;
  const bool isReplayRun = context.play.replayMode;
  if (!isReplayRun) {
    if (ui::iconButton("##result_retry", assetTexture(context, "res/retry.png", 96),
            ImVec2(0.0F, size.height - buttonHeight - 12.0F * scale),
            ImVec2(buttonWidth, buttonHeight), "重新开始", true, false, true)) {
      const PlaySetup setup = context.play;
      if (!setup.chartId.empty()) {
        startChart(context, setup.practice);
      } else {
        context.play = setup;
        if (gameEnter(context)) context.screen = Screen::Game;
      }
    }
  }
  if (ui::iconButton("##result_exit", assetTexture(context, "res/continue.png", 96),
          ImVec2(size.width - buttonWidth, size.height - buttonHeight - 12.0F * scale),
          ImVec2(buttonWidth, buttonHeight), "返回", false, true, false)) {
    navigateTo(context, Screen::Library);
  }

  if (context.lastRunHasReplay) {
    const float chipWidth = 132.0F * scale;
    const float chipHeight = 44.0F * scale;
    const float rowY = size.height - chipHeight - 18.0F * scale;
    const float centerX = size.width * 0.5F;
    if (!isReplayRun) {
      if (ui::skewButton("##result_play_replay", "播放回放",
              ImVec2(centerX - chipWidth - 8.0F * scale, rowY), ImVec2(chipWidth, chipHeight),
              18.0F * scale, -1.0F, ui::rgba(ui::kPanelFill), IM_COL32(255, 255, 255, 140),
              false, true)) {
        playReplay(context);
      }
    }
    if (ui::skewButton("##result_save_replay", "保存回放",
            ImVec2(centerX + 8.0F * scale, rowY), ImVec2(chipWidth, chipHeight), 18.0F * scale,
            -1.0F, ui::rgba(ui::kPanelFill), IM_COL32(255, 255, 255, 140), false, true)) {
      saveReplay(context);
    }
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
