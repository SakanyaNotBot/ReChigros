#pragma once

#include "rechigros/app/UiText.hpp"

#include "imgui.h"

#include <cstdint>
#include <string_view>

// Geometry and drawing primitives shared by every interface screen.
//
// The interface uses two coordinate spaces that are kept separate here:
//   * design units: a 731 x 415 canvas scaled uniformly to the viewport and centred;
//   * density units: the platform-independent dp value, scaled by the display density.
// At the reference display size the density is 1.75 and the canvas scale is 1080 / 415, so
// one density unit is kDensityRatio design units. Every metric in this file takes design
// units; helpers that accept a density value convert through Canvas::dp.
namespace rechigros::app::ui {

constexpr float kDesignWidth = 731.0F;
constexpr float kDesignHeight = 415.0F;
constexpr float kDensityRatio = 1.75F / (1080.0F / kDesignHeight);

struct Canvas {
  float scale{1.0F};
  ImVec2 origin{};
  ImVec2 viewport{};
  [[nodiscard]] ImVec2 point(float x, float y) const {
    return ImVec2(origin.x + x * scale, origin.y + y * scale);
  }
  [[nodiscard]] float units(float design) const { return design * scale; }
  // Density units to pixels.
  [[nodiscard]] float dp(float value) const { return value * kDensityRatio * scale; }
};

// Canvas of the current frame's main viewport.
[[nodiscard]] Canvas canvas();

struct Box {
  float x0{};
  float y0{};
  float x1{};
  float y1{};
  [[nodiscard]] float width() const { return x1 - x0; }
  [[nodiscard]] float height() const { return y1 - y0; }
  [[nodiscard]] float centerX() const { return (x0 + x1) * 0.5F; }
  [[nodiscard]] float centerY() const { return (y0 + y1) * 0.5F; }
};

struct PxRect {
  ImVec2 min{};
  ImVec2 max{};
};

[[nodiscard]] inline PxRect toPixels(const Canvas& c, const Box& box) {
  return PxRect{c.point(box.x0, box.y0), c.point(box.x1, box.y1)};
}

// Colours are written as 0xAARRGGBB, matching the palette notation of the layout.
[[nodiscard]] inline ImU32 argb(std::uint32_t value) {
  return IM_COL32(static_cast<int>((value >> 16) & 0xFFU),
      static_cast<int>((value >> 8) & 0xFFU), static_cast<int>(value & 0xFFU),
      static_cast<int>((value >> 24) & 0xFFU));
}
[[nodiscard]] inline ImU32 fade(ImU32 color, float alpha) {
  const int base = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFFU);
  const int applied = static_cast<int>(static_cast<float>(base) * alpha + 0.5F);
  return (color & ~IM_COL32_A_MASK)
      | (static_cast<ImU32>(applied <= 0 ? 0 : (applied >= 255 ? 255 : applied))
          << IM_COL32_A_SHIFT);
}
// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

// Style of one skewed panel. The geometry follows the interface's parallelogram panels:
// a 15 degree leading-edge skew with an optional straight edge, a top-wider variant and a
// bright accent strip along one edge.
struct PanelStyle {
  ImU32 fill{};
  ImU32 stroke{};
  float strokeWidthDp{1.0F};
  float skewDegrees{15.0F};
  bool straightLeft{};
  bool straightRight{};
  bool topWider{};
  int accentEdge{}; // 0 none, 1 left, 2 right
  ImU32 accent{0xFFFFFFFFU};
  float accentWidthDp{};
};

[[nodiscard]] PanelStyle glassPanel(ImU32 fill, ImU32 stroke);
[[nodiscard]] PanelStyle rectPanel(ImU32 fill, ImU32 stroke);

void drawPanelShape(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const PanelStyle& style);
[[nodiscard]] bool panelContains(const Canvas& c, const Box& designBox, const PanelStyle& style,
    ImVec2 screenPoint);

// Invisible interactive area shaped like the panel. Returns true once per release inside.
struct Interaction {
  bool pressed{};
  bool hovered{};
};
Interaction panelInteraction(const char* id, const Canvas& c, const Box& designBox,
    const PanelStyle& style);

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct TextStyle {
  float sizeDp{18.0F};
  bool bold{};
  ImU32 color{0xFFFFFFFFU};
  HAlign align{HAlign::Left};
  VAlign valign{VAlign::Middle};
  bool fitWidth{true};
  bool ellipsize{};
  bool marquee{};
  float paddingLeftDp{};
  float paddingRightDp{};
  float fadingEdgeDp{};
};

// Draws one line of text inside the design box. Returns the advance width in design units.
float drawLabel(ImDrawList* drawList, const Canvas& c, const Box& designBox, std::string_view text,
    const TextStyle& style, double nowSec);
// Line-box top-left variant for text placed by custom controls.
void drawLabelLine(ImDrawList* drawList, const Canvas& c, ImVec2 designTopLeft,
    std::string_view text, float sizeDp, bool bold, ImU32 color, ImVec2* measured = nullptr);
// Advance width of a single line, in design units.
float labelWidth(const Canvas& c, std::string_view text, float sizeDp, bool bold);
// Height of a one-line layout box, in design units (includes the font's vertical padding).
float labelLineHeight(const Canvas& c, float sizeDp);

// ---------------------------------------------------------------------------
// Images and icons
// ---------------------------------------------------------------------------

enum class ImageFit { Fill, FitCenter, CenterCrop, Center };

void drawImage(ImDrawList* drawList, const PxRect& box, std::uint64_t texture, int textureWidth,
    int textureHeight, ImageFit fit, ImU32 tint = 0xFFFFFFFFU);

// Skewed plate carrying a centred icon, the shape used by the corner actions.
void drawIconPlate(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const PanelStyle& style, std::uint64_t texture, int textureWidth, int textureHeight,
    float paddingXDp, float paddingYDp, float translateXDp, ImU32 tint = 0xFFFFFFFFU);

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------

enum class Interp {
  Linear,
  AccelerateQuad,
  DecelerateQuad,
  AccelerateDecelerate,
  Overshoot,
  EaseOutCubic,
  DecelerateCubic,
};

float interpolate(Interp interp, float t);
// Normalised progress of an animation that started at `startSec`; 1 when not running.
float progress(double startSec, float durationSec, Interp interp, double nowSec);

// ---------------------------------------------------------------------------
// Tab bar
// ---------------------------------------------------------------------------

// Sliding tab strip: one dark track plate and a bright plate that slides between slices.
// The selected plate's leading edge is skewed; labels fade between the two states.
struct TabBarStyle {
  const char* const* labels{};
  int count{};
  int from{};
  int to{};
  double startSec{0.0};
};

void drawTabBar(ImDrawList* drawList, const Canvas& c, const Box& designBox,
    const TabBarStyle& style, double nowSec);
// Index of the slice under a screen point, or -1 outside the strip.
[[nodiscard]] int hitTabBar(const Canvas& c, const Box& designBox, ImVec2 screenPoint, int count);

} // namespace rechigros::app::ui
