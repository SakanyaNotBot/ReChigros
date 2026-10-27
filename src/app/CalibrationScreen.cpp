#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include "rechigros/audio/AudioEngine.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>

// Latency calibration screen. A full-screen stage on the left shows a looping note that
// crosses the judgement line once per bar, and a control panel on the right holds the offset
// slider, the fine adjustment buttons and the navigation buttons. The stage and the panel
// reproduce the reference activity layout and drawing.
namespace rechigros::app {
namespace {

constexpr double kLoopSeconds = 2.0;
constexpr double kNoteHitSeconds = 1.0;
constexpr double kNoteSpawnSeconds = 1.5;
constexpr double kNoteFallSeconds = 1.5;
constexpr double kHitEffectSeconds = 0.5;
constexpr int kHitFxColumns = 5;
constexpr int kHitFxRows = 6;
constexpr std::uint32_t kPerfectColor = 0xFFFFEC9FU; // 255, 236, 160
constexpr std::uint32_t kApIndicatorColor = 0xFFFEFFA9U; // 254, 255, 169
constexpr float kPerfectAlpha = 0.882F; // 0xE1 / 255

// Stage geometry of the reference calibration view.
constexpr float kJudgeLineHeightFactor = 0.28F;
constexpr float kJudgeLineYFactor = 0.75F;
constexpr float kNoteWidthFactor = 0.1234375F;

struct Metrics {
  ui::Canvas canvas;
  float scale{1.0F};
  float density{1.75F};
  float width{};
  float height{};

  [[nodiscard]] float dp(float value) const { return std::round(value * density); }
  [[nodiscard]] ui::Box pixelBox(float x0, float y0, float x1, float y1) const {
    return ui::Box{(x0 - canvas.origin.x) / scale, (y0 - canvas.origin.y) / scale,
        (x1 - canvas.origin.x) / scale, (y1 - canvas.origin.y) / scale};
  }
};

Metrics metrics() {
  Metrics result;
  result.canvas = ui::canvas();
  result.scale = std::max(0.01F, result.canvas.scale);
  result.density = result.canvas.dp(1.0F);
  result.width = result.canvas.viewport.x;
  result.height = result.canvas.viewport.y;
  return result;
}

void drawTextPx(ImDrawList* drawList, const Metrics& m, float left, float centerY, float width,
    float sizeDp, bool bold, ImU32 color, const std::string& text,
    ui::HAlign align = ui::HAlign::Left) {
  if (drawList == nullptr || text.empty() || width <= 0.0F) return;
  // Platform text views measure a little taller than the bare font metrics; use the same
  // factor for every panel line so the stack keeps the reference proportions.
  const float boxHeight = std::round(sizeDp * m.density * 1.2F);
  ui::TextStyle style;
  style.sizeDp = sizeDp;
  style.bold = bold;
  style.color = color;
  style.align = align;
  style.valign = ui::VAlign::Middle;
  style.fitWidth = true;
  const ui::Box box =
      m.pixelBox(left, centerY - boxHeight * 0.5F, left + width, centerY + boxHeight * 0.5F);
  ui::drawLabel(drawList, m.canvas, box, text, style, 0.0);
}

// Length in bytes of the UTF-8 sequence that starts at `index`.
std::size_t utf8LengthAt(const std::string& text, std::size_t index) {
  if (index >= text.size()) return 0;
  const auto lead = static_cast<unsigned char>(text[index]);
  std::size_t length = 1;
  if ((lead & 0x80U) == 0) {
    length = 1;
  } else if ((lead & 0xE0U) == 0xC0U) {
    length = 2;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4;
  }
  return std::min(length, text.size() - index);
}

// Breaks a single line of text at codepoint boundaries so it fits `maxWidthPx`.
std::vector<std::string> wrapText(const Metrics& m, const std::string& text, float sizeDp,
    float maxWidthPx) {
  std::vector<std::string> lines;
  std::string current;
  std::size_t index = 0;
  while (index < text.size()) {
    const std::size_t length = utf8LengthAt(text, index);
    const std::string candidate = current + text.substr(index, length);
    const float width = ui::labelWidth(m.canvas, candidate, sizeDp, false) * m.scale;
    if (width > maxWidthPx && !current.empty()) {
      lines.push_back(current);
      current.clear();
      continue;
    }
    current = candidate;
    index += length;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

double wrapLoop(double value) {
  double result = std::fmod(value, kLoopSeconds);
  if (result < 0.0) result += kLoopSeconds;
  return result;
}

// Normalised fall progress in [0,1], or NaN while the note is between the spawn and hit times.
double noteDrawProgress(double loopTime) {
  if (loopTime >= kNoteSpawnSeconds) {
    return (loopTime - kNoteSpawnSeconds) / kNoteFallSeconds;
  }
  if (loopTime <= kNoteHitSeconds) {
    return (loopTime + kLoopSeconds - kNoteSpawnSeconds) / kNoteFallSeconds;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

double noteMarkerProgress(double loopTime) {
  if (loopTime >= kNoteSpawnSeconds) {
    return (loopTime - kNoteSpawnSeconds) / kNoteFallSeconds;
  }
  return (loopTime + kLoopSeconds - kNoteSpawnSeconds) / kNoteFallSeconds;
}

struct HitEffect {
  float x{};
  float y{};
  double startSec{};
  float rotateDeg[4]{};
  float radiusBase[4]{};
};

struct TouchMarker {
  double startSec{};
  float y{};
};

struct StageState {
  bool wasBeforeHit{};
  double previousLoopTime{-1.0};
  float lastNoteY{};
  bool noteVisible{};
  std::vector<HitEffect> hitEffects;
  std::vector<TouchMarker> touchMarkers;
  std::mt19937 random{0xC411B47EU};
  std::uint64_t hitFxTexture{};
  int hitFxWidth{};
  int hitFxHeight{};
  std::uint64_t noteTexture{};
  int noteWidth{};
  int noteHeight{};
};

StageState& stageState() {
  static StageState state;
  return state;
}

void resetStage() {
  StageState& state = stageState();
  state.wasBeforeHit = false;
  state.previousLoopTime = -1.0;
  state.lastNoteY = 0.0F;
  state.noteVisible = false;
  state.hitEffects.clear();
  state.touchMarkers.clear();
  state.random.seed(0xC411B47EU);
}

struct PanelLayout {
  float x{};
  float width{};
  float paddingX{};
  float paddingY{};
  float innerWidth{};
  float titleHeight{};
  float hintTop{};
  float hintHeight{};
  float valueTop{};
  float valueHeight{};
  float sliderTop{};
  float sliderHeight{};
  float stepTop{};
  float stepHeight{};
  float resetTop{};
  float buttonHeight{};
  float resetHeight{};
  float doneTop{};
};

PanelLayout panelLayout(const Metrics& m) {
  PanelLayout layout;
  const float requested = m.dp(320.0F);
  layout.width = std::min(requested, m.width * 0.45F);
  layout.x = m.width - layout.width;
  layout.paddingX = m.dp(20.0F);
  layout.paddingY = m.dp(18.0F);
  layout.innerWidth = std::max(1.0F, layout.width - layout.paddingX * 2.0F);

  const float titlePx = 20.0F * m.density;
  const float hintPx = 13.0F * m.density;
  const float valuePx = 28.0F * m.density;
  layout.titleHeight = std::round(titlePx * 1.2F);
  layout.hintTop = m.dp(30.0F) + m.dp(14.0F); // title start plus its top margin
  {
    const auto lines = wrapText(m, "听到节拍时点击左侧区域，调整到音符与判定线同时到达", 13.0F,
        m.dp(320.0F) - m.dp(40.0F));
    const float lineHeight = std::round(hintPx * 1.2F);
    layout.hintHeight = lineHeight * static_cast<float>(std::max<std::size_t>(1, lines.size()));
  }
  layout.valueTop = layout.hintTop + layout.hintHeight + m.dp(24.0F);
  layout.valueHeight = std::round(valuePx * 1.2F);
  layout.sliderTop = layout.valueTop + layout.valueHeight + m.dp(12.0F);
  layout.sliderHeight = m.dp(48.0F);
  layout.stepTop = layout.sliderTop + layout.sliderHeight + m.dp(16.0F);
  layout.stepHeight = m.dp(48.0F);
  layout.buttonHeight = m.dp(48.0F);
  layout.resetHeight = layout.stepTop + layout.stepHeight;
  layout.resetHeight += m.dp(16.0F); // flexible spacer minimum
  layout.doneTop = 0.0F;
  return layout;
}

// Small helper mirroring the immediate-mode pointer handling used elsewhere on the screens.
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
  snapshot.clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  snapshot.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  return snapshot;
}

struct PanelPointer {
  std::string activeKey;
};

PanelPointer& panelPointer() {
  static PanelPointer pointer;
  return pointer;
}

bool contains(ImVec2 point, float x0, float y0, float x1, float y1) {
  return point.x >= x0 && point.x <= x1 && point.y >= y0 && point.y <= y1;
}

bool buttonPressed(const char* key, float x0, float y0, float x1, float y1) {
  PanelPointer& pointer = panelPointer();
  const PointerSnapshot snapshot = pointerSnapshot();
  const bool inside = contains(snapshot.position, x0, y0, x1, y1);
  if (snapshot.clicked && inside && pointer.activeKey.empty()) {
    pointer.activeKey = key;
    return false;
  }
  if (pointer.activeKey == key && snapshot.released) {
    pointer.activeKey.clear();
    return inside;
  }
  return false;
}

void drawRoundedButton(ImDrawList* drawList, const Metrics& m, float x, float y, float width,
    float height, const std::string& label) {
  const float radius = m.dp(8.0F);
  drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), ui::argb(0xFF263343U),
      radius);
  drawList->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), ui::argb(0xFF4A5A6EU), radius, 0,
      std::max(1.0F, m.dp(1.0F)));
  const float centerY = y + height * 0.5F;
  drawTextPx(drawList, m, x, centerY, width, 14.0F, true, ui::argb(0xFFF3F7FBU), label,
      ui::HAlign::Center);
}

// Material slider: rounded track with the active part in the accent colour and a circular
// thumb with a translucent halo.
void drawSlider(ImDrawList* drawList, const Metrics& m, float x, float y, float width,
    float height, float value, float from, float to) {
  const float trackHeight = std::max(2.0F, m.dp(4.0F));
  const float trackLeft = x + m.dp(10.0F);
  const float trackRight = x + width - m.dp(10.0F);
  const float centerY = y + height * 0.5F;
  const float fraction = to > from ? std::clamp((value - from) / (to - from), 0.0F, 1.0F) : 0.0F;
  const float thumbRadius = m.dp(10.0F);
  const float thumbX = trackLeft + (trackRight - trackLeft) * fraction;
  const float radius = trackHeight * 0.5F;
  drawList->AddRectFilled(ImVec2(trackLeft, centerY - radius),
      ImVec2(trackRight, centerY + radius), ui::argb(0x3DFFFFFFU), radius);
  drawList->AddRectFilled(ImVec2(trackLeft, centerY - radius), ImVec2(thumbX, centerY + radius),
      ui::argb(0xFF56D6C9U), radius);
  drawList->AddCircleFilled(ImVec2(thumbX, centerY), thumbRadius * 2.0F,
      ui::argb(0x1F56D6C9U), 32);
  drawList->AddCircleFilled(ImVec2(thumbX, centerY), thumbRadius, ui::argb(0xFF56D6C9U), 32);
}

// One frame of the reference hit effect atlas plus its square particles.
void drawHitEffects(const Metrics& m, ImDrawList* drawList, float stageWidth, float noteWidth,
    double nowSec) {
  StageState& state = stageState();
  const float effectSize = noteWidth * 1.375F * 1.12F;
  const float particleScale = stageWidth / 4040.0F * 3.0F;
  const float baseParticleSize = particleScale * 30.0F;
  const float duration = static_cast<float>(kHitEffectSeconds);

  for (auto iterator = state.hitEffects.begin(); iterator != state.hitEffects.end();) {
    const float p = static_cast<float>((nowSec - iterator->startSec) / duration);
    if (p >= 1.0F) {
      iterator = state.hitEffects.erase(iterator);
      continue;
    }
    if (p < 0.0F) {
      ++iterator;
      continue;
    }

    if (state.hitFxTexture != 0 && state.hitFxWidth > 0 && state.hitFxHeight > 0) {
      const int frameWidth = state.hitFxWidth / kHitFxColumns;
      const int frameHeight = state.hitFxHeight / kHitFxRows;
      if (frameWidth > 2 && frameHeight > 2) {
        int index = static_cast<int>(std::floor(p * kHitFxColumns * kHitFxRows));
        index = std::clamp(index, 0, kHitFxColumns * kHitFxRows - 1);
        const int column = index % kHitFxColumns;
        const int row = index / kHitFxColumns;
        const float u0 = static_cast<float>(column * frameWidth + 1)
            / static_cast<float>(state.hitFxWidth);
        const float v0 = static_cast<float>(row * frameHeight + 1)
            / static_cast<float>(state.hitFxHeight);
        const float u1 = static_cast<float>(column * frameWidth + frameWidth - 1)
            / static_cast<float>(state.hitFxWidth);
        const float v1 = static_cast<float>(row * frameHeight + frameHeight - 1)
            / static_cast<float>(state.hitFxHeight);
        const ImU32 tint = IM_COL32(255, 236, 160,
            static_cast<int>(std::lround(kPerfectAlpha * 255.0F)));
        drawList->AddImage(static_cast<ImTextureID>(state.hitFxTexture),
            ImVec2(iterator->x - effectSize * 0.5F, iterator->y - effectSize * 0.5F),
            ImVec2(iterator->x + effectSize * 0.5F, iterator->y + effectSize * 0.5F),
            ImVec2(u0, v0), ImVec2(u1, v1), tint);
      }
    }

    const float k = 9.0F * p / (8.0F * p + 1.0F);
    const float alpha = 1.0F - p;
    const float size = baseParticleSize
        * ((((0.2078F * p - 1.6524F) * p) + 1.6399F) * p + 0.4988F);
    const ImU32 particleColor = IM_COL32(255, 236, 160,
        static_cast<int>(std::lround(std::clamp(alpha, 0.0F, 1.0F) * 255.0F)));
    for (int index = 0; index < 4; ++index) {
      const float particleRadius = particleScale * iterator->radiusBase[index] * k;
      const double radians = static_cast<double>(iterator->rotateDeg[index]) * 3.14159265358979
          / 180.0;
      const float px = iterator->x + static_cast<float>(particleRadius * std::cos(radians));
      const float py = iterator->y + static_cast<float>(particleRadius * std::sin(radians));
      drawList->AddRectFilled(ImVec2(px - size * 0.5F, py - size * 0.5F),
          ImVec2(px + size * 0.5F, py + size * 0.5F), particleColor);
    }
    ++iterator;
  }
}

void drawTouchMarkers(const Metrics& m, ImDrawList* drawList, float centerX, float lineWidth,
    double nowSec) {
  StageState& state = stageState();
  const float halfThickness = 1.0F * m.density;
  for (auto iterator = state.touchMarkers.begin(); iterator != state.touchMarkers.end();) {
    const float progress = static_cast<float>(nowSec - iterator->startSec);
    if (progress >= 1.0F) {
      iterator = state.touchMarkers.erase(iterator);
      continue;
    }
    const float clamped = std::max(0.0F, progress);
    const float alpha = 1.0F - clamped;
    const ImU32 color = IM_COL32(255, 255, 255,
        static_cast<int>(std::lround(255.0F * std::clamp(alpha, 0.0F, 1.0F))));
    const float half = lineWidth * 0.5F * clamped;
    drawList->AddRectFilled(ImVec2(centerX - half, iterator->y - halfThickness),
        ImVec2(centerX + half, iterator->y + halfThickness), color);
    ++iterator;
  }
}

void drawStage(AppContext& context, const Metrics& m, const PanelLayout& panel, double nowSec) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  StageState& state = stageState();

  // Vertical gradient backdrop of the reference stage.
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(m.width, m.height * 0.5F),
      ui::argb(0xFF10131AU), ui::argb(0xFF10131AU), ui::argb(0xFF151A23U),
      ui::argb(0xFF151A23U));
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, m.height * 0.5F), ImVec2(m.width, m.height),
      ui::argb(0xFF151A23U), ui::argb(0xFF151A23U), ui::argb(0xFF0D1016U),
      ui::argb(0xFF0D1016U));

  const float playRight = std::max(m.width * 0.55F, m.width - panel.width);
  const float centerX = playRight * 0.5F;
  const float lineHalf = playRight * kJudgeLineHeightFactor;
  const float lineY = m.height * kJudgeLineYFactor;
  const float noteWidth = playRight * kNoteWidthFactor;
  const float noteAspect = state.noteWidth > 0 && state.noteHeight > 0
      ? static_cast<float>(state.noteHeight) / static_cast<float>(state.noteWidth)
      : 0.36F;
  const float noteHeight = noteWidth * noteAspect;

  const double playhead = context.audio != nullptr ? context.audio->cursorSeconds() : 0.0;
  const double loopTime =
      wrapLoop(playhead - static_cast<double>(context.calibration.offsetMs) / 1000.0);
  const double progress = noteDrawProgress(loopTime);

  // Judgement line; the reference draws it in the AP indicator colour when that is enabled.
  const ImU32 lineColor = context.settings.apfcIndicator ? ui::argb(kApIndicatorColor)
                                                         : IM_COL32(255, 255, 255, 255);
  drawList->AddRectFilled(ImVec2(centerX - lineHalf, lineY - m.dp(2.0F)),
      ImVec2(centerX + lineHalf, lineY + m.dp(2.0F)), lineColor);

  if (std::isfinite(progress)) {
    const float y = lineY * static_cast<float>(progress);
    state.lastNoteY = y;
    state.noteVisible = y >= 0.0F && y <= m.height;
    if (state.noteTexture != 0) {
      drawList->AddImage(static_cast<ImTextureID>(state.noteTexture),
          ImVec2(centerX - noteWidth * 0.5F, y - noteHeight * 0.5F),
          ImVec2(centerX + noteWidth * 0.5F, y + noteHeight * 0.5F));
    } else {
      drawList->AddRectFilled(ImVec2(centerX - noteWidth * 0.5F, y - noteHeight * 0.5F),
          ImVec2(centerX + noteWidth * 0.5F, y + noteHeight * 0.5F), ui::argb(0xFF56D6C9U),
          noteHeight * 0.5F);
    }
    state.wasBeforeHit = true;
  } else {
    if (state.wasBeforeHit) {
      if (context.audio != nullptr) context.audio->triggerSoundEffect(1);
      HitEffect effect;
      effect.x = centerX;
      effect.y = lineY;
      effect.startSec = nowSec;
      for (int index = 0; index < 4; ++index) {
        std::uniform_real_distribution<float> rotation(0.0F, 360.0F);
        std::uniform_real_distribution<float> radius(185.0F, 265.0F);
        effect.rotateDeg[index] = rotation(state.random);
        effect.radiusBase[index] = radius(state.random);
      }
      state.hitEffects.push_back(effect);
    }
    state.wasBeforeHit = false;
    state.noteVisible = false;
  }

  // Touch input on the stage records an expanding marker at the note's current height.
  const PointerSnapshot snapshot = pointerSnapshot();
  if (snapshot.clicked && snapshot.position.x < m.width - panel.width) {
    const double markerProgress = noteMarkerProgress(loopTime);
    const float markerY = lineY * static_cast<float>(markerProgress);
    if (state.noteVisible && markerY >= 0.0F && markerY <= m.height) {
      state.touchMarkers.push_back(TouchMarker{nowSec, markerY});
    }
  }

  drawHitEffects(m, drawList, playRight, noteWidth, nowSec);
  const float lineWidth = std::max(centerX, m.width - centerX) * 2.0F;
  drawTouchMarkers(m, drawList, centerX, lineWidth, nowSec);
}

void drawPanel(AppContext& context, const Metrics& m, const PanelLayout& panel, double nowSec) {
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  PanelPointer& pointer = panelPointer();

  // Panel surface.
  drawList->AddRectFilled(ImVec2(panel.x, 0.0F), ImVec2(m.width, m.height),
      ui::argb(0xCC1B222DU));

  const float contentX = panel.x + panel.paddingX;
  const float contentTop = panel.paddingY;
  float y = contentTop;

  // Title.
  y += panel.titleHeight * 0.5F;
  drawTextPx(drawList, m, contentX, y, panel.innerWidth, 20.0F, true, ui::argb(0xFFF3F7FBU),
      "延迟校准", ui::HAlign::Center);
  y += panel.titleHeight * 0.5F + m.dp(14.0F);

  // Hint, wrapped to the panel width.
  {
    const auto lines = wrapText(m, "听到节拍时点击左侧区域，调整到音符与判定线同时到达", 13.0F,
        panel.innerWidth);
    const float lineHeight = std::round(13.0F * m.density * 1.2F);
    for (const auto& line : lines) {
      y += lineHeight * 0.5F;
      drawTextPx(drawList, m, contentX, y, panel.innerWidth, 13.0F, false, ui::argb(0xFFA9B4C2U),
          line, ui::HAlign::Center);
      y += lineHeight * 0.5F;
    }
  }
  y += m.dp(24.0F);

  // Offset value.
  {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d ms",
        static_cast<int>(std::lround(context.calibration.offsetMs)));
    const float boxHeight = std::round(28.0F * m.density * 1.2F);
    y += boxHeight * 0.5F;
    drawTextPx(drawList, m, contentX, y, panel.innerWidth, 28.0F, true, ui::argb(0xFFF3F7FBU),
        buffer, ui::HAlign::Center);
    y += boxHeight * 0.5F + m.dp(12.0F);
  }

  // Offset slider. A press jumps to the pressed position, then drags continuously.
  const float sliderTop = y;
  const float sliderHeight = panel.sliderHeight;
  {
    const PointerSnapshot snapshot = pointerSnapshot();
    const bool inside = contains(snapshot.position, contentX, sliderTop, contentX + panel.innerWidth,
        sliderTop + sliderHeight);
    if (snapshot.clicked && inside && pointer.activeKey.empty()) pointer.activeKey = "slider";
    if (pointer.activeKey == "slider") {
      if (snapshot.down || snapshot.released) {
        const float trackLeft = contentX + m.dp(10.0F);
        const float trackRight = contentX + panel.innerWidth - m.dp(10.0F);
        const float fraction = std::clamp((snapshot.position.x - trackLeft)
                / std::max(1.0F, trackRight - trackLeft), 0.0F, 1.0F);
        context.calibration.offsetMs = -500.0F + fraction * 1000.0F;
      }
      if (snapshot.released) pointer.activeKey.clear();
    }
  }
  drawSlider(drawList, m, contentX, sliderTop, panel.innerWidth, sliderHeight,
      context.calibration.offsetMs, -500.0F, 500.0F);
  y += sliderHeight + m.dp(16.0F);

  // Fine adjustment buttons.
  {
    const float gap = m.dp(3.0F);
    const float buttonWidth = (panel.innerWidth - gap * 8.0F) / 4.0F;
    const char* const labels[4] = {"-50", "-5", "+5", "+50"};
    const int deltas[4] = {-50, -5, 5, 50};
    for (int index = 0; index < 4; ++index) {
      const float buttonX = contentX + gap * (1.0F + static_cast<float>(index) * 2.0F)
          + buttonWidth * static_cast<float>(index);
      char key[16];
      std::snprintf(key, sizeof(key), "step%d", index);
      if (buttonPressed(key, buttonX, y, buttonX + buttonWidth, y + panel.stepHeight)) {
        context.calibration.offsetMs = std::clamp(
            context.calibration.offsetMs + static_cast<float>(deltas[index]), -500.0F, 500.0F);
      }
      drawRoundedButton(drawList, m, buttonX, y, buttonWidth, panel.stepHeight, labels[index]);
    }
  }
  y += panel.stepHeight;

  // Flexible spacer keeps the action buttons on the bottom edge of the panel.
  const float resetHeight = panel.buttonHeight;
  const float doneHeight = panel.buttonHeight;
  const float bottomMargin = m.dp(10.0F);
  const float buttonsTop = m.height - panel.paddingY - doneHeight - bottomMargin - resetHeight;
  y = std::max(y + m.dp(16.0F), buttonsTop);

  if (buttonPressed("reset", contentX, y, contentX + panel.innerWidth, y + resetHeight)) {
    context.calibration.offsetMs = 0.0F;
  }
  drawRoundedButton(drawList, m, contentX, y, panel.innerWidth, resetHeight, "重置");
  y += resetHeight + bottomMargin;

  if (buttonPressed("done", contentX, y, contentX + panel.innerWidth, y + doneHeight)) {
    exitCalibration(context, true);
    return;
  }
  drawRoundedButton(drawList, m, contentX, y, panel.innerWidth, doneHeight, "完成");

  (void)nowSec;
}

} // namespace

void drawCalibrationScreen(AppContext& context) {
  const Metrics m = metrics();
  ui::beginFullscreenWindow("##calibration");
  StageState& state = stageState();

  // Load the calibration loop on the first frame. A missing audio device still draws one
  // static frame (so the screen always produces content, including the headless self-test)
  // and then leaves the screen with the previous offset unchanged.
  bool loadFailed = false;
  if (!context.calibration.audioLoaded) {
    bool loaded = false;
    if (context.audio != nullptr) {
      // The bundled assets are not files in a packaged build: they live inside the package and
      // only the platform's asset reader can reach them. Materialising hands back a real path the
      // decoder can open, and returns the path unchanged on a plain directory tree.
      const auto music =
          platform::AssetManager::materialize(platform::assetRoot(), "audio/calibration.wav");
      context.audio->stop();
      context.audio->setMusicLooping(true);
      context.audio->setVolume(1.0F);
      context.audio->setSoundEffectVolume(1.0F);
      context.audio->setPlaybackSpeed(1.0F);
      if (context.audio->loadMusic(music)) {
        context.audio->play();
        loaded = true;
      }
    }
    context.calibration.audioLoaded = true;
    context.calibration.markers.clear();
    context.calibration.previousLoopTime = -1.0;
    context.calibration.offsetMs = static_cast<float>(context.settings.audioOffsetMs);
    resetStage();
    if (!loaded) {
      loadFailed = true;
    } else {
      state.noteTexture = assetTexture(context, "res/click.png", 512);
      state.noteWidth = state.noteTexture != 0 ? context.textures.width(state.noteTexture) : 0;
      state.noteHeight = state.noteTexture != 0 ? context.textures.height(state.noteTexture) : 0;
      state.hitFxTexture = assetTexture(context, "res/hit_fx.png");
      state.hitFxWidth = state.hitFxTexture != 0 ? context.textures.width(state.hitFxTexture) : 0;
      state.hitFxHeight = state.hitFxTexture != 0 ? context.textures.height(state.hitFxTexture) : 0;
    }
  }

  const PanelLayout panel = panelLayout(m);
  if (loadFailed) {
    // One frame of the stage and the panel surface, then report and return to the caller.
    drawStage(context, m, panel, context.nowSec);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(panel.x, 0.0F), ImVec2(m.width, m.height),
        ui::argb(0xCC1B222DU));
    ui::endFullscreenWindow();
    notify(context, "校准音频加载失败");
    exitCalibration(context, false);
    return;
  }

  // The reference activity finishes with the current offset on the system back gesture.
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    exitCalibration(context, true);
    ui::endFullscreenWindow();
    return;
  }

  // Textures are requested lazily and may still be queued on the first frames; retry until
  // they resolve so the note and the hit effect use the bundled artwork.
  if (state.noteTexture == 0) {
    state.noteTexture = assetTexture(context, "res/click.png", 512);
    state.noteWidth = state.noteTexture != 0 ? context.textures.width(state.noteTexture) : 0;
    state.noteHeight = state.noteTexture != 0 ? context.textures.height(state.noteTexture) : 0;
  }
  if (state.hitFxTexture == 0) {
    state.hitFxTexture = assetTexture(context, "res/hit_fx.png");
    state.hitFxWidth = state.hitFxTexture != 0 ? context.textures.width(state.hitFxTexture) : 0;
    state.hitFxHeight = state.hitFxTexture != 0 ? context.textures.height(state.hitFxTexture) : 0;
  }

  drawStage(context, m, panel, context.nowSec);
  drawPanel(context, m, panel, context.nowSec);

  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) panelPointer().activeKey.clear();

  ui::endFullscreenWindow();
}

void exitCalibration(AppContext& context, bool apply) {
  if (context.audio != nullptr) {
    context.audio->stop();
    context.audio->setMusicLooping(false);
    context.audio->setLowPassEnabled(false);
    context.audio->setPlaybackSpeed(1.0F);
  }
  context.calibration.audioLoaded = false;
  context.calibration.markers.clear();
  resetStage();
  if (apply) {
    context.settings.audioOffsetMs =
        std::clamp(static_cast<int>(std::lround(context.calibration.offsetMs)), -500, 500);
    context.settingsDirty = true;
  }
  commitSettings(context);
  navigateTo(context, context.calibration.returnTo);
}

} // namespace rechigros::app
