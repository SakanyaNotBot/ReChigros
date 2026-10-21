#pragma once

#include "rechigros/app/AppContext.hpp"

#include "imgui.h"

#include <cstdint>
#include <string>

namespace rechigros::app {

struct ScreenSize {
  float width{};
  float height{};
};

[[nodiscard]] ScreenSize viewportSize();
// Uniform scale for the 731x415 design space; clamped so tiny and huge windows
// both stay usable.
[[nodiscard]] float layoutScale();
[[nodiscard]] ImVec2 designPosition(ImVec2 size, float scale, float x, float y);

// Texture for an asset stored below `assets/`, e.g. "res/start.png".
[[nodiscard]] std::uint64_t assetTexture(AppContext& context, const std::string& relativePath,
    int maxSize = 0);
[[nodiscard]] std::uint64_t coverTexture(AppContext& context, const ChartRecord& record,
    int maxSize);
[[nodiscard]] const char* gradeAssetName(int gradeIndex);
[[nodiscard]] ImU32 gradeColor(int gradeIndex);
void drawGradeImage(AppContext& context, ImDrawList* drawList, ImVec2 center, float size,
    int gradeIndex, float alpha = 1.0F);

void drawTextRight(ImDrawList* drawList, ImVec2 rightCenter, float size, ImU32 color,
    const char* text);
void drawTextLeftCentered(ImDrawList* drawList, ImVec2 leftCenter, float size, ImU32 color,
    const char* text);
[[nodiscard]] std::string formatPlayTime(double seconds);

// Draws the standard top-left back control and reports a click.
bool backButton(AppContext& context, const char* id, float scale);

} // namespace rechigros::app
