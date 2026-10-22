#include "rechigros/app/AppContext.hpp"

#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiLayout.hpp"
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

std::filesystem::path resolveInside(const std::filesystem::path& directory,
    const std::string& relativePath) {
  if (relativePath.empty()) return {};
  const auto joined = platform::joinPortable(directory, relativePath);
  if (!platform::fileExists(joined)) return {};
  // The chart file is the root of an ad-hoc pack, so a sidecar path may point into its
  // subdirectories but never outside it.
  std::error_code error;
  const auto relative = std::filesystem::relative(
      std::filesystem::weakly_canonical(joined, error),
      std::filesystem::weakly_canonical(directory, error), error);
  if (error || relative.empty() || relative.native().rfind(std::filesystem::path(".."), 0) == 0) {
    return {};
  }
  return joined;
}

// Fills `context.play` from a chart file and its sidecar metadata. The chart is
// allowed to live outside the library (drag-and-drop, command line, batch mode). An ad-hoc
// chart has no pack root, so every sidecar lookup only looks at the chart's own directory:
// a sibling directory's metadata or artwork belongs to a different pack.
bool buildAdhocSetup(AppContext& context, const std::filesystem::path& chartFile) {
  const auto directory = chartFile.parent_path();
  ChartInfo info;
  const auto infoFile = findSidecarInfoFile(directory);
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
    context.play.musicFile = findSidecarMusicFile(directory);
  }
  context.play.bgFile = resolveInside(directory, info.illustration);
  if (context.play.bgFile.empty()) {
    context.play.bgFile = findSidecarImageFile(directory);
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
  // The modes and speeds stored in the replay describe the run that was recorded; playback
  // judges and draws with the viewer's own settings. Judging is driven by the recorded
  // judgements, so only the display modes are affected.
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
  // Playback uses the viewer's own settings, exactly like startAdhocReplay: the modes stored
  // in the replay are display-only.
  context.play.chartOffsetMs = record->chartOffsetMs;
  context.play.audioOffsetMs = context.settings.audioOffsetMs;
  context.play.musicSpeed = context.settings.musicSpeed;
  context.play.scrollSpeed = context.settings.scrollSpeed;
  context.play.mirrorX = context.settings.mirrorX;
  context.play.challenge = context.settings.challengeMode;
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
  const ui::Canvas canvas = ui::canvas();
  const ImVec2 viewport(canvas.viewport.x, canvas.viewport.y);
  ui::beginFullscreenWindow("##home");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  // Fallback artwork behind the interface. The GPU backdrop draws the same scene when it
  // is available, in which case this call is suppressed.
  ui::drawBackdrop(drawList, viewport, context.nowSec);
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), viewport, ui::argb(0x46000000U));

  // Title block: 280 x 92 design units, 22 units below the top edge, horizontally centred.
  const ui::Box titleBox{(ui::kDesignWidth - 280.0F) * 0.5F, 22.0F,
      (ui::kDesignWidth + 280.0F) * 0.5F, 114.0F};
  ui::TextStyle titleStyle;
  titleStyle.sizeDp = 46.0F;
  titleStyle.bold = true;
  titleStyle.align = ui::HAlign::Center;
  titleStyle.valign = ui::VAlign::Middle;
  ui::drawLabel(drawList, canvas, titleBox, "ReChigros", titleStyle, context.nowSec);

  // Menu block: a 457 x 204 design-unit area 134 units below the top edge, centred. It
  // holds the start panel, the character that overlaps its lower edge and the side entries.
  constexpr float kMenuWidth = 457.0F;
  constexpr float kMenuLeft = (ui::kDesignWidth - kMenuWidth) * 0.5F;
  constexpr float kMenuTop = 134.0F;
  constexpr float kStartWidth = 292.0F;
  constexpr float kStartHeight = 204.0F;
  constexpr float kSideGap = 13.0F;
  constexpr float kSideWidth = 152.0F;
  constexpr float kButtonHeight = 52.0F;
  constexpr float kButtonGap = 20.0F;
  constexpr float kSkewSlope = 0.2679492F; // tan(15 degrees)

  const ui::Box startBox{kMenuLeft, kMenuTop, kMenuLeft + kStartWidth, kMenuTop + kStartHeight};
  const ui::PanelStyle startPanel =
      ui::glassPanel(ui::argb(0xA5222A35U), ui::argb(0xB8FFFFFFU));

  // The panel's top edge starts after the skew offset, so the label follows that edge.
  const float startSkew = std::min(kSkewSlope * kStartHeight, kStartWidth * 0.42F);
  const float labelLeft = kMenuLeft + startSkew + 16.0F;
  const ui::Box startLabelBox{labelLeft, kMenuTop + 8.0F,
      labelLeft + (kStartWidth - startSkew - 16.0F - 12.0F), kMenuTop + 8.0F + 54.0F};
  ui::TextStyle startLabelStyle;
  // The start panel label is drawn smaller than its box height: 28 density units keeps the
  // rendered label at the same size as the running interface.
  startLabelStyle.sizeDp = 28.0F;
  startLabelStyle.bold = true;
  startLabelStyle.align = ui::HAlign::Left;
  startLabelStyle.valign = ui::VAlign::Middle;

  const ui::Interaction start = ui::panelInteraction("##home_start", canvas, startBox, startPanel);
  ui::drawPanelShape(drawList, canvas, startBox, startPanel);
  ui::drawLabel(drawList, canvas, startLabelBox, "开始游玩", startLabelStyle, context.nowSec);

  // Character artwork, anchored to the bottom edge of the start panel and overlapping it.
  const std::uint64_t meowTexture = assetTexture(context, "res/meow.png", 320);
  if (meowTexture != 0) {
    const int meowTextureWidth = context.textures.width(meowTexture);
    const int meowTextureHeight = context.textures.height(meowTexture);
    const float meowSize = canvas.units(104.0F);
    const float outset = std::max(1.0F, std::round(meowSize * 11.0F / 524.0F));
    const ui::PxRect menuPixels = ui::toPixels(canvas,
        ui::Box{kMenuLeft, kMenuTop, kMenuLeft + kMenuWidth, kMenuTop + kStartHeight});
    // The horizontal offset is a layout-space value in the same space as the panel sizes.
    const float left = menuPixels.min.x
        + std::round((canvas.units(kStartWidth) - meowSize) * 0.5F) - std::round(canvas.units(14.0F));
    const float top = menuPixels.min.y + canvas.units(kStartHeight) - meowSize + outset;
    ui::drawImage(drawList,
        ui::PxRect{ImVec2(left, top), ImVec2(left + meowSize, top + meowSize)}, meowTexture,
        meowTextureWidth, meowTextureHeight, ui::ImageFit::Fill);
  }

  // Side entries: each one is 52 units tall, 20 apart, and shifted left by the slant of
  // the stack so their leading edges line up with the start panel's edge.
  const char* const sideLabels[3] = {"游玩设置", "资源包", "活动"};
  int sidePressed = -1;
  for (int index = 0; index < 3; ++index) {
    const float stackTop = canvas.units((kButtonHeight + kButtonGap) * static_cast<float>(index));
    const float left = kMenuLeft + kStartWidth + kSideGap
        - std::round(stackTop * kSkewSlope) / canvas.scale;
    const float top = kMenuTop + (kButtonHeight + kButtonGap) * static_cast<float>(index);
    const ui::Box box{left, top, left + kSideWidth, top + kButtonHeight};
    const ui::PanelStyle panel =
        ui::glassPanel(ui::argb(0x9A222A35U), ui::argb(0x96FFFFFFU));
    const std::string id = "##home_side_" + std::to_string(index);
    const ui::Interaction side = ui::panelInteraction(id.c_str(), canvas, box, panel);
    ui::drawPanelShape(drawList, canvas, box, panel);
    ui::TextStyle labelStyle;
    labelStyle.sizeDp = 22.0F;
    labelStyle.bold = true;
    labelStyle.align = ui::HAlign::Center;
    labelStyle.valign = ui::VAlign::Middle;
    ui::drawLabel(drawList, canvas, box, sideLabels[index], labelStyle, context.nowSec);
    if (side.pressed) sidePressed = index;
  }

  // Device footer and the project mark in the corner.
  const ui::Box footerBox{0.0F, ui::kDesignHeight - 8.0F - 30.0F, ui::kDesignWidth,
      ui::kDesignHeight - 8.0F};
  ui::TextStyle footerStyle;
  footerStyle.sizeDp = 16.0F;
  footerStyle.bold = false;
  footerStyle.color = ui::argb(0xB8FFFFFFU);
  footerStyle.align = ui::HAlign::Center;
  footerStyle.valign = ui::VAlign::Middle;
  ui::drawLabel(drawList, canvas, footerBox, rechigros::platform::deviceDescription(),
      footerStyle, context.nowSec);

  {
    const ui::Box markBox{ui::kDesignWidth - 14.0F - 32.0F, ui::kDesignHeight - 12.0F - 32.0F,
        ui::kDesignWidth - 14.0F, ui::kDesignHeight - 12.0F};
    const ui::PanelStyle markPanel = ui::rectPanel(0x00000000U, 0x00000000U);
    const ui::Interaction mark = ui::panelInteraction("##home_link", canvas, markBox, markPanel);
    const std::uint64_t markTexture = assetTexture(context, "res/github.png", 128);
    if (markTexture != 0) {
      const int markWidth = context.textures.width(markTexture);
      const int markHeight = context.textures.height(markTexture);
      // The mark sits inside a 2 dp inset and uses a slightly translucent tint, matching
      // the corner badge of the interface.
      const ui::PxRect pixels = ui::toPixels(canvas, markBox);
      const float inset = canvas.dp(2.0F);
      ui::PxRect content;
      content.min = ImVec2(pixels.min.x + inset, pixels.min.y + inset);
      content.max = ImVec2(pixels.max.x - inset, pixels.max.y - inset);
      const int alpha = mark.hovered ? 255 : 214;
      ui::drawImage(drawList, content, markTexture, markWidth, markHeight,
          ui::ImageFit::FitCenter, IM_COL32(255, 255, 255, alpha));
    }
    if (mark.pressed) notify(context, "项目主页");
  }

  ui::endFullscreenWindow();

  if (start.pressed) {
    context.libraryTab = 0;
    context.library.invalidate();
    context.screen = Screen::Library;
  } else if (sidePressed == 0) {
    context.settingsTab = 0;
    context.settingsReturn = Screen::Home;
    context.screen = Screen::Settings;
  } else if (sidePressed == 1) {
    context.settingsTab = 0;
    context.settingsReturn = Screen::Home;
    context.screen = Screen::Settings;
    notify(context, "可在视频页导入皮肤资源");
  } else if (sidePressed == 2) {
    notify(context, "暂无活动");
  }
}

void drawToast(AppContext& context) {
  if (context.toastText.empty() || context.nowSec > context.toastDeadlineSec) return;
  const ui::Canvas canvas = ui::canvas();
  ImDrawList* drawList = ImGui::GetForegroundDrawList();
  if (drawList == nullptr) return;
  constexpr float kTextSizeDp = 16.0F;
  const float textWidth = ui::labelWidth(canvas, context.toastText, kTextSizeDp, false);
  const float lineHeight = ui::labelLineHeight(canvas, kTextSizeDp);
  const float paddingX = canvas.dp(18.0F) / canvas.scale;
  const float paddingY = canvas.dp(9.0F) / canvas.scale;
  const float width = textWidth + paddingX * 2.0F;
  const float height = lineHeight + paddingY * 2.0F;
  const float bottom = ui::kDesignHeight - canvas.dp(64.0F) / canvas.scale;
  const ui::PxRect panel = ui::toPixels(canvas,
      ui::Box{(ui::kDesignWidth - width) * 0.5F, bottom - height,
          (ui::kDesignWidth + width) * 0.5F, bottom});
  const float radius = canvas.dp(8.0F);
  drawList->AddRectFilled(panel.min, panel.max, IM_COL32(20, 24, 30, 235), radius);
  drawList->AddRect(panel.min, panel.max, IM_COL32(255, 255, 255, 40), radius);
  ui::TextStyle style;
  style.sizeDp = kTextSizeDp;
  style.bold = false;
  style.align = ui::HAlign::Center;
  style.valign = ui::VAlign::Middle;
  style.fitWidth = true;
  ui::drawLabel(drawList, canvas,
      ui::Box{(ui::kDesignWidth - width) * 0.5F, bottom - height,
          (ui::kDesignWidth + width) * 0.5F, bottom},
      context.toastText, style, context.nowSec);
}

} // namespace rechigros::app
