#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"

#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

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
    if (backButton(context, "##detail_back", scale)) {
      navigateTo(context, Screen::Library);
    }
    ui::endFullscreenWindow();
    return;
  }

  const auto point = [&](float x, float y) {
    return designPosition(ImVec2(size.width, size.height), scale, x, y);
  };

  // Background artwork with a dark scrim.
  const std::uint64_t background = coverTexture(context, *record, 1024);
  if (background != 0) {
    drawList->AddImage(static_cast<ImTextureID>(background), ImVec2(0.0F, 0.0F),
        ImVec2(size.width, size.height), ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F),
        IM_COL32(255, 255, 255, 88));
  }
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      IM_COL32(0, 0, 0, 138));

  // Title panel.
  const ImVec2 titleTopLeft = point(0.0F, 82.0F);
  const ImVec2 titleSize(342.0F * scale, 56.0F * scale);
  ui::drawPanel(drawList, titleTopLeft,
      ImVec2(titleTopLeft.x + titleSize.x, titleTopLeft.y + titleSize.y),
      titleSize.y * ui::kSkewRatio * 0.5F, IM_COL32(0, 0, 0, 184), IM_COL32(0, 0, 0, 0), 0.0F,
      true, false);
  char nameBuffer[200];
  std::snprintf(nameBuffer, sizeof(nameBuffer), "%.48s", record->songName.c_str());
  ui::drawText(drawList, ImVec2(titleTopLeft.x + 14.0F * scale,
                        titleTopLeft.y + titleSize.y * 0.5F - 12.0F * scale),
      22.0F * scale, ui::rgba(ui::kTextPrimary), nameBuffer);

  // Difficulty chip.
  const ImVec2 chipBackTopLeft = point(312.0F, 82.0F);
  ui::drawPanel(drawList, chipBackTopLeft,
      ImVec2(chipBackTopLeft.x + 116.0F * scale, chipBackTopLeft.y + 56.0F * scale),
      56.0F * scale * ui::kSkewRatio, IM_COL32(0, 0, 0, 184), IM_COL32(0, 0, 0, 0), 0.0F, true,
      false);
  const ImVec2 chipTopLeft = point(316.0F, 68.0F);
  const ImVec2 chipSize(108.0F * scale, 82.0F * scale);
  ui::drawPanel(drawList, chipTopLeft, ImVec2(chipTopLeft.x + chipSize.x,
                                          chipTopLeft.y + chipSize.y),
      chipSize.y * ui::kSkewRatio * 0.5F, IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 0), 0.0F,
      true, false);
  ui::drawTextCentered(drawList, ImVec2(chipTopLeft.x + chipSize.x * 0.5F,
                               chipTopLeft.y + chipSize.y * 0.5F),
      24.0F * scale, IM_COL32(5, 7, 10, 255), record->difficulty.c_str());

  // Charter and composer rows.
  const ImVec2 infoTopLeft = point(0.0F, 138.0F);
  const ImVec2 infoSize(318.0F * scale, 120.0F * scale);
  ui::drawPanel(drawList, infoTopLeft, ImVec2(infoTopLeft.x + infoSize.x,
                                         infoTopLeft.y + infoSize.y),
      infoSize.y * ui::kSkewRatio * 0.5F, IM_COL32(0, 0, 0, 122), IM_COL32(0, 0, 0, 0), 0.0F, true,
      false);
  const float rowLeft = infoTopLeft.x + 16.0F * scale;
  const float rowTop = infoTopLeft.y + 12.0F * scale;
  const char* labels[] = {"谱师", "曲师"};
  const std::string values[] = {orUnknown(record->info.charter), orUnknown(record->info.composer)};
  for (int index = 0; index < 2; ++index) {
    const float offset = static_cast<float>(index) * 52.0F * scale;
    ui::drawText(drawList, ImVec2(rowLeft, rowTop + offset), 14.0F * scale,
        IM_COL32(184, 190, 200, 255), labels[index]);
    ui::drawText(drawList, ImVec2(rowLeft, rowTop + offset + 18.0F * scale), 19.0F * scale,
        ui::rgba(ui::kTextPrimary), values[index].c_str());
  }

  // Best score panel.
  const ImVec2 scoreTopLeft = point(0.0F, 415.0F - 56.0F - 62.0F);
  const ImVec2 scoreSize(376.0F * scale, 62.0F * scale);
  ui::drawPanel(drawList, scoreTopLeft, ImVec2(scoreTopLeft.x + scoreSize.x,
                                          scoreTopLeft.y + scoreSize.y),
      scoreSize.y * ui::kSkewRatio * 0.5F, IM_COL32(0, 0, 0, 122), IM_COL32(0, 0, 0, 0), 0.0F,
      true, false);
  const float gradeSize = 44.0F * scale;
  drawGradeImage(context, drawList,
      ImVec2(scoreTopLeft.x + 16.0F * scale + gradeSize * 0.5F,
          scoreTopLeft.y + scoreSize.y * 0.5F),
      gradeSize, record->bestGradeIndex(), record->bestScore > 0 ? 1.0F : 0.35F);
  const std::string scoreText = record->bestScore > 0 ? formatScore(record->bestScore) : "0000000";
  ui::drawText(drawList, ImVec2(scoreTopLeft.x + 70.0F * scale,
                        scoreTopLeft.y + scoreSize.y * 0.5F - 20.0F * scale),
      36.0F * scale, ui::rgba(ui::kTextPrimary), scoreText.c_str());
  const std::string accuracyText =
      record->bestAcc >= 0 ? formatAccuracy(static_cast<double>(record->bestAcc) / 10000.0)
                           : "--.--%";
  drawTextRight(drawList, ImVec2(scoreTopLeft.x + 254.0F * scale + 96.0F * scale,
                            scoreTopLeft.y + scoreSize.y - 20.0F * scale),
      20.0F * scale, ui::rgba(ui::kTextSecondary), accuracyText.c_str());

  // Corner controls.
  if (backButton(context, "##detail_back", scale)) {
    navigateTo(context, Screen::Library);
  }
  const float iconWidth = 78.0F * scale;
  const float iconHeight = 48.0F * scale;
  if (ui::iconButton("##detail_info", assetTexture(context, "res/detail.png", 96),
          ImVec2(size.width - iconWidth, 18.0F * scale), ImVec2(iconWidth, iconHeight), "详细信息",
          false, true, true)) {
    ImGui::OpenPopup("##detail_info_popup");
  }
  if (ui::iconButton("##detail_settings", assetTexture(context, "res/settings.png", 96),
          ImVec2(size.width - iconWidth, 88.0F * scale), ImVec2(iconWidth, iconHeight), "设置",
          false, true, true)) {
    context.settingsTab = 0;
    context.settingsReturn = Screen::Detail;
    context.screen = Screen::Settings;
  }
  if (ui::iconButton("##detail_start", assetTexture(context, "res/start.png", 96),
          ImVec2(size.width - iconWidth, size.height - (58.0F + 48.0F) * scale),
          ImVec2(iconWidth, iconHeight), "开始游戏", false, true, true)) {
    startChart(context, false);
  }
  if (ui::iconButton("##detail_practice", assetTexture(context, "res/play.png", 96),
          ImVec2(size.width - iconWidth, size.height - 48.0F * scale),
          ImVec2(iconWidth, iconHeight), "练习模式", false, true, true)) {
    startChart(context, true);
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
