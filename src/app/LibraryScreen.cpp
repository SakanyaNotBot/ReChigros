#include "rechigros/app/AppContext.hpp"

#include "ImportDialogs.hpp"

#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/platform/FileDialog.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

// Chart list screen: the local library, the two catalogue placeholder tabs and the
// manage-mode layout. Geometry is authored in density units taken from the reference
// layout, converted to screen pixels, then handed to the shared layout primitives in
// design coordinates so drawing and hit testing stay consistent.
namespace rechigros::app {
namespace {

// ---------------------------------------------------------------------------
// Palette and metrics (density units)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kPanelFill = 0x7B070A10U;
constexpr std::uint32_t kCardFill = 0xB2171A20U;
constexpr std::uint32_t kCardStroke = 0x6FFFFFFFU;
constexpr std::uint32_t kCoverShade = 0x85000000U;
constexpr std::uint32_t kStateText = 0xDFFFFFFFU;
constexpr std::uint32_t kPlateFill = 0x8610171DU;
constexpr std::uint32_t kSearchFill = 0x9DE7EBF0U;
constexpr std::uint32_t kSearchStroke = 0x7EFFFFFFU;
constexpr std::uint32_t kPageFill = 0x99E2E6EBU;
constexpr std::uint32_t kPageStroke = 0x70FFFFFFU;
constexpr std::uint32_t kCheckFill = 0xFFFFFFFFU;
constexpr std::uint32_t kDialogContent = 0xF0202429U;
constexpr std::uint32_t kDialogStrip = 0xD9000000U;
constexpr int kCoverAlpha = 184; // 0.72 * 255
constexpr int kGridColumns = 4;
constexpr int kOnlinePageSize = 16;
constexpr float kSlope = 0.2679492F; // tan(15 degrees)
constexpr double kTabSwitchIntervalSeconds = 0.26;
constexpr double kDialogEnterSeconds = 0.2;
constexpr double kReentryGuardSeconds = 0.25;
// A press that stays inside the tap slop for this long claims the card it landed on and
// opens manage mode. The value is the platform's own long-press timeout.
constexpr double kLongPressSeconds = 0.5;
// Movement of up to this many density units is still a press rather than a scroll.
constexpr float kTapSlopDp = 6.0F;

const char* const kChartTabs[] = {"本地", "上架", "未上架"};

// ---------------------------------------------------------------------------
// Screen-space mapping
// ---------------------------------------------------------------------------

// Maps the density-unit geometry of the reference layout onto the current viewport. A
// density value `v` becomes `v * densityPixels` screen pixels measured from the screen
// origin; boxes are finally converted to the centred design canvas the draw helpers use.
struct ScreenMap {
  ui::Canvas canvas;
  ImVec2 origin{};
  ImVec2 viewport{};
  float scale{1.0F};

  static ScreenMap current() {
    ScreenMap map;
    map.canvas = ui::canvas();
    map.origin = map.canvas.origin;
    map.viewport = map.canvas.viewport;
    map.scale = map.canvas.scale;
    return map;
  }

  // Density units to pixels.
  [[nodiscard]] float px(float value) const { return canvas.dp(value); }
  // Density value to an integral pixel count. The epsilon compensates for the float
  // rounding of the density ratio so exact half-pixel values round the way the reference
  // layout rounds them.
  [[nodiscard]] int rpx(float value) const {
    return static_cast<int>(std::lround(static_cast<double>(canvas.dp(value)) + 1.0e-3));
  }
  [[nodiscard]] float designX(float pixel) const { return (pixel - origin.x) / scale; }
  [[nodiscard]] float designY(float pixel) const { return (pixel - origin.y) / scale; }
  // Pixel rectangle to a design-space box.
  [[nodiscard]] ui::Box box(float x0, float y0, float x1, float y1) const {
    return ui::Box{designX(x0), designY(y0), designX(x1), designY(y1)};
  }
};

[[nodiscard]] ui::TextStyle labelStyle(float sizeDp, bool bold, ImU32 color,
    ui::HAlign align = ui::HAlign::Left, ui::VAlign valign = ui::VAlign::Middle) {
  ui::TextStyle style;
  style.sizeDp = sizeDp;
  style.bold = bold;
  style.color = color;
  style.align = align;
  style.valign = valign;
  style.fitWidth = true;
  return style;
}

// Height in pixels of a one-line label at a density-unit size.
[[nodiscard]] float lineHeightPixels(const ScreenMap& map, float sizeDp) {
  return ui::labelLineHeight(map.canvas, sizeDp) * map.scale;
}

[[nodiscard]] bool insideRect(float x0, float y0, float x1, float y1, ImVec2 point) {
  return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}

// Releases the press inside the rectangle once, mirroring the panel helper for flat areas.
struct RectInteraction {
  bool pressed{};
  bool hovered{};
};

[[nodiscard]] RectInteraction rectInteraction(const char* id, float x0, float y0, float x1,
    float y1) {
  RectInteraction result;
  if (x1 <= x0 || y1 <= y0) return result;
  ImGui::SetCursorScreenPos(ImVec2(x0, y0));
  // The button only reports a release after a press of its own, so a gesture that started on
  // another control or on the page before this one never reaches the handler.
  result.pressed = ImGui::InvisibleButton(id, ImVec2(x1 - x0, y1 - y0));
  result.hovered = ImGui::IsItemHovered();
  return result;
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

// Number of UTF-16 code units, which is what the reference marquee threshold counts.
std::size_t textLength(std::string_view text) {
  std::size_t length = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t step = 1;
    if ((lead & 0x80U) == 0) {
      step = 1;
    } else if ((lead & 0xE0U) == 0xC0U) {
      step = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      step = 3;
    } else {
      step = 4;
    }
    length += step == 4 ? 2 : 1;
    index += step;
  }
  return length;
}

bool shouldMarquee(std::string_view text) { return textLength(text) > 12; }

std::string nonEmpty(const std::string& value) {
  return value.empty() ? std::string("Unkown") : value;
}

std::string formatScore(int score) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", score > 0 ? score : 0);
  return buffer;
}

bool matchesSearch(const ChartRecord& record, const std::string& query) {
  if (query.empty()) return true;
  const std::string haystack = toLower(record.songName + " " + record.difficulty);
  return haystack.find(toLower(query)) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Persistent screen state
// ---------------------------------------------------------------------------

struct TabAnimation {
  int from{};
  int to{};
  double startSec{};
};

struct GridState {
  int scroll{};
  bool dragging{};
  bool armed{};
  ImVec2 dragStart{};
  int dragStartScroll{};
  float dragDistance{};
  // Press-and-hold bookkeeping. The target is fixed when the press lands, because by the
  // time the hold is long enough to count the row may already have scrolled away from the
  // finger.
  double pressStartSec{};
  int pressedCard{-1};
  bool longPressFired{};
};

struct DialogState {
  enum class Kind { None, Search, PageJump, ImportSource, DeleteConfirm };
  Kind kind{Kind::None};
  double openSec{-1.0};
  char buffer[256]{};
  bool focusPending{};
};

struct ListState {
  TabAnimation tabs;
  GridState grid;
  DialogState dialog;
  int onlinePage{};
  // 0 none, 1 chart package, 2 chart folder, 3 loose files. Set by the import dialog and
  // serviced after the frame so the picker never opens inside a draw.
  int pendingImport{};
  double lastTabSwitchSec{-100.0};
  double lastDrawSec{-1.0};
};

// Immediate-mode screens keep their animation and scroll state next to the draw code.
ListState& state() {
  static ListState instance;
  return instance;
}

// ---------------------------------------------------------------------------
// Grid geometry
// ---------------------------------------------------------------------------

struct GridGeometry {
  int panelL{};
  int panelT{};
  int panelR{};
  int panelB{};
  int gridL{};
  int gridT{};
  int gridR{};
  int gridB{};
  int margins{};
  int bottomPadding{};
  std::array<int, kGridColumns + 1> borders{};
  int rowHeight{};

  [[nodiscard]] int cardLeft(int column) const { return gridL + borders[column] + margins; }
  [[nodiscard]] int cardRight(int column) const {
    return gridL + borders[column + 1] - margins;
  }
  [[nodiscard]] int cardWidth(int column) const { return cardRight(column) - cardLeft(column); }
  [[nodiscard]] int cardHeight(int column) const {
    return static_cast<int>(std::lround(cardWidth(column) * 9.0 / 16.0));
  }
  [[nodiscard]] int cardTop(int row, int scroll) const {
    return gridT - scroll + row * rowHeight + margins;
  }
  [[nodiscard]] int gridHeight() const { return gridB - gridT; }
  [[nodiscard]] int contentHeight(int rows) const {
    return rows * rowHeight + bottomPadding;
  }
};

// Span borders distributed the way the reference grid layout manager spreads the leftover
// pixels: the remainder is handed out from the leading spans.
std::array<int, kGridColumns + 1> distributeSpans(int total) {
  std::array<int, kGridColumns + 1> borders{};
  borders[0] = 0;
  const int sizePerSpan = total / kGridColumns;
  const int remainder = total % kGridColumns;
  int consumed = 0;
  int additional = 0;
  for (int span = 1; span <= kGridColumns; ++span) {
    int itemSize = sizePerSpan;
    additional += remainder;
    if (additional > 0 && (kGridColumns - additional) < remainder) {
      itemSize += 1;
      additional -= kGridColumns;
    }
    consumed += itemSize;
    borders[static_cast<std::size_t>(span)] = consumed;
  }
  return borders;
}

GridGeometry computeGrid(const ScreenMap& map) {
  GridGeometry grid;
  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int viewportHeight = static_cast<int>(std::lround(map.viewport.y));
  grid.panelL = map.rpx(72.0F);
  grid.panelT = map.rpx(74.0F);
  grid.panelR = viewportWidth - map.rpx(98.0F);
  grid.panelB = viewportHeight - map.rpx(6.0F);
  grid.gridL = grid.panelL + map.rpx(8.0F);
  grid.gridT = grid.panelT + map.rpx(10.0F);
  grid.gridR = grid.panelR - map.rpx(8.0F);
  grid.gridB = grid.panelB;
  grid.margins = map.rpx(5.0F);
  grid.bottomPadding = map.rpx(18.0F);
  grid.borders = distributeSpans(std::max(1, grid.gridR - grid.gridL));
  int tallest = 0;
  for (int column = 0; column < kGridColumns; ++column) {
    tallest = std::max(tallest, grid.cardHeight(column));
  }
  grid.rowHeight = tallest + grid.margins * 2;
  return grid;
}

// ---------------------------------------------------------------------------
// Card drawing
// ---------------------------------------------------------------------------

// Draws the card name. The shared label pass owns the marquee motion and the continuous
// edge fade; the card only supplies the label box and its text style.
void drawCardName(ImDrawList* drawList, const ScreenMap& map, const ui::Box& designBox,
    const std::string& text, const ui::TextStyle& style, double nowSec) {
  ui::drawLabel(drawList, map.canvas, designBox, text, style, nowSec);
}

void drawChartCard(const ScreenMap& map, AppContext& context, const ChartRecord& record,
    int x0, int y0, int x1, int y1, bool manageMode, bool selected, double nowSec) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (x1 <= x0 || y1 <= y0) return;
  const ImVec2 topLeft(static_cast<float>(x0), static_cast<float>(y0));
  const ImVec2 bottomRight(static_cast<float>(x1), static_cast<float>(y1));

  // Plate fill, then the artwork and its shade, then the plate stroke on top.
  drawList->AddRectFilled(topLeft, bottomRight, ui::argb(kCardFill));
  const std::uint64_t cover = coverTexture(context, record, 320);
  if (cover != 0) {
    drawList->PushClipRect(topLeft, bottomRight, true);
    ui::drawImage(drawList, ui::PxRect{topLeft, bottomRight}, cover,
        context.textures.width(cover), context.textures.height(cover), ui::ImageFit::CenterCrop,
        IM_COL32(255, 255, 255, kCoverAlpha));
    drawList->PopClipRect();
  }
  drawList->AddRectFilled(topLeft, bottomRight, ui::argb(kCoverShade));
  ui::PanelStyle border = ui::rectPanel(0x00000000U, ui::argb(kCardStroke));
  border.strokeWidthDp = 1.0F;
  ui::drawPanelShape(drawList, map.canvas, map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x1), static_cast<float>(y1)), border);

  const ImU32 white = ui::argb(0xFFFFFFFFU);
  const int halfWidth = (x1 - x0) / 2;
  const int bandHeight = map.rpx(28.0F);

  // Difficulty on the left half of the top band. The sizes are calibrated to the ink boxes
  // measured on the reference capture: the deployed interface renders these labels smaller
  // than the layout source's density values.
  ui::TextStyle difficultyStyle = labelStyle(9.2F, true, white);
  difficultyStyle.paddingLeftDp = 9.0F;
  difficultyStyle.paddingRightDp = 8.0F;
  ui::drawLabel(drawList, map.canvas,
      map.box(static_cast<float>(x0), static_cast<float>(y0),
          static_cast<float>(x0 + halfWidth), static_cast<float>(y0 + bandHeight)),
      nonEmpty(record.difficulty), difficultyStyle, nowSec);

  // Best score on the right half of the top band.
  ui::TextStyle scoreStyle = labelStyle(9.8F, false, white, ui::HAlign::Right);
  scoreStyle.paddingRightDp = 8.5F;
  ui::drawLabel(drawList, map.canvas,
      map.box(static_cast<float>(x0 + halfWidth), static_cast<float>(y0),
          static_cast<float>(x1), static_cast<float>(y0 + bandHeight)),
      formatScore(record.bestScore), scoreStyle, nowSec);

  // Chart name along the bottom edge; long names scroll, manage mode reserves the check
  // plate's corner.
  const int nameHeight = map.rpx(32.0F);
  const int nameBottom = y1 - map.rpx(4.0F);
  ui::TextStyle nameStyle = labelStyle(10.7F, true, white);
  nameStyle.fitWidth = false;
  nameStyle.marquee = manageMode || shouldMarquee(record.songName);
  nameStyle.paddingLeftDp = 10.0F;
  nameStyle.paddingRightDp = manageMode ? 44.0F : 12.0F;
  nameStyle.fadingEdgeDp = 18.0F;
  drawCardName(drawList, map,
      map.box(static_cast<float>(x0), static_cast<float>(nameBottom - nameHeight),
          static_cast<float>(x1), static_cast<float>(nameBottom)),
      nonEmpty(record.songName), nameStyle, nowSec);

  // Selection plate shown while managing the library.
  if (manageMode) {
    const int checkWidth = map.rpx(28.0F);
    const int checkHeight = map.rpx(20.0F);
    const int checkX = x1 - checkWidth;
    const int checkY = y1 - checkHeight;
    ui::PanelStyle check = ui::glassPanel(ui::argb(kCheckFill), ui::argb(kCheckFill));
    check.straightRight = true;
    check.strokeWidthDp = 1.0F;
    ui::drawPanelShape(drawList, map.canvas,
        map.box(static_cast<float>(checkX), static_cast<float>(checkY),
            static_cast<float>(x1), static_cast<float>(y1)),
        check);
    if (selected) {
      const std::uint64_t checked = assetTexture(context, "res/checked.png", 64);
      if (checked != 0) {
        const int padLeft = map.rpx(6.0F);
        const int padTop = map.rpx(3.0F);
        const int padRight = map.rpx(5.0F);
        const int padBottom = map.rpx(3.0F);
        ui::drawImage(drawList,
            ui::PxRect{ImVec2(static_cast<float>(checkX + padLeft),
                          static_cast<float>(checkY + padTop)),
                ImVec2(static_cast<float>(x1 - padRight),
                    static_cast<float>(y1 - padBottom))},
            checked, context.textures.width(checked), context.textures.height(checked),
            ui::ImageFit::FitCenter);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Corner plates, back plate, tab strip and search field
// ---------------------------------------------------------------------------

struct PlateMetrics {
  int width{};
  int height{};
};

PlateMetrics sidePlateMetrics(const ScreenMap& map) {
  PlateMetrics metrics;
  metrics.width = map.rpx(78.0F);
  metrics.height = map.rpx(48.0F);
  return metrics;
}

// One 78 x 48 dp corner plate with a bright leading edge and a centred icon.
bool drawSidePlate(const ScreenMap& map, AppContext& context, const char* id, const char* asset,
    int rightMarginDp, int bottomMarginDp, bool topAligned, int accentEdge, bool interactive) {
  const PlateMetrics metrics = sidePlateMetrics(map);
  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int viewportHeight = static_cast<int>(std::lround(map.viewport.y));
  const int x1 = viewportWidth - map.rpx(static_cast<float>(rightMarginDp));
  int y0 = 0;
  int y1 = 0;
  if (topAligned) {
    y0 = map.rpx(static_cast<float>(bottomMarginDp));
    y1 = y0 + metrics.height;
  } else {
    y1 = viewportHeight - (map.rpx(static_cast<float>(bottomMarginDp)) + map.rpx(8.0F));
    y0 = y1 - metrics.height;
  }
  const int x0 = x1 - metrics.width;

  ui::PanelStyle style = ui::glassPanel(ui::argb(kPlateFill), 0x00FFFFFFU);
  style.strokeWidthDp = 0.0F;
  style.straightRight = true;
  style.accentEdge = accentEdge;
  style.accent = ui::argb(0xFFFFFFFFU);
  style.accentWidthDp = 3.2F;
  const ui::Box box = map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x1), static_cast<float>(y1));
  ui::Interaction interaction;
  if (interactive) {
    interaction = ui::panelInteraction(id, map.canvas, box, style);
  }
  const std::uint64_t texture = assetTexture(context, asset, 96);
  ui::drawIconPlate(ImGui::GetWindowDrawList(), map.canvas, box, style, texture,
      context.textures.width(texture), context.textures.height(texture), 16.0F, 10.0F, 3.0F);
  return interaction.pressed;
}

// Page indicator plate used by the catalogue tabs: 88 x 34 dp above the two page buttons.
bool drawPagePlate(const ScreenMap& map, int page, int pageCount, bool interactive) {
  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int viewportHeight = static_cast<int>(std::lround(map.viewport.y));
  const int width = map.rpx(88.0F);
  const int height = map.rpx(34.0F);
  const int x1 = viewportWidth;
  const int x0 = x1 - width;
  const int y1 = viewportHeight - map.rpx(124.0F);
  const int y0 = y1 - height;
  ui::PanelStyle style = ui::glassPanel(ui::argb(kPageFill), ui::argb(kPageStroke));
  style.strokeWidthDp = 1.0F;
  style.straightRight = true;
  const ui::Box box = map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x1), static_cast<float>(y1));
  ui::Interaction interaction;
  if (interactive) {
    interaction = ui::panelInteraction("##library_page", map.canvas, box, style);
  }
  ui::drawPanelShape(ImGui::GetWindowDrawList(), map.canvas, box, style);
  ui::TextStyle textStyle = labelStyle(12.4F, true, ui::argb(0xFFFFFFFFU), ui::HAlign::Center);
  const std::string text = std::to_string(page + 1) + "/" + std::to_string(pageCount);
  ui::drawLabel(ImGui::GetWindowDrawList(), map.canvas, box, text, textStyle, 0.0);
  return interaction.pressed;
}

bool drawBackPlate(const ScreenMap& map, AppContext& context, bool interactive) {
  const int x0 = 0;
  const int y0 = map.rpx(8.0F);
  const int x1 = map.rpx(78.0F);
  const int y1 = y0 + map.rpx(48.0F);
  ui::PanelStyle style = ui::glassPanel(ui::argb(kPlateFill), 0x00FFFFFFU);
  style.strokeWidthDp = 0.0F;
  style.straightLeft = true;
  style.accentEdge = 2; // right edge
  style.accent = ui::argb(0xFFFFFFFFU);
  style.accentWidthDp = 3.2F;
  const ui::Box box = map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x1), static_cast<float>(y1));
  ui::Interaction interaction;
  if (interactive) {
    interaction = ui::panelInteraction("##library_back", map.canvas, box, style);
  }
  // The visible arrow is centred on the parallelogram's centroid, not its bounding box.
  const float widthDp = 78.0F;
  const float heightDp = 48.0F;
  const float skewDp = std::min(heightDp * kSlope, widthDp * 0.42F);
  const float imageSizeDp = std::min(widthDp - 32.0F, heightDp - 20.0F);
  const float translateXDp = -skewDp * 0.25F + imageSizeDp * (0.5F - 87.337F / 200.0F);
  const std::uint64_t texture = assetTexture(context, "res/return.png", 96);
  ui::drawIconPlate(ImGui::GetWindowDrawList(), map.canvas, box, style, texture,
      context.textures.width(texture), context.textures.height(texture), 16.0F, 10.0F,
      translateXDp);
  return interaction.pressed;
}

int drawTabStrip(const ScreenMap& map, AppContext& context, bool interactive) {
  const int x0 = map.rpx(170.0F);
  const int y0 = map.rpx(11.0F);
  const int width = map.rpx(330.0F);
  const int height = map.rpx(42.0F);
  const ui::Box box = map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x0 + width), static_cast<float>(y0 + height));
  int clicked = -1;
  if (interactive) {
    const RectInteraction interaction = rectInteraction("##library_tabs",
        static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x0 + width),
        static_cast<float>(y0 + height));
    if (interaction.pressed) {
      // The slice comes from the press position, so a finger that slides from one tab to the
      // next before lifting selects the tab it was pressed on.
      clicked = ui::hitTabBar(map.canvas, box,
          ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left],
          static_cast<int>(std::size(kChartTabs)));
    }
  }
  const ListState& current = state();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float pixelWidth = static_cast<float>(width);
  const float pixelHeight = static_cast<float>(height);
  const float barTop = pixelHeight * 0.14F;
  const float barBottom = pixelHeight * 0.86F;
  const float trackSkew = std::tan(15.0F * 3.14159265358979323846F / 180.0F)
      * (barBottom - barTop);
  // The sliding plate spans the full tab height, so its trailing edge slants further than
  // the dark track behind it.
  const float plateSkew = std::tan(15.0F * 3.14159265358979323846F / 180.0F) * pixelHeight;
  const auto skewRect = [&](float leftTop, float top, float rightTop, float bottom,
      float skew, ImU32 color) {
    ImVec2 points[4];
    points[0] = ImVec2(static_cast<float>(x0) + leftTop, static_cast<float>(y0) + top);
    points[1] = ImVec2(static_cast<float>(x0) + rightTop, static_cast<float>(y0) + top);
    points[2] = ImVec2(static_cast<float>(x0) + rightTop - skew, static_cast<float>(y0) + bottom);
    points[3] = ImVec2(static_cast<float>(x0) + leftTop - skew, static_cast<float>(y0) + bottom);
    drawList->AddConvexPolyFilled(points, 4, color);
  };

  static constexpr float kCenters[3] = {0.22F, 0.50F, 0.78F};
  static constexpr float kWidths[3] = {0.23F, 0.23F, 0.29F};
  const float progress = ui::progress(current.tabs.startSec, 0.34F,
      ui::Interp::EaseOutCubic, context.nowSec);
  const bool moving = current.tabs.from != current.tabs.to && current.tabs.startSec > 0.0;
  const int fromIndex = std::clamp(current.tabs.from, 0, 2);
  const int toIndex = std::clamp(current.tabs.to, 0, 2);
  const float selectedCenter = kCenters[fromIndex]
      + (kCenters[toIndex] - kCenters[fromIndex]) * progress;
  const float selectedWidth = kWidths[fromIndex]
      + (kWidths[toIndex] - kWidths[fromIndex]) * progress;
  const float selectedLeft = selectedCenter - selectedWidth * 0.5F;
  const float selectedRight = selectedCenter + selectedWidth * 0.5F;

  skewRect(pixelWidth * 0.055F + trackSkew, barTop, pixelWidth * 0.945F, barBottom, trackSkew,
      IM_COL32(5, 7, 11, 178));
  skewRect(selectedLeft * pixelWidth + trackSkew, 0.0F, selectedRight * pixelWidth, pixelHeight,
      plateSkew, IM_COL32(232, 234, 238, 255));

  for (int index = 0; index < 3; ++index) {
    float selectedAmount = 0.0F;
    if (!moving) {
      selectedAmount = index == toIndex ? 1.0F : 0.0F;
    } else if (index == fromIndex) {
      selectedAmount = 1.0F - progress;
    } else if (index == toIndex) {
      selectedAmount = progress;
    }
    const int tone = static_cast<int>(std::lround(255.0F * (1.0F - selectedAmount)));
    const ImU32 color = IM_COL32(tone, tone, tone, 255);
    const float available = std::max(1.0F,
        std::round(kWidths[index] * pixelWidth - trackSkew - map.px(8.0F)));
    const float centerX = static_cast<float>(x0) + kCenters[index] * pixelWidth;
    // The reference bakes the label into a fixed box centred on the slice and lets the box
    // fit the text; the calibrated size keeps the ink box on the captured one.
    ui::TextStyle style = labelStyle(11.8F, true, color, ui::HAlign::Center);
    ui::drawLabel(drawList, map.canvas,
        map.box(centerX - available * 0.5F, static_cast<float>(y0),
            centerX + available * 0.5F, static_cast<float>(y0 + height)),
        kChartTabs[index], style, context.nowSec);
  }
  return clicked;
}

bool drawSearchField(const ScreenMap& map, AppContext& context, bool interactive) {
  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int x1 = viewportWidth - map.rpx(98.0F);
  const int x0 = x1 - map.rpx(172.0F);
  const int y0 = map.rpx(14.0F);
  const int y1 = y0 + map.rpx(36.0F);
  ui::PanelStyle style = ui::rectPanel(ui::argb(kSearchFill), ui::argb(kSearchStroke));
  style.strokeWidthDp = 1.0F;
  const ui::Box box = map.box(static_cast<float>(x0), static_cast<float>(y0),
      static_cast<float>(x1), static_cast<float>(y1));
  bool pressed = false;
  if (interactive) {
    const RectInteraction interaction = rectInteraction("##library_search",
        static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
        static_cast<float>(y1));
    pressed = interaction.pressed;
  }
  ui::drawPanelShape(ImGui::GetWindowDrawList(), map.canvas, box, style);
  ui::TextStyle textStyle = labelStyle(11.9F, true, ui::argb(0xFFFFFFFFU));
  textStyle.paddingLeftDp = 18.0F;
  textStyle.paddingRightDp = 14.0F;
  const std::string text = context.searchQuery.empty() ? std::string("搜索曲名")
                                                       : context.searchQuery;
  ui::drawLabel(ImGui::GetWindowDrawList(), map.canvas, box, text, textStyle, context.nowSec);
  return pressed;
}

// ---------------------------------------------------------------------------
// Scrolling and card hits
// ---------------------------------------------------------------------------

void clampScroll(const GridGeometry& grid, int rows) {
  ListState& current = state();
  const int maxScroll = std::max(0, grid.contentHeight(rows) - grid.gridHeight());
  current.grid.scroll = std::clamp(current.grid.scroll, 0, maxScroll);
}

// Index of the card under a screen point, or -1 when the point is outside the grid or on
// the gutters between cards. The scroll offset is passed in because a press and its release
// are resolved against the offset that was current at that moment.
int cardAtPoint(const GridGeometry& grid, ImVec2 point, int scroll, int recordCount) {
  if (!insideRect(static_cast<float>(grid.gridL), static_cast<float>(grid.gridT),
          static_cast<float>(grid.gridR), static_cast<float>(grid.gridB), point)) {
    return -1;
  }
  const int localX = static_cast<int>(std::lround(point.x)) - grid.gridL;
  const int localY = static_cast<int>(std::lround(point.y)) - grid.gridT + scroll;
  int column = -1;
  for (int candidate = 0; candidate < kGridColumns; ++candidate) {
    if (localX >= grid.borders[static_cast<std::size_t>(candidate)]
        && localX < grid.borders[static_cast<std::size_t>(candidate) + 1]) {
      column = candidate;
      break;
    }
  }
  if (column < 0) return -1;
  const int row = localY / grid.rowHeight;
  if (row < 0) return -1;
  // localY is measured from the top of the scroll content, so a card starts at its row
  // offset plus the outer margin, not at the row offset plus the grid's screen origin.
  const int top = row * grid.rowHeight + grid.margins;
  const int height = grid.cardHeight(column);
  if (localY < top || localY >= top + height) return -1;
  const int index = row * kGridColumns + column;
  if (index < 0 || index >= recordCount) return -1;
  const int cardLeft = grid.cardLeft(column) - grid.gridL;
  const int cardWidth = grid.cardWidth(column);
  if (localX < cardLeft || localX >= cardLeft + cardWidth) return -1;
  return index;
}

struct GridInputResult {
  // Card index whose tap was released this frame, or -1.
  int tappedCard{-1};
  // Card index that completed a press-and-hold this frame, or -1. A hold is reported once
  // per gesture and is never also reported as a tap.
  int heldCard{-1};
};

// Handles wheel and drag scrolling, the press-and-hold that claims a card, and the tap that
// selects or opens one. `gestureOwned` is false when the press in flight began on another
// page; such a press may not start a drag or resolve as a tap here.
GridInputResult updateGridInput(const ScreenMap& map, const GridGeometry& grid, int rows,
    const std::vector<const ChartRecord*>& visible, double nowSec, bool gestureOwned) {
  GridInputResult result;
  ListState& current = state();
  ImGuiIO& io = ImGui::GetIO();
  const int recordCount = static_cast<int>(visible.size());
  const int maxScroll = std::max(0, grid.contentHeight(rows) - grid.gridHeight());
  const bool hovered = insideRect(static_cast<float>(grid.gridL), static_cast<float>(grid.gridT),
      static_cast<float>(grid.gridR), static_cast<float>(grid.gridB), io.MousePos);
  const float tapSlop = map.px(kTapSlopDp);

  if (hovered && io.MouseWheel != 0.0F) {
    current.grid.scroll -= static_cast<int>(std::lround(io.MouseWheel * map.px(80.0F)));
  }
  if (hovered && gestureOwned && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    current.grid.dragging = true;
    current.grid.armed = true;
    current.grid.dragStart = io.MousePos;
    current.grid.dragStartScroll = current.grid.scroll;
    current.grid.dragDistance = 0.0F;
    // The card is resolved on the press, not when the hold expires: by then the grid may
    // have scrolled and the finger would no longer sit over the card it started on.
    current.grid.pressStartSec = nowSec;
    current.grid.pressedCard = cardAtPoint(grid, io.MousePos, current.grid.scroll, recordCount);
    current.grid.longPressFired = false;
  }
  if (current.grid.dragging) {
    const ImVec2 delta(io.MousePos.x - current.grid.dragStart.x,
        io.MousePos.y - current.grid.dragStart.y);
    current.grid.dragDistance = std::max(current.grid.dragDistance,
        std::sqrt(delta.x * delta.x + delta.y * delta.y));
    current.grid.scroll = current.grid.dragStartScroll
        - static_cast<int>(std::lround(delta.y));
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      current.grid.dragging = false;
    }
  }
  current.grid.scroll = std::clamp(current.grid.scroll, 0, maxScroll);

  // A hold only counts while the finger stays put: moving past the tap slop hands the
  // gesture to the scroll view. It also has to still be down now; a release that happened
  // between two frames never reached the hold threshold while the finger was down.
  if (current.grid.armed && !current.grid.longPressFired && current.grid.pressedCard >= 0
      && current.grid.dragDistance <= tapSlop
      && ImGui::IsMouseDown(ImGuiMouseButton_Left)
      && nowSec - current.grid.pressStartSec >= kLongPressSeconds) {
    current.grid.longPressFired = true;
    result.heldCard = current.grid.pressedCard;
  }

  if (current.grid.armed) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return result;
    current.grid.armed = false;
    const bool wasDrag = current.grid.dragDistance > tapSlop;
    const bool consumedByHold = current.grid.longPressFired;
    current.grid.pressedCard = -1;
    current.grid.longPressFired = false;
    current.grid.dragDistance = 0.0F;
    // A drag scrolled the list and a hold already acted on the card; neither is a tap.
    if (wasDrag || consumedByHold) return result;
  } else {
    return result;
  }
  if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return result;
  // The tap is resolved against the offset on screen at release, which is the one the user
  // was looking at when the finger came up.
  const int tapped = cardAtPoint(grid, io.MousePos, current.grid.scroll, recordCount);
  if (tapped < 0) return result;
  // The click was released this frame; consume the drag bookkeeping.
  current.grid.dragDistance = 0.0F;
  result.tappedCard = tapped;
  return result;
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

struct DialogButton {
  const char* label;
  bool primary;
  int action; // 0 cancel, 1 neutral/clear, 2 confirm
};

void beginDialog(DialogState& dialog, DialogState::Kind kind, double nowSec) {
  dialog.kind = kind;
  dialog.openSec = nowSec;
  dialog.focusPending = true;
}

void closeDialog(DialogState& dialog) {
  dialog.kind = DialogState::Kind::None;
  dialog.openSec = -1.0;
  dialog.buffer[0] = '\0';
}

// Draws one of the reference dialogs: a slanted plate over a dimmed backdrop with a title,
// a slanted input box and a black action strip along the bottom.
void drawDialog(const ScreenMap& map, AppContext& context, int pageCount) {
  ListState& current = state();
  DialogState& dialog = current.dialog;
  if (dialog.kind == DialogState::Kind::None) return;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ImGuiIO& io = ImGui::GetIO();
  const double nowSec = context.nowSec;
  const bool justOpened = nowSec <= dialog.openSec;
  const float eased = justOpened ? 0.0F
                                 : ui::progress(dialog.openSec,
                                       static_cast<float>(kDialogEnterSeconds),
                                       ui::Interp::EaseOutCubic, nowSec);

  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int viewportHeight = static_cast<int>(std::lround(map.viewport.y));
  // Panel width: 58% of the available width capped at 620 dp, never below 340 dp.
  const int widthTarget = static_cast<int>(std::lround(
      std::min(static_cast<float>(viewportWidth) * 0.58F, map.px(620.0F))));
  const int widthMinimum = std::min(map.rpx(340.0F),
      static_cast<int>(std::lround(static_cast<float>(viewportWidth) * 0.92F)));
  const int panelWidth = std::max(widthMinimum, widthTarget);

  const int padHorizontal = map.rpx(26.0F);
  const int padTop = map.rpx(22.0F);
  const int padBottom = map.rpx(16.0F);
  const int stripHeight = map.rpx(52.0F);
  const int accentWidth = map.rpx(3.0F);
  const bool messageKind = dialog.kind == DialogState::Kind::ImportSource
      || dialog.kind == DialogState::Kind::DeleteConfirm;
  const int titleBand = static_cast<int>(std::lround(lineHeightPixels(map, 22.0F)))
      + map.rpx(2.0F);
  const int ruleHeight = map.rpx(2.0F);
  const int ruleTop = map.rpx(6.0F);
  const int ruleBottom = map.rpx(12.0F);
  const int inputHeight = messageKind
      ? static_cast<int>(std::lround(lineHeightPixels(map, 17.0F))) + map.rpx(16.0F) * 2
      : std::max(map.rpx(48.0F),
            static_cast<int>(std::lround(lineHeightPixels(map, 18.0F))) + map.rpx(10.0F) * 2);
  const int inputTop = map.rpx(6.0F);
  const int inputBottom = map.rpx(6.0F);
  const int contentHeight = padTop + titleBand + ruleTop + ruleHeight + ruleBottom + inputTop
      + inputHeight + inputBottom;
  const int totalHeight = contentHeight + padBottom + stripHeight;
  const float panelLeft = static_cast<float>((viewportWidth - panelWidth) / 2);
  const float panelTop = static_cast<float>((viewportHeight - totalHeight) / 2);
  const float centerX = panelLeft + static_cast<float>(panelWidth) * 0.5F;
  const float centerY = panelTop + static_cast<float>(totalHeight) * 0.5F;
  const float zoom = 0.65F + 0.35F * eased;
  const float panelAlpha = eased;

  const auto point = [&](float x, float y) {
    return ImVec2(centerX + (x - centerX) * zoom, centerY + (y - centerY) * zoom);
  };
  const auto transformBox = [&](float x0, float y0, float x1, float y1) {
    return map.box(centerX + (x0 - centerX) * zoom, centerY + (y0 - centerY) * zoom,
        centerX + (x1 - centerX) * zoom, centerY + (y1 - centerY) * zoom);
  };
  const auto fadeColor = [&](std::uint32_t color) {
    return ui::fade(ui::argb(color), panelAlpha);
  };

  // Dimmed backdrop behind the panel.
  const int backdropAlpha = static_cast<int>(std::lround(0.5F * panelAlpha * 255.0F));
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(map.viewport.x, map.viewport.y),
      IM_COL32(0, 0, 0, std::clamp(backdropAlpha, 0, 255)));

  const float slant = static_cast<float>(totalHeight) * kSlope;
  const float stripSlant = static_cast<float>(stripHeight) * kSlope;
  const float contentBottom = panelTop + static_cast<float>(contentHeight);
  ImVec2 contentPoints[4];
  contentPoints[0] = point(panelLeft + slant, panelTop);
  contentPoints[1] = point(panelLeft + static_cast<float>(panelWidth), panelTop);
  contentPoints[2] = point(panelLeft + static_cast<float>(panelWidth)
          - static_cast<float>(contentHeight) * kSlope,
      contentBottom);
  contentPoints[3] = point(panelLeft + stripSlant, contentBottom);
  drawList->AddConvexPolyFilled(contentPoints, 4, fadeColor(kDialogContent));

  ImVec2 stripPoints[4];
  stripPoints[0] = point(panelLeft + stripSlant, contentBottom);
  stripPoints[1] = point(panelLeft + static_cast<float>(panelWidth)
          - static_cast<float>(contentHeight) * kSlope,
      contentBottom);
  stripPoints[2] = point(panelLeft + static_cast<float>(panelWidth) - slant,
      panelTop + static_cast<float>(totalHeight));
  stripPoints[3] = point(panelLeft, panelTop + static_cast<float>(totalHeight));
  drawList->AddConvexPolyFilled(stripPoints, 4, fadeColor(kDialogStrip));

  ImVec2 accentPoints[4];
  accentPoints[0] = point(panelLeft + slant, panelTop);
  accentPoints[1] = point(panelLeft + slant + static_cast<float>(accentWidth), panelTop);
  accentPoints[2] = point(panelLeft + stripSlant + static_cast<float>(accentWidth), contentBottom);
  accentPoints[3] = point(panelLeft + stripSlant, contentBottom);
  drawList->AddConvexPolyFilled(accentPoints, 4, fadeColor(0xFFFFFFFFU));

  // Content rows follow the slanted left edge, shifted by their own centre height.
  const float contentLeft = panelLeft + static_cast<float>(std::lround(stripSlant))
      + static_cast<float>(padHorizontal);
  const float contentWidth = std::max(1.0F, static_cast<float>(panelWidth)
          - static_cast<float>(std::lround(slant)) - static_cast<float>(padHorizontal) * 2.0F);
  const auto rowShift = [&](float rowTop, float rowHeight) {
    const float center = static_cast<float>(padTop) + rowTop + rowHeight * 0.5F;
    return static_cast<float>(std::lround((static_cast<float>(contentHeight) - center) * kSlope));
  };
  const auto textSize = [&](float sizeDp) { return sizeDp * zoom; };

  // Title.
  {
    const float shift = rowShift(0.0F, static_cast<float>(titleBand));
    const ui::Box box = transformBox(contentLeft + shift, panelTop + static_cast<float>(padTop),
        contentLeft + shift + contentWidth,
        panelTop + static_cast<float>(padTop + titleBand));
    ui::TextStyle style = labelStyle(textSize(22.0F), false, fadeColor(0xFFFFFFFFU));
    style.color = fadeColor(0xFFFFFFFFU);
    const char* title = "搜索曲名";
    switch (dialog.kind) {
      case DialogState::Kind::Search: title = "搜索曲名"; break;
      case DialogState::Kind::PageJump: title = "跳转页号"; break;
      case DialogState::Kind::ImportSource: title = "导入谱面"; break;
      case DialogState::Kind::DeleteConfirm: title = "确认删除"; break;
      case DialogState::Kind::None: break;
    }
    ui::drawLabel(drawList, map.canvas, box, title, style, nowSec);
  }
  // Rule under the title.
  {
    const float shift = rowShift(static_cast<float>(titleBand + ruleTop),
        static_cast<float>(ruleHeight));
    const float ruleY = panelTop + static_cast<float>(padTop + titleBand + ruleTop);
    const ImVec2 min(centerX + (contentLeft + shift - centerX) * zoom,
        centerY + (ruleY - centerY) * zoom);
    const ImVec2 max(centerX + (contentLeft + shift + map.px(44.0F) - centerX) * zoom,
        centerY + (ruleY + static_cast<float>(ruleHeight) - centerY) * zoom);
    drawList->AddRectFilled(min, max, fadeColor(0x99FFFFFFU));
  }

  // Slanted input box with a bright underline; the message-only dialogs use the same band
  // for their text instead.
  const float inputTopY = panelTop + static_cast<float>(padTop + titleBand + ruleTop + ruleHeight
      + ruleBottom + inputTop);
  if (messageKind) {
    const float shift = rowShift(static_cast<float>(padTop + titleBand + ruleTop + ruleHeight
                                    + ruleBottom + inputTop),
        static_cast<float>(inputHeight));
    std::string message;
    if (dialog.kind == DialogState::Kind::ImportSource) {
      message = "选择要导入的来源";
    } else {
      message = "确定要删除选中的 " + std::to_string(context.selectedIds.size()) + " 个谱面吗？";
    }
    ui::TextStyle style = labelStyle(textSize(17.0F), false, fadeColor(0xE6FFFFFFU));
    style.fitWidth = true;
    ui::drawLabel(drawList, map.canvas,
        transformBox(contentLeft + shift, inputTopY, contentLeft + shift + contentWidth,
            inputTopY + static_cast<float>(inputHeight)),
        message, style, nowSec);
  } else {
    const float shift = rowShift(static_cast<float>(padTop + titleBand + ruleTop + ruleHeight
                                    + ruleBottom + inputTop),
        static_cast<float>(inputHeight));
    const float left = contentLeft + shift;
    const float right = left + contentWidth;
    const float inputSlant = static_cast<float>(inputHeight) * kSlope;
    ImVec2 points[4];
    points[0] = point(left + inputSlant, inputTopY);
    points[1] = point(right, inputTopY);
    points[2] = point(right - inputSlant, inputTopY + static_cast<float>(inputHeight));
    points[3] = point(left, inputTopY + static_cast<float>(inputHeight));
    drawList->AddConvexPolyFilled(points, 4, fadeColor(0x24FFFFFFU));
    const float lineHeight = std::max(1.0F, static_cast<float>(inputHeight) * 0.045F);
    ImVec2 linePoints[4];
    linePoints[0] = point(left, inputTopY + static_cast<float>(inputHeight) - lineHeight);
    linePoints[1] = point(right - inputSlant, inputTopY + static_cast<float>(inputHeight)
            - lineHeight);
    linePoints[2] = point(right - inputSlant, inputTopY + static_cast<float>(inputHeight));
    linePoints[3] = point(left, inputTopY + static_cast<float>(inputHeight));
    drawList->AddConvexPolyFilled(linePoints, 4, fadeColor(0xCCFFFFFFU));

    // Editable text: a transparent ImGui input handles typing while the label below
    // renders the value with the interface glyph rasteriser.
    const float textPadX = static_cast<float>(map.rpx(14.0F) + map.rpx(6.0F));
    const float textPadY = static_cast<float>(map.rpx(10.0F));
    const float innerWidth = std::max(1.0F, contentWidth - textPadX * 2.0F);
    const float innerHeight = std::max(1.0F, static_cast<float>(inputHeight) - textPadY * 2.0F);
    ImGui::SetCursorScreenPos(ImVec2(centerX + (left + textPadX - centerX) * zoom,
        centerY + (inputTopY + textPadY - centerY) * zoom));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    const float inputFramePadding = std::max(0.0F,
        (innerHeight - ImGui::GetFontSize()) * 0.5F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0F, inputFramePadding));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
    if (dialog.focusPending) {
      ImGui::SetKeyboardFocusHere();
      dialog.focusPending = false;
    }
    ImGui::SetNextItemWidth(innerWidth * zoom);
    const ImGuiInputTextFlags flags = dialog.kind == DialogState::Kind::PageJump
        ? ImGuiInputTextFlags_CharsDecimal
        : ImGuiInputTextFlags_None;
    ImGui::InputText("##library_dialog_input", dialog.buffer, sizeof(dialog.buffer), flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    ui::TextStyle inputStyle = labelStyle(textSize(18.0F), true, fadeColor(0xFFFFFFFFU));
    if (dialog.buffer[0] == '\0') {
      inputStyle.color = fadeColor(0x80FFFFFFU);
      ui::drawLabel(drawList, map.canvas,
          transformBox(left + textPadX, inputTopY + textPadY, right - textPadX,
              inputTopY + static_cast<float>(inputHeight) - textPadY),
          "输入曲名", inputStyle, nowSec);
    } else {
      ui::drawLabel(drawList, map.canvas,
          transformBox(left + textPadX, inputTopY + textPadY, right - textPadX,
              inputTopY + static_cast<float>(inputHeight) - textPadY),
          dialog.buffer, inputStyle, nowSec);
    }
  }

  // Action strip buttons.
  std::array<DialogButton, 3> buttons{};
  int buttonCount = 0;
  switch (dialog.kind) {
    case DialogState::Kind::Search:
      buttons[0] = DialogButton{"取消", false, 0};
      buttons[1] = DialogButton{"显示全部", false, 1};
      buttons[2] = DialogButton{"搜索", true, 2};
      buttonCount = 3;
      break;
    case DialogState::Kind::PageJump:
      buttons[0] = DialogButton{"取消", false, 0};
      buttons[1] = DialogButton{"确定", true, 2};
      buttonCount = 2;
      break;
    case DialogState::Kind::ImportSource:
      buttons[buttonCount++] = DialogButton{"压缩包", true, 1};
      if (platform::FileDialog::supportsChartFolder()) {
        buttons[buttonCount++] = DialogButton{"文件夹", true, 2};
      }
      buttons[buttonCount++] = DialogButton{"散文件", true, 3};
      break;
    case DialogState::Kind::DeleteConfirm:
      buttons[0] = DialogButton{"取消", false, 0};
      buttons[1] = DialogButton{"删除", true, 2};
      buttonCount = 2;
      break;
    case DialogState::Kind::None:
      break;
  }
  const float stripBase = static_cast<float>(panelWidth) - slant;
  const float step = stripBase / static_cast<float>(buttonCount);
  const float cellWidth = static_cast<float>(std::lround(stripBase / static_cast<float>(
      buttonCount)))
      + stripSlant + 1.0F;
  const float labelPad = stripSlant + map.px(22.0F);
  int pressedButton = -1;
  const bool interactionsAllowed = !justOpened && eased > 0.0F;
  // A dialog belongs to the page under it: a press that arrived while another page was up
  // cannot press its actions.
  const bool pageOwned = ui::pagePressOwned();
  for (int index = 0; index < buttonCount; ++index) {
    const float cellLeft = panelLeft + static_cast<float>(std::lround(static_cast<float>(index)
        * (stripBase / static_cast<float>(buttonCount))));
    const float cellTop = contentBottom;
    const float cellRight = cellLeft + cellWidth;
    const float cellBottom = cellTop + static_cast<float>(stripHeight);
    const float cellL = centerX + (cellLeft - centerX) * zoom;
    const float cellT = centerY + (cellTop - centerY) * zoom;
    const float cellR = centerX + (cellRight - centerX) * zoom;
    const float cellB = centerY + (cellBottom - centerY) * zoom;
    bool hovered = false;
    if (interactionsAllowed) {
      hovered = insideRect(cellL, cellT, cellR, cellB, io.MousePos);
      if (hovered && io.MouseDown[0]) {
        ImVec2 points[4];
        points[0] = point(cellLeft + stripSlant, cellTop);
        points[1] = point(cellRight, cellTop);
        points[2] = point(cellRight - stripSlant, cellBottom);
        points[3] = point(cellLeft, cellBottom);
        drawList->AddConvexPolyFilled(points, 4, fadeColor(0x33FFFFFFU));
      }
      // Both ends of the gesture have to sit in this cell: releasing over a button after
      // pressing the dimmed backdrop is not a press of the button.
      if (pageOwned && hovered
          && insideRect(cellL, cellT, cellR, cellB,
              io.MouseClickedPos[ImGuiMouseButton_Left])
          && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        pressedButton = index;
      }
    }
    ui::HAlign align = ui::HAlign::Center;
    if (buttonCount == 1) {
      align = ui::HAlign::Center;
    } else if (index == 0) {
      align = ui::HAlign::Left;
    } else if (index == buttonCount - 1) {
      align = ui::HAlign::Right;
    }
    ui::TextStyle style = labelStyle(textSize(18.0F), false,
        fadeColor(buttons[static_cast<std::size_t>(index)].primary ? 0xFFFFFFFFU
                                                                   : 0xD9FFFFFFU),
        align);
    style.fitWidth = true;
    const float textLeft = centerX
        + (cellLeft + labelPad - centerX) * zoom;
    const float textRight = centerX
        + (cellRight - labelPad - centerX) * zoom;
    const ui::Box box = map.box(textLeft, centerY + (cellTop - centerY) * zoom, textRight,
        centerY + (cellBottom - centerY) * zoom);
    ui::drawLabel(drawList, map.canvas, box,
        buttons[static_cast<std::size_t>(index)].label, style, nowSec);
  }

  if (!interactionsAllowed) return;
  bool cancelRequested = false;
  if (pressedButton >= 0) {
    const DialogButton& button = buttons[static_cast<std::size_t>(pressedButton)];
    if (dialog.kind == DialogState::Kind::ImportSource) {
      // The picker is opened after the frame so it does not re-enter the draw list.
      current.pendingImport = button.action;
      cancelRequested = true;
    } else if (button.action == 0) {
      cancelRequested = true;
    } else if (button.action == 1) {
      context.searchQuery.clear();
      current.grid.scroll = 0;
      current.onlinePage = 0;
      cancelRequested = true;
    } else if (dialog.kind == DialogState::Kind::DeleteConfirm) {
      const auto ids = context.selectedIds;
      context.library.remove(ids);
      context.selectedIds.clear();
      context.manageMode = false;
      cancelRequested = true;
    } else if (dialog.kind == DialogState::Kind::Search) {
      std::string query = dialog.buffer;
      while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) {
        query.erase(query.begin());
      }
      while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back()))) {
        query.pop_back();
      }
      context.searchQuery = query;
      current.grid.scroll = 0;
      current.onlinePage = 0;
      cancelRequested = true;
    } else {
      const int target = std::atoi(dialog.buffer);
      current.onlinePage = std::clamp(target - 1, 0, std::max(0, pageCount - 1));
      current.grid.scroll = 0;
      cancelRequested = true;
    }
  }
  if (!cancelRequested) {
    // A tap outside the panel dismisses the dialog, like the reference modal. The press has to
    // start outside as well, so dragging out of a dialog button does not count as a backdrop
    // tap, and a press left over from the page before this one is ignored.
    const auto outsidePanel = [&](ImVec2 point) {
      return !insideRect(centerX + (panelLeft - centerX) * zoom,
          centerY + (panelTop - centerY) * zoom,
          centerX + (panelLeft + static_cast<float>(panelWidth) - centerX) * zoom,
          centerY + (panelTop + static_cast<float>(totalHeight) - centerY) * zoom, point);
    };
    const bool pressedOutside =
        outsidePanel(io.MouseClickedPos[ImGuiMouseButton_Left]);
    if (pageOwned && pressedOutside && outsidePanel(io.MousePos)
        && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      cancelRequested = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelRequested = true;
  }
  if (cancelRequested) closeDialog(dialog);
}

} // namespace

// ---------------------------------------------------------------------------
// Screen entry point
// ---------------------------------------------------------------------------

void drawLibraryScreen(AppContext& context) {
  const ScreenMap map = ScreenMap::current();
  ListState& current = state();
  const double nowSec = context.nowSec;
  // A large gap between draws means the screen was left and re-entered; drop transient UI.
  if (current.lastDrawSec >= 0.0 && nowSec - current.lastDrawSec > kReentryGuardSeconds) {
    current.dialog = DialogState{};
    current.grid = GridState{};
    current.onlinePage = 0;
    current.pendingImport = 0;
  }
  current.lastDrawSec = nowSec;

  ui::beginFullscreenWindow("##library");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(map.viewport.x, map.viewport.y), nowSec);

  context.libraryTab = std::clamp(context.libraryTab, 0, 2);
  if (context.libraryTab != current.tabs.to) {
    current.tabs.from = context.libraryTab;
    current.tabs.to = context.libraryTab;
    current.tabs.startSec = 0.0;
    current.grid = GridState{};
  }
  const bool localTab = context.libraryTab == 0;
  if (!localTab) {
    context.manageMode = false;
    context.selectedIds.clear();
  } else if (!context.manageMode) {
    context.selectedIds.clear();
  }

  const bool dialogOpen = current.dialog.kind != DialogState::Kind::None;
  const bool interactive = !dialogOpen;
  const GridGeometry grid = computeGrid(map);
  const int viewportWidth = static_cast<int>(std::lround(map.viewport.x));
  const int viewportHeight = static_cast<int>(std::lround(map.viewport.y));

  // Popups are opened from the action handlers at the end of the frame; the ImGui popup
  // stack needs a current window, so the request is serviced here at the start of the
  // next frame.
  // List panel.
  drawList->AddRectFilled(ImVec2(static_cast<float>(grid.panelL), static_cast<float>(grid.panelT)),
      ImVec2(static_cast<float>(grid.panelR), static_cast<float>(grid.panelB)),
      ui::argb(kPanelFill));

  // Records shown by the current tab. The catalogue tabs have no network content in this
  // port and deliberately stay empty; their page controls still match the reference.
  // The warm-up scan never blocks this screen: until it has published the index the grid
  // draws an empty scanning state and the list appears on the frame it completes.
  const bool libraryReady = context.library.ready();
  std::vector<const ChartRecord*> visible;
  if (localTab && libraryReady) {
    for (const auto& record : context.library.records()) {
      if (matchesSearch(record, context.searchQuery)) visible.push_back(&record);
    }
  }
  const int onlinePageCount = std::max(1,
      static_cast<int>((visible.size() + static_cast<std::size_t>(kOnlinePageSize) - 1)
          / static_cast<std::size_t>(kOnlinePageSize)));
  current.onlinePage = std::clamp(current.onlinePage, 0, onlinePageCount - 1);

  const int rows = localTab
      ? static_cast<int>((visible.size() + static_cast<std::size_t>(kGridColumns) - 1)
          / static_cast<std::size_t>(kGridColumns))
      : 0;
  if (!interactive) {
    // Keep the scroll inside the content while a dialog is up.
  }
  clampScroll(grid, rows);
  int clickedCard = -1;
  int heldCard = -1;
  if (interactive) {
    const GridInputResult input =
        updateGridInput(map, grid, rows, visible, nowSec, ui::pagePressOwned());
    clickedCard = input.tappedCard;
    heldCard = input.heldCard;
  }

  // Card grid, clipped to the scroll viewport.
  drawList->PushClipRect(ImVec2(static_cast<float>(grid.gridL), static_cast<float>(grid.gridT)),
      ImVec2(static_cast<float>(grid.gridR), static_cast<float>(grid.gridB)), true);
  if (localTab && !libraryReady) {
    const char* scanning = "正在扫描本地谱面...";
    ImFont* font = ImGui::GetFont();
    const float textSize = map.px(16.0F);
    const ImVec2 extent = font->CalcTextSizeA(textSize, FLT_MAX, 0.0F, scanning);
    const ImVec2 centre((grid.gridL + grid.gridR) * 0.5F, (grid.gridT + grid.gridB) * 0.5F);
    drawList->AddText(font, textSize,
        ImVec2(centre.x - extent.x * 0.5F, centre.y - extent.y * 0.5F),
        ui::argb(ui::kTextSecondary), scanning);
  }
  for (std::size_t index = 0; index < visible.size(); ++index) {
    const int column = static_cast<int>(index) % kGridColumns;
    const int row = static_cast<int>(index) / kGridColumns;
    const int x0 = grid.cardLeft(column);
    const int x1 = grid.cardRight(column);
    const int y0 = grid.cardTop(row, current.grid.scroll);
    const int y1 = y0 + grid.cardHeight(column);
    if (y1 < grid.gridT || y0 > grid.gridB) continue;
    const auto selected = std::find(context.selectedIds.begin(), context.selectedIds.end(),
                                visible[index]->id)
        != context.selectedIds.end();
    drawChartCard(map, context, *visible[index], x0, y0, x1, y1, context.manageMode, selected,
        nowSec);
  }
  drawList->PopClipRect();

  // Empty / loading state inside the panel. While the local index is still scanning the
  // placeholder above owns the panel; the empty-state label must not claim the library is
  // empty before the scan has published its result.
  if (!localTab || (libraryReady && visible.empty())) {
    const std::string stateText = localTab ? std::string("本地暂无谱面") : std::string("暂无谱面");
    const ui::Box stateBox = map.box(static_cast<float>(grid.panelL),
        static_cast<float>(grid.panelT + map.rpx(26.0F)),
        static_cast<float>(grid.panelR),
        static_cast<float>(grid.panelT + map.rpx(26.0F) + map.rpx(34.0F)));
    ui::TextStyle stateStyle = labelStyle(11.4F, true, ui::argb(kStateText),
        ui::HAlign::Center);
    ui::drawLabel(drawList, map.canvas, stateBox, stateText, stateStyle, nowSec);
  }

  // Manage-mode selection counter at the top centre of the screen.
  if (localTab && context.manageMode && !visible.empty()) {
    const int labelWidth = map.rpx(260.0F);
    const int labelHeight = map.rpx(22.0F);
    const int labelX = (viewportWidth - labelWidth) / 2;
    const int labelY = map.rpx(64.0F);
    const std::string text = "已选择 " + std::to_string(context.selectedIds.size()) + " / "
        + std::to_string(visible.size());
    ui::TextStyle style = labelStyle(8.2F, true, ui::argb(kStateText), ui::HAlign::Center);
    ui::drawLabel(drawList, map.canvas,
        map.box(static_cast<float>(labelX), static_cast<float>(labelY),
            static_cast<float>(labelX + labelWidth), static_cast<float>(labelY + labelHeight)),
        text, style, nowSec);
  }

  // Top bar: back plate, tab strip and search field. These stay visible but inert while a
  // dialog covers the screen.
  const bool backPressed = drawBackPlate(map, context, interactive);
  const int clickedTab = drawTabStrip(map, context, interactive);
  const bool searchPressed = drawSearchField(map, context, interactive);

  // Bottom-right action plates.
  bool importPressed = false;
  bool managePressed = false;
  bool deletePressed = false;
  bool selectAllPressed = false;
  bool cancelManagePressed = false;
  bool refreshPressed = false;
  bool pagePressed = false;
  if (localTab) {
    if (!context.manageMode) {
      managePressed = drawSidePlate(map, context, "##library_manage", "res/edit.png", 0, 58,
          false, 1, interactive);
      importPressed = drawSidePlate(map, context, "##library_import", "res/import.png", 0, 0,
          false, 1, interactive);
    } else {
      deletePressed = drawSidePlate(map, context, "##library_delete", "res/delete.png", 0, 116,
          false, 1, interactive);
      selectAllPressed = drawSidePlate(map, context, "##library_select_all", "res/all.png", 0,
          58, false, 1, interactive);
      cancelManagePressed = drawSidePlate(map, context, "##library_cancel", "res/cancel.png", 0,
          0, false, 1, interactive);
    }
  } else {
    refreshPressed = drawSidePlate(map, context, "##library_refresh", "res/refresh.png", 0, 8,
        true, 1, interactive);
    // Page navigation: next above the indicator, previous below it.
    (void)drawSidePlate(map, context, "##library_next", "res/next.png", 0, 58, false, 1,
        interactive);
    pagePressed = drawPagePlate(map, current.onlinePage, onlinePageCount, interactive);
    (void)drawSidePlate(map, context, "##library_last", "res/last.png", 0, 0, false, 1,
        interactive);
  }

  // Modal dialogs and the library import/delete confirmations.
  if (dialogOpen) {
    drawDialog(map, context, onlinePageCount);
  }

ui::endFullscreenWindow();

  // Actions are applied after the window closes so navigation does not disturb drawing.
  if (backPressed) {
    context.manageMode = false;
    context.selectedIds.clear();
    navigateTo(context, Screen::Home);
  } else if (clickedTab >= 0 && clickedTab != context.libraryTab
      && nowSec - current.lastTabSwitchSec >= kTabSwitchIntervalSeconds) {
    current.lastTabSwitchSec = nowSec;
    current.tabs.from = current.tabs.to;
    current.tabs.to = clickedTab;
    current.tabs.startSec = nowSec;
    current.grid = GridState{};
    context.libraryTab = clickedTab;
    context.manageMode = false;
    context.selectedIds.clear();
    if (clickedTab != 0) current.onlinePage = 0;
  } else if (searchPressed) {
    beginDialog(current.dialog, DialogState::Kind::Search, nowSec);
    std::snprintf(current.dialog.buffer, sizeof(current.dialog.buffer), "%s",
        context.searchQuery.c_str());
  } else if (pagePressed) {
    beginDialog(current.dialog, DialogState::Kind::PageJump, nowSec);
    std::snprintf(current.dialog.buffer, sizeof(current.dialog.buffer), "%d",
        current.onlinePage + 1);
  } else if (managePressed) {
    context.manageMode = true;
    context.selectedIds.clear();
  } else if (importPressed) {
    beginDialog(current.dialog, DialogState::Kind::ImportSource, nowSec);
  } else if (deletePressed) {
    if (context.selectedIds.empty()) {
      notify(context, "未选择谱面");
    } else {
      beginDialog(current.dialog, DialogState::Kind::DeleteConfirm, nowSec);
    }
  } else if (selectAllPressed) {
    if (context.selectedIds.size() >= visible.size()) {
      context.selectedIds.clear();
    } else {
      context.selectedIds.clear();
      for (const ChartRecord* record : visible) context.selectedIds.push_back(record->id);
    }
  } else if (cancelManagePressed) {
    context.manageMode = false;
    context.selectedIds.clear();
  } else if (refreshPressed) {
    notify(context, "在线谱面暂不可用");
  } else if (heldCard >= 0 && heldCard < static_cast<int>(visible.size())) {
    // Holding a card is a second way into manage mode. Unlike the plate it keeps whatever
    // was already selected once the mode is on, so a hold while managing only adds to the
    // selection instead of starting it over.
    if (!context.manageMode) {
      context.manageMode = true;
      context.selectedIds.clear();
    }
    const std::string& heldId = visible[static_cast<std::size_t>(heldCard)]->id;
    if (std::find(context.selectedIds.begin(), context.selectedIds.end(), heldId)
        == context.selectedIds.end()) {
      context.selectedIds.push_back(heldId);
    }
  } else if (clickedCard >= 0 && clickedCard < static_cast<int>(visible.size())) {
    const ChartRecord& record = *visible[static_cast<std::size_t>(clickedCard)];
    if (context.manageMode) {
      const auto iterator = std::find(context.selectedIds.begin(), context.selectedIds.end(),
          record.id);
      if (iterator == context.selectedIds.end()) {
        context.selectedIds.push_back(record.id);
      } else {
        context.selectedIds.erase(iterator);
      }
    } else {
      // The detail page draws the artwork full screen at a much larger size than the card
      // thumbnail, and a new size means a fresh decode. Requesting it here starts that work
      // while the page transitions instead of leaving the detail page's first frames bare.
      (void)coverTexture(context, record, 2048);
      openChartDetail(context, record.id);
    }
  }

  // Import actions run after the frame so the platform picker can block and the resulting
  // navigation happens outside the draw list.
  if (current.pendingImport != 0) {
    const int action = current.pendingImport;
    current.pendingImport = 0;
    switch (action) {
      case 1: (void)importChartPackageFromDialog(context); break;
      case 2: (void)importChartFolderFromDialog(context); break;
      case 3: (void)importChartFilesFromDialog(context); break;
      default: break;
    }
  }
}

} // namespace rechigros::app
