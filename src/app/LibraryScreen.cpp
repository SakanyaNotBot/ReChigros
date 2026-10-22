#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

// Layout metrics, in design units of the list canvas.
constexpr float kBackWidth = 78.0F;
constexpr float kBackHeight = 48.0F;
constexpr float kBackTop = 8.0F;
constexpr float kTabsLeft = 170.0F;
constexpr float kTabsTop = 11.0F;
constexpr float kTabsWidth = 330.0F;
constexpr float kTabsHeight = 42.0F;
constexpr float kSearchTop = 14.0F;
constexpr float kPanelLeft = 72.0F;
constexpr float kPanelTop = 74.0F;
constexpr float kPanelRight = 98.0F;
constexpr float kPanelBottom = 6.0F;
constexpr float kGridInset = 8.0F;
constexpr float kCardMargin = 5.0F;
constexpr int kGridColumns = 4;
// Card internals, as fractions of the 16:9 card so they follow its width.
constexpr float kCardDifficultyHeight = 28.0F;
constexpr float kCardNameHeight = 32.0F;
constexpr float kCardNameBottomMargin = 4.0F;
constexpr float kCardCheckWidth = 28.0F;
constexpr float kCardCheckHeight = 20.0F;
constexpr float kCoverAlpha = 0.72F;
constexpr float kStateTextSize = 18.0F;
constexpr double kTabAnimationSeconds = 0.34;

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

// Tracks a tab change over time so the selected plate slides instead of jumping.
struct TabAnimation {
  int from{};
  int to{};
  double startSec{-1.0};

  void begin(int target, double nowSec) {
    from = to;
    to = target;
    startSec = nowSec;
  }

  [[nodiscard]] float progress(double nowSec) const {
    if (startSec < 0.0 || from == to) return 1.0F;
    const float t = static_cast<float>((nowSec - startSec) / kTabAnimationSeconds);
    if (t >= 1.0F) return 1.0F;
    const float clamped = std::max(0.0F, t);
    return 1.0F - std::pow(1.0F - clamped, 3.0F);
  }
};

TabAnimation g_chartTabAnimation;
TabAnimation g_settingsTabAnimation;

// Draws one 16:9 chart card: cover, shade, difficulty, best score, name, and the
// selection plate used while managing the library.
bool drawChartCard(AppContext& context, const ChartRecord& record, ImVec2 topLeft, float width,
    float height, float scale, bool selected, bool manageMode, double nowSec) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
  const std::string id = "##card_" + record.id;
  ImGui::SetCursorScreenPos(topLeft);
  ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked();
  if (clicked) {
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

  const ImU32 fill = hovered ? ui::rgba(0xCC1D2129U) : ui::rgba(ui::kCardFill);
  ui::roundedRect(drawList, topLeft, bottomRight, ui::kCardRadius * scale, fill,
      ui::rgba(ui::kCardBorder), 1.0F);

  // Cover artwork, dimmed so the text above it stays readable.
  const std::uint64_t cover = coverTexture(context, record, 320);
  if (cover != 0) {
    drawList->AddImage(static_cast<ImTextureID>(cover), topLeft, bottomRight);
    drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(0, 0, 0,
        static_cast<int>(255.0F * (1.0F - kCoverAlpha) + 0.5F)));
  } else {
    drawList->AddRectFilled(topLeft, bottomRight, ui::rgba(ui::kChip), ui::kCardRadius * scale);
  }
  // The shade keeps labels readable over bright covers.
  drawList->AddRectFilled(topLeft, bottomRight, ui::rgba(ui::kCoverShade), ui::kCardRadius * scale);

  const float difficultyHeight = kCardDifficultyHeight * scale;
  const float nameHeight = kCardNameHeight * scale;
  const float padding = 9.0F * scale;

  // Difficulty at the top-left, best score at the top-right.
  const std::string difficulty = record.difficulty.empty() ? "SP Lv.?" : record.difficulty;
  const float half = width * 0.5F;
  ui::drawClippedText(drawList, ImVec2(topLeft.x + padding,
                            topLeft.y + (difficultyHeight - 13.0F * scale) * 0.5F),
      topLeft, ImVec2(topLeft.x + half, topLeft.y + difficultyHeight), 13.0F * scale,
      ui::rgba(ui::kTextPrimary), ui::ellipsize(difficulty, 13.0F * scale, half - padding * 2.0F));

  char scoreBuffer[16];
  std::snprintf(scoreBuffer, sizeof(scoreBuffer), "%07d", record.bestScore > 0
      ? record.bestScore
      : 0);
  const ImVec2 scoreExtent = ui::measureText(scoreBuffer, 13.0F * scale);
  ui::drawText(drawList, ImVec2(bottomRight.x - padding - scoreExtent.x,
                            topLeft.y + (difficultyHeight - 13.0F * scale) * 0.5F),
      13.0F * scale, ui::rgba(ui::kTextPrimary), scoreBuffer);

  // Chart name along the bottom edge.
  const float nameLeft = topLeft.x + 10.0F * scale;
  const float nameTop = bottomRight.y - nameHeight - kCardNameBottomMargin * scale;
  const float nameWidth = width - (manageMode ? 44.0F : 22.0F) * scale;
  ui::drawMarqueeText(drawList, ImVec2(nameLeft, nameTop + (nameHeight - 14.0F * scale) * 0.5F),
      nameWidth, 14.0F * scale, ui::rgba(ui::kTextPrimary), record.songName, nowSec);

  // Selection plate in the bottom-right corner.
  if (manageMode) {
    const float checkWidth = kCardCheckWidth * scale;
    const float checkHeight = kCardCheckHeight * scale;
    const ImVec2 checkTopLeft(bottomRight.x - checkWidth, bottomRight.y - checkHeight);
    ui::skewQuad(drawList, checkTopLeft.x + checkHeight * ui::kSkewRatio, checkTopLeft.y,
        checkTopLeft.x + checkWidth, checkTopLeft.y + checkHeight,
        checkHeight * ui::kSkewRatio, ui::rgba(ui::kTabSelectedPlate));
    if (selected) {
      const std::uint64_t checked = assetTexture(context, "res/checked.png", 64);
      const ImVec2 inset(checkTopLeft.x + 6.0F * scale, checkTopLeft.y + 3.0F * scale);
      if (checked != 0) {
        drawList->AddImage(static_cast<ImTextureID>(checked), inset,
            ImVec2(checkTopLeft.x + checkWidth - 5.0F * scale,
                checkTopLeft.y + checkHeight - 3.0F * scale));
      } else {
        ui::drawTextCentered(drawList, ImVec2(checkTopLeft.x + checkWidth * 0.5F,
                                    checkTopLeft.y + checkHeight * 0.5F),
            13.0F * scale, IM_COL32(16, 21, 27, 255), "v");
      }
    }
  }
  return clicked;
}

// The three chart tabs shown above the grid.
const char* const kChartTabs[] = {"本地", "上架", "未上架"};

} // namespace

void drawLibraryScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##library");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  // Everything is laid out on one design canvas so the proportions hold
  // at any window size.
  const ui::DesignCanvas canvas = ui::designCanvas(ImVec2(size.width, size.height),
      ui::kListCanvasWidth, ui::kListCanvasHeight);
  const float unit = canvas.scale;
  const auto point = [&](float x, float y) { return ui::canvasPoint(canvas, x, y); };

  // Back control in the top-left corner.
  {
    const ImVec2 topLeft = point(0.0F, kBackTop);
    if (ui::cornerButton("##library_back", topLeft, ImVec2(kBackWidth * unit, kBackHeight * unit),
            true, assetTexture(context, "res/return.png", 96), nullptr, 20.0F * unit, "返回")) {
      navigateTo(context, Screen::Home);
    }
  }

  // Tab strip: local charts first, then the two catalogue tabs.
  context.libraryTab = std::clamp(context.libraryTab, 0, 2);
  const ui::DesignCanvas tabCanvas = canvas;
  const float tabProgress = g_chartTabAnimation.progress(context.nowSec);
  const int previousTab = g_chartTabAnimation.from;
  const int pressedTab = ui::tabStrip("##library_tabs", point(kTabsLeft, kTabsTop),
      kTabsWidth * unit, kTabsHeight * unit, context.libraryTab, previousTab, tabProgress,
      kChartTabs, 3, 18.0F * unit);
  (void)tabCanvas;
  if (pressedTab >= 0 && pressedTab != context.libraryTab) {
    g_chartTabAnimation.begin(pressedTab, context.nowSec);
    context.libraryTab = pressedTab;
    context.manageMode = false;
    context.selectedIds.clear();
  }

  // Search field on the right of the tab row.
  {
    const ImVec2 topLeft = point(ui::kListCanvasWidth - kPanelRight - ui::kSearchWidth, kSearchTop);
    const ImVec2 fieldSize(ui::kSearchWidth * unit, ui::kSearchHeight * unit);
    ImGui::SetCursorScreenPos(topLeft);
    const bool clicked = ImGui::InvisibleButton("##library_search", fieldSize);
    const bool hovered = ImGui::IsItemHovered();
    ui::roundedRect(drawList, topLeft,
        ImVec2(topLeft.x + fieldSize.x, topLeft.y + fieldSize.y), ui::kCardRadius * unit,
        hovered ? ui::rgba(0xB8E7EBF0U) : ui::rgba(0x9DE7EBF0U));
    const std::string label = context.searchQuery.empty() ? "搜索曲名" : context.searchQuery;
    ui::drawClippedText(drawList,
        ImVec2(topLeft.x + 14.0F * unit, topLeft.y + (fieldSize.y - 16.0F * unit) * 0.5F),
        topLeft, ImVec2(topLeft.x + fieldSize.x, topLeft.y + fieldSize.y), 16.0F * unit,
        ui::rgba(ui::kTextPrimary), ui::ellipsize(label, 16.0F * unit, fieldSize.x - 24.0F * unit));
    if (clicked) ImGui::OpenPopup("##search_popup");
  }

  // Scroll panel holding the card grid.
  const ImVec2 panelTopLeft = point(kPanelLeft, kPanelTop);
  const ImVec2 panelBottomRight = point(ui::kListCanvasWidth - kPanelRight,
      ui::kListCanvasHeight - kPanelBottom);
  drawList->AddRectFilled(panelTopLeft, panelBottomRight, ui::rgba(ui::kListPanel));

  const float panelWidth = panelBottomRight.x - panelTopLeft.x;
  const float panelHeight = panelBottomRight.y - panelTopLeft.y;
  ImGui::SetCursorScreenPos(panelTopLeft);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
  ImGui::BeginChild("##library_grid", ImVec2(panelWidth, panelHeight), false,
      ImGuiWindowFlags_NoBackground);
  const ImVec2 gridOrigin = ImGui::GetCursorScreenPos();

  const float gridInset = kGridInset * unit;
  const float cardMargin = kCardMargin * unit;
  const float columnWidth = (panelWidth - gridInset * 2.0F) / static_cast<float>(kGridColumns);
  const float cardWidth = columnWidth - cardMargin * 2.0F;
  const float cardHeight = ui::coverHeightFor(cardWidth);
  const float rowHeight = cardHeight + cardMargin * 2.0F;

  if (context.libraryTab == 0) {
    std::vector<const ChartRecord*> visible;
    for (const auto& record : context.library.records()) {
      if (matchesSearch(record, context.searchQuery)) visible.push_back(&record);
    }
    const int rows = static_cast<int>((visible.size() + static_cast<std::size_t>(kGridColumns)
        - 1) / static_cast<std::size_t>(kGridColumns));
    const float contentHeight = static_cast<float>(std::max(0, rows)) * rowHeight;
    ImGui::Dummy(ImVec2(panelWidth - gridInset * 2.0F, std::max(contentHeight, 1.0F)));
    for (std::size_t index = 0; index < visible.size(); ++index) {
      const int row = static_cast<int>(index) / kGridColumns;
      const int column = static_cast<int>(index) % kGridColumns;
      const ImVec2 cardTopLeft(gridOrigin.x + gridInset + static_cast<float>(column) * columnWidth
              + cardMargin,
          gridOrigin.y + gridInset + static_cast<float>(row) * rowHeight + cardMargin);
      const bool selected = std::find(context.selectedIds.begin(), context.selectedIds.end(),
                                  visible[index]->id)
          != context.selectedIds.end();
      drawChartCard(context, *visible[index], cardTopLeft, cardWidth, cardHeight, unit, selected,
          context.manageMode, context.nowSec);
    }
    if (visible.empty()) {
      ui::drawTextCentered(drawList, ImVec2(gridOrigin.x + panelWidth * 0.5F,
                                  gridOrigin.y + panelHeight * 0.42F),
          kStateTextSize * unit, ui::rgba(ui::kTextPrimary),
          context.searchQuery.empty() ? "本地暂无谱面" : "没有匹配的谱面");
    }
    // Selection counter, shown only while managing.
    if (context.manageMode && !visible.empty()) {
      const std::string state = "已选择 " + std::to_string(context.selectedIds.size()) + " / "
          + std::to_string(visible.size());
      ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, point(0.0F, 64.0F).y),
          13.0F * unit, ui::rgba(ui::kTextPrimary), state.c_str());
    }
  } else {
    ui::drawTextCentered(drawList, ImVec2(gridOrigin.x + panelWidth * 0.5F,
                                gridOrigin.y + panelHeight * 0.42F),
        kStateTextSize * unit, ui::rgba(ui::kTextPrimary),
        context.libraryTab == 1 ? "暂无上架谱面" : "暂无未上架谱面");
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // Vertical icon column in the bottom-right corner.
  const auto sideIcon = [&](const char* id, const char* asset, float bottomOffset,
                            const char* tooltip) {
    const ImVec2 topLeft = point(ui::kListCanvasWidth - kBackWidth,
        ui::kListCanvasHeight - bottomOffset - kBackHeight - 8.0F);
    return ui::cornerButton(id, topLeft, ImVec2(kBackWidth * unit, kBackHeight * unit), false,
        assetTexture(context, asset, 96), nullptr, 18.0F * unit, tooltip);
  };

  if (context.libraryTab == 0) {
    if (!context.manageMode) {
      if (sideIcon("##library_manage", "res/edit.png", 58.0F, "管理")) {
        context.manageMode = true;
        context.selectedIds.clear();
      }
      if (sideIcon("##library_import", "res/import.png", 0.0F, "导入谱面包")) {
        ImGui::OpenPopup("##import_popup");
      }
    } else {
      if (sideIcon("##library_delete", "res/delete.png", 116.0F, "删除选中")) {
        if (context.selectedIds.empty()) {
          notify(context, "未选择谱面");
        } else {
          ImGui::OpenPopup("##delete_popup");
        }
      }
      if (sideIcon("##library_select_all", "res/all.png", 58.0F, "全选")) {
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
      if (sideIcon("##library_cancel_manage", "res/cancel.png", 0.0F, "取消")) {
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
