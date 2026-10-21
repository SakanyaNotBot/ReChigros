#include "rechigros/app/UiStyle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rechigros::app::ui {
namespace {

constexpr int kGridLayers = 3;

ImVec2 panelPoint(ImVec2 topLeft, ImVec2 bottomRight, float skew, int index) {
  const float x0 = topLeft.x;
  const float y0 = topLeft.y;
  const float x1 = bottomRight.x;
  const float y1 = bottomRight.y;
  switch (index) {
    case 0: return ImVec2(x0 + skew, y0);
    case 1: return ImVec2(x1, y0);
    case 2: return ImVec2(x1 - skew, y1);
    default: return ImVec2(x0, y1);
  }
}

ImU32 brighten(ImU32 color, int amount) {
  const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFFU);
  int red = static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFFU) + amount;
  int green = static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFFU) + amount;
  int blue = static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFFU) + amount;
  red = red > 255 ? 255 : red;
  green = green > 255 ? 255 : green;
  blue = blue > 255 ? 255 : blue;
  return IM_COL32(red, green, blue, alpha);
}

} // namespace

void applyTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(0.0F, 0.0F);
  style.FramePadding = ImVec2(10.0F, 6.0F);
  style.ItemSpacing = ImVec2(8.0F, 7.0F);
  style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
  style.WindowRounding = 0.0F;
  style.ChildRounding = 4.0F;
  style.FrameRounding = 4.0F;
  style.PopupRounding = 4.0F;
  style.ScrollbarRounding = 4.0F;
  style.GrabRounding = 4.0F;
  style.WindowBorderSize = 0.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = 0.0F;
  style.ScrollbarSize = 10.0F;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10F, 0.12F, 0.15F, 0.82F);
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08F, 0.10F, 0.13F, 0.96F);
  style.Colors[ImGuiCol_Border] = ImVec4(1.0F, 1.0F, 1.0F, 0.16F);
  style.Colors[ImGuiCol_Text] = ImVec4(0.96F, 0.97F, 0.98F, 1.0F);
  style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.70F, 0.74F, 0.80F, 1.0F);
  style.Colors[ImGuiCol_Button] = ImVec4(0.16F, 0.19F, 0.24F, 0.92F);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26F, 0.31F, 0.38F, 0.95F);
  style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.84F, 0.20F, 0.12F, 1.0F);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0F, 1.0F, 1.0F, 0.10F);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0F, 1.0F, 1.0F, 0.16F);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0F, 1.0F, 1.0F, 0.22F);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.93F, 0.95F, 0.97F, 1.0F);
  style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.98F, 0.83F, 0.33F, 1.0F);
  style.Colors[ImGuiCol_Separator] = ImVec4(1.0F, 1.0F, 1.0F, 0.18F);
  style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.18F);
  style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.0F, 1.0F, 1.0F, 0.28F);
  style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0F, 1.0F, 1.0F, 0.30F);
}

void drawBackdrop(ImDrawList* drawList, ImVec2 size, double timeSec) {
  if (drawList == nullptr || size.x <= 1.0F || size.y <= 1.0F) return;
  // Vertical gradient built from two multi-colour quads.
  const ImU32 top = IM_COL32(16, 19, 26, 255);
  const ImU32 middle = IM_COL32(21, 26, 35, 255);
  const ImU32 bottom = IM_COL32(13, 16, 22, 255);
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(size.x, size.y * 0.5F), top, top,
      middle, middle);
  drawList->AddRectFilledMultiColor(ImVec2(0.0F, size.y * 0.5F), ImVec2(size.x, size.y), middle,
      middle, bottom, bottom);

  const ImVec2 center(size.x * 0.5F, size.y * 0.5F);
  const float diagonal = std::sqrt(size.x * size.x + size.y * size.y);
  const float time = static_cast<float>(timeSec);
  for (int layer = 0; layer < kGridLayers; ++layer) {
    const float rotation = time * (0.012F + 0.004F * static_cast<float>(layer))
        + static_cast<float>(layer) * 0.62F;
    const float spacing = 96.0F + static_cast<float>(layer) * 34.0F;
    const int alpha = 26 - layer * 6;
    const ImU32 color = IM_COL32(150, 190, 220, alpha);
    for (int direction = 0; direction < 3; ++direction) {
      const float angle = rotation + static_cast<float>(direction) * 1.0471976F;
      const float dirX = std::cos(angle);
      const float dirY = std::sin(angle);
      const float normalX = -dirY;
      const float normalY = dirX;
      const int lineCount = static_cast<int>(diagonal / spacing) + 2;
      for (int line = -lineCount; line <= lineCount; ++line) {
        const float offset = static_cast<float>(line) * spacing;
        const float baseX = center.x + normalX * offset;
        const float baseY = center.y + normalY * offset;
        drawList->AddLine(ImVec2(baseX - dirX * diagonal, baseY - dirY * diagonal),
            ImVec2(baseX + dirX * diagonal, baseY + dirY * diagonal), color, 1.0F);
      }
    }
  }
}

void drawPanel(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, float skew, ImU32 fill,
    ImU32 border, float borderWidth, bool straightLeft, bool straightRight) {
  if (drawList == nullptr || bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) return;
  ImVec2 points[4];
  if (straightLeft) {
    points[0] = ImVec2(topLeft.x, topLeft.y);
    points[1] = ImVec2(bottomRight.x, topLeft.y);
    points[2] = ImVec2(bottomRight.x - skew, bottomRight.y);
    points[3] = ImVec2(topLeft.x, bottomRight.y);
  } else if (straightRight) {
    points[0] = ImVec2(topLeft.x + skew, topLeft.y);
    points[1] = ImVec2(bottomRight.x, topLeft.y);
    points[2] = ImVec2(bottomRight.x, bottomRight.y);
    points[3] = ImVec2(topLeft.x, bottomRight.y);
  } else {
    points[0] = ImVec2(topLeft.x + skew, topLeft.y);
    points[1] = ImVec2(bottomRight.x + skew, topLeft.y);
    points[2] = ImVec2(bottomRight.x, bottomRight.y);
    points[3] = ImVec2(topLeft.x, bottomRight.y);
  }
  drawList->AddConvexPolyFilled(points, 4, fill);
  if (borderWidth > 0.0F) {
    drawList->AddPolyline(points, 4, border, ImDrawFlags_Closed, borderWidth);
  }
}

void drawText(ImDrawList* drawList, ImVec2 position, float size, ImU32 color, const char* text) {
  if (drawList == nullptr || text == nullptr) return;
  ImFont* font = ImGui::GetFont();
  if (font == nullptr) return;
  drawList->AddText(font, size, position, color, text);
}

void drawTextCentered(ImDrawList* drawList, ImVec2 center, float size, ImU32 color,
    const char* text) {
  if (drawList == nullptr || text == nullptr) return;
  ImFont* font = ImGui::GetFont();
  if (font == nullptr) return;
  const ImVec2 extent = font->CalcTextSizeA(size, 4096.0F, 0.0F, text);
  drawList->AddText(font, size, ImVec2(center.x - extent.x * 0.5F, center.y - extent.y * 0.5F),
      color, text);
}

ImVec2 measureText(const char* text, float size) {
  ImFont* font = ImGui::GetFont();
  if (font == nullptr || text == nullptr) return ImVec2(0.0F, 0.0F);
  return font->CalcTextSizeA(size, 4096.0F, 0.0F, text);
}

bool skewButton(const char* id, const char* label, ImVec2 topLeft, ImVec2 size, float textSize,
    float skew, ImU32 fill, ImU32 border, bool straightLeft, bool straightRight,
    bool accentEdge) {
  if (skew < 0.0F) skew = size.y * kSkewRatio;
  const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
  ImGui::SetCursorScreenPos(topLeft);
  const bool pressed = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 actualFill = hovered ? brighten(fill, 16) : fill;
  drawPanel(drawList, topLeft, bottomRight, skew, actualFill, border, 1.0F, straightLeft,
      straightRight);
  if (accentEdge) {
    const ImU32 accent = rgba(kAccent);
    if (straightLeft) {
      drawList->AddLine(ImVec2(topLeft.x, topLeft.y), ImVec2(topLeft.x, bottomRight.y), accent,
          3.0F);
    } else {
      drawList->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x - skew, bottomRight.y),
          accent, 3.0F);
    }
  }
  drawTextCentered(drawList, ImVec2((topLeft.x + bottomRight.x) * 0.5F + skew * 0.5F,
                        (topLeft.y + bottomRight.y) * 0.5F),
      textSize, rgba(kTextPrimary), label != nullptr ? label : "");
  return pressed;
}

bool iconButton(const char* id, std::uint64_t texture, ImVec2 topLeft, ImVec2 size,
    const char* tooltip, bool straightLeft, bool straightRight, bool accentEdge) {
  const float skew = size.y * kSkewRatio;
  const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
  ImGui::SetCursorScreenPos(topLeft);
  const bool pressed = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 fill = hovered ? IM_COL32(42, 52, 66, 190) : IM_COL32(16, 23, 29, 134);
  drawPanel(drawList, topLeft, bottomRight, skew, fill, IM_COL32(255, 255, 255, 150), 1.0F,
      straightLeft, straightRight);
  if (accentEdge) {
    const ImU32 accent = rgba(kAccent);
    if (straightLeft) {
      drawList->AddLine(ImVec2(topLeft.x, topLeft.y), ImVec2(topLeft.x, bottomRight.y), accent,
          3.0F);
    } else {
      drawList->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x - skew, bottomRight.y),
          accent, 3.0F);
    }
  }
  if (texture != 0) {
    const float margin = size.y * 0.24F;
    const float available = std::min(size.x - skew, size.y) - margin * 2.0F;
    const ImVec2 imageSize(available, available);
    const ImVec2 imageMin((topLeft.x + bottomRight.x) * 0.5F - imageSize.x * 0.5F,
        (topLeft.y + bottomRight.y) * 0.5F - imageSize.y * 0.5F);
    ImGui::SetCursorScreenPos(imageMin);
    ImGui::Image(static_cast<ImTextureID>(texture), imageSize);
  }
  if (tooltip != nullptr && hovered) ImGui::SetTooltip("%s", tooltip);
  return pressed;
}

void beginFullscreenWindow(const char* id) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
  ImGui::Begin(id, nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
          | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
}

void endFullscreenWindow() {
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

} // namespace rechigros::app::ui
