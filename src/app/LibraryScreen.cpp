#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

bool matchesSearch(const ChartRecord& record, const std::string& query) {
  if (query.empty()) return true;
  const std::string haystack = toLower(record.songName + " " + record.difficulty);
  return haystack.find(toLower(query)) != std::string::npos;
}

void drawChartCard(AppContext& context, const ChartRecord& record, ImVec2 topLeft, float cardWidth,
    float cardHeight, bool selected, bool manageMode) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + cardWidth, topLeft.y + cardHeight);
  const float scale = layoutScale();
  const float coverHeight = cardWidth * 0.86F;

  ImGui::SetCursorScreenPos(topLeft);
  const std::string id = "##card_" + record.id;
  ImGui::InvisibleButton(id.c_str(), ImVec2(cardWidth, cardHeight));
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked()) {
    if (manageMode) {
      const auto iterator = std::find(context.selectedIds.begin(), context.selectedIds.end(),
          record.id);
      if (iterator == context.selectedIds.end()) {
        context.selectedIds.push_back(record.id);
      } else {
        context.selectedIds.erase(iterator);
      }
    } else {
      openChartDetail(context, record.id);
    }
  }

  const ImU32 fill = hovered ? IM_COL32(40, 48, 62, 210) : IM_COL32(24, 30, 40, 176);
  ui::drawPanel(drawList, topLeft, bottomRight, cardHeight * ui::kSkewRatio * 0.5F, fill,
      selected ? ui::rgba(ui::kAccent) : IM_COL32(255, 255, 255, 40), selected ? 2.5F : 1.0F,
      false, true);

  // Cover artwork fills the upper part of the card.
  const std::uint64_t cover = coverTexture(context, record, 256);
  const ImVec2 coverMin(topLeft.x + 5.0F * scale, topLeft.y + 5.0F * scale);
  const ImVec2 coverMax(bottomRight.x - 9.0F * scale - cardHeight * ui::kSkewRatio * 0.5F,
      coverMin.y + coverHeight - 10.0F * scale);
  if (cover != 0) {
    drawList->AddImage(static_cast<ImTextureID>(cover), coverMin, coverMax);
  } else {
    drawList->AddRectFilled(coverMin, coverMax, IM_COL32(38, 46, 58, 255));
    ui::drawTextCentered(drawList, ImVec2((coverMin.x + coverMax.x) * 0.5F,
                              (coverMin.y + coverMax.y) * 0.5F),
        16.0F * scale, IM_COL32(150, 165, 185, 200), "NO COVER");
  }

  // Difficulty chip over the cover.
  const float chipHeight = 24.0F * scale;
  const ImVec2 chipMin(coverMin.x + 6.0F * scale, coverMax.y - chipHeight - 4.0F * scale);
  const std::string difficulty = record.difficulty.empty() ? "SP Lv.?" : record.difficulty;
  const ImVec2 chipTextExtent = ui::measureText(difficulty.c_str(), 14.0F * scale);
  drawList->AddRectFilled(chipMin,
      ImVec2(chipMin.x + chipTextExtent.x + 12.0F * scale, chipMin.y + chipHeight),
      IM_COL32(0, 0, 0, 190), 3.0F);
  ui::drawText(drawList, ImVec2(chipMin.x + 6.0F * scale, chipMin.y + 4.0F * scale),
      14.0F * scale, ui::rgba(ui::kTextPrimary), difficulty.c_str());

  // Song name.
  char nameBuffer[160];
  std::snprintf(nameBuffer, sizeof(nameBuffer), "%.42s", record.songName.c_str());
  ui::drawText(drawList, ImVec2(topLeft.x + 8.0F * scale, coverMax.y + 8.0F * scale),
      17.0F * scale, ui::rgba(ui::kTextPrimary), nameBuffer);

  // Grade badge and best score.
  const float badgeSize = 30.0F * scale;
  const ImVec2 badgeCenter(bottomRight.x - badgeSize * 0.5F - 12.0F * scale,
      coverMax.y + 14.0F * scale + badgeSize * 0.5F);
  drawGradeImage(context, drawList, badgeCenter, badgeSize, record.bestGradeIndex(),
      record.bestScore > 0 ? 1.0F : 0.35F);
  const std::string scoreText = record.bestScore > 0 ? formatScore(record.bestScore) : "-------";
  ui::drawText(drawList, ImVec2(topLeft.x + 8.0F * scale, coverMax.y + 30.0F * scale),
      15.0F * scale, ui::rgba(ui::kTextSecondary), scoreText.c_str());

  if (manageMode) {
    const ImVec2 boxMin(topLeft.x + cardWidth - 26.0F * scale, topLeft.y + 8.0F * scale);
    drawList->AddRectFilled(boxMin,
        ImVec2(boxMin.x + 18.0F * scale, boxMin.y + 18.0F * scale),
        selected ? ui::rgba(ui::kAccent) : IM_COL32(0, 0, 0, 150), 3.0F);
    if (selected) {
      ui::drawTextCentered(drawList,
          ImVec2(boxMin.x + 9.0F * scale, boxMin.y + 8.0F * scale), 15.0F * scale,
          IM_COL32(255, 255, 255, 255), "v");
    }
  }
}

} // namespace

void drawLibraryScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##library");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  if (backButton(context, "##library_back", scale)) {
    navigateTo(context, Screen::Home);
  }

  // Tabs: local charts and the (inactive) online catalogue.
  const char* tabLabels[] = {"本地谱面", "在线谱面"};
  for (int index = 0; index < 2; ++index) {
    const bool selected = context.libraryTab == index;
    const ImVec2 tabTopLeft((150.0F + static_cast<float>(index) * 132.0F) * scale, 11.0F * scale);
    const std::string id = "##library_tab_" + std::to_string(index);
    if (ui::skewButton(id.c_str(), tabLabels[index], tabTopLeft,
            ImVec2(126.0F * scale, 42.0F * scale), 20.0F * scale, -1.0F,
            selected ? IM_COL32(215, 222, 232, 230) : ui::rgba(ui::kPanelFill),
            IM_COL32(255, 255, 255, 90), false, true)) {
      if (context.libraryTab != index) {
        context.libraryTab = index;
        context.manageMode = false;
        context.selectedIds.clear();
      }
    }
  }

  // Search field.
  const ImVec2 searchTopLeft(size.width - (98.0F + 172.0F) * scale, 14.0F * scale);
  const ImVec2 searchSize(172.0F * scale, 36.0F * scale);
  ImGui::SetCursorScreenPos(searchTopLeft);
  const bool searchClicked = ImGui::InvisibleButton("##search_field", searchSize);
  ui::drawPanel(drawList, searchTopLeft,
      ImVec2(searchTopLeft.x + searchSize.x, searchTopLeft.y + searchSize.y),
      searchSize.y * ui::kSkewRatio, IM_COL32(231, 235, 240, 158), IM_COL32(255, 255, 255, 110),
      1.0F, false, true);
  ui::drawText(drawList, ImVec2(searchTopLeft.x + 16.0F * scale, searchTopLeft.y + 9.0F * scale),
      17.0F * scale, ui::rgba(ui::kTextPrimary),
      context.searchQuery.empty() ? "搜索曲名" : context.searchQuery.c_str());
  if (searchClicked) ImGui::OpenPopup("##search_popup");

  // List panel.
  const ImVec2 panelTopLeft(72.0F * scale, 74.0F * scale);
  const ImVec2 panelSize(size.width - (72.0F + 98.0F) * scale, size.height - 80.0F * scale);
  drawList->AddRectFilled(panelTopLeft,
      ImVec2(panelTopLeft.x + panelSize.x, panelTopLeft.y + panelSize.y),
      IM_COL32(7, 10, 16, 123));

  const float gridPadding = 8.0F * scale;
  const float gridWidth = panelSize.x - gridPadding * 2.0F;
  const int columns = 4;
  const float gap = 9.0F * scale;
  const float cardWidth = (gridWidth - gap * static_cast<float>(columns - 1))
      / static_cast<float>(columns);
  const float cardHeight = cardWidth * 0.86F + 52.0F * scale;

  ImGui::SetCursorScreenPos(ImVec2(panelTopLeft.x + gridPadding, panelTopLeft.y + gridPadding));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
  ImGui::BeginChild("##library_grid", ImVec2(panelSize.x - gridPadding * 2.0F,
                        panelSize.y - gridPadding * 2.0F),
      false, ImGuiWindowFlags_NoBackground);
  const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

  if (context.libraryTab == 0) {
    std::vector<const ChartRecord*> visible;
    const auto& records = context.library.records();
    for (const auto& record : records) {
      if (matchesSearch(record, context.searchQuery)) visible.push_back(&record);
    }
    const auto rows = static_cast<int>((visible.size() + static_cast<std::size_t>(columns) - 1)
        / static_cast<std::size_t>(columns));
    const float contentHeight = static_cast<float>(std::max(0, rows)) * (cardHeight + gap);
    ImGui::Dummy(ImVec2(gridWidth, std::max(contentHeight, 1.0F)));
    for (std::size_t index = 0; index < visible.size(); ++index) {
      const auto row = static_cast<int>(index) / columns;
      const int column = static_cast<int>(index) % columns;
      const ImVec2 cellTopLeft(gridOrigin.x + static_cast<float>(column) * (cardWidth + gap),
          gridOrigin.y + static_cast<float>(row) * (cardHeight + gap));
      const bool selected = std::find(context.selectedIds.begin(), context.selectedIds.end(),
                                visible[index]->id)
          != context.selectedIds.end();
      drawChartCard(context, *visible[index], cellTopLeft, cardWidth, cardHeight, selected,
          context.manageMode);
    }
    if (visible.empty()) {
      const ImVec2 center(gridOrigin.x + gridWidth * 0.5F,
          gridOrigin.y + (panelSize.y - gridPadding * 2.0F) * 0.45F);
      ui::drawTextCentered(drawList, center, 20.0F * scale, ui::rgba(ui::kTextSecondary),
          context.searchQuery.empty() ? "本地暂无谱面" : "没有匹配的谱面");
    } else if (context.manageMode) {
      const std::string state = "已选择 " + std::to_string(context.selectedIds.size()) + " / "
          + std::to_string(visible.size());
      ui::drawText(drawList, ImVec2(panelTopLeft.x + 12.0F * scale,
                           panelTopLeft.y + 8.0F * scale),
          15.0F * scale, ui::rgba(ui::kTextSecondary), state.c_str());
    }
  } else {
    const ImVec2 center(gridOrigin.x + gridWidth * 0.5F,
        gridOrigin.y + (panelSize.y - gridPadding * 2.0F) * 0.42F);
    ui::drawTextCentered(drawList, center, 21.0F * scale, ui::rgba(ui::kTextSecondary),
        "在线谱面目录未在此构建中启用");
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // Action column.
  const float actionWidth = 78.0F * scale;
  const float actionHeight = 48.0F * scale;
  const float actionRight = size.width - actionWidth;
  const float actionBottom = size.height - 8.0F * scale;
  const auto drawAction = [&](const char* id, const char* asset, float bottomOffset,
                              const char* tooltip) {
    const std::uint64_t texture = assetTexture(context, asset, 96);
    return ui::iconButton(id, texture,
        ImVec2(actionRight, actionBottom - bottomOffset - actionHeight), ImVec2(actionWidth,
            actionHeight),
        tooltip, false, true, true);
  };

  if (context.libraryTab == 0) {
    if (!context.manageMode) {
      if (drawAction("##manage", "res/edit.png", 58.0F * scale, "管理")) {
        context.manageMode = true;
        context.selectedIds.clear();
      }
      if (drawAction("##import", "res/import.png", 0.0F, "导入谱面包")) {
        ImGui::OpenPopup("##import_popup");
      }
    } else {
      if (drawAction("##delete", "res/delete.png", 116.0F * scale, "删除选中")) {
        if (context.selectedIds.empty()) {
          notify(context, "未选择谱面");
        } else {
          ImGui::OpenPopup("##delete_popup");
        }
      }
      if (drawAction("##select_all", "res/all.png", 58.0F * scale, "全选")) {
        std::vector<std::string> visibleIds;
        for (const auto& record : context.library.records()) {
          if (matchesSearch(record, context.searchQuery)) visibleIds.push_back(record.id);
        }
        if (context.selectedIds.size() >= visibleIds.size()) {
          context.selectedIds.clear();
        } else {
          context.selectedIds = std::move(visibleIds);
        }
      }
      if (drawAction("##cancel_manage", "res/cancel.png", 0.0F, "取消")) {
        context.manageMode = false;
        context.selectedIds.clear();
      }
    }
  }

  // Search dialog.
  ImGui::SetNextWindowSize(ImVec2(380.0F * scale, 0.0F), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.4F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##search_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted("搜索曲名");
    static char buffer[160] = {};
    if (ImGui::IsWindowAppearing()) {
      std::snprintf(buffer, sizeof(buffer), "%s", context.searchQuery.c_str());
    }
    ImGui::SetNextItemWidth(340.0F * scale);
    if (ImGui::InputText("##search_text", buffer, sizeof(buffer))) {
      context.searchQuery = buffer;
    }
    if (ImGui::Button("确定", ImVec2(120.0F * scale, 36.0F * scale))) {
      context.searchQuery = buffer;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("清除", ImVec2(120.0F * scale, 36.0F * scale))) {
      context.searchQuery.clear();
      buffer[0] = '\0';
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Import dialog: the archive path is typed or dropped onto the window.
  ImGui::SetNextWindowSize(ImVec2(520.0F * scale, 0.0F), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.45F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##import_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted("导入谱面包 (.zip)");
    ImGui::TextDisabled("也可以直接把 zip 文件拖入窗口");
    static char pathBuffer[512] = {};
    ImGui::SetNextItemWidth(480.0F * scale);
    ImGui::InputText("##import_path", pathBuffer, sizeof(pathBuffer));
    if (ImGui::Button("导入", ImVec2(130.0F * scale, 38.0F * scale))) {
      std::filesystem::path path = pathBuffer;
      try {
        const auto result = context.library.importZip(path);
        notify(context, result.imported ? "已导入谱面" : "谱面包已在库中");
        ImGui::CloseCurrentPopup();
      } catch (const std::exception& exception) {
        notify(context, std::string("导入失败: ") + exception.what());
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(130.0F * scale, 38.0F * scale))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Delete confirmation.
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.45F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##delete_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    ImGui::Text("确定删除选中的 %d 个谱面吗？", static_cast<int>(context.selectedIds.size()));
    if (ImGui::Button("删除", ImVec2(130.0F * scale, 38.0F * scale))) {
      context.library.remove(context.selectedIds);
      context.selectedIds.clear();
      context.manageMode = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消", ImVec2(130.0F * scale, 38.0F * scale))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
