#include "rechigros/app/ScreenCommon.hpp"

#include "ImportDialogs.hpp"

#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Settings screen. The reference builds the page from a vertical list of skewed rows that
// sit on a full-screen slanted field: section headings, label/value rows, sliders with a
// skewed track and thumb, switches with an animated knob, edit rows, action rows and replay
// entries. Every row is placed on the same 15 degree band and translated per its absolute
// position, so the band slants with scrolling. This file reproduces that layout, the exact
// row metrics and the widget drawing of the reference custom views.
namespace rechigros::app {
namespace {

// ---------------------------------------------------------------------------
// Reference metrics
// ---------------------------------------------------------------------------

constexpr float kSlope = 0.26794919F; // tan(15 degrees), the band angle
// Line box height of one text line in the reference view layer, in multiples of the text
// size. The reference layout is built by a platform text view whose measured line height is
// this many text sizes; row heights depend on it.
constexpr float kTextLineFactor = 2.88F;
constexpr float kToggleSkewFactor = 0.268F;
constexpr int kTabCount = 4;
constexpr const char* kTabLabels[kTabCount] = {"视频", "音频", "回放", "更多"};
constexpr double kTabAnimationSec = 0.34;
constexpr double kTabSwitchMinSec = 0.26;
constexpr double kToggleAnimationSec = 0.14;
constexpr float kSectionBottomGapDp = 24.0F;
constexpr float kSafeTextInsetDp = 0.0F;

// Reference palette (0xAARRGGBB).
constexpr std::uint32_t kBackdropFill = 0x70505559U;
constexpr std::uint32_t kBackPlateFill = 0x8610171DU;
constexpr std::uint32_t kTabTrack = 0xB205070BU;
constexpr std::uint32_t kTabSelectedPlate = 0xFFE8EAEEU;
constexpr std::uint32_t kHeadingText = 0xAFFFFFFFU;
constexpr std::uint32_t kSliderName = 0xF4FFFFFFU;
constexpr std::uint32_t kSliderValue = 0xCFFFFFFFU;
constexpr std::uint32_t kSliderTrackColor = 0x66303338U;
constexpr std::uint32_t kSwitchName = 0xF4FFFFFFU;
constexpr std::uint32_t kSwitchTrack = 0x303338U;
constexpr std::uint32_t kEditName = 0xEFFFFFFFU;
constexpr std::uint32_t kEditField = 0x35FFFFFFU;
constexpr std::uint32_t kActionName = 0xEFFFFFFFU;
constexpr std::uint32_t kActionValue = 0xCFFFFFFFU;
constexpr std::uint32_t kSmallButtonFill = 0xFFFFFFFFU;
constexpr std::uint32_t kSmallButtonText = 0xFF10151BU;
constexpr std::uint32_t kReplayName = 0xFFFFFFFFU;
constexpr std::uint32_t kReplayScore = 0xDFFFFFFFU;
constexpr std::uint32_t kEmptyNotice = 0xB8FFFFFFU;

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

// The interface canvas maps a 731 x 415 design box onto the viewport. The settings layout is
// authored in density pixels like the reference (dp values scaled by the display density),
// so every position is computed in pixels and converted to design units only when a shared
// draw helper needs a design box.
struct Metrics {
  ui::Canvas canvas;
  float scale{1.0F};
  float density{1.75F};
  float width{};
  float height{};

  // Reference dp(): density value rounded to the nearest whole pixel.
  [[nodiscard]] float dp(float value) const { return std::round(value * density); }
  // One reference text line box, in pixels.
  [[nodiscard]] float lineBox(float sizeDp) const {
    return std::round(kTextLineFactor * sizeDp * density);
  }
  [[nodiscard]] ui::Box pixelBox(float x0, float y0, float x1, float y1) const {
    return ui::Box{(x0 - canvas.origin.x) / scale, (y0 - canvas.origin.y) / scale,
        (x1 - canvas.origin.x) / scale, (y1 - canvas.origin.y) / scale};
  }
};

Metrics metrics() {
  Metrics result;
  result.canvas = ui::canvas();
  result.scale = std::max(0.01F, result.canvas.scale);
  // Density is the page's dp-to-pixel factor. It already folds the canvas scale in, which is
  // why a 22dp label lands at the same physical size on any window; the interface scale is
  // applied on top of it so the user's zoom enlarges the page as a whole.
  result.density = result.canvas.dp(1.0F);
  result.width = result.canvas.viewport.x;
  result.height = result.canvas.viewport.y;
  return result;
}

// Background pixels per density unit for the current frame.
float contentTopPx(const Metrics& m) { return m.dp(90.0F); }

float rowWidthPx(const Metrics& m) {
  const float safe = m.dp(kSafeTextInsetDp) * 2.0F;
  return std::max(1.0F, m.width - m.dp(96.0F) - std::round(kSlope * m.height) - safe);
}

// Row x offset on the slanted band. The reference derives the translation from the row's
// absolute position inside the scroll container, including the container's top padding, so
// the rows follow the backdrop edge. Reproduced verbatim for identical offsets.
float rowLeftPx(const Metrics& m, float contentY, float scrollY) {
  const float base = m.dp(48.0F) + kSlope * m.height;
  const float absolute = 2.0F * contentTopPx(m) + contentY - scrollY;
  return std::round(base - kSlope * absolute);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void fillQuad(ImDrawList* drawList, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color) {
  ImVec2 points[4] = {a, b, c, d};
  drawList->AddConvexPolyFilled(points, 4, color);
}

// Parallelogram used by several reference custom views: the bottom edge is shifted left.
void skewedBlock(ImDrawList* drawList, float x, float y, float width, float height, float skew,
    ImU32 color) {
  fillQuad(drawList, ImVec2(x + skew, y), ImVec2(x + width, y),
      ImVec2(x + width - skew, y + height), ImVec2(x, y + height), color);
}

// One line of interface text centred vertically on `centerY`, in pixels.
float drawTextPx(ImDrawList* drawList, const Metrics& m, float left, float centerY, float width,
    float sizeDp, bool bold, ImU32 color, const std::string& text, bool ellipsize = false,
    ui::HAlign align = ui::HAlign::Left) {
  if (drawList == nullptr || text.empty() || width <= 0.0F) return 0.0F;
  const float boxHeight = m.lineBox(sizeDp);
  ui::TextStyle style;
  style.sizeDp = sizeDp;
  style.bold = bold;
  style.color = color;
  style.align = align;
  style.valign = ui::VAlign::Middle;
  style.fitWidth = true;
  style.ellipsize = ellipsize;
  const ui::Box box =
      m.pixelBox(left, centerY - boxHeight * 0.5F, left + width, centerY + boxHeight * 0.5F);
  return ui::drawLabel(drawList, m.canvas, box, text, style, 0.0);
}

// Text size the reference view layer settles on for a label inside a fixed box: the line box
// may not exceed the available height and the advance may not exceed the available width.
float fittedSizeDp(const Metrics& m, const std::string& text, float requestedDp,
    float availableWidthPx, float availableHeightPx, bool bold) {
  float size = requestedDp;
  if (availableHeightPx > 0.0F) {
    size = std::min(size, availableHeightPx / (kTextLineFactor * m.density));
  }
  if (availableWidthPx > 0.0F) {
    const float measured = ui::labelWidth(m.canvas, text, size, bold) * m.scale;
    if (measured > availableWidthPx) size *= availableWidthPx / std::max(1.0F, measured);
  }
  return std::max(1.0F, size);
}

struct Rect {
  float x0{};
  float y0{};
  float x1{};
  float y1{};
};

bool pointInside(ImVec2 point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x <= x1 && point.y >= y0 && point.y <= y1;
}

// Hit test of the reference skewed panel: a tap only counts inside the slanted shape.
bool skewedContains(ImVec2 point, float x, float y, float width, float height, float skew,
    bool straightLeft, bool straightRight, bool topWider) {
  if (point.x < x - skew || point.x > x + width + skew) return false;
  if (point.y < y || point.y > y + height) return false;
  const float t = (point.y - y) / std::max(1.0F, height);
  const float leftEdge =
      straightLeft ? x : (topWider ? x + skew * t : x + skew * (1.0F - t));
  const float rightEdge = straightRight ? x + width : x + width - skew * t;
  return point.x >= leftEdge && point.x <= rightEdge;
}

// ---------------------------------------------------------------------------
// Interaction state
// ---------------------------------------------------------------------------

struct PointerState {
  std::string activeKey;
  bool sliderDragging{};
  bool scrollDragging{};
  bool backPressed{};
  bool tabPressed{};
  float pressX{};
  float pressY{};
  float scrollAtPress{};
  // A press that started on a row is held here until the gesture resolves: a vertical
  // move past the slop turns it into a scroll, a quick release turns it into a click.
  // This is the parent-scroll-container interception the reference gets from its
  // ScrollView, and it is what lets a finger drag the list from on top of a row.
  bool rowPressPending{};
  bool rowPressOnControl{};
  float rowPressY{};
  float rowScrollAtPress{};
};

PointerState& pointerState() {
  static PointerState state;
  return state;
}

struct DialogState {
  bool aspectOpen{};
  bool skinSelectOpen{};
  bool skinImportOpen{};
  bool replayImportOpen{};
  char aspectText[32]{};
  char skinPath[512]{};
  char replayPath[512]{};

  // Skin selection surface animation. Closed means no panel is drawn; Entering/Open accept
  // input; Closing keeps the page blocked until the shrink-out finishes.
  enum class SkinPhase { Closed, Entering, Open, Closing };
  SkinPhase skinPhase{SkinPhase::Closed};
  double skinPhaseStart{-1.0};
  float skinExitFrom{1.0F};
  float skinScroll{};
  int skinPressedRow{-1};
  bool skinPressOutside{};
  bool skinScrollDragging{};
  float skinPressY{};
  float skinScrollAtPress{};
};

DialogState& dialogState() {
  static DialogState state;
  return state;
}

bool anyDialogOpen() {
  const DialogState& dialogs = dialogState();
  return dialogs.aspectOpen || dialogs.skinSelectOpen || dialogs.skinImportOpen
      || dialogs.replayImportOpen;
}

// Opens the reference-styled skin selection surface (defined with the dialog section below).
void openSkinDialog(double nowSec);

// ---------------------------------------------------------------------------
// Widget geometry
// ---------------------------------------------------------------------------

struct SliderGeometry {
  float trackLeft{};
  float trackRight{};
  float valueLeft{};
  float valueRight{};
  float endInset{};
  float capWidth{};
  float capHeight{};
  float thumbWidth{};
  float trackHeight{};
};

// Exact port of the reference slider geometry; all reference dp values are rounded ints.
SliderGeometry sliderGeometry(const Metrics& m, float width) {
  SliderGeometry geometry;
  const float scale = std::min(1.0F, width / m.dp(100.0F));
  const float endInset = m.dp(16.0F);
  const float capWidth = m.dp(9.0F);
  const float thumbWidth = m.dp(30.0F);
  const float thumbGap = m.dp(2.0F);
  const float capHeight = m.dp(26.0F);
  const float trackHeight = m.dp(20.0F);
  const float halfCapDelta = m.dp(26.0F) * scale * kSlope;
  geometry.endInset = endInset / std::max(0.01F, scale) * scale;
  geometry.capWidth = capWidth * scale;
  geometry.capHeight = capHeight * scale;
  geometry.thumbWidth = thumbWidth * scale;
  geometry.trackHeight = trackHeight * scale;
  geometry.trackLeft = endInset * scale + geometry.capWidth - halfCapDelta
      - (m.dp(20.0F) * scale * kSlope) * 0.5F;
  geometry.trackRight = width - geometry.trackLeft;
  geometry.valueLeft = endInset * scale + geometry.capWidth - halfCapDelta
      + thumbGap * scale + geometry.thumbWidth * 0.5F;
  geometry.valueRight = std::max(geometry.valueLeft,
      width - endInset * scale - geometry.capWidth + halfCapDelta - thumbGap * scale
          - geometry.thumbWidth * 0.5F);
  geometry.endInset = endInset * scale;
  return geometry;
}

// DecelerateInterpolator(1.6), the curve of the reference switch knob animation.
float decelerate16(float t) {
  t = std::clamp(t, 0.0F, 1.0F);
  return 1.0F - std::pow(1.0F - t, 1.6F);
}

float easeOutCubic(float t) {
  const float inverse = 1.0F - std::clamp(t, 0.0F, 1.0F);
  return 1.0F - inverse * inverse * inverse;
}

// ---------------------------------------------------------------------------
// Block model
// ---------------------------------------------------------------------------

enum class BlockKind { Heading, Slider, Switch, Edit, Action, Dual, Replay, Empty, Blank };
enum class SliderBind {
  UiScale,
  KeyScale,
  Brightness,
  ScrollSpeed,
  JudgeWidth,
  MusicVolume,
  SfxVolume,
  MusicSpeed,
  AudioOffset
};
enum class ValueFormat { TwoDecimals, OneDecimal, PercentFraction, PercentInt, Milliseconds };
enum class ActionKind {
  None,
  CycleHitOffset,
  SkinSelect,
  SkinImport,
  SkinDelete,
  Calibration,
  ReplayImport,
  ReplayRefresh,
  ReplayPlay,
  ReplayDelete
};

struct Block {
  BlockKind kind{BlockKind::Blank};
  float height{};
  std::string label;
  std::string value;
  std::string action;
  std::string secondaryAction;
  std::string key;
  std::string replayId;
  SliderBind bind{SliderBind::KeyScale};
  ValueFormat format{ValueFormat::TwoDecimals};
  float from{};
  float to{};
  float step{};
  bool* boolean{};
  ActionKind act{ActionKind::None};
  ActionKind act2{ActionKind::None};
};

void markChanged(AppContext& context) { context.settingsDirty = true; }

void applyVolumes(AppContext& context) {
  if (context.audio == nullptr) return;
  context.audio->setMusicVolume(context.settings.musicVolume);
  context.audio->setSoundEffectVolume(context.settings.soundEffectVolume);
}

std::string formatScore(int score) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%07d", std::max(0, score));
  return buffer;
}

const char* hitOffsetLabel(const AppContext& context) {
  const int mode = std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2);
  if (mode == 1) return "扇形";
  if (mode == 2) return "横线";
  return "禁用";
}

std::string currentSkinName(const AppContext& context) {
  if (context.settings.selectedSkin.empty()) return "内置皮肤";
  return context.settings.selectedSkin;
}

std::vector<std::filesystem::path> listSkinDirectories(const AppContext& context) {
  std::vector<std::filesystem::path> skins;
  const auto root = context.dataRoot / "skins";
  std::error_code error;
  if (!std::filesystem::is_directory(root, error)) return skins;
  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    if (error) break;
    if (entry.is_directory(error)) skins.push_back(entry.path());
  }
  std::sort(skins.begin(), skins.end());
  return skins;
}

float sliderValue(const Block& block, const AppContext& context) {
  switch (block.bind) {
    case SliderBind::UiScale: return context.settings.uiScale;
    case SliderBind::KeyScale: return context.settings.keyScale;
    case SliderBind::Brightness: return 1.0F - context.settings.backgroundDim;
    case SliderBind::ScrollSpeed: return context.settings.scrollSpeed;
    case SliderBind::JudgeWidth: return context.settings.judgeLineWidth;
    case SliderBind::MusicVolume: return context.settings.musicVolume * 100.0F;
    case SliderBind::SfxVolume: return context.settings.soundEffectVolume * 100.0F;
    case SliderBind::MusicSpeed: return context.settings.musicSpeed;
    case SliderBind::AudioOffset: return static_cast<float>(context.settings.audioOffsetMs);
  }
  return 0.0F;
}

float snapSliderValue(const Block& block, float value) {
  float snapped = value;
  if (block.step > 0.0F) {
    snapped = block.from + std::round((value - block.from) / block.step) * block.step;
  }
  return std::clamp(snapped, block.from, block.to);
}

void applySliderValue(AppContext& context, const Block& block, float value) {
  switch (block.bind) {
    case SliderBind::UiScale:
      context.settings.uiScale = std::clamp(value, 0.85F, 2.0F);
      // Apply immediately so the slider itself grows under the finger as it is dragged.
      ui::setInterfaceScale(context.settings.uiScale);
      break;
    case SliderBind::KeyScale:
      context.settings.keyScale = std::clamp(value, 0.7F, 1.5F);
      break;
    case SliderBind::Brightness:
      context.settings.backgroundDim = std::clamp(1.0F - value, 0.0F, 1.0F);
      break;
    case SliderBind::ScrollSpeed:
      context.settings.scrollSpeed = std::clamp(value, 0.1F, 5.0F);
      break;
    case SliderBind::JudgeWidth:
      context.settings.judgeLineWidth = std::clamp(value, 0.5F, 2.0F);
      break;
    case SliderBind::MusicVolume:
      context.settings.musicVolume = std::clamp(value / 100.0F, 0.0F, 5.0F);
      applyVolumes(context);
      break;
    case SliderBind::SfxVolume:
      context.settings.soundEffectVolume = std::clamp(value / 100.0F, 0.0F, 5.0F);
      applyVolumes(context);
      break;
    case SliderBind::MusicSpeed:
      context.settings.musicSpeed = std::clamp(value, 0.5F, 2.0F);
      break;
    case SliderBind::AudioOffset:
      context.settings.audioOffsetMs = std::clamp(static_cast<int>(std::lround(value)), -500, 500);
      break;
  }
  markChanged(context);
}

std::string formatSliderValue(const Block& block, float value) {
  char buffer[64] = {};
  switch (block.format) {
    case ValueFormat::TwoDecimals:
      std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
      break;
    case ValueFormat::OneDecimal:
      std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(value));
      break;
    case ValueFormat::PercentFraction:
      std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(value * 100.0F));
      break;
    case ValueFormat::PercentInt:
      std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value)));
      break;
    case ValueFormat::Milliseconds:
      std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(value)));
      break;
  }
  return buffer;
}

// ---------------------------------------------------------------------------
// Page construction
// ---------------------------------------------------------------------------

struct BlockBuilder {
  const Metrics& m;
  std::vector<Block>& blocks;

  void heading(const char* label) {
    Block block;
    block.kind = BlockKind::Heading;
    block.label = label;
    block.height = m.dp(8.0F) + m.lineBox(14.0F) + m.dp(12.0F);
    blocks.push_back(block);
  }

  void sectionGap() {
    Block block;
    block.kind = BlockKind::Blank;
    block.height = m.dp(kSectionBottomGapDp);
    blocks.push_back(block);
  }

  void slider(const char* key, const char* label, SliderBind bind, float from, float to,
      float step, ValueFormat format) {
    Block block;
    block.kind = BlockKind::Slider;
    block.key = key;
    block.label = label;
    block.bind = bind;
    block.from = from;
    block.to = to;
    block.step = step;
    block.format = format;
    block.height = m.dp(14.0F) + std::max(m.lineBox(22.0F), m.dp(30.0F)) + m.dp(56.0F)
        + m.dp(22.0F);
    blocks.push_back(block);
  }

  void toggle(const char* key, const char* label, bool* value) {
    Block block;
    block.kind = BlockKind::Switch;
    block.key = key;
    block.label = label;
    block.boolean = value;
    block.height = std::max({m.dp(68.0F), m.dp(56.0F), m.lineBox(20.0F)});
    blocks.push_back(block);
  }

  void edit(const char* key, const char* label, const std::string& value) {
    Block block;
    block.kind = BlockKind::Edit;
    block.key = key;
    block.label = label;
    block.value = value;
    block.height = std::max({m.dp(60.0F), m.lineBox(18.0F), m.dp(48.0F)});
    blocks.push_back(block);
  }

  void action(const char* key, const char* label, const std::string& value, const char* button,
      ActionKind kind) {
    Block block;
    block.kind = BlockKind::Action;
    block.key = key;
    block.label = label;
    block.value = value;
    block.action = button;
    block.act = kind;
    block.height = m.dp(8.0F) + m.lineBox(18.0F)
        + std::max(m.lineBox(15.0F), m.dp(48.0F)) + m.dp(12.0F);
    blocks.push_back(block);
  }

  void dual(const char* key, const char* label, const char* left, ActionKind leftKind,
      const char* right, ActionKind rightKind) {
    Block block;
    block.kind = BlockKind::Dual;
    block.key = key;
    block.label = label;
    block.action = left;
    block.act = leftKind;
    block.secondaryAction = right;
    block.act2 = rightKind;
    block.height = m.dp(8.0F) + m.lineBox(18.0F) + m.dp(48.0F) + m.dp(12.0F);
    blocks.push_back(block);
  }

  void replayRow(const ReplayInfo& info) {
    Block block;
    block.kind = BlockKind::Replay;
    block.key = "replay:" + info.id;
    block.replayId = info.id;
    block.label = info.meta.songName + "  " + info.meta.difficulty;
    block.value = formatScore(info.meta.score);
    block.height = m.dp(10.0F) + m.lineBox(17.0F)
        + std::max(m.lineBox(15.0F), m.dp(48.0F)) + m.dp(14.0F);
    blocks.push_back(block);
  }

  void emptyNotice(const char* label) {
    Block block;
    block.kind = BlockKind::Empty;
    block.label = label;
    block.height = m.dp(18.0F) + m.lineBox(16.0F) + m.dp(18.0F);
    blocks.push_back(block);
  }

  void minHeightBlank(float dpHeight) {
    Block block;
    block.kind = BlockKind::Blank;
    block.height = std::max(m.dp(dpHeight), m.lineBox(16.0F));
    blocks.push_back(block);
  }
};

std::string aspectRatioText(const AppContext& context) {
  char buffer[32];
  if (context.settings.aspectRatio <= 0.0F) {
    std::snprintf(buffer, sizeof(buffer), "0");
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.5f", static_cast<double>(context.settings.aspectRatio));
  }
  return buffer;
}

std::vector<Block> buildVideoBlocks(const Metrics& m, AppContext& context) {
  std::vector<Block> blocks;
  BlockBuilder builder{m, blocks};
  builder.heading("画面");
  builder.slider("video.ui_scale", "界面缩放", SliderBind::UiScale, 0.85F, 2.0F, 0.05F,
      ValueFormat::TwoDecimals);
  builder.slider("video.key_scale", "按键缩放", SliderBind::KeyScale, 0.7F, 1.5F, 0.01F,
      ValueFormat::TwoDecimals);
  builder.slider("video.brightness", "背景亮度", SliderBind::Brightness, 0.0F, 1.0F, 0.01F,
      ValueFormat::PercentFraction);
  builder.edit("video.aspect", "舞台宽高比", aspectRatioText(context));
  builder.slider("video.scroll", "谱面流速", SliderBind::ScrollSpeed, 0.1F, 5.0F, 0.1F,
      ValueFormat::OneDecimal);
  builder.slider("video.judge_width", "判定线粗细", SliderBind::JudgeWidth, 0.5F, 2.0F, 0.01F,
      ValueFormat::TwoDecimals);
  builder.sectionGap();

  builder.heading("游玩显示");
  builder.toggle("video.mirror", "镜像演示（X 轴）", &context.settings.mirrorX);
  builder.toggle("video.autoplay", "自动演示", &context.settings.autoplay);
  builder.toggle("video.challenge", "挑战模式", &context.settings.challengeMode);
  builder.toggle("video.lowres", "低分辨率模式", &context.settings.lowResolutionMode);
  builder.toggle("video.antialias", "抗锯齿", &context.settings.antialias);
  builder.toggle("video.show_fps", "显示 FPS", &context.settings.showFps);
  builder.toggle("video.realtime_acc", "ACC 实时显示", &context.settings.realtimeAccuracy);
  builder.toggle("video.score_jump", "分数跳动效果", &context.settings.scoreJumpEffect);
  builder.toggle("video.disable_shader", "禁用谱面 Shader", &context.settings.disableChartShader);
  builder.toggle("video.debug_info", "显示调试信息", &context.settings.showDebugInfo);
  builder.toggle("video.chart_reveal", "谱面揭秘", &context.settings.chartReveal);
  builder.toggle("video.multi_press", "多押高亮提示", &context.settings.multiPressHighlight);
  builder.toggle("video.apfc", "AP / FC 指示器", &context.settings.apfcIndicator);
  builder.toggle("video.judge_dist", "判定详情分布",
      &context.settings.judgementDetailDistribution);
  builder.action("video.hit_offset", "判定偏移指示器", hitOffsetLabel(context), "切换",
      ActionKind::CycleHitOffset);
  builder.toggle("video.story", "剧情模式", &context.settings.storyMode);
  builder.sectionGap();

  builder.heading("皮肤");
  builder.action("video.skin_select", "当前皮肤", currentSkinName(context), "选择",
      ActionKind::SkinSelect);
  builder.dual("video.skin_resources", "皮肤资源", "导入", ActionKind::SkinImport, "删除",
      ActionKind::SkinDelete);
  builder.sectionGap();
  return blocks;
}

std::vector<Block> buildAudioBlocks(const Metrics& m, AppContext& context) {
  std::vector<Block> blocks;
  BlockBuilder builder{m, blocks};
  builder.heading("音频");
  builder.slider("audio.music_volume", "音乐音量", SliderBind::MusicVolume, 0.0F, 500.0F, 1.0F,
      ValueFormat::PercentInt);
  builder.slider("audio.sfx_volume", "打击音效音量", SliderBind::SfxVolume, 0.0F, 500.0F, 1.0F,
      ValueFormat::PercentInt);
  builder.slider("audio.music_speed", "音乐倍速", SliderBind::MusicSpeed, 0.5F, 2.0F, 0.01F,
      ValueFormat::TwoDecimals);
  builder.slider("audio.offset", "延迟校准", SliderBind::AudioOffset, -500.0F, 500.0F, 1.0F,
      ValueFormat::Milliseconds);
  {
    char value[48];
    std::snprintf(value, sizeof(value), "%d ms", context.settings.audioOffsetMs);
    builder.action("audio.calibration", "校准工具", value, "开始", ActionKind::Calibration);
  }
  builder.sectionGap();
  return blocks;
}

std::vector<Block> buildReplayBlocks(const Metrics& m, AppContext& context) {
  std::vector<Block> blocks;
  BlockBuilder builder{m, blocks};
  builder.heading("回放");
  builder.toggle("replay.enabled", "启用回放记录", &context.settings.replayEnabled);
  builder.dual("replay.files", "回放文件", "导入", ActionKind::ReplayImport, "刷新",
      ActionKind::ReplayRefresh);
  const auto replays = context.replays.list();
  if (replays.empty()) {
    builder.emptyNotice("暂无回放记录");
  } else {
    for (const auto& replay : replays) builder.replayRow(replay);
  }
  builder.sectionGap();
  return blocks;
}

std::vector<Block> buildMoreBlocks(const Metrics& m, AppContext&) {
  std::vector<Block> blocks;
  BlockBuilder builder{m, blocks};
  builder.heading("更多");
  builder.minHeightBlank(180.0F);
  builder.sectionGap();
  return blocks;
}

std::vector<Block> buildBlocks(const Metrics& m, AppContext& context, int tab) {
  switch (tab) {
    case 0: return buildVideoBlocks(m, context);
    case 1: return buildAudioBlocks(m, context);
    case 2: return buildReplayBlocks(m, context);
    default: return buildMoreBlocks(m, context);
  }
}

float contentHeightPx(const Metrics& m, const std::vector<Block>& blocks) {
  float height = m.dp(20.0F) + m.dp(36.0F);
  for (const Block& block : blocks) height += block.height;
  return height;
}

// ---------------------------------------------------------------------------
// Persistent screen state
// ---------------------------------------------------------------------------

struct ScreenState {
  int tabFrom{};
  int tabTo{};
  double tabStart{-1.0};
  int lastTab{-1};
  double lastTabSwitchSec{-100.0};
  float scroll[4]{};
  std::vector<std::array<float, 4>> controlRects;

  struct ToggleAnimation {
    std::string key;
    float from{};
    float to{};
    double start{};
  };
  std::vector<ToggleAnimation> toggleAnimations;
  // A native picker is opened after the page has finished drawing, not from inside the row
  // that was pressed. The next draw owns the toast and the replay list refresh.
  ActionKind pendingImport{ActionKind::None};
};

ScreenState& screenState() {
  static ScreenState state;
  return state;
}

float toggleProgress(const std::string& key, bool checked, double nowSec) {
  const float target = checked ? 1.0F : 0.0F;
  for (const auto& animation : screenState().toggleAnimations) {
    if (animation.key != key) continue;
    const float t = static_cast<float>((nowSec - animation.start) / kToggleAnimationSec);
    if (t >= 1.0F) return target;
    return animation.from + (target - animation.from) * decelerate16(std::max(0.0F, t));
  }
  return target;
}

void startToggleAnimation(const std::string& key, float from, float to, double nowSec) {
  auto& animations = screenState().toggleAnimations;
  for (auto& animation : animations) {
    if (animation.key != key) continue;
    animation.from = from;
    animation.to = to;
    animation.start = nowSec;
    return;
  }
  ScreenState::ToggleAnimation animation;
  animation.key = key;
  animation.from = from;
  animation.to = to;
  animation.start = nowSec;
  animations.push_back(animation);
}

// ---------------------------------------------------------------------------
// Pointer helpers
// ---------------------------------------------------------------------------

struct PointerSnapshot {
  ImVec2 position{};
  bool down{};
  bool clicked{};
  bool released{};
};

PointerSnapshot pointerSnapshot() {
  const ImGuiIO& io = ImGui::GetIO();
  PointerSnapshot snapshot;
  snapshot.position = io.MousePos;
  snapshot.down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  // A press belongs to the page it started on: a gesture carried over from the page that
  // opened this one may not latch a row here.
  snapshot.clicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ui::pagePressOwned();
  snapshot.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  return snapshot;
}

// Screen-space bounds of the scroll viewport. Controls are only interactive inside it, so a
// row clipped away by scrolling cannot receive a press.
float gInteractionClipTop = 0.0F;
float gInteractionClipBottom = 1.0e9F;

// Button-like control: fires once on release inside after a press inside the same shape.
bool pressReleased(const char* key, float x0, float y0, float x1, float y1,
    bool interactionsEnabled) {
  PointerState& state = pointerState();
  if (!interactionsEnabled) return false;
  const PointerSnapshot pointer = pointerSnapshot();
  const bool inside = pointInside(pointer.position, x0, y0, x1, y1)
      && pointer.position.y >= gInteractionClipTop && pointer.position.y <= gInteractionClipBottom;
  if (pointer.clicked && inside && state.activeKey.empty() && !state.scrollDragging) {
    state.activeKey = key;
    state.pressX = pointer.position.x;
    state.pressY = pointer.position.y;
    state.sliderDragging = false;
    return false;
  }
  // A drag that the page claimed mid-press cancels this control: the gesture was a scroll,
  // so releasing over the row must not also toggle it.
  if (state.activeKey == key && state.scrollDragging) {
    state.activeKey.clear();
    state.sliderDragging = false;
    return false;
  }
  if (state.activeKey == key && pointer.released) {
    state.activeKey.clear();
    state.sliderDragging = false;
    return inside;
  }
  return false;
}

struct SliderInput {
  bool active{};
  float fraction{};
};

// Slider capture: drag starts after a horizontal move, and a release also commits the value.
SliderInput sliderInput(const char* key, float x0, float y0, float x1, float y1,
    float valueX0, float valueX1, float dragThresholdPx, bool interactionsEnabled) {
  PointerState& state = pointerState();
  SliderInput result;
  if (!interactionsEnabled) return result;
  const PointerSnapshot pointer = pointerSnapshot();
  const bool inside = pointInside(pointer.position, x0, y0, x1, y1)
      && pointer.position.y >= gInteractionClipTop && pointer.position.y <= gInteractionClipBottom;
  if (pointer.clicked && inside && state.activeKey.empty() && !state.scrollDragging) {
    state.activeKey = key;
    state.pressX = pointer.position.x;
    state.pressY = pointer.position.y;
    state.sliderDragging = false;
  }
  if (state.activeKey != key) return result;
  if (pointer.down) {
    const float deltaX = pointer.position.x - state.pressX;
    const float deltaY = pointer.position.y - state.pressY;
    if (!state.sliderDragging && std::abs(deltaX) > dragThresholdPx
        && std::abs(deltaX) > std::abs(deltaY)) {
      state.sliderDragging = true;
      state.scrollDragging = false;
    }
    if (state.sliderDragging) {
      result.active = true;
      result.fraction = (pointer.position.x - valueX0) / std::max(1.0F, valueX1 - valueX0);
      state.pressX = pointer.position.x; // track deltas from the latest position
      state.pressY = pointer.position.y;
    }
  }
  if (pointer.released) {
    result.active = true;
    result.fraction = (pointer.position.x - valueX0) / std::max(1.0F, valueX1 - valueX0);
    state.activeKey.clear();
    state.sliderDragging = false;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Block drawing
// ---------------------------------------------------------------------------

struct DrawContext {
  AppContext& context;
  Metrics m;
  ImDrawList* drawList{};
  double nowSec{};
  float scrollY{};
  float rowWidth{};
  bool interactionsEnabled{};
  std::vector<std::array<float, 4>>& controlRects;
};

void drawHeading(const DrawContext& draw, const Block& block, float left, float top) {
  const float centerY = top + draw.m.dp(8.0F) + draw.m.lineBox(14.0F) * 0.5F;
  drawTextPx(draw.drawList, draw.m, left, centerY, draw.rowWidth, 14.0F, false,
      ui::argb(kHeadingText), block.label);
}

void drawSlider(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float headHeight = std::max(m.lineBox(22.0F), m.dp(30.0F));
  const float headCenterY = top + m.dp(14.0F) + headHeight * 0.5F;

  float value = sliderValue(block, context);
  // Capture first so the value, thumb and label reflect this frame's drag.
  const SliderGeometry geometry = sliderGeometry(m, draw.rowWidth);
  const Rect sliderRect{left, top + m.dp(14.0F) + headHeight, left + draw.rowWidth,
      top + m.dp(14.0F) + headHeight + m.dp(56.0F)};
  const float dragThreshold = m.dp(6.0F);
  const SliderInput input = sliderInput(("slider:" + block.key).c_str(), sliderRect.x0,
      sliderRect.y0, sliderRect.x1, sliderRect.y1, left + geometry.valueLeft,
      left + geometry.valueRight, dragThreshold, draw.interactionsEnabled);
  if (input.active) {
    const float fraction = std::clamp(input.fraction, 0.0F, 1.0F);
    const float next = snapSliderValue(block, block.from + fraction * (block.to - block.from));
    if (std::abs(next - value) > block.step * 0.25F) {
      value = next;
      applySliderValue(context, block, next);
    }
  }

  drawTextPx(draw.drawList, m, left, headCenterY, draw.rowWidth - m.dp(128.0F), 22.0F, false,
      ui::argb(kSliderName), block.label);

  const std::string text = formatSliderValue(block, value);
  const float valueBoxWidth = m.dp(128.0F);
  const float valueBoxHeight = m.dp(30.0F);
  const float valueTextSize = fittedSizeDp(m, text, 15.0F, valueBoxWidth - m.dp(16.0F),
      valueBoxHeight, false);
  const float valueCenterY = top + m.dp(14.0F) + headHeight * 0.5F;
  {
    ui::TextStyle style;
    style.sizeDp = valueTextSize;
    style.bold = false;
    style.color = ui::argb(kSliderValue);
    style.align = ui::HAlign::Right;
    style.valign = ui::VAlign::Middle;
    style.paddingLeftDp = 8.0F;
    style.paddingRightDp = 8.0F;
    const float boxHeight = m.lineBox(valueTextSize);
    const ui::Box box = m.pixelBox(left + draw.rowWidth - valueBoxWidth,
        valueCenterY - boxHeight * 0.5F, left + draw.rowWidth,
        valueCenterY + boxHeight * 0.5F);
    ui::drawLabel(draw.drawList, m.canvas, box, text, style, draw.nowSec);
  }

  // Slider track, end caps and thumb, drawn with the reference geometry.
  const float centerY = sliderRect.y0 + m.dp(56.0F) * 0.5F;
  const ImU32 trackColor = ui::argb(kSliderTrackColor);
  const ImU32 white = ui::argb(0xFFFFFFFFU);
  const float trackLeft = left + geometry.trackLeft;
  const float trackRight = left + geometry.trackRight;
  skewedBlock(draw.drawList, trackLeft, centerY - geometry.trackHeight * 0.5F,
      trackRight - trackLeft, geometry.trackHeight, geometry.trackHeight * kSlope, trackColor);
  const float capY = centerY - geometry.capHeight * 0.5F;
  skewedBlock(draw.drawList, left + geometry.endInset, capY, geometry.capWidth,
      geometry.capHeight, geometry.capHeight * kSlope, white);
  skewedBlock(draw.drawList, left + draw.rowWidth - geometry.endInset - geometry.capWidth, capY,
      geometry.capWidth, geometry.capHeight, geometry.capHeight * kSlope, white);
  const float span = block.to - block.from;
  const float fraction = span > 0.0F ? std::clamp((value - block.from) / span, 0.0F, 1.0F) : 0.0F;
  const float thumbCenter =
      left + geometry.valueLeft + (geometry.valueRight - geometry.valueLeft) * fraction;
  skewedBlock(draw.drawList, thumbCenter - geometry.thumbWidth * 0.5F, capY,
      geometry.thumbWidth, geometry.capHeight, geometry.capHeight * kSlope, white);

  draw.controlRects.push_back({sliderRect.x0, sliderRect.y0, sliderRect.x1, sliderRect.y1});
}

void drawSwitch(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float rowHeight = block.height;
  const float centerY = top + rowHeight * 0.5F;
  const bool checked = block.boolean != nullptr && *block.boolean;
  const std::string key = "switch:" + block.key;
  const float progress = toggleProgress(key, checked, draw.nowSec);

  const float toggleWidth = m.dp(80.0F);
  const float toggleHeight = m.dp(56.0F);
  const float toggleLeft = left + draw.rowWidth - toggleWidth;
  const float toggleTop = top + (rowHeight - toggleHeight) * 0.5F;
  const float rowPressHeight = rowHeight;
  const bool pressed = pressReleased(key.c_str(), left, top, left + draw.rowWidth,
      top + rowPressHeight, draw.interactionsEnabled);
  if (pressed && block.boolean != nullptr) {
    const float from = progress;
    *block.boolean = !*block.boolean;
    startToggleAnimation(key, from, *block.boolean ? 1.0F : 0.0F, draw.nowSec);
    markChanged(context);
    // Revealing chart detail only makes sense with the automatic demonstration; the
    // reference flips autoplay on when reveal is enabled and keeps the row in sync.
    if (context.settings.chartReveal && !context.settings.autoplay) {
      context.settings.autoplay = true;
      startToggleAnimation("switch:video.autoplay", 0.0F, 1.0F, draw.nowSec);
    }
  }

  drawTextPx(draw.drawList, m, left, centerY, draw.rowWidth - toggleWidth, 20.0F, false,
      ui::argb(kSwitchName), block.label);

  // Switch body: the knob slides between the two ends while the track brightens.
  const float bodyHeight = m.dp(26.0F);
  const float bodyY = toggleTop + (toggleHeight - bodyHeight) * 0.5F;
  const float bodyX = toggleLeft + (toggleWidth - m.dp(66.0F)) * 0.5F;
  const int trackAlpha = 0x66 + static_cast<int>(std::lround(0x22 * progress));
  const ImU32 trackColor = ui::argb((static_cast<std::uint32_t>(trackAlpha) << 24)
      | static_cast<std::uint32_t>(kSwitchTrack));
  const float trackHeight = bodyHeight - m.dp(4.0F);
  skewedBlock(draw.drawList, bodyX, bodyY + m.dp(2.0F), m.dp(66.0F), trackHeight,
      trackHeight * kToggleSkewFactor, trackColor);
  const float thumbX = bodyX + m.dp(31.0F) * progress;
  skewedBlock(draw.drawList, thumbX, bodyY, m.dp(35.0F), bodyHeight,
      bodyHeight * kToggleSkewFactor, ui::argb(0xFFFFFFFFU));
  // The checked mark is anchored to the track's left end, not to the thumb: the thumb
  // slides away from it as the switch turns on, which leaves the mark on the dark track
  // where a white stroke reads clearly.
  if (progress > 0.01F) {
    const float alpha = std::clamp(progress, 0.0F, 1.0F);
    const ImU32 markColor = ui::fade(ui::argb(0xFFFFFFFFU), alpha);
    const float markWidth = m.dp(1.5F);
    const ImVec2 mark[3] = {
        ImVec2(bodyX + m.dp(13.0F), bodyY + m.dp(13.0F)),
        ImVec2(bodyX + m.dp(17.0F), bodyY + m.dp(17.0F)),
        ImVec2(bodyX + m.dp(24.0F), bodyY + m.dp(8.0F)),
    };
    draw.drawList->AddPolyline(mark, 3, markColor, ImDrawFlags_RoundCornersAll, markWidth);
    // Round caps: ImGui's polyline leaves the stroke ends square, and at 1.5 dp that is
    // visible at the tips the reference draws with ROUND cap.
    for (const ImVec2& point : mark) {
      draw.drawList->AddCircleFilled(point, markWidth * 0.5F, markColor, 12);
    }
  }
  draw.controlRects.push_back({left, top, left + draw.rowWidth, top + rowPressHeight});
}

void drawEdit(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float centerY = top + block.height * 0.5F;
  const float editWidth = m.dp(120.0F);
  const float editHeight = m.dp(48.0F);
  const float editLeft = left + draw.rowWidth - editWidth;
  const float editTop = top + (block.height - editHeight) * 0.5F;
  const bool pressed = pressReleased(("edit:" + block.key).c_str(), editLeft, editTop,
      editLeft + editWidth, editTop + editHeight, draw.interactionsEnabled);
  if (pressed) {
    DialogState& dialogs = dialogState();
    dialogs.aspectOpen = true;
    std::snprintf(dialogs.aspectText, sizeof(dialogs.aspectText), "%s", block.value.c_str());
    ImGui::OpenPopup("##settings_aspect");
  }

  drawTextPx(draw.drawList, m, left, centerY, draw.rowWidth - editWidth, 18.0F, false,
      ui::argb(kEditName), block.label);
  draw.drawList->AddRectFilled(ImVec2(editLeft, editTop), ImVec2(editLeft + editWidth,
                              editTop + editHeight), ui::argb(kEditField));
  drawTextPx(draw.drawList, m, editLeft + m.dp(12.0F), editTop + editHeight * 0.5F,
      editWidth - m.dp(24.0F), 15.0F, false, ui::argb(0xFFFFFFFFU), block.value);
  draw.controlRects.push_back({editLeft, editTop, editLeft + editWidth, editTop + editHeight});
}

void drawSmallButton(const DrawContext& draw, float x, float y, float width, float height,
    const char* label, bool hovered) {
  const float skew = std::min(height * kSlope, width * 0.42F);
  (void)hovered;
  skewedBlock(draw.drawList, x, y, width, height, skew, ui::argb(kSmallButtonFill));
  const float centerY = y + height * 0.5F;
  drawTextPx(draw.drawList, draw.m, x, centerY, width, 16.0F, true, ui::argb(kSmallButtonText),
      label, false, ui::HAlign::Center);
}

void drawAction(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float labelHeight = m.lineBox(18.0F);
  const float rowTop = top + m.dp(8.0F) + labelHeight;
  const float rowHeight = std::max(m.lineBox(15.0F), m.dp(48.0F));
  const float buttonWidth = m.dp(92.0F);
  const float buttonLeft = left + draw.rowWidth - buttonWidth;
  const bool pressed = pressReleased(("action:" + block.key).c_str(), buttonLeft, rowTop,
      buttonLeft + buttonWidth, rowTop + rowHeight, draw.interactionsEnabled);

  drawTextPx(draw.drawList, m, left, top + m.dp(8.0F) + labelHeight * 0.5F, draw.rowWidth, 18.0F,
      false, ui::argb(kActionName), block.label);
  const float valueWidth = draw.rowWidth - buttonWidth - m.dp(12.0F);
  drawTextPx(draw.drawList, m, left, rowTop + rowHeight * 0.5F, valueWidth, 15.0F, false,
      ui::argb(kActionValue), block.value, true);
  drawSmallButton(draw, buttonLeft, rowTop, buttonWidth, rowHeight, block.action.c_str(), false);
  draw.controlRects.push_back({buttonLeft, rowTop, buttonLeft + buttonWidth, rowTop + rowHeight});

  if (!pressed) return;
  switch (block.act) {
    case ActionKind::CycleHitOffset:
      context.settings.hitOffsetIndicatorMode =
          (std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2) + 1) % 3;
      markChanged(context);
      break;
    case ActionKind::SkinSelect:
      openSkinDialog(draw.nowSec);
      break;
    case ActionKind::Calibration:
      context.calibration.offsetMs = static_cast<float>(context.settings.audioOffsetMs);
      context.calibration.audioLoaded = false;
      context.calibration.markers.clear();
      // The calibration page returns to the settings page, which keeps its own target for when
      // the player leaves it. Overwriting that target here is what made the settings back action
      // land on the settings page again instead of the page it was opened from.
      context.calibration.returnTo = Screen::Settings;
      context.screen = Screen::Calibration;
      break;
    default:
      break;
  }
}

void drawDual(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float labelHeight = m.lineBox(18.0F);
  const float rowTop = top + m.dp(8.0F) + labelHeight;
  const float rowHeight = m.dp(48.0F);
  const float gap = m.dp(12.0F);
  const float buttonWidth = (draw.rowWidth - gap) * 0.5F;
  const float leftButton = left;
  const float rightButton = left + buttonWidth + gap;
  const bool leftPressed = pressReleased(("dual:" + block.key + ":l").c_str(), leftButton, rowTop,
      leftButton + buttonWidth, rowTop + rowHeight, draw.interactionsEnabled);
  const bool rightPressed = pressReleased(("dual:" + block.key + ":r").c_str(), rightButton,
      rowTop, rightButton + buttonWidth, rowTop + rowHeight, draw.interactionsEnabled);

  drawTextPx(draw.drawList, m, left, top + m.dp(8.0F) + labelHeight * 0.5F, draw.rowWidth, 18.0F,
      false, ui::argb(kActionName), block.label);
  drawSmallButton(draw, leftButton, rowTop, buttonWidth, rowHeight, block.action.c_str(), false);
  drawSmallButton(draw, rightButton, rowTop, buttonWidth, rowHeight,
      block.secondaryAction.c_str(), false);
  draw.controlRects.push_back({leftButton, rowTop, leftButton + buttonWidth, rowTop + rowHeight});
  draw.controlRects.push_back({rightButton, rowTop, rightButton + buttonWidth, rowTop + rowHeight});

  const auto runAction = [&](ActionKind kind) {
    switch (kind) {
      case ActionKind::SkinImport:
        // The platform picker installs the skin and reports the result through the toast.
        (void)importSkinFromDialog(context);
        break;
      case ActionKind::SkinDelete: {
        if (context.settings.selectedSkin.empty()) {
          notify(context, "内置皮肤不可删除");
        } else {
          const auto directory = installedSkinDirectory(context);
          if (!directory.empty()) platform::removeRecursively(directory);
          context.settings.selectedSkin.clear();
          markChanged(context);
        }
        break;
      }
      case ActionKind::ReplayImport:
        // The platform picker imports the replay; the list below rebuilds from the store.
        (void)importReplayFromDialog(context);
        break;
      case ActionKind::ReplayRefresh:
        break;
      default:
        break;
    }
  };
  const auto dispatch = [&](ActionKind kind) {
    if (kind == ActionKind::SkinImport || kind == ActionKind::ReplayImport) {
      screenState().pendingImport = kind;
      return;
    }
    runAction(kind);
  };
  if (leftPressed) dispatch(block.act);
  if (rightPressed) dispatch(block.act2);
}

void drawReplayRow(const DrawContext& draw, const Block& block, float left, float top) {
  AppContext& context = draw.context;
  const Metrics& m = draw.m;
  const float nameHeight = m.lineBox(17.0F);
  const float rowTop = top + m.dp(10.0F) + nameHeight;
  const float rowHeight = std::max(m.lineBox(15.0F), m.dp(48.0F));
  const float buttonWidth = m.dp(76.0F);
  const float gap = m.dp(8.0F);
  const float deleteLeft = left + draw.rowWidth - buttonWidth;
  const float playLeft = deleteLeft - gap - buttonWidth;
  const bool playPressed = pressReleased(("replay:play:" + block.replayId).c_str(), playLeft,
      rowTop, playLeft + buttonWidth, rowTop + rowHeight, draw.interactionsEnabled);
  const bool deletePressed = pressReleased(("replay:delete:" + block.replayId).c_str(),
      deleteLeft, rowTop, deleteLeft + buttonWidth, rowTop + rowHeight,
      draw.interactionsEnabled);

  drawTextPx(draw.drawList, m, left, top + m.dp(10.0F) + nameHeight * 0.5F, draw.rowWidth, 17.0F,
      false, ui::argb(kReplayName), block.label, true);
  drawTextPx(draw.drawList, m, left, rowTop + rowHeight * 0.5F,
      draw.rowWidth - 2.0F * (buttonWidth + gap), 15.0F, false, ui::argb(kReplayScore),
      block.value, true);
  drawSmallButton(draw, playLeft, rowTop, buttonWidth, rowHeight, "播放", false);
  drawSmallButton(draw, deleteLeft, rowTop, buttonWidth, rowHeight, "删除", false);
  draw.controlRects.push_back({playLeft, rowTop, playLeft + buttonWidth, rowTop + rowHeight});
  draw.controlRects.push_back({deleteLeft, rowTop, deleteLeft + buttonWidth, rowTop + rowHeight});

  if (playPressed) startSavedReplay(context, block.replayId);
  if (deletePressed) context.replays.remove(block.replayId);
}

void drawEmpty(const DrawContext& draw, const Block& block, float left, float top) {
  const Metrics& m = draw.m;
  const float centerY = top + m.dp(18.0F) + m.lineBox(16.0F) * 0.5F;
  const float centerX = left + draw.rowWidth * 0.5F;
  const float characterWidth = ui::labelWidth(m.canvas, block.label, 16.0F, false) * m.scale;
  drawTextPx(draw.drawList, m, centerX - characterWidth * 0.5F, centerY,
      std::max(1.0F, characterWidth + 2.0F), 16.0F, false, ui::argb(kEmptyNotice), block.label);
}

// ---------------------------------------------------------------------------
// Skin selection dialog
// ---------------------------------------------------------------------------

// The reference dialog surface: one 75 degree parallelogram whose content area sits above a
// 52 dp black action strip. The skin list is a list dialog, so the strip carries no actions;
// the panel scales in from 65% over 200 ms, shrinks out over 140 ms and dims the page behind
// it to half black.
constexpr std::uint32_t kDialogContentFill = 0xF0202429U;
constexpr std::uint32_t kDialogStripFill = 0xD9000000U;
constexpr std::uint32_t kDialogAccentFill = 0xFFFFFFFFU;
constexpr std::uint32_t kDialogRuleFill = 0x99FFFFFFU;
constexpr std::uint32_t kDialogTitleText = 0xFFFFFFFFU;
constexpr std::uint32_t kDialogRowText = 0xFFFFFFFFU;
constexpr std::uint32_t kDialogRowPressedFill = 0x2EFFFFFFU;
constexpr double kDialogEnterSec = 0.20;
constexpr double kDialogExitSec = 0.14;
constexpr float kDialogBackdropDim = 0.5F;
constexpr float kDialogPadHDp = 26.0F;
constexpr float kDialogPadTopDp = 22.0F;
constexpr float kDialogPadBottomDp = 16.0F;
constexpr float kDialogStripDp = 52.0F;
constexpr float kDialogAccentDp = 3.0F;
constexpr float kDialogTitleDp = 22.0F;
constexpr float kDialogRowHeightDp = 46.0F;
constexpr float kDialogRowGapDp = 2.0F;
constexpr float kDialogRowTextDp = 18.0F;
constexpr float kDialogRuleWidthDp = 44.0F;
constexpr float kDialogRuleHeightDp = 2.0F;

struct SkinDialogLayout {
  int rowCount{};
  float viewWidth{};
  float viewHeight{};
  float panelWidth{};
  float panelHeight{};
  float panelLeft{};
  float panelTop{};
  float strip{};
  float slant{};
  float stripSlant{};
  float padTop{};
  float padBottom{};
  float contentWidth{};
  float contentLeft{};
  float contentBottom{};
  float titleTop{};
  float titleLine{};
  float titleBlock{};
  float ruleTop{};
  float listTop{};
  float listViewport{};
  float rowsTotal{};
  float rowHeight{};
  float rowGap{};
  float rowStride{};
  bool rowsOverflow{};
};


SkinDialogLayout skinDialogLayout(const Metrics& m, int rowCount) {
  SkinDialogLayout layout;
  layout.rowCount = std::max(0, rowCount);
  layout.viewWidth = m.width;
  layout.viewHeight = m.height;
  const float padH = m.dp(kDialogPadHDp);
  layout.padTop = m.dp(kDialogPadTopDp);
  layout.padBottom = m.dp(kDialogPadBottomDp);
  layout.strip = m.dp(kDialogStripDp);
  layout.titleLine = m.lineBox(kDialogTitleDp);
  layout.titleBlock = layout.titleLine + m.dp(2.0F);
  const float ruleBlock = m.dp(6.0F) + m.dp(kDialogRuleHeightDp) + m.dp(12.0F);
  layout.rowHeight = m.dp(kDialogRowHeightDp);
  layout.rowGap = m.dp(kDialogRowGapDp);
  layout.rowStride = layout.rowHeight + layout.rowGap;
  layout.rowsTotal = layout.rowCount > 0
      ? layout.rowCount * layout.rowHeight + (layout.rowCount - 1) * layout.rowGap
      : 0.0F;
  const float desiredContent = layout.titleBlock + ruleBlock + layout.rowsTotal;
  // The reference caps every dialog at 86% of the window (and never below three strips) and
  // lets the item list scroll inside that cap.
  const float maxTotalHeight = std::max(layout.strip * 3.0F, layout.viewHeight * 0.86F);
  const float maxContentHeight = std::max(0.0F, maxTotalHeight - layout.strip
      - layout.padTop - layout.padBottom);
  const float contentMeasured = std::min(desiredContent, maxContentHeight);
  const float listSpace = std::max(0.0F, contentMeasured - layout.titleBlock - ruleBlock);
  layout.listViewport = std::min(layout.rowsTotal, listSpace);
  layout.rowsOverflow = layout.rowsTotal > layout.listViewport + 0.5F;

  const float wideTarget = std::min(layout.viewWidth * 0.58F, m.dp(620.0F));
  layout.panelWidth =
      std::max(std::min(m.dp(340.0F), layout.viewWidth * 0.92F), wideTarget);
  layout.panelHeight = layout.padTop + contentMeasured + layout.padBottom + layout.strip;
  layout.panelLeft = (layout.viewWidth - layout.panelWidth) * 0.5F;
  layout.panelTop = (layout.viewHeight - layout.panelHeight) * 0.5F;
  layout.contentBottom = layout.panelTop + layout.padTop + contentMeasured + layout.padBottom;
  layout.slant = layout.panelHeight * kSlope;
  layout.stripSlant = layout.strip * kSlope;
  layout.contentLeft = layout.panelLeft + layout.stripSlant + padH;
  layout.contentWidth = layout.panelWidth - layout.slant - padH * 2.0F;
  layout.titleTop = layout.panelTop + layout.padTop;
  layout.ruleTop = layout.titleTop + layout.titleBlock + m.dp(6.0F);
  layout.listTop = layout.titleTop + layout.titleBlock + ruleBlock;
  return layout;
}

std::vector<std::string> skinNames(const AppContext& context) {
  std::vector<std::string> names{"内置皮肤"};
  for (const auto& skin : listSkinDirectories(context)) {
    names.push_back(platform::pathToUtf8(skin.filename()));
  }
  return names;
}

// Multiplies the alpha channel of a palette colour by the dialog's current opacity.
ImU32 fadeColor(std::uint32_t color, float alphaScale) {
  const int baseAlpha = static_cast<int>((color >> 24) & 0xFFU);
  const int alpha = std::clamp(static_cast<int>(std::lround(baseAlpha * alphaScale)), 0, 255);
  return ui::argb((static_cast<std::uint32_t>(alpha) << 24) | (color & 0x00FFFFFFU));
}

void beginSkinDialogClose(double nowSec);

// Advances the dialog animation and returns the linear progress (0 hidden, 1 settled).
float skinDialogProgress(DialogState& dialogs, double nowSec) {
  switch (dialogs.skinPhase) {
    case DialogState::SkinPhase::Closed: return 0.0F;
    case DialogState::SkinPhase::Entering: {
      const float linear = static_cast<float>(
          std::clamp((nowSec - dialogs.skinPhaseStart) / kDialogEnterSec, 0.0, 1.0));
      if (linear >= 1.0F) {
        dialogs.skinPhase = DialogState::SkinPhase::Open;
        return 1.0F;
      }
      return linear;
    }
    case DialogState::SkinPhase::Open: return 1.0F;
    case DialogState::SkinPhase::Closing: {
      // The reference shrink-out starts from the current panel opacity and shortens with it.
      const double duration = std::max(1.0e-3, kDialogExitSec * dialogs.skinExitFrom);
      const float linear = static_cast<float>(
          std::clamp(1.0 - (nowSec - dialogs.skinPhaseStart) / duration, 0.0, 1.0));
      if (linear <= 0.0F) {
        dialogs.skinPhase = DialogState::SkinPhase::Closed;
        dialogs.skinSelectOpen = false;
        dialogs.skinPressedRow = -1;
        dialogs.skinScrollDragging = false;
        return 0.0F;
      }
      return linear * dialogs.skinExitFrom;
    }
  }
  return 0.0F;
}

void drawSkinSelectDialog(
    AppContext& context, const Metrics& m, ImDrawList* drawList, double nowSec) {
  DialogState& dialogs = dialogState();
  if (dialogs.skinPhase == DialogState::SkinPhase::Closed) return;
  const std::vector<std::string> names = skinNames(context);
  const SkinDialogLayout layout =
      skinDialogLayout(m, static_cast<int>(names.size()));
  if (dialogs.skinScroll > 0.0F) {
    const float maxScroll = std::max(0.0F, layout.rowsTotal - layout.listViewport);
    dialogs.skinScroll = std::clamp(dialogs.skinScroll, 0.0F, maxScroll);
  }

  const float linear = std::max(0.0F, skinDialogProgress(dialogs, nowSec));
  if (dialogs.skinPhase == DialogState::SkinPhase::Closed) return;
  const float inverse = 1.0F - std::clamp(linear, 0.0F, 1.0F);
  const float eased = 1.0F - inverse * inverse * inverse;
  const float panelScale = 0.65F + 0.35F * eased;

  const float centerX = layout.panelLeft + layout.panelWidth * 0.5F;
  const float centerY = layout.panelTop + layout.panelHeight * 0.5F;
  const auto fit = [&](float x, float y) {
    return ImVec2(centerX + (x - centerX) * panelScale, centerY + (y - centerY) * panelScale);
  };
  const auto inverseFit = [&](ImVec2 point) {
    return ImVec2(centerX + (point.x - centerX) / std::max(0.01F, panelScale),
        centerY + (point.y - centerY) / std::max(0.01F, panelScale));
  };

  // Page dimmer.
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(m.width, m.height),
      IM_COL32(0, 0, 0,
          static_cast<int>(std::lround(255.0F * kDialogBackdropDim * eased))));

  // Panel: content parallelogram, empty action strip below it and the white edge accent.
  const float stripTop = layout.panelTop + layout.panelHeight - layout.strip;
  const ImVec2 contentPoints[4] = {
      fit(layout.panelLeft + layout.slant, layout.panelTop),
      fit(layout.panelLeft + layout.panelWidth, layout.panelTop),
      fit(layout.panelLeft + layout.panelWidth
              - (layout.panelHeight - layout.strip) * kSlope, stripTop),
      fit(layout.panelLeft + layout.stripSlant, stripTop),
  };
  drawList->AddConvexPolyFilled(contentPoints, 4, fadeColor(kDialogContentFill, eased));
  const ImVec2 stripPoints[4] = {
      fit(layout.panelLeft + layout.stripSlant, stripTop),
      fit(layout.panelLeft + layout.panelWidth
              - (layout.panelHeight - layout.strip) * kSlope, stripTop),
      fit(layout.panelLeft + layout.panelWidth - layout.slant,
          layout.panelTop + layout.panelHeight),
      fit(layout.panelLeft, layout.panelTop + layout.panelHeight),
  };
  const float stripAlpha = ((kDialogStripFill >> 24) & 0xFFU) / 255.0F;
  drawList->AddConvexPolyFilled(stripPoints, 4,
      IM_COL32(0, 0, 0, static_cast<int>(std::lround(255.0F * stripAlpha * eased))));
  const ImVec2 accentPoints[4] = {
      fit(layout.panelLeft + layout.slant, layout.panelTop),
      fit(layout.panelLeft + layout.slant + m.dp(kDialogAccentDp), layout.panelTop),
      fit(layout.panelLeft + layout.stripSlant + m.dp(kDialogAccentDp), stripTop),
      fit(layout.panelLeft + layout.stripSlant, stripTop),
  };
  drawList->AddConvexPolyFilled(accentPoints, 4, fadeColor(kDialogAccentFill, eased));

  // Title and its rule follow the slanted left edge at their own height.
  const float titleCenterY = layout.panelTop + layout.padTop + layout.titleLine * 0.5F;
  const float titleShift = (layout.contentBottom - titleCenterY) * kSlope;
  const ImVec2 titleCenter = fit(layout.contentLeft + titleShift, titleCenterY);
  drawTextPx(drawList, m, titleCenter.x, titleCenter.y,
      std::max(1.0F, layout.contentWidth * panelScale), kDialogTitleDp * panelScale, false,
      fadeColor(kDialogTitleText, eased), "选择皮肤", true);
  const float ruleShift = (layout.contentBottom - (layout.ruleTop + m.dp(1.0F))) * kSlope;
  const ImVec2 ruleTopLeft = fit(layout.contentLeft + ruleShift, layout.ruleTop);
  drawList->AddRectFilled(ruleTopLeft,
      ImVec2(ruleTopLeft.x + m.dp(kDialogRuleWidthDp) * panelScale,
          ruleTopLeft.y + m.dp(kDialogRuleHeightDp) * panelScale),
      fadeColor(kDialogRuleFill, eased));

  // The list scrolls inside its viewport; each row's box is shifted along the slanted edge the
  // same way the reference view applies the panel slant.
  const float rowSlant = layout.rowHeight * kSlope;
  drawList->PushClipRect(fit(layout.panelLeft, layout.listTop),
      fit(layout.panelLeft + layout.panelWidth, layout.listTop + layout.listViewport), true);
  for (int index = 0; index < layout.rowCount; ++index) {
    const float rowTop = layout.listTop + index * layout.rowStride - dialogs.skinScroll;
    if (rowTop + layout.rowHeight < layout.listTop - 0.5F
        || rowTop > layout.listTop + layout.listViewport + 0.5F) {
      continue;
    }
    const float rowCenter = rowTop + layout.rowHeight * 0.5F;
    const float clampedCenter = std::clamp(
        rowCenter, layout.listTop, layout.listTop + layout.listViewport);
    const float shift = (layout.contentBottom - clampedCenter) * kSlope;
    const float rowLeft = layout.contentLeft + shift;
    const float rowRight = rowLeft + layout.contentWidth;
    if (dialogs.skinPressedRow == index) {
      const ImVec2 pressedPoints[4] = {
          fit(rowLeft + rowSlant, rowTop),
          fit(rowRight, rowTop),
          fit(rowRight - rowSlant, rowTop + layout.rowHeight),
          fit(rowLeft, rowTop + layout.rowHeight),
      };
      drawList->AddConvexPolyFilled(pressedPoints, 4, fadeColor(kDialogRowPressedFill, eased));
    }
    const ImVec2 rowTextCenter = fit(rowLeft, rowCenter);
    drawTextPx(drawList, m, rowTextCenter.x, rowTextCenter.y,
        std::max(1.0F, layout.contentWidth * panelScale), kDialogRowTextDp * panelScale, false,
        fadeColor(kDialogRowText, eased), names[static_cast<std::size_t>(index)], true);
  }
  drawList->PopClipRect();

  // Input: taps outside cancel, a tap on a row selects it and dismisses, dragging the list
  // scrolls it like the reference ScrollView.
  if (dialogs.skinPhase == DialogState::SkinPhase::Closing) return;
  const bool interactive = eased > 0.2F;
  if (!interactive) return;
  // The reference dialog is cancelable: the back key closes it like an outside tap.
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) beginSkinDialogClose(nowSec);
  const PointerSnapshot pointer = pointerSnapshot();
  const ImVec2 local = inverseFit(pointer.position);
  const bool insidePanel = pointInside(local, layout.panelLeft, layout.panelTop,
      layout.panelLeft + layout.panelWidth, layout.panelTop + layout.panelHeight);
  const bool insideList = pointInside(local, layout.contentLeft, layout.listTop,
      layout.contentLeft + layout.contentWidth, layout.listTop + layout.listViewport);
  const auto rowAtPosition = [&](ImVec2 position) -> int {
    if (!pointInside(position, layout.contentLeft, layout.listTop,
            layout.contentLeft + layout.contentWidth,
            layout.listTop + layout.listViewport)) {
      return -1;
    }
    const float offset = position.y - layout.listTop + dialogs.skinScroll;
    if (offset < 0.0F || layout.rowStride <= 0.0F) return -1;
    const int index = static_cast<int>(offset / layout.rowStride);
    if (index < 0 || index >= layout.rowCount) return -1;
    const float rowTop = layout.listTop + index * layout.rowStride - dialogs.skinScroll;
    const float rowCenter = rowTop + layout.rowHeight * 0.5F;
    const float clampedCenter = std::clamp(
        rowCenter, layout.listTop, layout.listTop + layout.listViewport);
    const float rowLeft = layout.contentLeft + (layout.contentBottom - clampedCenter) * kSlope;
    if (position.x < rowLeft - 0.5F || position.x > rowLeft + layout.contentWidth + 0.5F) {
      return -1;
    }
    return index;
  };

  if (pointer.clicked) {
    const int row = rowAtPosition(local);
    dialogs.skinPressedRow = row;
    dialogs.skinScrollDragging = false;
    dialogs.skinPressOutside = !insidePanel;
    if (insideList) {
      dialogs.skinPressY = local.y;
      dialogs.skinScrollAtPress = dialogs.skinScroll;
    }
  }
  if (pointer.down && dialogs.skinPressedRow >= 0 && insideList) {
    const float delta = local.y - dialogs.skinPressY;
    if (std::abs(delta) > m.dp(6.0F)) dialogs.skinScrollDragging = true;
    if (dialogs.skinScrollDragging) {
      const float maxScroll = std::max(0.0F, layout.rowsTotal - layout.listViewport);
      dialogs.skinScroll =
          std::clamp(dialogs.skinScrollAtPress - delta, 0.0F, maxScroll);
    }
  }
  const float wheel = ImGui::GetIO().MouseWheel;
  if (insideList && std::abs(wheel) > 0.0F && !dialogs.skinScrollDragging) {
    const float maxScroll = std::max(0.0F, layout.rowsTotal - layout.listViewport);
    dialogs.skinScroll = std::clamp(dialogs.skinScroll - wheel * m.dp(60.0F), 0.0F, maxScroll);
  }
  if (pointer.released) {
    if (dialogs.skinPressedRow >= 0 && !dialogs.skinScrollDragging) {
      const int row = rowAtPosition(local);
      if (row == dialogs.skinPressedRow) {
        if (row == 0) {
          context.settings.selectedSkin.clear();
        } else {
          const auto skins = listSkinDirectories(context);
          const auto index = static_cast<std::size_t>(row - 1);
          if (index < skins.size()) {
            context.settings.selectedSkin = platform::pathToUtf8(skins[index].filename());
          }
        }
        markChanged(context);
        beginSkinDialogClose(nowSec);
      }
    } else if (dialogs.skinPressOutside && !insidePanel) {
      beginSkinDialogClose(nowSec);
    }
    dialogs.skinPressedRow = -1;
    dialogs.skinScrollDragging = false;
    dialogs.skinPressOutside = false;
  }
}

void beginSkinDialogClose(double nowSec) {
  DialogState& dialogs = dialogState();
  if (dialogs.skinPhase != DialogState::SkinPhase::Entering
      && dialogs.skinPhase != DialogState::SkinPhase::Open) {
    return;
  }
  // Start the shrink-out from the current panel opacity, like the reference animator.
  float linear = 1.0F;
  if (dialogs.skinPhase == DialogState::SkinPhase::Entering) {
    linear = static_cast<float>(
        std::clamp((nowSec - dialogs.skinPhaseStart) / kDialogEnterSec, 0.0, 1.0));
  }
  const float inverse = 1.0F - linear;
  dialogs.skinPhase = DialogState::SkinPhase::Closing;
  dialogs.skinPhaseStart = nowSec;
  dialogs.skinExitFrom = std::max(0.05F, 1.0F - inverse * inverse * inverse);
}

void openSkinDialog(double nowSec) {
  DialogState& dialogs = dialogState();
  dialogs.skinSelectOpen = true;
  dialogs.skinPhase = DialogState::SkinPhase::Entering;
  dialogs.skinPhaseStart = nowSec;
  dialogs.skinScroll = 0.0F;
  dialogs.skinPressedRow = -1;
  dialogs.skinScrollDragging = false;
  dialogs.skinPressOutside = false;
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

struct BackButtonLayout {
  float x{};
  float y{};
  float width{};
  float height{};
  float skew{};
};

BackButtonLayout backButtonLayout(const Metrics& m) {
  BackButtonLayout layout;
  layout.x = 0.0F;
  layout.y = m.dp(8.0F);
  layout.width = m.dp(78.0F);
  layout.height = m.dp(48.0F);
  layout.skew = std::min(std::tan(15.0F * 3.14159265358979323846F / 180.0F) * layout.height,
      layout.width * 0.42F);
  return layout;
}

void drawBackButton(const DrawContext& draw) {
  const Metrics& m = draw.m;
  const BackButtonLayout layout = backButtonLayout(m);
  const float skew = layout.skew;
  fillQuad(draw.drawList, ImVec2(layout.x, layout.y), ImVec2(layout.x + layout.width, layout.y),
      ImVec2(layout.x + layout.width - skew, layout.y + layout.height),
      ImVec2(layout.x, layout.y + layout.height), ui::argb(kBackPlateFill));
  const float accentWidth = m.dp(3.2F);
  fillQuad(draw.drawList, ImVec2(layout.x + layout.width - accentWidth, layout.y),
      ImVec2(layout.x + layout.width, layout.y),
      ImVec2(layout.x + layout.width - skew, layout.y + layout.height),
      ImVec2(layout.x + layout.width - skew - accentWidth, layout.y + layout.height),
      ui::argb(0xFFFFFFFFU));

  const std::uint64_t texture = assetTexture(draw.context, "res/return.png", 96);
  if (texture != 0) {
    const float padX = m.dp(16.0F);
    const float padY = m.dp(10.0F);
    const float contentWidth = layout.width - padX * 2.0F;
    const float contentHeight = layout.height - padY * 2.0F;
    const int textureWidth = draw.context.textures.width(texture);
    const int textureHeight = draw.context.textures.height(texture);
    if (textureWidth > 0 && textureHeight > 0) {
      const float aspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
      float imageWidth = contentWidth;
      float imageHeight = contentWidth / aspect;
      if (imageHeight > contentHeight) {
        imageHeight = contentHeight;
        imageWidth = contentHeight * aspect;
      }
      const float translationX = -skew * 0.25F
          + std::min(layout.width - m.dp(32.0F), layout.height - m.dp(20.0F)) * 0.063315F;
      const float imageLeft =
          layout.x + padX + (contentWidth - imageWidth) * 0.5F + translationX;
      const float imageTop = layout.y + padY + (contentHeight - imageHeight) * 0.5F;
      ui::drawImage(draw.drawList,
          ui::PxRect{ImVec2(imageLeft, imageTop), ImVec2(imageLeft + imageWidth, imageTop + imageHeight)},
          texture, textureWidth, textureHeight, ui::ImageFit::Fill);
    }
  }
}

struct TabBarLayout {
  float x{};
  float y{};
  float width{};
  float height{};
};

TabBarLayout tabBarLayout(const Metrics& m) {
  const int screenWidthDp = static_cast<int>(std::lround(m.width / std::max(0.01F, m.density)));
  const int widthDp = std::min(440, std::max(200, screenWidthDp - 112));
  TabBarLayout layout;
  layout.x = m.dp(96.0F) + m.dp(kSafeTextInsetDp);
  layout.y = m.dp(11.0F);
  layout.width = m.dp(static_cast<float>(widthDp));
  layout.height = m.dp(42.0F);
  return layout;
}

float tabCenterFor(int tab, int count) {
  return 0.08F + (static_cast<float>(tab) + 0.5F) * 0.84F / static_cast<float>(count);
}

float tabWidthFor(int tab, int count) {
  return 0.84F / static_cast<float>(count);
}

void drawTabBar(const DrawContext& draw, int tab, double tabStart, int tabFrom) {
  const Metrics& m = draw.m;
  const TabBarLayout layout = tabBarLayout(m);
  const float width = layout.width;
  const float height = layout.height;
  const float barTop = height * 0.14F;
  const float barBottom = height * 0.86F;
  const float trackSkew = std::tan(15.0F * 3.14159265358979323846F / 180.0F)
      * (barBottom - barTop);
  const float fullSkew = std::tan(15.0F * 3.14159265358979323846F / 180.0F) * height;

  // Sliding plate progress: 1 when no animation is running.
  float progress = 1.0F;
  if (tabStart > 0.0 && tabFrom != tab) {
    const float t = static_cast<float>((draw.nowSec - tabStart) / kTabAnimationSec);
    progress = t >= 1.0F ? 1.0F : easeOutCubic(std::max(0.0F, t));
  }

  // Dark track behind the labels.
  fillQuad(draw.drawList,
      ImVec2(layout.x + width * 0.055F + trackSkew, layout.y + barTop),
      ImVec2(layout.x + width * 0.945F, layout.y + barTop),
      ImVec2(layout.x + width * 0.945F - trackSkew, layout.y + barBottom),
      ImVec2(layout.x + width * 0.055F, layout.y + barBottom), ui::argb(kTabTrack));

  // Bright plate behind the selected slice. Its leading edge uses the track skew, matching
  // the reference tab bar, so the shape is not a perfect parallelogram.
  const float selectedCenter = tabCenterFor(tabFrom, kTabCount)
      + (tabCenterFor(tab, kTabCount) - tabCenterFor(tabFrom, kTabCount)) * progress;
  const float selectedWidth = tabWidthFor(tabFrom, kTabCount)
      + (tabWidthFor(tab, kTabCount) - tabWidthFor(tabFrom, kTabCount)) * progress;
  const float selectedLeft = selectedCenter - selectedWidth * 0.5F;
  const float selectedRight = selectedCenter + selectedWidth * 0.5F;
  const float plateLeftTop = layout.x + selectedLeft * width + trackSkew;
  const float plateRightTop = layout.x + selectedRight * width;
  fillQuad(draw.drawList, ImVec2(plateLeftTop, layout.y), ImVec2(plateRightTop, layout.y),
      ImVec2(plateRightTop - fullSkew, layout.y + height),
      ImVec2(plateLeftTop - fullSkew, layout.y + height), ui::argb(kTabSelectedPlate));

  // Labels: white for the unselected slices, black over the bright plate.
  const float labelSpace = std::round(height * 0.8F);
  for (int index = 0; index < kTabCount; ++index) {
    float selectedAmount = 0.0F;
    if (progress >= 1.0F || tabFrom == tab) {
      selectedAmount = index == tab ? 1.0F : 0.0F;
    } else if (index == tabFrom) {
      selectedAmount = 1.0F - progress;
    } else if (index == tab) {
      selectedAmount = progress;
    }
    const int tone = static_cast<int>(std::lround(255.0F * (1.0F - selectedAmount)));
    const ImU32 color = IM_COL32(tone, tone, tone, 255);
    const float available = std::max(1.0F,
        std::round(tabWidthFor(index, kTabCount) * width - trackSkew - m.dp(8.0F)));
    const std::string label = kTabLabels[index];
    const float sizeDp = fittedSizeDp(m, label, 18.0F, available, labelSpace, true);
    const float centerX = layout.x + tabCenterFor(index, kTabCount) * width;
    const float centerY = layout.y + height * 0.5F;
    const float textWidth = ui::labelWidth(m.canvas, label, sizeDp, true) * m.scale;
    drawTextPx(draw.drawList, m, centerX - std::max(textWidth, 1.0F) * 0.5F, centerY,
        std::max(1.0F, textWidth), sizeDp, true, color, label);
  }
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void drawDialogs(AppContext& context, const Metrics& m, const BackButtonLayout& backLayout,
    const TabBarLayout& tabLayout) {
  DialogState& dialogs = dialogState();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
      viewport->WorkPos.y + viewport->WorkSize.y * 0.5F);
  const float scale = std::clamp(m.scale * 0.62F, 0.7F, 1.6F);

  // Aspect ratio editor.
  if (dialogs.aspectOpen) {
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(420.0F, 0.0F), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##settings_aspect", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::SetWindowFontScale(scale);
      ImGui::TextUnformatted("舞台宽高比");
      ImGui::SetNextItemWidth(360.0F);
      const bool submitted = ImGui::InputText("##aspect_input", dialogs.aspectText,
          sizeof(dialogs.aspectText),
          ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
      if (ImGui::Button("确定", ImVec2(150.0F, 0.0F)) || submitted) {
        char* end = nullptr;
        const float parsed = std::strtof(dialogs.aspectText, &end);
        if (end != dialogs.aspectText && std::isfinite(parsed)) {
          float value = parsed;
          if (value != 0.0F) value = std::clamp(value, 0.5F, 5.0F);
          context.settings.aspectRatio = value;
          markChanged(context);
        }
        dialogs.aspectOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("取消", ImVec2(150.0F, 0.0F))) {
        dialogs.aspectOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }


  // Skin import.
  if (dialogs.skinImportOpen) {
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(460.0F, 0.0F), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##settings_skin_import", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::SetWindowFontScale(scale);
      ImGui::TextUnformatted("皮肤目录路径");
      ImGui::SetNextItemWidth(420.0F);
      ImGui::InputText("##skin_path", dialogs.skinPath, sizeof(dialogs.skinPath));
      if (ImGui::Button("导入", ImVec2(200.0F, 0.0F))) {
        const std::filesystem::path source = dialogs.skinPath;
        std::error_code error;
        if (!std::filesystem::is_directory(source, error)) {
          notify(context, "皮肤目录不存在");
        } else {
          const auto destination = context.dataRoot / "skins" / source.filename();
          platform::removeRecursively(destination);
          std::filesystem::create_directories(destination.parent_path(), error);
          std::filesystem::copy(source, destination,
              std::filesystem::copy_options::recursive, error);
          if (error) {
            notify(context, "皮肤导入失败");
          } else {
            context.settings.selectedSkin = platform::pathToUtf8(source.filename());
            markChanged(context);
            notify(context, "皮肤已导入");
          }
        }
        dialogs.skinImportOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("取消", ImVec2(200.0F, 0.0F))) {
        dialogs.skinImportOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  // Replay import.
  if (dialogs.replayImportOpen) {
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(460.0F, 0.0F), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##settings_replay_import", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::SetWindowFontScale(scale);
      ImGui::TextUnformatted("回放文件路径（replay.json）");
      ImGui::SetNextItemWidth(420.0F);
      ImGui::InputText("##replay_path", dialogs.replayPath, sizeof(dialogs.replayPath));
      if (ImGui::Button("导入", ImVec2(200.0F, 0.0F))) {
        const auto data = readReplayFile(dialogs.replayPath);
        if (!data) {
          notify(context, "回放文件无法读取");
        } else {
          bool duplicate = false;
          const std::string id = context.replays.save(*data, &duplicate);
          notify(context, id.empty() ? (duplicate ? "本地已存在该回放" : "回放导入失败")
                                     : "回放已导入");
        }
        dialogs.replayImportOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("取消", ImVec2(200.0F, 0.0F))) {
        dialogs.replayImportOpen = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  (void)backLayout;
  (void)tabLayout;
}

} // namespace

// ---------------------------------------------------------------------------
// Screen entry point
// ---------------------------------------------------------------------------

void drawSettingsScreen(AppContext& context) {
  const Metrics m = metrics();


  ScreenState& state = screenState();
  PointerState& pointer = pointerState();

  ui::beginFullscreenWindow("##settings");
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  // Tab bar and back control are hit-tested before the page so a tab switch rebuilds the
  // page in the same frame, exactly like the reference re-renders on selection.
  const TabBarLayout tabLayout = tabBarLayout(m);
  const BackButtonLayout backLayout = backButtonLayout(m);

  const bool dialogsOpen = anyDialogOpen();
  if (!dialogsOpen) {
    const PointerSnapshot snapshot = pointerSnapshot();
    const bool backInside = skewedContains(snapshot.position, backLayout.x, backLayout.y,
        backLayout.width, backLayout.height, backLayout.skew, true, false, false);
    if (snapshot.clicked && backInside) pointer.backPressed = true;
    if (snapshot.released) {
      if (pointer.backPressed && backInside) {
        const Screen target = context.settingsReturn;
        commitSettings(context);
        pointer.backPressed = false;
        pointer.activeKey.clear();
        pointer.scrollDragging = false;
        ui::endFullscreenWindow();
        navigateTo(context, target == Screen::Game ? Screen::Home : target);
        return;
      }
      pointer.backPressed = false;
    }

    const bool tabInside = pointInside(snapshot.position, tabLayout.x, tabLayout.y,
        tabLayout.x + tabLayout.width, tabLayout.y + tabLayout.height);
    if (snapshot.clicked && tabInside) pointer.tabPressed = true;
    if (snapshot.released) {
      if (pointer.tabPressed && tabInside) {
        const int count = kTabCount;
        const float slice = std::max(1.0F, tabLayout.width / static_cast<float>(count));
        const int target = std::clamp(
            static_cast<int>((snapshot.position.x - tabLayout.x) / slice), 0, count - 1);
        if (target != context.settingsTab
            && context.nowSec - state.lastTabSwitchSec >= kTabSwitchMinSec) {
          state.tabFrom = std::clamp(context.settingsTab, 0, kTabCount - 1);
          state.tabTo = target;
          state.tabStart = context.nowSec;
          state.lastTabSwitchSec = context.nowSec;
          context.settingsTab = target;
        }
      }
      pointer.tabPressed = false;
    }
  } else {
    pointer.backPressed = false;
    pointer.tabPressed = false;
  }

  // First frame on the page: the tab plate is already at its target position.
  const int tab = std::clamp(context.settingsTab, 0, kTabCount - 1);
  if (state.lastTab < 0 || state.tabStart < 0.0) {
    state.tabFrom = tab;
    state.tabTo = tab;
    state.tabStart = -1.0;
  } else if (state.tabTo != tab && state.tabFrom != tab) {
    state.tabFrom = tab;
    state.tabTo = tab;
    state.tabStart = -1.0;
  }
  state.lastTab = tab;

  const std::vector<Block> blocks = buildBlocks(m, context, tab);
  const float contentHeight = contentHeightPx(m, blocks);
  const float visibleHeight = std::max(1.0F, m.height - contentTopPx(m));
  const float maxScroll = std::max(0.0F, contentHeight - visibleHeight);
  float& scroll = state.scroll[tab];

  DrawContext draw{context, m, drawList, context.nowSec, scroll, rowWidthPx(m), !dialogsOpen,
      state.controlRects};

  // Scrolling: wheel over the page, and a left-drag anywhere in the page area — including
  // on top of a row. A press that starts on a row is provisional: it only fires as a click
  // if the pointer is released without a vertical move past the slop. That mirrors the
  // reference, whose ScrollView takes the gesture from its children once it decides the
  // drag is vertical, so the list is draggable from the middle of a row and not only from
  // the empty margins.
  if (!dialogsOpen) {
    const PointerSnapshot snapshot = pointerSnapshot();
    const bool overContent = pointInside(snapshot.position, 0.0F, contentTopPx(m), m.width,
        m.height);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (overContent && std::abs(wheel) > 0.0F) {
      scroll = std::clamp(scroll - wheel * m.dp(60.0F), 0.0F, maxScroll);
    }
    if (snapshot.clicked && overContent && pointer.activeKey.empty()
        && !pointer.scrollDragging) {
      bool overControl = false;
      for (const auto& rect : state.controlRects) {
        if (pointInside(snapshot.position, rect[0], rect[1], rect[2], rect[3])) {
          overControl = true;
          break;
        }
      }
      pointer.rowPressPending = true;
      pointer.rowPressOnControl = overControl;
      pointer.rowPressY = snapshot.position.y;
      pointer.rowScrollAtPress = scroll;
      if (!overControl) {
        pointer.scrollDragging = true;
        pointer.pressY = snapshot.position.y;
        pointer.scrollAtPress = scroll;
      }
    }
    if (pointer.rowPressPending) {
      const float travel = snapshot.position.y - pointer.rowPressY;
      const float lateral = snapshot.position.x - pointer.pressX;
      if (!snapshot.down) {
        pointer.rowPressPending = false;
      } else if (!pointer.sliderDragging && std::abs(travel) > m.dp(6.0F)
          && std::abs(travel) > std::abs(lateral)) {
        // The gesture is a vertical drag, not a click. A row may already have latched the
        // press on the frame it went down, so the press is released here rather than
        // waiting for it to expire; otherwise the list could never be dragged from a row.
        pointer.rowPressPending = false;
        pointer.activeKey.clear();
        pointer.sliderDragging = false;
        pointer.scrollDragging = true;
        pointer.pressY = pointer.rowPressY;
        pointer.scrollAtPress = pointer.rowScrollAtPress;
      }
    }
    if (pointer.scrollDragging) {
      if (snapshot.down) {
        scroll = std::clamp(pointer.scrollAtPress - (snapshot.position.y - pointer.pressY),
            0.0F, maxScroll);
      } else {
        pointer.scrollDragging = false;
        pointer.rowPressPending = false;
      }
    }
  } else {
    pointer.scrollDragging = false;
  }
  scroll = std::clamp(scroll, 0.0F, maxScroll);
  draw.scrollY = scroll;
  state.controlRects.clear();

  // Full-height slanted field behind the page.
  {
    ui::PanelStyle backdrop = ui::glassPanel(ui::argb(kBackdropFill), 0x00FFFFFFU);
    backdrop.skewDegrees = 15.0F;
    backdrop.strokeWidthDp = 0.0F;
    ui::drawPanelShape(drawList, m.canvas, m.pixelBox(0.0F, 0.0F, m.width, m.height), backdrop);
  }

  // Page rows, clipped to the scroll viewport (which starts below the top padding).
  gInteractionClipTop = contentTopPx(m);
  gInteractionClipBottom = m.height;
  drawList->PushClipRect(ImVec2(0.0F, contentTopPx(m)), ImVec2(m.width, m.height), true);
  float contentY = m.dp(20.0F);
  for (const Block& block : blocks) {
    const float top = contentTopPx(m) + contentY - scroll;
    const float left = rowLeftPx(m, contentY, scroll);
    switch (block.kind) {
      case BlockKind::Heading: drawHeading(draw, block, left, top); break;
      case BlockKind::Slider: drawSlider(draw, block, left, top); break;
      case BlockKind::Switch: drawSwitch(draw, block, left, top); break;
      case BlockKind::Edit: drawEdit(draw, block, left, top); break;
      case BlockKind::Action: drawAction(draw, block, left, top); break;
      case BlockKind::Dual: drawDual(draw, block, left, top); break;
      case BlockKind::Replay: drawReplayRow(draw, block, left, top); break;
      case BlockKind::Empty: drawEmpty(draw, block, left, top); break;
      case BlockKind::Blank: break;
    }
    contentY += block.height;
  }
  drawList->PopClipRect();

  // Corner control and tab strip sit above the scroll view.
  drawBackButton(draw);
  drawTabBar(draw, tab, state.tabStart, state.tabFrom);

  // Pointer bookkeeping at the end of the frame: a release that no control consumed must not
  // leave a control latched.
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    pointer.activeKey.clear();
    pointer.sliderDragging = false;
  }

  drawDialogs(context, m, backLayout, tabLayout);
  drawSkinSelectDialog(context, m, drawList, context.nowSec);

  ui::endFullscreenWindow();

  // Run a native picker only after every row and dialog has been drawn. On a platform whose
  // picker blocks, the wait no longer sits in the middle of the settings row's draw call.
  const ActionKind pendingImport = state.pendingImport;
  state.pendingImport = ActionKind::None;
  switch (pendingImport) {
    case ActionKind::SkinImport:
      (void)importSkinFromDialog(context);
      break;
    case ActionKind::ReplayImport:
      (void)importReplayFromDialog(context);
      break;
    default:
      break;
  }
}

std::vector<std::array<float, 4>> registeredSettingsControlRects() {
  return screenState().controlRects;
}

} // namespace rechigros::app
