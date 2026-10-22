#include "rechigros/app/ScreenCommon.hpp"

#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rechigros::app {
namespace {

constexpr const char* kGradeAssets[] = {"F", "C", "B", "A", "S", "V", "FC", "AP"};

std::uint64_t textureForPath(AppContext& context, const std::filesystem::path& path,
    int maxSize) {
  if (path.empty()) return 0;
  return context.textures.textureFor(path, maxSize);
}

} // namespace

ScreenSize viewportSize() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  return ScreenSize{viewport->WorkSize.x, viewport->WorkSize.y};
}

float layoutScale() {
  const auto size = viewportSize();
  if (size.width <= 1.0F || size.height <= 1.0F) return 1.0F;
  const float scale = std::min(size.width / 731.0F, size.height / 415.0F);
  return std::clamp(scale, 0.55F, 2.4F);
}

ImVec2 designPosition(ImVec2 size, float scale, float x, float y) {
  const float offsetX = size.x * 0.5F - 731.0F * scale * 0.5F;
  const float offsetY = size.y * 0.5F - 415.0F * scale * 0.5F;
  return ImVec2(offsetX + x * scale, offsetY + y * scale);
}

std::uint64_t assetTexture(AppContext& context, const std::string& relativePath, int maxSize) {
  return textureForPath(context, platform::assetRoot() / relativePath, maxSize);
}

std::uint64_t coverTexture(AppContext& context, const ChartRecord& record, int maxSize) {
  return textureForPath(context, record.bgFile, maxSize);
}

const char* gradeAssetName(int gradeIndex) {
  return kGradeAssets[std::clamp(gradeIndex, 0, 7)];
}

ImU32 gradeColor(int gradeIndex) {
  switch (std::clamp(gradeIndex, 0, 7)) {
    case 7: return ui::rgba(ui::kGradePerfect);
    case 6: return ui::rgba(ui::kGradeCombo);
    case 5: return ui::rgba(0xFFFFD86BU);
    default: return ui::rgba(0xFFD9E2ECU);
  }
}

void drawGradeImage(AppContext& context, ImDrawList* drawList, ImVec2 center, float size,
    int gradeIndex, float alpha) {
  if (drawList == nullptr || size <= 0.0F) return;
  const std::string asset = std::string("grades/") + gradeAssetName(gradeIndex) + ".png";
  const std::uint64_t texture = assetTexture(context, asset, 128);
  const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
  if (texture != 0) {
    drawList->AddImage(static_cast<ImTextureID>(texture),
        ImVec2(center.x - size * 0.5F, center.y - size * 0.5F),
        ImVec2(center.x + size * 0.5F, center.y + size * 0.5F), ImVec2(0.0F, 0.0F),
        ImVec2(1.0F, 1.0F), tint);
    return;
  }
  // Fallback while the artwork is still loading or absent.
  ui::drawTextCentered(drawList, center, size * 0.55F, gradeColor(gradeIndex),
      gradeAssetName(gradeIndex));
}

void drawTextRight(ImDrawList* drawList, ImVec2 rightCenter, float size, ImU32 color,
    const char* text) {
  const ImVec2 extent = ui::measureText(text, size);
  ui::drawText(drawList, ImVec2(rightCenter.x - extent.x, rightCenter.y - extent.y * 0.5F), size,
      color, text);
}

void drawTextLeftCentered(ImDrawList* drawList, ImVec2 leftCenter, float size, ImU32 color,
    const char* text) {
  const ImVec2 extent = ui::measureText(text, size);
  ui::drawText(drawList, ImVec2(leftCenter.x, leftCenter.y - extent.y * 0.5F), size, color, text);
}

std::string formatPlayTime(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
  const int total = static_cast<int>(seconds);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
  return buffer;
}

bool backButton(AppContext& context, const char* id, float scale) {
  (void)context;
  const ImVec2 position(0.0F, 8.0F * scale);
  const std::uint64_t texture = assetTexture(context, "res/return.png", 96);
  return ui::iconButton(id, texture, position, ImVec2(78.0F * scale, 48.0F * scale), "返回", true,
      false, true);
}

} // namespace rechigros::app
