#pragma once

#include "rechigros/app/UiStyle.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// Reusable interface pieces shared by the screens. Every metric is expressed in the
// reference design units so a layout keeps its proportions at any window size.
namespace rechigros::app::ui {

// Spacing tokens taken from the reference layout, in design units.
constexpr float kToolbarHeight = 56.0F;
constexpr float kTabBarHeight = 48.0F;
constexpr float kScreenMargin = 16.0F;
constexpr float kCardRadius = 8.0F;
constexpr float kTabBarWidth = 330.0F;
constexpr float kTabBarHeightShort = 42.0F;
constexpr float kTabBarTop = 11.0F;
constexpr float kSearchWidth = 172.0F;
constexpr float kSearchHeight = 36.0F;
constexpr float kCornerButtonWidth = 78.0F;
constexpr float kCornerButtonHeight = 48.0F;
// Aspect ratio of every list thumbnail; the reference draws covers 16:9.
constexpr float kCoverAspect = 16.0F / 9.0F;
// Design canvas used by the list screens, matching the reference's 16:9 dp space.
constexpr float kListCanvasWidth = 1097.0F;
constexpr float kListCanvasHeight = 617.0F;
// Design canvas used by the detail screen.
constexpr float kDetailCanvasWidth = 731.0F;
constexpr float kDetailCanvasHeight = 415.0F;

// Uniform scale plus centring offset that maps a design canvas onto the viewport.
struct DesignCanvas {
  float scale{1.0F};
  ImVec2 origin{};
};

[[nodiscard]] inline DesignCanvas designCanvas(ImVec2 size, float designWidth,
    float designHeight) {
  DesignCanvas canvas;
  if (size.x <= 1.0F || size.y <= 1.0F) return canvas;
  canvas.scale = std::min(size.x / designWidth, size.y / designHeight);
  canvas.origin = ImVec2(size.x * 0.5F - designWidth * canvas.scale * 0.5F,
      size.y * 0.5F - designHeight * canvas.scale * 0.5F);
  return canvas;
}

[[nodiscard]] inline ImVec2 canvasPoint(const DesignCanvas& canvas, float x, float y) {
  return ImVec2(canvas.origin.x + x * canvas.scale, canvas.origin.y + y * canvas.scale);
}

// Draws a parallelogram whose bottom edge is shifted left by `skew`, the shape the
// reference uses for its panels and tab plates.
inline void skewQuad(ImDrawList* drawList, float leftTop, float top, float rightTop, float bottom,
    float skew, ImU32 fill, ImU32 border = 0, float borderWidth = 0.0F) {
  if (drawList == nullptr || bottom <= top || rightTop <= leftTop) return;
  drawPanel(drawList, ImVec2(leftTop - skew, top), ImVec2(rightTop - skew, bottom), skew, fill,
      border, borderWidth);
}

// Rounded rectangle used by the search field and section cards.
inline void roundedRect(ImDrawList* drawList, ImVec2 topLeft, ImVec2 bottomRight, float radius,
    ImU32 fill, ImU32 border = 0, float borderWidth = 0.0F) {
  if (drawList == nullptr || bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) return;
  drawList->AddRectFilled(topLeft, bottomRight, fill, radius);
  if (borderWidth > 0.0F) drawList->AddRect(topLeft, bottomRight, border, radius, 0, borderWidth);
}

// Number of bytes forming the UTF-8 sequence starting at `index`.
[[nodiscard]] inline std::size_t utf8SequenceLength(const std::string& text, std::size_t index) {
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

// Truncates on a codepoint boundary so multi-byte text never splits mid-glyph.
[[nodiscard]] inline std::string ellipsize(const std::string& text, float size, float maxWidth) {
  if (text.empty() || maxWidth <= 0.0F) return {};
  if (measureText(text.c_str(), size).x <= maxWidth) return text;
  const std::string suffix = "...";
  const float suffixWidth = measureText(suffix.c_str(), size).x;
  std::string result;
  std::size_t index = 0;
  while (index < text.size()) {
    const std::size_t length = utf8SequenceLength(text, index);
    const std::string candidate = result + text.substr(index, length);
    if (measureText(candidate.c_str(), size).x + suffixWidth > maxWidth) break;
    result = candidate;
    index += length;
  }
  result += suffix;
  return result;
}

// Text clipped to a rectangle; used where a label must not spill out of its card.
inline void drawClippedText(ImDrawList* drawList, ImVec2 position, ImVec2 clipMin, ImVec2 clipMax,
    float size, ImU32 color, const std::string& text) {
  if (drawList == nullptr || text.empty()) return;
  drawList->PushClipRect(clipMin, clipMax, true);
  drawText(drawList, position, size, color, text.c_str());
  drawList->PopClipRect();
}

// Scrolling marquee for a single-line label that does not fit its box, mirroring the
// horizontal drift the reference applies to long chart names.
inline void drawMarqueeText(ImDrawList* drawList, ImVec2 position, float maxWidth, float size,
    ImU32 color, const std::string& text, double timeSec) {
  if (drawList == nullptr || text.empty() || maxWidth <= 0.0F) return;
  const float width = measureText(text.c_str(), size).x;
  ImVec2 cursor = position;
  if (width <= maxWidth) {
    drawText(drawList, cursor, size, color, text.c_str());
    return;
  }
  const float gap = size * 1.6F;
  const float span = width + gap;
  const float period = 7.0F;
  const float phase = static_cast<float>(std::fmod(timeSec, static_cast<double>(period)) / period);
  const float offset = phase * span;
  const ImVec2 clipMin(position.x, position.y - size * 0.2F);
  const ImVec2 clipMax(position.x + maxWidth, position.y + size * 1.4F);
  drawList->PushClipRect(clipMin, clipMax, true);
  drawText(drawList, ImVec2(position.x - offset, position.y), size, color, text.c_str());
  drawText(drawList, ImVec2(position.x - offset + span, position.y), size, color, text.c_str());
  drawList->PopClipRect();
}

// Segmented tab bar: one slanted backing plate with a brighter plate behind the
// selected slice. Returns the tab the user pressed, or -1.
inline int tabStrip(const char* id, ImVec2 topLeft, float width, float height, int selected,
    int previous, float progress, const char* const* labels, int count, float textSize) {
  if (count <= 0 || labels == nullptr) return -1;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float skew = std::tan(0.2617994F) * (height * 0.86F - height * 0.14F);
  const float barTop = height * 0.14F;
  const float barBottom = height * 0.86F;

  // Slice centres and widths within the bar. Three-tab strips use the reference's
  // uneven slices; other counts share the width evenly.
  const float threeCenters[3] = {0.22F, 0.50F, 0.78F};
  const float threeWidths[3] = {0.23F, 0.23F, 0.29F};
  const auto centerFor = [count, &threeCenters](int index) {
    if (count == 3) return threeCenters[std::clamp(index, 0, 2)];
    return 0.08F + (static_cast<float>(index) + 0.5F) * 0.84F / static_cast<float>(count);
  };
  const auto widthFor = [count, &threeWidths](int index) {
    if (count == 3) return threeWidths[std::clamp(index, 0, 2)];
    return 0.84F / static_cast<float>(count);
  };

  skewQuad(drawList, topLeft.x + width * 0.055F + skew, topLeft.y + barTop,
      topLeft.x + width * 0.945F, topLeft.y + barBottom, skew, rgba(kTabTrack));

  const float from = centerFor(std::clamp(previous, 0, count - 1));
  const float to = centerFor(std::clamp(selected, 0, count - 1));
  const float fromWidth = widthFor(std::clamp(previous, 0, count - 1));
  const float toWidth = widthFor(std::clamp(selected, 0, count - 1));
  const float center = from + (to - from) * progress;
  const float sliceWidth = fromWidth + (toWidth - fromWidth) * progress;
  skewQuad(drawList, topLeft.x + (center - sliceWidth * 0.5F) * width + skew, topLeft.y,
      topLeft.x + (center + sliceWidth * 0.5F) * width, topLeft.y + height, skew,
      rgba(kTabSelectedPlate));

  const ImVec2 clipMin(topLeft.x, topLeft.y);
  const ImVec2 clipMax(topLeft.x + width, topLeft.y + height);
  for (int index = 0; index < count; ++index) {
    float amount = 0.0F;
    if (progress >= 1.0F || previous == selected) {
      amount = index == selected ? 1.0F : 0.0F;
    } else if (index == previous) {
      amount = 1.0F - progress;
    } else if (index == selected) {
      amount = progress;
    }
    const float shade = amount;
    const int tone = static_cast<int>(std::lround(255.0F * (1.0F - shade)));
    const ImU32 color = IM_COL32(tone, tone, tone, 255);
    const float sliceCenter = topLeft.x + centerFor(index) * width;
    const ImVec2 extent = measureText(labels[index], textSize);
    drawClippedText(drawList, ImVec2(sliceCenter - extent.x * 0.5F,
                        topLeft.y + (height - textSize) * 0.5F - textSize * 0.12F),
        clipMin, clipMax, textSize, color, labels[index]);
  }

  // Hit areas are computed from the same slice geometry, so labels and taps agree.
  ImGui::SetCursorScreenPos(topLeft);
  ImGui::InvisibleButton(id, ImVec2(width, height));
  int pressed = -1;
  if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    const float local = (ImGui::GetIO().MousePos.x - topLeft.x) / std::max(1.0F, width);
    pressed = std::clamp(static_cast<int>(local * static_cast<float>(count)), 0, count - 1);
  }
  return pressed;
}

// Slanted corner control: a flat plate with a bright leading edge, carrying either an
// icon texture or a short text label. The plate is the shape the reference uses for
// every corner action.
inline bool cornerButton(const char* id, ImVec2 topLeft, ImVec2 size, bool leadingEdgeOnLeft,
    std::uint64_t texture, const char* label, float textSize, const char* tooltip = nullptr) {
  const float skew = std::tan(0.2617994F) * size.y;
  ImGui::SetCursorScreenPos(topLeft);
  const bool pressed = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 fill = hovered ? rgba(kIconButtonFillHover) : rgba(kIconButtonFill);
  const float leftTop = leadingEdgeOnLeft ? topLeft.x + skew : topLeft.x;
  const float rightTop = topLeft.x + size.x;
  skewQuad(drawList, leftTop, topLeft.y, rightTop, topLeft.y + size.y, skew, fill);
  const ImU32 edge = rgba(kCornerEdge);
  if (leadingEdgeOnLeft) {
    drawList->AddLine(ImVec2(leftTop, topLeft.y), ImVec2(leftTop - skew, topLeft.y + size.y), edge,
        3.2F);
  } else {
    drawList->AddLine(ImVec2(rightTop, topLeft.y), ImVec2(rightTop - skew, topLeft.y + size.y), edge,
        3.2F);
  }
  // Glyphs sit on the centroid of the visible plate, not on the bounding box.
  const float centerX = leftTop + (rightTop - leftTop) * 0.5F - skew * 0.5F;
  const float centerY = topLeft.y + size.y * 0.5F;
  if (texture != 0) {
    const float glyph = std::min(size.x - skew, size.y) * 0.52F;
    drawList->AddImage(static_cast<ImTextureID>(texture),
        ImVec2(centerX - glyph * 0.5F, centerY - glyph * 0.5F),
        ImVec2(centerX + glyph * 0.5F, centerY + glyph * 0.5F));
  } else if (label != nullptr) {
    drawTextCentered(drawList, ImVec2(centerX, centerY), textSize, rgba(kTextPrimary), label);
  }
  if (tooltip != nullptr && hovered) ImGui::SetTooltip("%s", tooltip);
  return pressed;
}

[[nodiscard]] inline float coverHeightFor(float cardWidth) {
  return cardWidth / kCoverAspect;
}

} // namespace rechigros::app::ui
