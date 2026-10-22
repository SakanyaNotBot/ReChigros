#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace rechigros::app {
namespace {

constexpr float kSkewSlope = 0.2679492F; // tan(15 degrees)
constexpr float kCanvasHeight = 415.0F;

// The page is authored on the reference's 731 x 415 design canvas. One design unit equals one
// density value of the reference layout, so the panel numbers below are used as written in the
// reference source. The scaled canvas is anchored to the top-left corner of the window (the
// reference keeps a strip of unused viewport on the right rather than letterboxing both sides),
// and the few unscaled corner controls hug the real window edges on top of it.
constexpr float kTitleTop = 82.0F;
constexpr float kTitleWidth = 342.0F;
constexpr float kTitleHeight = 56.0F;
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
constexpr float kGradeSize = 44.0F;
constexpr float kCoverAlpha = 0.34F;

// Glyph sizes. These labels sit inside the scaled canvas of the reference page, so their pixel
// size is the layout value multiplied by the canvas scale. ui::TextStyle sizes are density units
// and the canvas already carries the display scale, hence the conversion through kDensityRatio.
constexpr float kCanvasTextScale = 1.0F / ui::kDensityRatio;

// Reference design sizes of the labels.
constexpr float kNameSize = 22.0F;
constexpr float kInfoKeySize = 15.0F;
constexpr float kInfoValueSize = 18.0F;
constexpr float kChipTextSize = 20.0F;
constexpr float kScoreTextSize = 36.0F;
constexpr float kAccuracyTextSize = 20.0F;

ui::Canvas detailCanvas() {
  ui::Canvas canvas = ui::canvas();
  canvas.origin = ImVec2(0.0F, 0.0F);
  return canvas;
}

// Corner controls are laid out in real pixels in the reference (they live on the unscaled host
// view), so their density values are converted into canvas design units instead of being scaled
// with the canvas.
constexpr float kHostUnit = ui::kDensityRatio;

ui::Box hostBox(float leftDp, float topDp, float widthDp, float heightDp) {
  return ui::Box{leftDp * kHostUnit, topDp * kHostUnit, (leftDp + widthDp) * kHostUnit,
      (topDp + heightDp) * kHostUnit};
}

ui::TextStyle canvasTextStyle(float designSize, bool bold, ImU32 color) {
  ui::TextStyle style;
  style.sizeDp = designSize * kCanvasTextScale;
  style.bold = bold;
  style.color = color;
  style.fitWidth = false;
  return style;
}

ui::TextStyle canvasFitTextStyle(float designSize, bool bold, ImU32 color) {
  ui::TextStyle style = canvasTextStyle(designSize, bold, color);
  style.fitWidth = true;
  return style;
}

std::string orUnknown(const std::string& value) {
  return value.empty() ? "Unkown" : value;
}

std::string fileNameOrUnknown(const std::filesystem::path& file) {
  return file.empty() ? std::string("Unkown") : file.filename().string();
}

std::uint64_t assetTextureSized(
    AppContext& context, const std::string& path, int maxSize, int& width, int& height) {
  const std::uint64_t texture = assetTexture(context, path, maxSize);
  width = static_cast<int>(context.textures.width(texture));
  height = static_cast<int>(context.textures.height(texture));
  return texture;
}

void drawIconOnPlate(AppContext& context, ImDrawList* drawList, const ui::Canvas& canvas,
    const ui::Box& box, const ui::PanelStyle& style, const std::string& asset) {
  int textureWidth = 0;
  int textureHeight = 0;
  const std::uint64_t texture =
      assetTextureSized(context, asset, 128, textureWidth, textureHeight);
  ui::drawIconPlate(drawList, canvas, box, style, texture, textureWidth, textureHeight, 16.0F,
      10.0F, 3.0F);
}

// The back plate centres the glyph's ink on the centroid of the visible parallelogram, which is
// what the reference does for the return icon.
void drawBackPlate(AppContext& context, ImDrawList* drawList, const ui::Canvas& canvas,
    const ui::Box& box, const ui::PanelStyle& style) {
  ui::drawPanelShape(drawList, canvas, box, style);
  int textureWidth = 0;
  int textureHeight = 0;
  const std::uint64_t texture =
      assetTextureSized(context, "res/return.png", 128, textureWidth, textureHeight);
  if (texture == 0 || textureWidth <= 0 || textureHeight <= 0) return;
  const ui::PxRect rect = ui::toPixels(canvas, box);
  const float width = rect.max.x - rect.min.x;
  const float height = rect.max.y - rect.min.y;
  const float skew = std::min(kSkewSlope * height, width * 0.42F);
  const float imageSize =
      std::max(1.0F, std::min(width - canvas.dp(32.0F), height - canvas.dp(20.0F)));
  const float assetOffset = imageSize * (0.5F - 87.337F / 200.0F);
  const float translateX = -skew * 0.25F + assetOffset;
  const float translateY = imageSize * (0.5F - 99.223F / 200.0F);
  const ui::PxRect content{ImVec2(rect.min.x + canvas.dp(16.0F) + translateX,
                               rect.min.y + canvas.dp(10.0F) + translateY),
      ImVec2(rect.max.x - canvas.dp(16.0F) + translateX,
          rect.max.y - canvas.dp(10.0F) + translateY)};
  ui::drawImage(drawList, content, texture, textureWidth, textureHeight, ui::ImageFit::FitCenter);
}

ui::PanelStyle backPlateStyle() {
  ui::PanelStyle style = ui::glassPanel(ui::argb(0x8610171DU), ui::argb(0x00FFFFFFU));
  style.strokeWidthDp = 0.0F;
  style.straightLeft = true;
  style.accentEdge = 2;
  style.accent = ui::argb(0xFFFFFFFFU);
  style.accentWidthDp = 3.2F;
  return style;
}

ui::PanelStyle topPlateStyle() {
  ui::PanelStyle style = ui::glassPanel(ui::argb(0x8610171DU), ui::argb(0x00FFFFFFU));
  style.strokeWidthDp = 0.0F;
  style.skewDegrees = 12.0F;
  style.straightRight = true;
  style.topWider = true;
  style.accentEdge = 1;
  style.accent = ui::argb(0xFFFFFFFFU);
  style.accentWidthDp = 4.2F;
  return style;
}

ui::PanelStyle sidePlateStyle() {
  ui::PanelStyle style = ui::glassPanel(ui::argb(0x8610171DU), ui::argb(0x00FFFFFFU));
  style.strokeWidthDp = 0.0F;
  style.straightRight = true;
  style.accentEdge = 1;
  style.accent = ui::argb(0xFFFFFFFFU);
  style.accentWidthDp = 3.2F;
  return style;
}

double dialogEase(double startSec, double nowSec) {
  if (startSec <= 0.0) return 1.0;
  const float t = std::clamp(static_cast<float>((nowSec - startSec) / 0.2), 0.0F, 1.0F);
  const float k = 1.0F - t;
  return static_cast<double>(1.0F - k * k * k);
}

// The information dialog: one 75 degree parallelogram carrying the title and the record
// details, with a black action strip along the bottom. The reference eases the panel in from
// 65% size over 200 ms and dims the page behind it to half black.
// Returns false when the dialog was dismissed this frame.
bool drawInfoDialog(AppContext& context, ImDrawList* drawList, const ui::Canvas& canvas,
    const ChartRecord& record, double startSec, ImVec2 viewport) {
  const double eased = dialogEase(startSec, context.nowSec);
  const float panelScale = static_cast<float>(0.65 + 0.35 * eased);
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport,
      IM_COL32(0, 0, 0, static_cast<int>(std::lround(127.5 * eased))));

  // The dialog lives on the unscaled window: density values become design units here, while the
  // text keeps its plain density size.
  const float design = kHostUnit;
  const float viewWidth = viewport.x / canvas.scale;
  const float viewHeight = viewport.y / canvas.scale;
  const float padH = 26.0F * design;
  const float padTop = 22.0F * design;
  const float padBottom = 16.0F * design;
  const float stripH = 52.0F * design;
  const float accentWidth = 3.0F * design;

  const char* const keys[] = {"曲名: ", "难度: ", "谱师: ", "曲师: ", "画师: ", "音乐: ", "谱面: "};
  const std::string values[] = {record.songName, record.difficulty,
      orUnknown(record.info.charter), orUnknown(record.info.composer),
      orUnknown(record.info.illustrator), fileNameOrUnknown(record.musicFile),
      fileNameOrUnknown(record.chartFile)};
  constexpr int kLineCount = 7;

  const float titleLine = ui::labelLineHeight(canvas, 22.0F);
  const float messageLine = ui::labelLineHeight(canvas, 17.0F);
  const float messageSpacing = 4.0F * design;
  const float contentHeight = titleLine + 2.0F * design + 20.0F * design + 8.0F * design
      + static_cast<float>(kLineCount) * (messageLine + messageSpacing);
  const float totalHeight = contentHeight + padTop + padBottom + stripH;

  const float panelWidth =
      std::max(std::min(340.0F * design, viewWidth * 0.92F),
          std::min(viewWidth * 0.58F, 620.0F * design));
  const float panelLeft = (viewWidth - panelWidth) * 0.5F;
  const float panelTop = (viewHeight - totalHeight) * 0.5F;
  const float centerX = panelLeft + panelWidth * 0.5F;
  const float centerY = panelTop + totalHeight * 0.5F;
  const auto fit = [&](float x, float y) {
    return ImVec2(centerX + (x - centerX) * panelScale, centerY + (y - centerY) * panelScale);
  };
  const auto toScreen = [&](float x, float y) {
    const ImVec2 fitted = fit(x, y);
    return canvas.point(fitted.x, fitted.y);
  };

  const float slant = totalHeight * kSkewSlope;
  const float stripSlant = stripH * kSkewSlope;
  const float stripTop = panelTop + contentHeight;
  const float panelBottom = panelTop + totalHeight;

  ImVec2 contentPoints[4] = {
      toScreen(panelLeft + slant, panelTop),
      toScreen(panelLeft + panelWidth, panelTop),
      toScreen(panelLeft + panelWidth - contentHeight * kSkewSlope, stripTop),
      toScreen(panelLeft + stripSlant, stripTop),
  };
  ImVec2 stripPoints[4] = {
      toScreen(panelLeft + stripSlant, stripTop),
      toScreen(panelLeft + panelWidth - contentHeight * kSkewSlope, stripTop),
      toScreen(panelLeft + panelWidth - slant, panelBottom),
      toScreen(panelLeft, panelBottom),
  };
  ImVec2 accentPoints[4] = {
      toScreen(panelLeft + slant, panelTop),
      toScreen(panelLeft + slant + accentWidth, panelTop),
      toScreen(panelLeft + stripSlant + accentWidth, stripTop),
      toScreen(panelLeft + stripSlant, stripTop),
  };
  drawList->AddConvexPolyFilled(contentPoints, 4, IM_COL32(240, 32, 36, 41));
  drawList->AddConvexPolyFilled(stripPoints, 4, IM_COL32(0, 0, 0, 217));
  drawList->AddConvexPolyFilled(accentPoints, 4, IM_COL32(255, 255, 255, 255));

  const float contentLeft = panelLeft + stripSlant + padH;
  const float contentWidth = panelWidth - totalHeight * kSkewSlope - padH * 2.0F;
  const auto rowShift = [&](float rowTop, float rowHeight) {
    return (stripTop - (rowTop + rowHeight * 0.5F)) * kSkewSlope;
  };
  const auto drawRow = [&](float rowTop, float rowHeight, const std::string& label, float sizeDp,
                           ImU32 color) {
    const ImVec2 origin = fit(contentLeft + rowShift(rowTop, rowHeight), rowTop);
    ui::TextStyle style;
    style.sizeDp = sizeDp;
    style.bold = false;
    style.color = color;
    style.ellipsize = true;
    ui::drawLabel(drawList, canvas,
        ui::Box{origin.x, origin.y, origin.x + contentWidth, origin.y + rowHeight}, label, style,
        context.nowSec);
  };

  float cursor = panelTop + padTop;
  drawRow(cursor, titleLine, "详细信息", 22.0F, IM_COL32(255, 255, 255, 255));
  cursor += titleLine + 2.0F * design;
  {
    const ImVec2 ruleTopLeft = toScreen(contentLeft + rowShift(cursor, 2.0F * design), cursor);
    drawList->AddRectFilled(ruleTopLeft,
        ImVec2(ruleTopLeft.x + canvas.units(44.0F * design),
            ruleTopLeft.y + canvas.units(2.0F * design)),
        IM_COL32(255, 255, 255, 153));
    cursor += 2.0F * design + 6.0F * design + 12.0F * design;
  }
  cursor += 4.0F * design;
  std::string lines[kLineCount];
  for (int index = 0; index < kLineCount; ++index) {
    lines[index] = std::string(keys[index]) + values[index];
    drawRow(cursor, messageLine, lines[index], 17.0F, IM_COL32(230, 255, 255, 255));
    cursor += messageLine + messageSpacing;
  }

  // The strip carries the single confirm action.
  bool dismissed = false;
  ImGui::SetCursorScreenPos(ImVec2(0.0F, 0.0F));
  ImGui::InvisibleButton("##detail_dialog_backdrop", viewport);
  if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dismissed = true;
  {
    const ImVec2 topLeft = toScreen(panelLeft + stripSlant, stripTop);
    ImGui::SetCursorScreenPos(topLeft);
    ImGui::PushID("##detail_dialog_strip");
    ImGui::InvisibleButton("confirm",
        ImVec2(std::max(1.0F, canvas.units(panelWidth - stripSlant)),
            std::max(1.0F, canvas.units(stripH))));
    const bool confirm = ImGui::IsItemHovered()
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    ImGui::PopID();
    const ImVec2 labelOrigin = fit(panelLeft + stripSlant + 22.0F * design, stripTop);
    ui::TextStyle style;
    style.sizeDp = 18.0F;
    style.bold = false;
    style.color = IM_COL32(255, 255, 255, 255);
    style.fitWidth = true;
    style.align = ui::HAlign::Center;
    ui::drawLabel(drawList, canvas,
        ui::Box{labelOrigin.x, labelOrigin.y, labelOrigin.x + panelWidth - slant
                    - 44.0F * design,
            labelOrigin.y + stripH},
        "确定", style, context.nowSec);
    if (confirm) dismissed = true;
  }
  return !dismissed;
}

} // namespace

void drawDetailScreen(AppContext& context) {
  const ui::Canvas canvas = detailCanvas();
  const ImVec2 viewport = canvas.viewport;
  ui::beginFullscreenWindow("##detail");
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  const auto record = context.library.find(context.activeChartId);
  if (!record) {
    ui::drawBackdrop(drawList, viewport, context.nowSec);
    drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport, ui::argb(0x46000000U));
    ui::TextStyle message;
    message.sizeDp = 22.0F;
    message.bold = true;
    message.align = ui::HAlign::Center;
    message.valign = ui::VAlign::Middle;
    message.color = ui::argb(ui::kTextSecondary);
    ui::drawLabel(drawList, canvas, ui::Box{0.0F, 0.0F, viewport.x / canvas.scale,
                                      viewport.y / canvas.scale},
        "谱面不存在或已删除", message, context.nowSec);
    const ui::PanelStyle backStyle = backPlateStyle();
    const ui::Box backBox = hostBox(0.0F, 8.0F, 78.0F, 48.0F);
    const ui::Interaction back = ui::panelInteraction("##detail_back", canvas, backBox, backStyle);
    drawBackPlate(context, drawList, canvas, backBox, backStyle);
    ui::endFullscreenWindow();
    if (back.pressed) navigateTo(context, Screen::Library);
    return;
  }

  static bool dialogOpen = false;
  static double dialogStartSec = -1.0;
  const bool dialogWasOpen = dialogOpen;

  // The page never owns a background: the GPU backdrop already draws the chart illustration
  // behind the grid (Application.cpp feeds it the active record). The reference adds the same
  // root shade as every other screen, then one more dimmed copy of the artwork plus a flat
  // scrim on top of it.
  const float contentWidth = viewport.x / canvas.scale;
  const float contentHeight = viewport.y / canvas.scale;
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport, ui::argb(0x46000000U));
  const std::uint64_t cover = coverTexture(context, *record, 1200);
  if (cover != 0) {
    const int coverWidth = static_cast<int>(context.textures.width(cover));
    const int coverHeight = static_cast<int>(context.textures.height(cover));
    ui::drawImage(drawList, ui::PxRect{ImVec2(0.0F, 0.0F), viewport}, cover, coverWidth,
        coverHeight, ui::ImageFit::CenterCrop,
        IM_COL32(255, 255, 255, static_cast<int>(std::lround(255.0F * kCoverAlpha))));
  }
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport, ui::argb(ui::kDetailCoverShade));

  // Corner controls are real-pixel plates on the unscaled host of the reference.
  const ui::PanelStyle backStyle = backPlateStyle();
  const ui::PanelStyle topStyle = topPlateStyle();
  const ui::PanelStyle sideStyle = sidePlateStyle();
  const ui::Box backBox = hostBox(0.0F, 8.0F, 78.0F, 48.0F);
  const ui::Box infoBox = ui::Box{contentWidth - 78.0F * kHostUnit, 18.0F * kHostUnit,
      contentWidth, 66.0F * kHostUnit};
  const ui::Box settingsBox = ui::Box{contentWidth - 78.0F * kHostUnit, 88.0F * kHostUnit,
      contentWidth, 136.0F * kHostUnit};
  const ui::Box startBox = ui::Box{contentWidth - 78.0F * kHostUnit,
      contentHeight - 66.0F * kHostUnit, contentWidth, contentHeight - 18.0F * kHostUnit};

  bool backPressed = false;
  bool infoPressed = false;
  bool settingsPressed = false;
  bool startPressed = false;
  if (!dialogWasOpen) {
    backPressed = ui::panelInteraction("##detail_back", canvas, backBox, backStyle).pressed;
    infoPressed = ui::panelInteraction("##detail_info", canvas, infoBox, topStyle).pressed;
    settingsPressed =
        ui::panelInteraction("##detail_settings", canvas, settingsBox, topStyle).pressed;
    // The start plate begins a normal run on a tap and a practice run on a long press.
    ImGui::SetCursorScreenPos(canvas.point(startBox.x0, startBox.y0));
    ImGui::InvisibleButton("##detail_start", ImVec2(canvas.units(startBox.x1 - startBox.x0),
                                            canvas.units(startBox.y1 - startBox.y0)));
    static double pressStartSec = -1.0;
    static bool practiceTriggered = false;
    if (ImGui::IsItemActivated()) {
      pressStartSec = context.nowSec;
      practiceTriggered = false;
    }
    if (ImGui::IsItemActive() && !practiceTriggered && pressStartSec >= 0.0
        && context.nowSec - pressStartSec > 0.45) {
      practiceTriggered = true;
      startChart(context, true);
    }
    if (ImGui::IsItemDeactivated()) {
      const bool wasTap = pressStartSec >= 0.0 && !practiceTriggered;
      pressStartSec = -1.0;
      practiceTriggered = false;
      if (wasTap) startPressed = true;
    }
  }

  // ---- content of the scaled canvas ----
  // Title plate, flush to the left edge.
  {
    ui::PanelStyle style = ui::glassPanel(ui::argb(ui::kDetailTitlePanel), ui::argb(0x00FFFFFFU));
    style.straightLeft = true;
    style.strokeWidthDp = 0.0F;
    const ui::Box box{0.0F, kTitleTop, kTitleWidth, kTitleTop + kTitleHeight};
    ui::drawPanelShape(drawList, canvas, box, style);
    ui::TextStyle text = canvasTextStyle(kNameSize, true, ui::argb(ui::kTextPrimary));
    text.ellipsize = true;
    text.paddingLeftDp = 2.0F * kCanvasTextScale;
    text.paddingRightDp = 28.0F * kCanvasTextScale;
    ui::drawLabel(drawList, canvas, box, record->songName, text, context.nowSec);
  }

  // Charter and composer block, flush to the left edge.
  {
    ui::PanelStyle style = ui::glassPanel(ui::argb(ui::kDetailInfoPanel), ui::argb(0x00FFFFFFU));
    style.straightLeft = true;
    style.strokeWidthDp = 0.0F;
    const ui::Box box{0.0F, kInfoTop, kInfoWidth, kInfoTop + kInfoHeight};
    ui::drawPanelShape(drawList, canvas, box, style);
    const float left = 2.0F;
    const float right = kInfoWidth - 28.0F;
    const char* const keys[] = {"charter", "composer"};
    const std::string values[] = {record->info.charter, record->info.composer};
    for (int index = 0; index < 2; ++index) {
      const float rowTop = kInfoTop + 8.0F + static_cast<float>(index) * 48.0F;
      ui::TextStyle key = canvasFitTextStyle(kInfoKeySize, false, ui::argb(0xB8FFFFFFU));
      ui::drawLabel(drawList, canvas, ui::Box{left, rowTop, right, rowTop + 22.0F}, keys[index],
          key, context.nowSec);
      ui::TextStyle value = canvasTextStyle(kInfoValueSize, false, ui::argb(ui::kTextPrimary));
      value.ellipsize = true;
      ui::drawLabel(drawList, canvas, ui::Box{left, rowTop + 22.0F, right, rowTop + 48.0F},
          orUnknown(values[index]), value, context.nowSec);
    }
  }

  // Difficulty chip: a dark backing plate behind a bright plate carrying the label.
  {
    ui::PanelStyle back = ui::glassPanel(ui::argb(ui::kDetailTitlePanel), ui::argb(0x00FFFFFFU));
    back.strokeWidthDp = 0.0F;
    ui::drawPanelShape(drawList, canvas,
        ui::Box{kChipBackLeft, kChipBackTop, kChipBackLeft + kChipBackWidth,
            kChipBackTop + kChipBackHeight},
        back);
    ui::PanelStyle plate = ui::glassPanel(0xFFFFFFFFU, ui::argb(0x00FFFFFFU));
    plate.strokeWidthDp = 0.0F;
    const ui::Box box{kChipLeft, kChipTop, kChipLeft + kChipWidth, kChipTop + kChipHeight};
    ui::drawPanelShape(drawList, canvas, box, plate);
    ui::TextStyle text = canvasFitTextStyle(kChipTextSize, true, IM_COL32(5, 7, 10, 255));
    text.align = ui::HAlign::Center;
    text.paddingLeftDp = 16.0F * kCanvasTextScale;
    text.paddingRightDp = 16.0F * kCanvasTextScale;
    ui::drawLabel(drawList, canvas, box, record->difficulty, text, context.nowSec);
  }

  // Best score plate along the bottom-left.
  {
    const float panelTop = kCanvasHeight - kScoreBottom - kScoreHeight;
    ui::PanelStyle style = ui::glassPanel(ui::argb(ui::kDetailInfoPanel), ui::argb(0x00FFFFFFU));
    style.straightLeft = true;
    style.strokeWidthDp = 0.0F;
    const ui::Box box{0.0F, panelTop, kScoreWidth, panelTop + kScoreHeight};
    ui::drawPanelShape(drawList, canvas, box, style);

    const ImVec2 gradeCenter =
        canvas.point(16.0F + kGradeSize * 0.5F, panelTop + kScoreHeight * 0.5F);
    drawGradeImage(context, drawList, gradeCenter, canvas.units(kGradeSize),
        record->bestGradeIndex(), record->bestScore > 0 ? 1.0F : 0.68F);

    const std::string scoreText =
        record->bestScore > 0 ? formatScore(record->bestScore) : "0000000";
    ui::TextStyle score = canvasFitTextStyle(kScoreTextSize, true, ui::argb(ui::kTextPrimary));
    ui::drawLabel(drawList, canvas,
        ui::Box{70.0F, panelTop, 70.0F + 176.0F, panelTop + kScoreHeight}, scoreText, score,
        context.nowSec);

    const std::string accuracy = record->bestAcc >= 0
        ? formatAccuracy(static_cast<double>(record->bestAcc) / 10000.0)
        : "--.--%";
    ui::TextStyle acc = canvasFitTextStyle(kAccuracyTextSize, false, ui::argb(0xEFFFFFFFU));
    acc.valign = ui::VAlign::Bottom;
    ui::drawLabel(drawList, canvas,
        ui::Box{254.0F, panelTop + kScoreHeight - 12.0F - 30.0F, 254.0F + 96.0F,
            panelTop + kScoreHeight - 12.0F},
        accuracy, acc, context.nowSec);
  }

  // ---- plates drawn after the content so they read against it ----
  drawBackPlate(context, drawList, canvas, backBox, backStyle);
  drawIconOnPlate(context, drawList, canvas, infoBox, topStyle, "res/detail.png");
  drawIconOnPlate(context, drawList, canvas, settingsBox, topStyle, "res/settings.png");
  drawIconOnPlate(context, drawList, canvas, startBox, sideStyle, "res/start.png");

  // ---- information dialog ----
  if (dialogOpen) {
    dialogOpen = drawInfoDialog(context, drawList, canvas, *record, dialogStartSec, viewport);
    if (!dialogOpen) dialogStartSec = -1.0;
  }

  ui::endFullscreenWindow();

  if (infoPressed) {
    dialogOpen = true;
    dialogStartSec = context.nowSec;
  }
  if (backPressed) navigateTo(context, Screen::Library);
  if (settingsPressed) {
    context.settingsTab = 0;
    context.settingsReturn = Screen::Detail;
    navigateTo(context, Screen::Settings);
  }
  if (startPressed) startChart(context, false);
}

} // namespace rechigros::app
