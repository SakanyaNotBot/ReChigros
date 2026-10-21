#pragma once

#include "imgui.h"

#include <cstdint>

// Small drawing helpers shared by every screen. The application style is a dark
// backdrop with slightly tilted translucent panels, following the reference
// layout proportions without copying any platform-specific view code.
namespace rechigros::app::ui {

constexpr float kSkewRatio = 0.2679492F; // tan(15 degrees)

// Installs the shared dark theme once after the ImGui context exists.
void applyTheme();

[[nodiscard]] inline ImU32 rgba(std::uint32_t value) {
  // Colours are written as 0xAARRGGBB, the layout the reference design tokens use.
  return IM_COL32(static_cast<int>((value >> 16) & 0xFFU), static_cast<int>((value >> 8) & 0xFFU),
      static_cast<int>(value & 0xFFU), static_cast<int>((value >> 24) & 0xFFU));
}

// Full-window animated backdrop: three slowly rotating triangle grids.
void drawBackdrop(ImDrawList* drawList, ImVec2 size, double timeSec);

// Draws one parallelogram panel. `skew` is the horizontal offset between the top
// and bottom edge; set `straightLeft`/`straightRight` to keep that edge vertical.
void drawPanel(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, float skew, ImU32 fill,
    ImU32 border, float borderWidth, bool straightLeft = false, bool straightRight = false);

// Text helpers working directly on a draw list, so layout can mix widgets and
// custom drawing without fighting ImGui's cursor.
void drawText(ImDrawList* drawList, ImVec2 position, float size, ImU32 color, const char* text);
void drawTextCentered(ImDrawList* drawList, ImVec2 center, float size, ImU32 color,
    const char* text);
[[nodiscard]] ImVec2 measureText(const char* text, float size);

// Invisible hit area with a drawn tilted panel and centered label. Returns true
// on a release inside the panel.
bool skewButton(const char* id, const char* label, ImVec2 topLeft, ImVec2 size, float textSize,
    float skew = -1.0F, ImU32 fill = IM_COL32(34, 42, 53, 154), ImU32 border = IM_COL32(255, 255, 255, 150),
    bool straightLeft = false, bool straightRight = false, bool accentEdge = false);

// Square icon button that draws `texture` centered inside a tilted panel.
bool iconButton(const char* id, std::uint64_t texture, ImVec2 topLeft, ImVec2 size,
    const char* tooltip = nullptr, bool straightLeft = false, bool straightRight = false,
    bool accentEdge = false);

// Starts a borderless full-window container covering the whole viewport.
void beginFullscreenWindow(const char* id);
void endFullscreenWindow();

// Colour constants shared by the screens.
constexpr std::uint32_t kTextPrimary = 0xF4FFFFFFU;
constexpr std::uint32_t kTextSecondary = 0xCFFFFFFFU;
constexpr std::uint32_t kTextMuted = 0xB8FFFFFFU;
constexpr std::uint32_t kPanelFill = 0x9A222A35U;
constexpr std::uint32_t kPanelBorder = 0x96FFFFFFU;
constexpr std::uint32_t kAccent = 0xFFD3624FU;
constexpr std::uint32_t kGradePerfect = 0xFFFEFFA9U;
constexpr std::uint32_t kGradeCombo = 0xFFA2EEFFU;

} // namespace rechigros::app::ui
