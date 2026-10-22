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

// Every position below is a design unit on the reference's 731x415 canvas.
constexpr float kBackWidth = 78.0F;
constexpr float kBackHeight = 48.0F;
constexpr float kBackTop = 8.0F;
constexpr float kTitleLeft = 0.0F;
constexpr float kTitleTop = 82.0F;
constexpr float kTitleWidth = 342.0F;
constexpr float kTitleHeight = 56.0F;
constexpr float kInfoLeft = 0.0F;
constexpr float kInfoTop = 138.0F;
constexpr float kInfoWidth = 318.0F;
constexpr float kInfoHeight = 120.0F;
constexpr float kChipBackLeft = 312.0F;
constexpr float kChipBackTop = 82.0F;
constexpr float kChipBackWidth = 116.0F;
constexpr float kChipBackHeight = 56.0F;
constexpr float kChipLeft = 316.0F;
constexpr float kChipTop = 68.0F;
constexpr float kChipWidth = 108.0F;
constexpr float kChipHeight = 82.0F;
constexpr float kScoreWidth = 376.0F;
constexpr float kScoreHeight = 62.0F;
constexpr float kScoreBottom = 56.0F;
constexpr float kCoverAlpha = 0.34F;
constexpr float kGradeSize = 44.0F;

std::string orUnknown(const std::string& value) {
  return value.empty() ? "未知" : value;
}

} // namespace

void drawDetailScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##detail");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  const auto record = context.library.find(context.activeChartId);
  if (!record) {
    ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, size.height * 0.5F),
        22.0F * scale, ui::rgba(ui::kTextSecondary), "谱面不存在或已删除");
    if (ui::cornerButton("##detail_back", ImVec2(0.0F, kBackTop * scale),
            ImVec2(kBackWidth * scale, kBackHeight * scale), true,
            assetTexture(context, "res/return.png", 96), nullptr, 20.0F * scale, "返回")) {
      navigateTo(context, Screen::Library);
    }
    ui::endFullscreenWindow();
    return;
  }

  const ui::DesignCanvas canvas = ui::designCanvas(ImVec2(size.width, size.height),
      ui::kDetailCanvasWidth, ui::kDetailCanvasHeight);
  const float unit = canvas.scale;
  const auto point = [&](float x, float y) { return ui::canvasPoint(canvas, x, y); };
  const auto length = [&](float value) { return value * unit; };

  // Chart artwork as the page background, dimmed so the panels read against it.
  const std::uint64_t background = coverTexture(context, *record, 1200);
  if (background != 0) {
    drawList->AddImage(static_cast<ImTextureID>(background), ImVec2(0.0F, 0.0F),
        ImVec2(size.width, size.height), ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F),
        IM_COL32(255, 255, 255, static_cast<int>(255.0F * kCoverAlpha)));
  }
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      ui::rgba(ui::kDetailCoverShade));

  // Title plate, flush to the left edge.
  const ImVec2 titleTopLeft = point(kTitleLeft, kTitleTop);
  ui::skewQuad(drawList, titleTopLeft.x, titleTopLeft.y, titleTopLeft.x + length(kTitleWidth),
      titleTopLeft.y + length(kTitleHeight), length(kTitleHeight) * ui::kSkewRatio,
      ui::rgba(ui::kDetailTitlePanel));
  {
    char nameBuffer[200];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%.48s", record->songName.c_str());
    ui::drawClippedText(drawList,
        ImVec2(titleTopLeft.x + length(14.0F),
            titleTopLeft.y + (length(kTitleHeight) - 22.0F * unit) * 0.5F),
        titleTopLeft,
        ImVec2(titleTopLeft.x + length(kTitleWidth), titleTopLeft.y + length(kTitleHeight)),
        22.0F * unit, ui::rgba(ui::kTextPrimary), nameBuffer);
  }

  // Charter and composer rows.
  const ImVec2 infoTopLeft = point(kInfoLeft, kInfoTop);
  ui::skewQuad(drawList, infoTopLeft.x, infoTopLeft.y, infoTopLeft.x + length(kInfoWidth),
      infoTopLeft.y + length(kInfoHeight), length(kInfoHeight) * ui::kSkewRatio,
      ui::rgba(ui::kDetailInfoPanel));
  {
    const char* labels[] = {"charter", "composer"};
    const std::string values[] = {orUnknown(record->info.charter),
        orUnknown(record->info.composer)};
    const ImVec2 clipMin = infoTopLeft;
    const ImVec2 clipMax(infoTopLeft.x + length(kInfoWidth), infoTopLeft.y + length(kInfoHeight));
    for (int index = 0; index < 2; ++index) {
      const float rowTop = infoTopLeft.y + length(8.0F)
          + static_cast<float>(index) * length(56.0F);
      const float left = infoTopLeft.x + length(14.0F);
      ui::drawClippedText(drawList, ImVec2(left, rowTop), clipMin, clipMax, 15.0F * unit,
          ui::rgba(0xB8FFFFFFU), labels[index]);
      ui::drawClippedText(drawList, ImVec2(left, rowTop + length(18.0F)), clipMin, clipMax,
          18.0F * unit, ui::rgba(ui::kTextPrimary),
          ui::ellipsize(values[index], 18.0F * unit, length(kInfoWidth) - length(28.0F)));
    }
  }

  // Difficulty chip: a dark backing plate behind a bright plate holding the label.
  const ImVec2 chipBackTopLeft = point(kChipBackLeft, kChipBackTop);
  ui::skewQuad(drawList, chipBackTopLeft.x, chipBackTopLeft.y,
      chipBackTopLeft.x + length(kChipBackWidth), chipBackTopLeft.y + length(kChipBackHeight),
      length(kChipBackHeight) * ui::kSkewRatio, ui::rgba(ui::kDetailTitlePanel));
  const ImVec2 chipTopLeft = point(kChipLeft, kChipTop);
  ui::skewQuad(drawList, chipTopLeft.x, chipTopLeft.y, chipTopLeft.x + length(kChipWidth),
      chipTopLeft.y + length(kChipHeight), length(kChipHeight) * ui::kSkewRatio,
      ui::rgba(0xFFFFFFFFU));
  ui::drawTextCentered(drawList, ImVec2(chipTopLeft.x + length(kChipWidth) * 0.5F
                                - length(kChipHeight) * ui::kSkewRatio * 0.5F,
                              chipTopLeft.y + length(kChipHeight) * 0.5F),
      20.0F * unit, IM_COL32(5, 7, 10, 255), record->difficulty.c_str());

  // Best score plate along the bottom-left.
  const ImVec2 scoreTopLeft = point(0.0F, ui::kDetailCanvasHeight - kScoreBottom - kScoreHeight);
  ui::skewQuad(drawList, scoreTopLeft.x, scoreTopLeft.y, scoreTopLeft.x + length(kScoreWidth),
      scoreTopLeft.y + length(kScoreHeight), length(kScoreHeight) * ui::kSkewRatio,
      ui::rgba(ui::kDetailInfoPanel));
  {
    const float gradeSize = length(kGradeSize);
    const ImVec2 gradeCenter(scoreTopLeft.x + length(16.0F) + gradeSize * 0.5F,
        scoreTopLeft.y + length(kScoreHeight) * 0.5F);
    drawGradeImage(context, drawList, gradeCenter, gradeSize, record->bestGradeIndex(),
        record->bestScore > 0 ? 1.0F : 0.68F);
    const std::string scoreText = record->bestScore > 0 ? formatScore(record->bestScore)
                                                        : "0000000";
    ui::drawText(drawList, ImVec2(scoreTopLeft.x + length(70.0F),
                            scoreTopLeft.y + length(kScoreHeight) * 0.5F - 20.0F * unit),
        36.0F * unit, ui::rgba(ui::kTextPrimary), scoreText.c_str());
    const std::string accuracyText = record->bestAcc >= 0
        ? formatAccuracy(static_cast<double>(record->bestAcc) / 10000.0)
        : "--.--%";
    drawTextRight(drawList, ImVec2(scoreTopLeft.x + length(254.0F + 96.0F),
                             scoreTopLeft.y + length(kScoreHeight) - length(12.0F)),
        20.0F * unit, ui::rgba(ui::kTextSecondary), accuracyText.c_str());
  }

  // Corner controls: back, information, settings, and start (long press = practice).
  if (ui::cornerButton("##detail_back", ImVec2(0.0F, kBackTop * scale),
          ImVec2(kBackWidth * scale, kBackHeight * scale), true,
          assetTexture(context, "res/return.png", 96), nullptr, 20.0F * scale, "返回")) {
    navigateTo(context, Screen::Library);
  }
  const ImVec2 cornerSize(kBackWidth * scale, kBackHeight * scale);
  if (ui::cornerButton("##detail_info", ImVec2(size.width - cornerSize.x, 18.0F * scale),
          cornerSize, false, assetTexture(context, "res/detail.png", 96), nullptr,
          18.0F * scale, "详细信息")) {
    ImGui::OpenPopup("##detail_info_popup");
  }
  if (ui::cornerButton("##detail_settings", ImVec2(size.width - cornerSize.x, 88.0F * scale),
          cornerSize, false, assetTexture(context, "res/settings.png", 96), nullptr,
          18.0F * scale, "设置")) {
    context.settingsTab = 0;
    context.settingsReturn = Screen::Detail;
    context.screen = Screen::Settings;
  }

  // Start button: a tap starts the chart, a long press starts practice.
  {
    const ImVec2 topLeft(size.width - cornerSize.x,
        size.height - (58.0F + kBackHeight) * scale);
    ImGui::SetCursorScreenPos(topLeft);
    ImGui::InvisibleButton("##detail_start", cornerSize);
    const bool hovered = ImGui::IsItemHovered();
    static double pressStartSec = -1.0;
    static bool practiceTriggered = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (pressStartSec < 0.0) {
        pressStartSec = context.nowSec;
        practiceTriggered = false;
      } else if (!practiceTriggered && context.nowSec - pressStartSec > 0.45) {
        practiceTriggered = true;
        startChart(context, true);
      }
    } else if (ImGui::IsItemDeactivated()) {
      if (pressStartSec >= 0.0 && !practiceTriggered) startChart(context, false);
      pressStartSec = -1.0;
      practiceTriggered = false;
    }
    // Plate drawn after the hit test so the press state can tint it.
    const float skew = cornerSize.y * ui::kSkewRatio;
    ui::skewQuad(drawList, topLeft.x, topLeft.y, topLeft.x + cornerSize.x,
        topLeft.y + cornerSize.y, skew,
        ui::rgba(hovered ? ui::kIconButtonFillHover : ui::kIconButtonFill));
    const std::uint64_t icon = assetTexture(context, "res/start.png", 96);
    const float glyph = std::min(cornerSize.x - skew, cornerSize.y) * 0.52F;
    const ImVec2 center(topLeft.x + cornerSize.x * 0.5F - skew * 0.5F,
        topLeft.y + cornerSize.y * 0.5F);
    if (icon != 0) {
      drawList->AddImage(static_cast<ImTextureID>(icon),
          ImVec2(center.x - glyph * 0.5F, center.y - glyph * 0.5F),
          ImVec2(center.x + glyph * 0.5F, center.y + glyph * 0.5F));
    } else {
      ui::drawTextCentered(drawList, center, 18.0F * scale, ui::rgba(ui::kTextPrimary), "开始");
    }
  }

  // Detail information dialog.
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.5F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##detail_info_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    ImGui::Text("曲名: %s", record->songName.c_str());
    ImGui::Text("难度: %s", record->difficulty.c_str());
    ImGui::Text("谱师: %s", orUnknown(record->info.charter).c_str());
    ImGui::Text("曲师: %s", orUnknown(record->info.composer).c_str());
    ImGui::Text("画师: %s", orUnknown(record->info.illustrator).c_str());
    ImGui::Text("音乐: %s", record->musicFile.filename().string().c_str());
    ImGui::Text("谱面: %s", record->chartFile.filename().string().c_str());
    ImGui::Text("偏移: %d ms", record->chartOffsetMs);
    if (ImGui::Button("确定", ImVec2(120.0F * scale, 36.0F * scale))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
