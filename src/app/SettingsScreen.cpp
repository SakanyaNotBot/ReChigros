#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiWidgets.hpp"
#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

constexpr int kSettingsTabCount = 4;
constexpr const char* kSettingsTabs[kSettingsTabCount] = {"视频", "音频", "回放", "更多"};
// Layout metrics in design units.
constexpr float kBackWidth = 78.0F;
constexpr float kBackHeight = 48.0F;
constexpr float kBackTop = 8.0F;
constexpr float kTabsLeft = 96.0F;
constexpr float kTabsTop = 11.0F;
constexpr float kTabsHeight = 42.0F;
constexpr float kScrollTop = 90.0F;
constexpr float kContentTop = 20.0F;
constexpr float kContentBottom = 36.0F;
constexpr float kSectionGap = 24.0F;
constexpr float kSectionHeadingHeight = 34.0F;
constexpr float kSliderRowHeight = 92.0F;
constexpr float kSwitchRowHeight = 68.0F;
constexpr float kActionRowHeight = 76.0F;
constexpr float kToggleWidth = 80.0F;
constexpr float kToggleHeight = 56.0F;
constexpr float kSmallButtonWidth = 92.0F;
constexpr float kSmallButtonHeight = 48.0F;
constexpr float kValueWidth = 128.0F;
constexpr double kTabAnimationSeconds = 0.34;

void markChanged(AppContext& context) {
  context.settingsDirty = true;
}

void applyVolumes(AppContext& context) {
  if (context.audio == nullptr) return;
  context.audio->setMusicVolume(context.settings.musicVolume);
  context.audio->setSoundEffectVolume(context.settings.soundEffectVolume);
}

// Tab plate animation, so switching tabs slides the bright slice.
struct TabAnimation {
  int from{};
  int to{};
  double startSec{-1.0};

  void begin(int target, double nowSec) {
    from = to;
    to = target;
    startSec = nowSec;
  }

  [[nodiscard]] float progress(double nowSec) const {
    if (startSec < 0.0 || from == to) return 1.0F;
    const float t = static_cast<float>((nowSec - startSec) / kTabAnimationSeconds);
    if (t >= 1.0F) return 1.0F;
    const float clamped = std::max(0.0F, t);
    return 1.0F - std::pow(1.0F - clamped, 3.0F);
  }
};

TabAnimation g_tabAnimation;

std::vector<std::filesystem::path> listSkinDirectories(AppContext& context) {
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

std::string currentSkinName(AppContext& context) {
  if (context.settings.selectedSkin.empty()) return "内置皮肤";
  return context.settings.selectedSkin;
}

// Places the next row on the slanted content band. Each row is shifted by
// `base - slope * y`, so the left edge follows the backdrop's 15 degree edge; deriving
// it from the row's own screen position keeps it correct while scrolling.
struct RowLayout {
  float left{};
  float width{};
};

RowLayout placeRow(float scale, ImVec2 viewport) {
  const float slope = ui::kSkewRatio;
  const float base = 48.0F * scale + slope * viewport.y;
  const ImVec2 position = ImGui::GetCursorScreenPos();
  RowLayout layout;
  layout.left = base - slope * position.y;
  layout.width = std::max(1.0F, viewport.x - 96.0F * scale - slope * viewport.y);
  ImGui::SetCursorScreenPos(ImVec2(layout.left, position.y));
  return layout;
}

// Section heading, placed on the same slanted edge as the rows beneath it.
void sectionHeading(const char* title, float scale, ImVec2 viewport) {
  ImGui::Dummy(ImVec2(1.0F, 8.0F * scale));
  placeRow(scale, viewport);
  ui::drawText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 14.0F * scale,
      ui::rgba(ui::kSectionHeading), title);
  ImGui::Dummy(ImVec2(1.0F, kSectionHeadingHeight * scale));
}

// Horizontal slider with a filled track and round thumb. Returns true while dragging.
bool sliderControl(const char* id, ImVec2 topLeft, float width, float height, float from, float to,
    float* value, float scale) {
  ImGui::SetCursorScreenPos(topLeft);
  ImGui::InvisibleButton(id, ImVec2(width, height));
  const bool active = ImGui::IsItemActive();
  const bool hovered = ImGui::IsItemHovered();
  if (active || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
    const float local = std::clamp((ImGui::GetIO().MousePos.x - topLeft.x) / std::max(1.0F, width),
        0.0F, 1.0F);
    *value = from + (to - from) * local;
  }
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float trackY = topLeft.y + height * 0.5F;
  const float trackHeight = 4.0F * scale;
  const float inset = 8.0F * scale;
  const float trackLeft = topLeft.x + inset;
  const float trackRight = topLeft.x + width - inset;
  drawList->AddRectFilled(ImVec2(trackLeft, trackY - trackHeight * 0.5F),
      ImVec2(trackRight, trackY + trackHeight * 0.5F), ui::rgba(ui::kPanelBorderStrong),
      trackHeight * 0.5F);
  const float span = to - from;
  const float fraction = span != 0.0F ? std::clamp((*value - from) / span, 0.0F, 1.0F) : 0.0F;
  const float thumbX = trackLeft + (trackRight - trackLeft) * fraction;
  drawList->AddRectFilled(ImVec2(trackLeft, trackY - trackHeight * 0.5F),
      ImVec2(thumbX, trackY + trackHeight * 0.5F), ui::rgba(ui::kAccent), trackHeight * 0.5F);
  drawList->AddCircleFilled(ImVec2(thumbX, trackY), (active ? 10.0F : 8.5F) * scale,
      ui::rgba(active || hovered ? ui::kTextPrimary : 0xFFE8EAEEU), 0);
  return active;
}

// Rounded pill switch used by the boolean rows.
bool switchControl(const char* id, ImVec2 topLeft, float width, float height, bool* value) {
  ImGui::SetCursorScreenPos(topLeft);
  ImGui::InvisibleButton(id, ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked()) *value = !*value;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float inset = 6.0F * height / kToggleHeight;
  const ImVec2 trackMin(topLeft.x, topLeft.y + inset);
  const ImVec2 trackMax(topLeft.x + width, topLeft.y + height - inset);
  const float radius = (trackMax.y - trackMin.y) * 0.5F;
  const ImU32 track = *value ? ui::rgba(ui::kAccent)
                             : (hovered ? ui::rgba(ui::kPanelPressed) : ui::rgba(ui::kChip));
  drawList->AddRectFilled(trackMin, trackMax, track, radius);
  const float knobRadius = radius * 0.78F;
  const float knobX = *value ? trackMax.x - radius : trackMin.x + radius;
  drawList->AddCircleFilled(ImVec2(knobX, (trackMin.y + trackMax.y) * 0.5F), knobRadius,
      ui::rgba(*value ? 0xFF0F141BU : ui::kTextPrimary), 0);
  return true;
}

// Small bright button used by the action rows.
bool smallButton(const char* id, const char* label, ImVec2 topLeft, float scale) {
  const ImVec2 size(kSmallButtonWidth * scale, kSmallButtonHeight * scale);
  ImGui::SetCursorScreenPos(topLeft);
  const bool pressed = ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
  const float skew = size.y * ui::kSkewRatio;
  ui::skewQuad(drawList, topLeft.x + skew, topLeft.y, bottomRight.x, bottomRight.y, skew,
      ui::rgba(hovered ? 0xFFFFFFFFU : 0xF2FFFFFFU));
  ui::drawTextCentered(drawList, ImVec2(topLeft.x + size.x * 0.5F - skew * 0.5F,
                                topLeft.y + size.y * 0.5F),
      16.0F * scale, IM_COL32(16, 21, 27, 255), label);
  return pressed;
}

} // namespace

void drawSettingsScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##settings");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  // The content sits on a full-height plate whose left edge follows the same slant as
  // the rows, so text never floats over the backdrop art.
  {
    const float skew = ui::kSkewRatio * size.height;
    const ImU32 fill = ui::rgba(ui::kSettingsBackdrop);
    ImVec2 points[4] = {ImVec2(skew, 0.0F), ImVec2(size.width, 0.0F),
        ImVec2(size.width - skew, size.height), ImVec2(0.0F, size.height)};
    drawList->AddConvexPolyFilled(points, 4, fill);
  }

  if (ui::cornerButton("##settings_back", ImVec2(0.0F, kBackTop * scale),
          ImVec2(kBackWidth * scale, kBackHeight * scale), true,
          assetTexture(context, "res/return.png", 96), nullptr, 20.0F * scale, "返回")) {
    const Screen target = context.settingsReturn;
    commitSettings(context);
    navigateTo(context, target == Screen::Game ? Screen::Home : target);
    ui::endFullscreenWindow();
    return;
  }

  // Tab strip across the top.
  context.settingsTab = std::clamp(context.settingsTab, 0, kSettingsTabCount - 1);
  {
    const float tabsWidth = std::clamp(size.width / std::max(0.01F, scale) - 112.0F, 200.0F, 440.0F)
        * scale;
    const int pressed = ui::tabStrip("##settings_tabs", ImVec2(kTabsLeft * scale, kTabsTop * scale),
        tabsWidth, kTabsHeight * scale, context.settingsTab, g_tabAnimation.from,
        g_tabAnimation.progress(context.nowSec), kSettingsTabs, kSettingsTabCount, 18.0F * scale);
    if (pressed >= 0 && pressed != context.settingsTab) {
      g_tabAnimation.begin(pressed, context.nowSec);
      context.settingsTab = pressed;
    }
  }

  // Scrolling body. Rows are placed on the slanted band by `placeRow`.
  ImGui::SetCursorScreenPos(ImVec2(0.0F, kScrollTop * scale));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
  ImGui::BeginChild("##settings_body", ImVec2(size.width, size.height - kScrollTop * scale), false,
      ImGuiWindowFlags_NoBackground);
  ImGui::Dummy(ImVec2(1.0F, kContentTop * scale));

  const auto drawSection = [&](const char* title) {
    sectionHeading(title, scale, ImVec2(size.width, size.height));
  };

  const auto drawSliderRow = [&](const char* id, const char* label, float* value, float from,
                                 float to, const char* format) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
    const float labelTop = start.y + 14.0F * scale;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(*value));
    ui::drawText(drawList, ImVec2(ImGui::GetCursorScreenPos().x, labelTop), 22.0F * scale,
        IM_COL32(244, 255, 255, 255), label);
    const ImVec2 valueExtent = ui::measureText(buffer, 15.0F * scale);
    ui::drawText(drawList, ImVec2(ImGui::GetCursorScreenPos().x + width - valueExtent.x, labelTop),
        15.0F * scale, IM_COL32(207, 255, 255, 255), buffer);
    if (sliderControl(id, ImVec2(ImGui::GetCursorScreenPos().x, start.y + kSliderRowHeight
                                 * scale - 56.0F * scale - 22.0F * scale),
            width, 56.0F * scale, from, to, value, scale)) {
      markChanged(context);
    }
    ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kSliderRowHeight * scale));
  };

  const auto drawSwitchRow = [&](const char* id, const char* label, bool* value) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
    const float rowTop = start.y;
    ui::drawText(drawList, ImVec2(ImGui::GetCursorScreenPos().x,
                            rowTop + (kSwitchRowHeight * scale - 20.0F * scale) * 0.5F),
        20.0F * scale, IM_COL32(244, 255, 255, 255), label);
    if (switchControl(id, ImVec2(ImGui::GetCursorScreenPos().x + width - kToggleWidth * scale,
                           rowTop + (kSwitchRowHeight * scale - kToggleHeight * scale) * 0.5F),
            kToggleWidth * scale, kToggleHeight * scale, value)) {
      markChanged(context);
    }
    ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kSwitchRowHeight * scale));
  };

  const auto drawActionRow = [&](const char* id, const char* label, const std::string& value,
                                 const char* action, bool* pressed) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
    const float left = ImGui::GetCursorScreenPos().x;
    ui::drawText(drawList, ImVec2(left, start.y + 8.0F * scale), 18.0F * scale,
        IM_COL32(239, 255, 255, 255), label);
    ui::drawClippedText(drawList, ImVec2(left, start.y + 36.0F * scale),
        ImVec2(left, start.y + 36.0F * scale),
        ImVec2(left + width - kSmallButtonWidth * scale - 12.0F * scale,
            start.y + (36.0F + 20.0F) * scale),
        15.0F * scale, IM_COL32(207, 255, 255, 255),
        ui::ellipsize(value, 15.0F * scale, width - kSmallButtonWidth * scale - 12.0F * scale));
    *pressed = smallButton(id, action,
        ImVec2(left + width - kSmallButtonWidth * scale, start.y + 26.0F * scale), scale);
    ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kActionRowHeight * scale));
  };

  const auto drawDualActionRow = [&](const char* leftId, const char* rightId, const char* label,
                                     const char* leftLabel, const char* rightLabel,
                                     bool* leftPressed, bool* rightPressed) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
    const float left = ImGui::GetCursorScreenPos().x;
    ui::drawText(drawList, ImVec2(left, start.y + 8.0F * scale), 18.0F * scale,
        IM_COL32(239, 255, 255, 255), label);
    const float halfWidth = (width - 12.0F * scale) * 0.5F;
    *leftPressed = smallButton(leftId, leftLabel, ImVec2(left, start.y + 30.0F * scale), scale);
    *rightPressed = smallButton(rightId, rightLabel,
        ImVec2(left + halfWidth + 12.0F * scale, start.y + 30.0F * scale), scale);
    ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kActionRowHeight * scale));
  };

  switch (context.settingsTab) {
    case 0: {
      drawSection("画面");
      drawSliderRow("##key_scale", "按键缩放", &context.settings.keyScale, 0.7F, 1.5F, "%.2f");
      float brightness = 1.0F - context.settings.backgroundDim;
      drawSliderRow("##brightness", "背景亮度", &brightness, 0.0F, 1.0F, "%.0f%%");
      context.settings.backgroundDim = 1.0F - brightness;
      drawSliderRow("##scroll_speed", "谱面流速", &context.settings.scrollSpeed, 0.1F, 5.0F,
          "%.1f");
      drawSliderRow("##judge_width", "判定线宽度", &context.settings.judgeLineWidth, 0.5F, 2.0F,
          "%.2f");

      drawSection("游玩显示");
      drawSwitchRow("##mirror", "镜像演示 (X 轴)", &context.settings.mirrorX);
      drawSwitchRow("##autoplay", "自动演示", &context.settings.autoplay);
      drawSwitchRow("##challenge", "挑战模式", &context.settings.challengeMode);
      drawSwitchRow("##lowres", "低分辨率模式", &context.settings.lowResolutionMode);
      drawSwitchRow("##aa", "抗锯齿", &context.settings.antialias);
      drawSwitchRow("##fps", "显示 FPS", &context.settings.showFps);
      drawSwitchRow("##rtacc", "ACC 实时显示", &context.settings.realtimeAccuracy);
      drawSwitchRow("##scorejump", "分数跳动效果", &context.settings.scoreJumpEffect);
      drawSwitchRow("##noshader", "禁用谱面 Shader", &context.settings.disableChartShader);
      drawSwitchRow("##debug", "显示调试信息", &context.settings.showDebugInfo);
      drawSwitchRow("##reveal", "谱面揭秘", &context.settings.chartReveal);
      drawSwitchRow("##multi", "多押高亮提示", &context.settings.multiPressHighlight);
      drawSwitchRow("##apfc", "AP / FC 指示器", &context.settings.apfcIndicator);
      drawSwitchRow("##judgedist", "判定详情分布", &context.settings.judgementDetailDistribution);
      drawSwitchRow("##story", "剧情模式", &context.settings.storyMode);
      {
        const char* modes[] = {"禁用", "扇形", "横线"};
        bool pressed = false;
        drawActionRow("##hitoffset", "判定偏移指示器",
            modes[std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2)], "切换", &pressed);
        if (pressed) {
          context.settings.hitOffsetIndicatorMode =
              (std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2) + 1) % 3;
          markChanged(context);
        }
      }

      drawSection("皮肤");
      bool selectPressed = false;
      drawActionRow("##skin_select", "当前皮肤", currentSkinName(context), "选择", &selectPressed);
      if (selectPressed) ImGui::OpenPopup("##skin_popup");
      bool importPressed = false;
      bool deletePressed = false;
      drawDualActionRow("##skin_import", "##skin_delete", "皮肤资源", "导入", "删除",
          &importPressed, &deletePressed);
      if (importPressed) ImGui::OpenPopup("##skin_import_popup");
      if (deletePressed) {
        if (context.settings.selectedSkin.empty()) {
          notify(context, "内置皮肤不可删除");
        } else {
          platform::removeRecursively(context.dataRoot / "skins" / context.settings.selectedSkin);
          context.settings.selectedSkin.clear();
          markChanged(context);
        }
      }
      break;
    }
    case 1: {
      drawSection("音频");
      drawSliderRow("##music_volume", "音乐音量", &context.settings.musicVolume, 0.0F, 5.0F,
          "%.0f%%");
      drawSliderRow("##sfx_volume", "打击音效音量", &context.settings.soundEffectVolume, 0.0F, 5.0F,
          "%.0f%%");
      drawSliderRow("##music_speed", "音乐倍速", &context.settings.musicSpeed, 0.5F, 2.0F, "%.2f");
      {
        float offset = static_cast<float>(context.settings.audioOffsetMs);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
        const float labelTop = start.y + 14.0F * scale;
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%d ms", context.settings.audioOffsetMs);
        ui::drawText(drawList, ImVec2(ImGui::GetCursorScreenPos().x, labelTop), 22.0F * scale,
            IM_COL32(244, 255, 255, 255), "延迟校准");
        const ImVec2 extent = ui::measureText(buffer, 15.0F * scale);
        ui::drawText(drawList, ImVec2(ImGui::GetCursorScreenPos().x + width - extent.x, labelTop),
            15.0F * scale, IM_COL32(207, 255, 255, 255), buffer);
        if (sliderControl("##audio_offset", ImVec2(ImGui::GetCursorScreenPos().x,
                              start.y + kSliderRowHeight * scale - 56.0F * scale - 22.0F * scale),
                width, 56.0F * scale, -500.0F, 500.0F, &offset, scale)) {
          context.settings.audioOffsetMs = static_cast<int>(std::lround(offset));
          markChanged(context);
        }
        ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kSliderRowHeight * scale));
      }
      {
        char value[48];
        std::snprintf(value, sizeof(value), "%d ms", context.settings.audioOffsetMs);
        bool pressed = false;
        drawActionRow("##calibrate", "校准工具", value, "开始", &pressed);
        if (pressed) {
          context.calibration.offsetMs = static_cast<float>(context.settings.audioOffsetMs);
          context.calibration.audioLoaded = false;
          context.calibration.markers.clear();
          context.settingsReturn = Screen::Settings;
          context.screen = Screen::Calibration;
        }
      }
      break;
    }
    case 2: {
      drawSection("回放");
      drawSwitchRow("##replay_enabled", "启用回放记录", &context.settings.replayEnabled);
      bool importPressed = false;
      bool refreshPressed = false;
      drawDualActionRow("##replay_import", "##replay_refresh", "回放文件", "导入", "刷新",
          &importPressed, &refreshPressed);
      if (importPressed) ImGui::OpenPopup("##replay_import_popup");

      const auto replays = context.replays.list();
      if (replays.empty()) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        placeRow(scale, ImVec2(size.width, size.height));
        ui::drawTextCentered(drawList, ImVec2(size.width * 0.5F, start.y + 26.0F * scale),
            16.0F * scale, IM_COL32(184, 255, 255, 255), "暂无回放记录");
        ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + 60.0F * scale));
      } else {
        for (const auto& replay : replays) {
          const ImVec2 start = ImGui::GetCursorScreenPos();
          const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
          const float left = ImGui::GetCursorScreenPos().x;
          const std::string title = replay.meta.songName + "  " + replay.meta.difficulty;
          ui::drawClippedText(drawList, ImVec2(left, start.y + 10.0F * scale),
              ImVec2(left, start.y), ImVec2(left + width, start.y + 34.0F * scale),
              17.0F * scale, IM_COL32(255, 255, 255, 255),
              ui::ellipsize(title, 17.0F * scale, width));
          const std::string score = formatScore(replay.meta.score);
          ui::drawText(drawList, ImVec2(left, start.y + 40.0F * scale), 15.0F * scale,
              IM_COL32(223, 255, 255, 255), score.c_str());
          const float buttonWidth = 76.0F * scale;
          const float buttonHeight = kSmallButtonHeight * scale;
          const std::string playId = "##play_" + replay.id;
          const std::string deleteId = "##delete_" + replay.id;
          const float buttonsLeft = left + width - buttonWidth * 2.0F - 8.0F * scale;
          if (smallButton(playId.c_str(), "播放",
                  ImVec2(buttonsLeft, start.y + 22.0F * scale), scale)) {
            startSavedReplay(context, replay.id);
          }
          if (smallButton(deleteId.c_str(), "删除",
                  ImVec2(buttonsLeft + buttonWidth + 8.0F * scale, start.y + 22.0F * scale),
                  scale)) {
            context.replays.remove(replay.id);
          }
          (void)buttonHeight;
          ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + kActionRowHeight * scale));
        }
      }
      break;
    }
    default: {
      drawSection("更多");
#ifdef RECHIGROS_VERSION
      const std::string version = std::string("版本: ReChigros ") + RECHIGROS_VERSION;
#else
      const std::string version = "版本: ReChigros";
#endif
      const auto drawInfoRow = [&](const char* label, const std::string& value) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = placeRow(scale, ImVec2(size.width, size.height)).width;
        const float left = ImGui::GetCursorScreenPos().x;
        ui::drawText(drawList, ImVec2(left, start.y), 18.0F * scale,
            IM_COL32(239, 255, 255, 255), label);
        ui::drawClippedText(drawList, ImVec2(left, start.y + 24.0F * scale),
            ImVec2(left, start.y + 24.0F * scale),
            ImVec2(left + width, start.y + 52.0F * scale), 15.0F * scale,
            IM_COL32(207, 255, 255, 255), ui::ellipsize(value, 15.0F * scale, width));
        ImGui::SetCursorScreenPos(ImVec2(0.0F, start.y + 56.0F * scale));
      };
      drawInfoRow("版本", version);
      drawInfoRow("数据目录", context.dataRoot.string());
      drawInfoRow("谱面数量", std::to_string(context.library.records().size()));
      drawInfoRow("回放数量", std::to_string(context.replays.list().size()));
      bool resetPressed = false;
      bool quitPressed = false;
      drawDualActionRow("##reset", "##quit", "操作", "恢复默认", "退出应用", &resetPressed,
          &quitPressed);
      if (resetPressed) {
        context.settings = Settings{};
        context.settingsDirty = true;
        applyVolumes(context);
        notify(context, "设置已恢复默认");
      }
      if (quitPressed) {
        commitSettings(context);
        context.quitRequested = true;
      }
      break;
    }
  }

  ImGui::Dummy(ImVec2(1.0F, kContentBottom * scale));
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // Skin selection dialog.
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.45F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##skin_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    if (ImGui::Button("内置皮肤")) {
      context.settings.selectedSkin.clear();
      markChanged(context);
      ImGui::CloseCurrentPopup();
    }
    for (const auto& skin : listSkinDirectories(context)) {
      if (ImGui::Button(skin.filename().string().c_str())) {
        context.settings.selectedSkin = skin.filename().string();
        markChanged(context);
        ImGui::CloseCurrentPopup();
      }
    }
    if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // Skin import dialog.
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.45F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##skin_import_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    static char path[512] = {};
    ImGui::TextUnformatted("皮肤目录路径");
    ImGui::SetNextItemWidth(420.0F * scale);
    ImGui::InputText("##skin_path", path, sizeof(path));
    if (ImGui::Button("导入")) {
      const std::filesystem::path source = path;
      std::error_code error;
      if (!std::filesystem::is_directory(source, error)) {
        notify(context, "皮肤目录不存在");
      } else {
        const auto destination = context.dataRoot / "skins" / source.filename();
        platform::removeRecursively(destination);
        std::filesystem::create_directories(destination.parent_path(), error);
        std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive,
            error);
        if (error) {
          notify(context, "皮肤导入失败");
        } else {
          context.settings.selectedSkin = source.filename().string();
          markChanged(context);
          notify(context, "皮肤已导入");
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // Replay import dialog.
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height * 0.45F), ImGuiCond_Appearing,
      ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##replay_import_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::SetWindowFontScale(scale);
    static char path[512] = {};
    ImGui::TextUnformatted("回放文件路径 (replay.json)");
    ImGui::SetNextItemWidth(420.0F * scale);
    ImGui::InputText("##replay_path", path, sizeof(path));
    if (ImGui::Button("导入")) {
      const auto data = readReplayFile(path);
      if (!data) {
        notify(context, "回放文件无法读取");
      } else {
        const std::string id = context.replays.save(*data);
        notify(context, id.empty() ? "回放导入失败" : "回放已导入");
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
