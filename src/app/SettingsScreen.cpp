#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace rechigros::app {
namespace {

constexpr int kSettingsTabCount = 4;
constexpr const char* kSettingsTabs[kSettingsTabCount] = {"视频", "音频", "回放", "更多"};

void markChanged(AppContext& context) {
  context.settingsDirty = true;
}

void applyVolumes(AppContext& context) {
  if (context.audio == nullptr) return;
  context.audio->setMusicVolume(context.settings.musicVolume);
  context.audio->setSoundEffectVolume(context.settings.soundEffectVolume);
}

void sliderRow(const char* label, float* value, float minimum, float maximum, const char* format,
    AppContext& context) {
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.86F);
  if (ImGui::SliderFloat((std::string("##") + label).c_str(), value, minimum, maximum, format)) {
    markChanged(context);
  }
}

void toggleRow(const char* label, bool* value, AppContext& context) {
  if (ImGui::Checkbox((std::string("##") + label).c_str(), value)) markChanged(context);
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
}

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

void buildVideoTab(AppContext& context) {
  ImGui::TextDisabled("画面");
  sliderRow("按键缩放", &context.settings.keyScale, 0.7F, 1.5F, "%.2f", context);
  float brightness = 1.0F - context.settings.backgroundDim;
  if (ImGui::SliderFloat("##背景亮度", &brightness, 0.0F, 1.0F, "%.0f%%")) {
    context.settings.backgroundDim = 1.0F - brightness;
    markChanged(context);
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("背景亮度");
  sliderRow("谱面流速", &context.settings.scrollSpeed, 0.1F, 5.0F, "%.1f", context);
  sliderRow("判定线宽度", &context.settings.judgeLineWidth, 0.5F, 2.0F, "%.2f", context);

  ImGui::Spacing();
  ImGui::TextDisabled("游玩显示");
  toggleRow("镜像演示 (X 轴)", &context.settings.mirrorX, context);
  toggleRow("自动演示", &context.settings.autoplay, context);
  toggleRow("挑战模式", &context.settings.challengeMode, context);
  toggleRow("低分辨率模式", &context.settings.lowResolutionMode, context);
  toggleRow("抗锯齿", &context.settings.antialias, context);
  toggleRow("显示 FPS", &context.settings.showFps, context);
  toggleRow("ACC 实时显示", &context.settings.realtimeAccuracy, context);
  toggleRow("分数跳动效果", &context.settings.scoreJumpEffect, context);
  toggleRow("禁用谱面 Shader", &context.settings.disableChartShader, context);
  toggleRow("显示调试信息", &context.settings.showDebugInfo, context);
  toggleRow("谱面揭秘", &context.settings.chartReveal, context);
  toggleRow("多押高亮提示", &context.settings.multiPressHighlight, context);
  toggleRow("AP / FC 指示器", &context.settings.apfcIndicator, context);
  toggleRow("判定详情分布", &context.settings.judgementDetailDistribution, context);
  toggleRow("剧情模式", &context.settings.storyMode, context);

  const char* modeNames[] = {"禁用", "扇形", "横线"};
  const int mode = std::clamp(context.settings.hitOffsetIndicatorMode, 0, 2);
  ImGui::Text("判定偏移指示器");
  ImGui::SameLine();
  if (ImGui::Button(modeNames[mode])) {
    context.settings.hitOffsetIndicatorMode = (mode + 1) % 3;
    markChanged(context);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("皮肤");
  ImGui::Text("当前皮肤: %s", currentSkinName(context).c_str());
  if (ImGui::Button("选择皮肤")) ImGui::OpenPopup("##skin_popup");
  ImGui::SameLine();
  if (ImGui::Button("导入皮肤")) ImGui::OpenPopup("##skin_import_popup");
  ImGui::SameLine();
  if (ImGui::Button("删除皮肤")) {
    if (context.settings.selectedSkin.empty()) {
      notify(context, "内置皮肤不可删除");
    } else {
      platform::removeRecursively(context.dataRoot / "skins" / context.settings.selectedSkin);
      context.settings.selectedSkin.clear();
      markChanged(context);
    }
  }

  ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.5F,
                             ImGui::GetMainViewport()->WorkSize.y * 0.45F),
      ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
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

  ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.5F,
                             ImGui::GetMainViewport()->WorkSize.y * 0.45F),
      ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##skin_import_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    static char path[512] = {};
    ImGui::TextUnformatted("皮肤目录路径");
    ImGui::SetNextItemWidth(420.0F);
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
        std::filesystem::copy(source, destination,
            std::filesystem::copy_options::recursive, error);
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
}

void buildAudioTab(AppContext& context) {
  ImGui::TextDisabled("音频");
  ImGui::Text("音乐音量");
  int musicPercent = static_cast<int>(context.settings.musicVolume * 100.0F);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.86F);
  if (ImGui::SliderInt("##music_volume", &musicPercent, 0, 500, "%d%%")) {
    context.settings.musicVolume = static_cast<float>(musicPercent) / 100.0F;
    applyVolumes(context);
    markChanged(context);
  }
  ImGui::Text("打击音效音量");
  int sfxPercent = static_cast<int>(context.settings.soundEffectVolume * 100.0F);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.86F);
  if (ImGui::SliderInt("##sfx_volume", &sfxPercent, 0, 500, "%d%%")) {
    context.settings.soundEffectVolume = static_cast<float>(sfxPercent) / 100.0F;
    applyVolumes(context);
    markChanged(context);
  }
  sliderRow("音乐倍速", &context.settings.musicSpeed, 0.5F, 2.0F, "%.2f", context);
  ImGui::Text("延迟校准");
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.86F);
  if (ImGui::SliderInt("##audio_offset", &context.settings.audioOffsetMs, -500, 500, "%d ms")) {
    markChanged(context);
  }
  if (ImGui::Button("校准工具")) {
    context.calibration.offsetMs = static_cast<float>(context.settings.audioOffsetMs);
    context.calibration.audioLoaded = false;
    context.calibration.markers.clear();
    context.settingsReturn = Screen::Settings;
    context.screen = Screen::Calibration;
  }
}

void buildReplayTab(AppContext& context) {
  ImGui::TextDisabled("回放");
  toggleRow("启用回放记录", &context.settings.replayEnabled, context);
  if (ImGui::Button("刷新列表")) {
    // The list is rebuilt every frame; this is only a visual affordance.
  }
  ImGui::SameLine();
  if (ImGui::Button("导入回放")) ImGui::OpenPopup("##replay_import_popup");

  const auto replays = context.replays.list();
  if (replays.empty()) {
    ImGui::TextDisabled("暂无回放记录");
  } else {
    for (const auto& replay : replays) {
      ImGui::Separator();
      ImGui::Text("%s  %s", replay.meta.songName.c_str(), replay.meta.difficulty.c_str());
      ImGui::TextDisabled("%s   %s", formatScore(replay.meta.score).c_str(),
          formatAccuracy(replay.meta.accuracy).c_str());
      ImGui::SameLine();
      const std::string playId = "播放##" + replay.id;
      if (ImGui::Button(playId.c_str())) {
        startSavedReplay(context, replay.id);
      }
      ImGui::SameLine();
      const std::string deleteId = "删除##" + replay.id;
      if (ImGui::Button(deleteId.c_str())) context.replays.remove(replay.id);
    }
  }

  ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkSize.x * 0.5F,
                             ImGui::GetMainViewport()->WorkSize.y * 0.45F),
      ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  if (ImGui::BeginPopupModal("##replay_import_popup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    static char path[512] = {};
    ImGui::TextUnformatted("回放文件路径 (replay.json)");
    ImGui::SetNextItemWidth(420.0F);
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
}

void buildMoreTab(AppContext& context) {
  ImGui::TextDisabled("更多");
#ifdef RECHIGROS_VERSION
  ImGui::Text("版本: ReChigros %s", RECHIGROS_VERSION);
#else
  ImGui::TextUnformatted("版本: ReChigros");
#endif
  ImGui::Text("数据目录: %s", context.dataRoot.string().c_str());
  ImGui::Text("谱面数量: %d", static_cast<int>(context.library.records().size()));
  ImGui::Text("回放数量: %d", static_cast<int>(context.replays.list().size()));
  ImGui::Spacing();
  if (ImGui::Button("恢复默认设置")) {
    context.settings = Settings{};
    context.settingsDirty = true;
    applyVolumes(context);
    notify(context, "设置已恢复默认");
  }
  if (ImGui::Button("退出应用")) {
    commitSettings(context);
    context.quitRequested = true;
  }
}

} // namespace

void drawSettingsScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##settings");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  if (backButton(context, "##settings_back", scale)) {
    const Screen target = context.settingsReturn;
    commitSettings(context);
    navigateTo(context, target == Screen::Game ? Screen::Home : target);
    ui::endFullscreenWindow();
    return;
  }

  context.settingsTab = std::clamp(context.settingsTab, 0, kSettingsTabCount - 1);
  for (int index = 0; index < kSettingsTabCount; ++index) {
    const bool selected = context.settingsTab == index;
    const ImVec2 topLeft((96.0F + static_cast<float>(index) * 108.0F) * scale, 11.0F * scale);
    const std::string id = "##settings_tab_" + std::to_string(index);
    if (ui::skewButton(id.c_str(), kSettingsTabs[index], topLeft,
            ImVec2(102.0F * scale, 42.0F * scale), 20.0F * scale, -1.0F,
            selected ? IM_COL32(215, 222, 232, 230) : ui::rgba(ui::kPanelFill),
            IM_COL32(255, 255, 255, 90), false, true)) {
      if (context.settingsTab != index) context.settingsTab = index;
    }
  }

  const ImVec2 contentTopLeft(96.0F * scale, 66.0F * scale);
  const ImVec2 contentSize(size.width - 200.0F * scale, size.height - 78.0F * scale);
  ImGui::SetCursorScreenPos(contentTopLeft);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05F, 0.07F, 0.09F, 0.72F));
  ImGui::BeginChild("##settings_content", ImVec2(contentSize.x, contentSize.y), true);
  ImGui::SetWindowFontScale(std::clamp(scale, 0.8F, 1.5F));
  ImGui::PushItemWidth(contentSize.x * 0.5F);
  switch (context.settingsTab) {
    case 0: buildVideoTab(context); break;
    case 1: buildAudioTab(context); break;
    case 2: buildReplayTab(context); break;
    default: buildMoreTab(context); break;
  }
  ImGui::PopItemWidth();
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
