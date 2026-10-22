#include "rechigros/app/AppContext.hpp"

#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include <SDL.h>
#include <SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

namespace rechigros::app {
namespace {

constexpr double kToastLifetimeSec = 2.6;

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::filesystem::path findInfoFileIn(const std::filesystem::path& directory) {
  for (const auto& file : platform::listFiles(directory)) {
    const std::string name = toLower(file.filename().string());
    if (name == "info.json" || name == "info.yml" || name == "info.yaml" || name == "info.txt"
        || name == "info.csv") {
      return file;
    }
  }
  return {};
}

std::filesystem::path resolveInside(const std::filesystem::path& directory,
    const std::string& relativePath) {
  if (relativePath.empty()) return {};
  const auto joined = platform::joinPortable(directory, relativePath);
  return platform::fileExists(joined) ? joined : std::filesystem::path{};
}

std::filesystem::path firstWithPredicate(const std::filesystem::path& directory,
    bool (*predicate)(const std::filesystem::path&)) {
  for (const auto& file : platform::listFiles(directory)) {
    if (predicate(file)) return file;
  }
  return {};
}

// Fills `context.play` from a chart file and its sidecar metadata. The chart is
// allowed to live outside the library (drag-and-drop, command line, batch mode).
bool buildAdhocSetup(AppContext& context, const std::filesystem::path& chartFile) {
  const auto directory = chartFile.parent_path();
  ChartInfo info;
  const auto infoFile = findInfoFileIn(directory);
  if (!infoFile.empty()) info = readInfoFile(infoFile);
  const ChartInfo meta = readInfoFromChartFile(chartFile);
  const auto fill = [](std::string& target, const std::string& candidate) {
    if (target.empty()) target = candidate;
  };
  fill(info.name, meta.name);
  fill(info.level, meta.level);
  fill(info.difficulty, meta.difficulty);
  fill(info.music, meta.music);
  fill(info.illustration, meta.illustration);

  context.play = PlaySetup{};
  context.play.chartId.clear();
  context.play.chartFile = chartFile;
  context.play.musicFile = resolveInside(directory, info.music);
  if (context.play.musicFile.empty()) {
    context.play.musicFile = firstWithPredicate(directory, isAudioCandidate);
  }
  context.play.bgFile = resolveInside(directory, info.illustration);
  if (context.play.bgFile.empty()) {
    context.play.bgFile = firstWithPredicate(directory, isImageCandidate);
  }
  if (info.name.empty()) info.name = chartFile.stem().string();
  context.play.songName = info.name;
  context.play.difficulty = buildDifficultyLabel(info, chartFile);
  context.play.chartOffsetMs = info.hasChartOffset ? info.chartOffsetMs : 0;
  context.play.audioOffsetMs = context.settings.audioOffsetMs;
  context.play.autoplay = context.settings.autoplay;
  context.play.challenge = context.settings.challengeMode;
  context.play.mirrorX = context.settings.mirrorX;
  context.play.musicSpeed = context.settings.musicSpeed;
  context.play.scrollSpeed = context.settings.scrollSpeed;
  context.play.practice = context.settings.practiceMode;
  // Verification runs override the mode on top of the stored settings.
  if (context.playOverrides.practice) context.play.practice = true;
  if (context.playOverrides.challenge) context.play.challenge = true;
  if (context.playOverrides.mirrorX) context.play.mirrorX = true;
  if (context.playOverrides.autoplay) context.play.autoplay = true;
  return true;
}

void startAdhocChart(AppContext& context, const std::filesystem::path& chartFile) {
  if (!buildAdhocSetup(context, chartFile)) return;
  if (!gameEnter(context)) {
    notify(context, "谱面加载失败");
    return;
  }
  context.screen = Screen::Game;
}

} // namespace

void startAdhocReplay(AppContext& context, const std::filesystem::path& chartFile,
    const std::filesystem::path& replayFile) {
  if (!buildAdhocSetup(context, chartFile)) return;
  const auto replay = readReplayFile(replayFile);
  if (!replay) {
    notify(context, "回放文件损坏");
    return;
  }
  context.play.replayMode = true;
  context.play.replayFile = replayFile;
  context.play.autoplay = false;
  context.play.practice = false;
  context.play.musicSpeed = replay->meta.musicSpeed > 0.0F ? replay->meta.musicSpeed : 1.0F;
  context.play.scrollSpeed = replay->meta.scrollSpeed > 0.0F ? replay->meta.scrollSpeed : 1.0F;
  context.play.mirrorX = replay->meta.mirrorX;
  context.play.challenge = replay->meta.challenge;
  context.play.chartOffsetMs = replay->meta.chartOffsetMs;
  context.play.audioOffsetMs = replay->meta.audioOffsetMs;
  if (!gameEnter(context)) {
    notify(context, "谱面加载失败");
    return;
  }
  context.screen = Screen::Game;
}

AppContext::AppContext(std::filesystem::path root)
    : dataRoot(std::move(root)),
      settingsFile(dataRoot / "settings.txt"),
      library(dataRoot / "packs", dataRoot / "library.txt"),
      replays(dataRoot / "replays") {
  std::error_code error;
  std::filesystem::create_directories(dataRoot, error);
  (void)settings.load(settingsFile);
  calibration.offsetMs = static_cast<float>(settings.audioOffsetMs);
  seedBundledSample();
}

void AppContext::seedBundledSample() {
  // A build that keeps its assets inside the package has no way to reach the library
  // folder, so the bundled example is copied out once. The marker file records that the
  // copy happened, so a user who deletes the example does not get it back.
  const auto marker = dataRoot / "sample-seeded";
  std::error_code error;
  if (std::filesystem::is_regular_file(marker, error)) return;
  const auto& assets = platform::assetRoot();
  // The example needs a chart, its description, an audio track and cover artwork.
  const char* const kFiles[] = {"chart.json", "info.json", "chart.wav", "cover.jpg"};
  std::vector<std::filesystem::path> materialised;
  for (const char* name : kFiles) {
    const auto path = platform::AssetManager::materialize(assets,
        std::string("sample/") + name);
    if (!path.empty()) materialised.push_back(path);
  }
  if (materialised.size() == std::size(kFiles)) {
    // The library imports a folder, so the copies are gathered into one.
    const auto staging = dataRoot / "sample-staging";
    std::filesystem::create_directories(staging, error);
    for (const auto& source : materialised) {
      std::filesystem::copy_file(source, staging / source.filename(),
          std::filesystem::copy_options::overwrite_existing, error);
    }
    try {
      (void)library.importDirectory(staging);
    } catch (const std::exception&) {
      // A malformed example must not stop the interface from starting.
    }
    std::filesystem::remove_all(staging, error);
  }
  std::ofstream writeMarker(marker);
  writeMarker << "1\n";
}

void commitSettings(AppContext& context) {
  if (!context.settingsDirty) return;
  (void)context.settings.save(context.settingsFile);
  context.settingsDirty = false;
}

void notify(AppContext& context, std::string message) {
  context.toastText = std::move(message);
  context.toastDeadlineSec = context.nowSec + kToastLifetimeSec;
}

void navigateTo(AppContext& context, Screen screen) {
  if (context.screen == Screen::Settings) commitSettings(context);
  if (context.screen == Screen::Game && screen != Screen::Game) gameLeave(context);
  if (screen == Screen::Library) {
    context.manageMode = false;
    context.selectedIds.clear();
    context.library.invalidate();
  }
  context.screen = screen;
}

void openChartDetail(AppContext& context, const std::string& chartId) {
  context.activeChartId = chartId;
  context.detailReturn = context.screen == Screen::Settings ? Screen::Library : context.screen;
  if (context.detailReturn != Screen::Library && context.detailReturn != Screen::Result) {
    context.detailReturn = Screen::Library;
  }
  context.screen = Screen::Detail;
}

void startChart(AppContext& context, bool practice) {
  const auto record = context.library.find(context.activeChartId);
  if (!record) {
    notify(context, "谱面不存在或已删除");
    return;
  }
  if (!record->complete()) {
    notify(context, "谱面文件不完整");
    return;
  }
  context.play = PlaySetup{};
  context.play.chartId = record->id;
  context.play.songName = record->songName;
  context.play.difficulty = record->difficulty;
  context.play.chartFile = record->chartFile;
  context.play.musicFile = record->musicFile;
  context.play.bgFile = record->bgFile;
  context.play.chartOffsetMs = record->chartOffsetMs;
  context.play.audioOffsetMs = context.settings.audioOffsetMs;
  context.play.practice = practice;
  context.play.autoplay = context.settings.autoplay;
  context.play.challenge = context.settings.challengeMode;
  context.play.mirrorX = context.settings.mirrorX;
  context.play.musicSpeed = context.settings.musicSpeed;
  context.play.scrollSpeed = context.settings.scrollSpeed;
  commitSettings(context);
  if (!gameEnter(context)) {
    notify(context, context.loadError.empty() ? "谱面加载失败" : context.loadError);
    return;
  }
  context.screen = Screen::Game;
}

void startSavedReplay(AppContext& context, const std::string& replayId) {
  const auto info = context.replays.find(replayId);
  if (!info) {
    notify(context, "回放不存在");
    return;
  }
  const auto replay = readReplayFile(info->file);
  if (!replay) {
    notify(context, "回放文件损坏");
    return;
  }
  std::optional<ChartRecord> record;
  if (!replay->meta.chartId.empty()) record = context.library.find(replay->meta.chartId);
  if (!record) {
    for (const auto& candidate : context.library.records()) {
      if (candidate.songName == replay->meta.songName) {
        record = candidate;
        break;
      }
    }
  }
  if (!record) {
    notify(context, "回放对应的谱面不在库中");
    return;
  }
  context.play = PlaySetup{};
  context.play.chartId = record->id;
  context.play.songName = record->songName;
  context.play.difficulty = record->difficulty;
  context.play.chartFile = record->chartFile;
  context.play.musicFile = record->musicFile;
  context.play.bgFile = record->bgFile;
  context.play.chartOffsetMs = replay->meta.chartOffsetMs;
  context.play.audioOffsetMs = replay->meta.audioOffsetMs;
  context.play.musicSpeed = replay->meta.musicSpeed > 0.0F ? replay->meta.musicSpeed : 1.0F;
  context.play.scrollSpeed = replay->meta.scrollSpeed > 0.0F ? replay->meta.scrollSpeed : 1.0F;
  context.play.mirrorX = replay->meta.mirrorX;
  context.play.challenge = replay->meta.challenge;
  context.play.replayMode = true;
  context.play.replayFile = info->file;
  if (!gameEnter(context)) {
    notify(context, context.loadError.empty() ? "谱面加载失败" : context.loadError);
    return;
  }
  context.screen = Screen::Game;
}

void handleIncomingPath(AppContext& context, const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    notify(context, "文件不存在");
    return;
  }
  const std::string extension = toLower(path.extension().string());
  try {
    if (std::filesystem::is_directory(path, error)) {
      const auto result = context.library.importDirectory(path);
      notify(context, result.imported ? "已导入谱面" : "谱面已在库中");
      openChartDetail(context, result.record.id);
      return;
    }
    if (extension == ".zip") {
      const auto result = context.library.importZip(path);
      notify(context, result.imported ? "已导入谱面包" : "谱面包已在库中");
      openChartDetail(context, result.record.id);
      return;
    }
    if (isChartCandidate(path)) {
      startAdhocChart(context, path);
      return;
    }
    notify(context, "不支持的文件类型");
  } catch (const std::exception& exception) {
    notify(context, std::string("导入失败: ") + exception.what());
  }
}

void drawHomeScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##home");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ui::drawBackdrop(drawList, ImVec2(size.width, size.height), context.nowSec);

  // Flat scrim over the backdrop, so every surface above it keeps its contrast.
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      ui::rgba(0x46000000U));

  const auto point = [&](float x, float y) { return designPosition(ImVec2(size.width, size.height),
        scale, x, y); };

  // Title.
  const ImVec2 titlePosition = point(365.0F, 22.0F + 46.0F);
  ui::drawTextCentered(drawList, titlePosition, 46.0F * scale, ui::rgba(ui::kTextPrimary),
      "ReChigros");

  // Start panel. The whole menu block is 457 design units wide and centred.
  const ImVec2 startTopLeft = point(137.0F, 134.0F);
  const ImVec2 startSize(292.0F * scale, 204.0F * scale);
  ImGui::SetCursorScreenPos(startTopLeft);
  const bool startPressed = ImGui::InvisibleButton("##start_chart", startSize);
  const bool startHovered = ImGui::IsItemHovered();
  ui::drawPanel(drawList, startTopLeft,
      ImVec2(startTopLeft.x + startSize.x, startTopLeft.y + startSize.y),
      204.0F * scale * ui::kSkewRatio, startHovered ? IM_COL32(48, 54, 68, 200)
                                                   : ui::rgba(ui::kPanelFill),
      ui::rgba(0xFFFFFFB8), 1.5F, false, true);
  const ImVec2 startLabelPosition(startTopLeft.x + 292.0F * scale * ui::kSkewRatio + 18.0F * scale,
      startTopLeft.y + 30.0F * scale);
  ui::drawText(drawList, startLabelPosition, 34.0F * scale, ui::rgba(ui::kTextPrimary), "开始游玩");
  const std::uint64_t startIcon = assetTexture(context, "res/play.png", 128);
  if (startIcon != 0) {
    const ImVec2 iconPosition(startTopLeft.x + 34.0F * scale, startTopLeft.y + 108.0F * scale);
    drawList->AddImage(static_cast<ImTextureID>(startIcon), iconPosition,
        ImVec2(iconPosition.x + 66.0F * scale, iconPosition.y + 66.0F * scale));
  }
  const std::uint64_t meow = assetTexture(context, "res/meow.png", 256);
  if (meow != 0) {
    const float meowSize = 104.0F * scale;
    const ImVec2 meowMin(startTopLeft.x + (startSize.x - meowSize) * 0.5F - 14.0F * scale,
        startTopLeft.y + 204.0F * scale - meowSize);
    drawList->AddImage(static_cast<ImTextureID>(meow), meowMin,
        ImVec2(meowMin.x + meowSize, meowMin.y + meowSize));
  }
  if (startPressed) {
    context.libraryTab = 0;
    context.library.invalidate();
    context.screen = Screen::Library;
  }

  // Side menu.
  struct MenuEntry {
    const char* label;
  };
  const MenuEntry entries[] = {{"游玩设置"}, {"资源包"}, {"活动"}};
  const float sideTop = 134.0F * scale;
  const float sideLeft = startTopLeft.x + (292.0F + 13.0F) * scale;
  const float buttonHeight = 52.0F * scale;
  const float buttonGap = 20.0F * scale;
  for (int index = 0; index < 3; ++index) {
    const ImVec2 buttonTopLeft(sideLeft - static_cast<float>(index) * (buttonHeight + buttonGap)
            * ui::kSkewRatio,
        sideTop + static_cast<float>(index) * (buttonHeight + buttonGap));
    const std::string id = "##home_menu_" + std::to_string(index);
    if (ui::skewButton(id.c_str(), entries[index].label, buttonTopLeft,
            ImVec2(152.0F * scale, buttonHeight), 22.0F * scale, -1.0F, ui::rgba(ui::kPanelFill),
            ui::rgba(0xFFFFFF96), false, true)) {
      if (index == 0) {
        context.settingsTab = 0;
        context.settingsReturn = Screen::Home;
        context.screen = Screen::Settings;
      } else if (index == 1) {
        context.settingsTab = 0;
        context.settingsReturn = Screen::Home;
        context.screen = Screen::Settings;
      } else {
        notify(context, "暂无活动");
      }
    }
  }

  // Footer: the device the game is running on, as the reference shows it.
  const std::string footer = rechigros::platform::deviceDescription();
  ui::drawTextCentered(drawList, point(365.0F, 415.0F - 24.0F), 16.0F * scale,
      ui::rgba(ui::kTextMuted), footer.c_str());

  // Project link in the bottom-right corner. The mark is drawn from primitives
  // because no packaged glyph asset exists for it.
  {
    const float markSize = 32.0F * scale;
    const ImVec2 topLeft(size.width - 14.0F * scale - markSize,
        size.height - 12.0F * scale - markSize);
    ImGui::SetCursorScreenPos(topLeft);
    const bool pressed = ImGui::InvisibleButton("##home_link", ImVec2(markSize, markSize));
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 tint = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 235);
    const ImVec2 center(topLeft.x + markSize * 0.5F, topLeft.y + markSize * 0.5F);
    const float radius = markSize * 0.46F;
    // Filled disc with a lighter notch, matching the solid badge the reference shows.
    drawList->AddCircleFilled(center, radius, tint, 0);
    const float notch = radius * 0.52F;
    drawList->AddRectFilled(ImVec2(center.x - notch, center.y - notch),
        ImVec2(center.x + notch, center.y + notch * 0.35F), IM_COL32(16, 20, 26, 255),
        notch * 0.4F);
    drawList->AddRectFilled(ImVec2(center.x - notch * 0.55F, center.y + notch * 0.5F),
        ImVec2(center.x + notch * 0.55F, center.y + notch * 1.05F), IM_COL32(16, 20, 26, 255),
        notch * 0.3F);
    if (pressed) notify(context, "项目主页");
    if (hovered) ImGui::SetTooltip("项目主页");
  }

  ui::endFullscreenWindow();
}

void drawToast(AppContext& context) {
  if (context.toastText.empty() || context.nowSec > context.toastDeadlineSec) return;
  const auto size = viewportSize();
  const float scale = layoutScale();
  ImGui::SetNextWindowPos(ImVec2(size.width * 0.5F, size.height - 90.0F * scale),
      ImGuiCond_Always, ImVec2(0.5F, 1.0F));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06F, 0.08F, 0.10F, 0.94F));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0F, 1.0F, 1.0F, 0.20F));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0F, 10.0F));
  ImGui::Begin("##toast", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
          | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::SetWindowFontScale(scale * 0.9F);
  ImGui::TextUnformatted(context.toastText.c_str());
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

} // namespace rechigros::app
