#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/platform/FileSystem.hpp"

#include "rechigros/audio/AudioEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace rechigros::app {
namespace {

constexpr double kLoopSeconds = 2.0;
constexpr double kNoteHitSeconds = 1.0;
constexpr double kNoteSpawnSeconds = 1.5;
constexpr double kNoteFallSeconds = 1.5;

double wrapLoop(double value) {
  double result = std::fmod(value, kLoopSeconds);
  if (result < 0.0) result += kLoopSeconds;
  return result;
}

// Normalised fall progress in [0,1], or NaN while the note is off screen.
double noteProgress(double loopTime) {
  if (loopTime >= kNoteSpawnSeconds) {
    return (loopTime - kNoteSpawnSeconds) / kNoteFallSeconds;
  }
  if (loopTime <= kNoteHitSeconds) {
    return (loopTime + kLoopSeconds - kNoteSpawnSeconds) / kNoteFallSeconds;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

void exitCalibration(AppContext& context, bool apply) {
  if (context.audio != nullptr) {
    context.audio->stop();
    context.audio->setMusicLooping(false);
    context.audio->setLowPassEnabled(false);
    context.audio->setPlaybackSpeed(1.0F);
  }
  context.calibration.audioLoaded = false;
  context.calibration.markers.clear();
  if (apply) {
    context.settings.audioOffsetMs =
        std::clamp(static_cast<int>(std::lround(context.calibration.offsetMs)), -500, 500);
    context.settingsDirty = true;
  }
  commitSettings(context);
  navigateTo(context, context.settingsReturn);
}

} // namespace

void drawCalibrationScreen(AppContext& context) {
  const auto size = viewportSize();
  const float scale = layoutScale();
  ui::beginFullscreenWindow("##calibration");
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(size.width, size.height),
      IM_COL32(16, 19, 26, 255));

  if (!context.calibration.audioLoaded) {
    bool loaded = false;
    if (context.audio != nullptr) {
      const auto music = platform::assetRoot() / "audio" / "calibration.wav";
      context.audio->stop();
      context.audio->setMusicLooping(true);
      context.audio->setVolume(1.0F);
      context.audio->setSoundEffectVolume(1.0F);
      context.audio->setPlaybackSpeed(1.0F);
      if (context.audio->loadMusic(music)) {
        context.audio->play();
        loaded = true;
      }
    }
    if (!loaded) {
      notify(context, "校准音频加载失败");
      context.calibration.audioLoaded = true;
      exitCalibration(context, false);
      ui::endFullscreenWindow();
      return;
    }
    context.calibration.audioLoaded = true;
    context.calibration.markers.clear();
    context.calibration.previousLoopTime = -1.0;
    context.calibration.offsetMs = static_cast<float>(context.settings.audioOffsetMs);
  }

  const float panelWidth = std::min(320.0F * scale, size.width * 0.45F);
  const float playRight = std::max(size.width * 0.55F, size.width - panelWidth);
  const float centerX = playRight * 0.5F;
  const float lineHalf = playRight * 0.28F;
  const float lineY = size.height * 0.75F;
  const float noteWidth = playRight * 0.1234375F;

  // Music playhead with the calibration offset applied.
  double playhead = 0.0;
  if (context.audio != nullptr) playhead = context.audio->cursorSeconds();
  const double loopTime = wrapLoop(playhead - static_cast<double>(context.calibration.offsetMs) / 1000.0);

  // Trigger the hit sound and a hit effect once per loop, when the note passes
  // the judge line.
  if (context.calibration.previousLoopTime >= 0.0) {
    const double previous = context.calibration.previousLoopTime;
    const bool crossed = (previous < kNoteHitSeconds && loopTime >= kNoteHitSeconds)
        || (loopTime < previous && previous >= kNoteHitSeconds);
    if (crossed) {
      if (context.audio != nullptr) context.audio->triggerSoundEffect(1);
      context.calibration.markers.push_back(CalibrationMarker{1.0, 0.0});
    }
  }
  context.calibration.previousLoopTime = loopTime;

  // Hit line.
  drawList->AddRectFilled(ImVec2(centerX - lineHalf, lineY - 2.0F * scale),
      ImVec2(centerX + lineHalf, lineY + 2.0F * scale), IM_COL32(255, 255, 255, 255));

  // Falling note.
  const double progress = noteProgress(loopTime);
  const std::uint64_t tapTexture = assetTexture(context, "res/click.png", 128);
  if (std::isfinite(progress)) {
    const float noteHeight = noteWidth * 0.36F;
    const float y = static_cast<float>(progress) * lineY;
    const ImVec2 minimum(centerX - noteWidth * 0.5F, y - noteHeight * 0.5F);
    const ImVec2 maximum(centerX + noteWidth * 0.5F, y + noteHeight * 0.5F);
    if (tapTexture != 0) {
      drawList->AddImage(static_cast<ImTextureID>(tapTexture), minimum, maximum);
    } else {
      drawList->AddRectFilled(minimum, maximum, IM_COL32(86, 214, 201, 255), noteHeight * 0.5F);
    }
  }

  // Touch and hit markers.
  const double delta = context.deltaSec;
  for (auto& marker : context.calibration.markers) {
    marker.ageSec += delta;
    const float age = static_cast<float>(marker.ageSec);
    if (age >= 1.0F) continue;
    const float alpha = 1.0F - age;
    if (marker.yNorm >= 0.999) {
      // Hit burst: expanding ring at the judge line.
      const float radius = (18.0F + 46.0F * age) * scale;
      drawList->AddCircle(ImVec2(centerX, lineY), radius,
          IM_COL32(255, 236, 160, static_cast<int>(alpha * 220.0F)), 32, 2.5F);
    } else {
      const float y = static_cast<float>(marker.yNorm) * lineY;
      const float half = lineHalf * age;
      drawList->AddRectFilled(ImVec2(centerX - half, y - 1.0F * scale),
          ImVec2(centerX + half, y + 1.0F * scale),
          IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0F)));
    }
  }
  context.calibration.markers.erase(
      std::remove_if(context.calibration.markers.begin(), context.calibration.markers.end(),
          [](const CalibrationMarker& marker) { return marker.ageSec >= 1.0; }),
      context.calibration.markers.end());

  // Touch input inside the play field.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < playRight - 8.0F) {
      const double noteY = std::isfinite(progress) ? progress : 1.0;
      context.calibration.markers.push_back(
          CalibrationMarker{std::min(1.0, std::max(0.0, noteY)), 0.0});
      if (context.audio != nullptr) context.audio->triggerSoundEffect(1);
    }
  }

  // Right hand panel.
  const ImVec2 panelTopLeft(size.width - panelWidth, 0.0F);
  drawList->AddRectFilled(panelTopLeft, ImVec2(size.width, size.height),
      IM_COL32(27, 34, 45, 204));

  ImGui::SetCursorScreenPos(ImVec2(panelTopLeft.x + 20.0F * scale, 18.0F * scale));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96F, 0.97F, 0.98F, 1.0F));
  ImGui::SetWindowFontScale(std::clamp(scale, 0.8F, 1.4F));
  ImGui::BeginChild("##calibration_panel", ImVec2(panelWidth - 40.0F * scale,
                        size.height - 36.0F * scale),
      false, ImGuiWindowFlags_NoBackground);
  ImGui::TextUnformatted("延迟校准");
  ImGui::Spacing();
  ImGui::TextWrapped("跟随节拍点击左侧区域，让打击音与音乐同步。");
  ImGui::Spacing();
  ImGui::SetWindowFontScale(std::clamp(scale, 0.8F, 1.4F) * 1.8F);
  char valueBuffer[32];
  std::snprintf(valueBuffer, sizeof(valueBuffer), "%d ms",
      static_cast<int>(std::lround(context.calibration.offsetMs)));
  ImGui::TextUnformatted(valueBuffer);
  ImGui::SetWindowFontScale(std::clamp(scale, 0.8F, 1.4F));
  ImGui::Spacing();
  int offset = static_cast<int>(std::lround(context.calibration.offsetMs));
  ImGui::SetNextItemWidth(panelWidth - 60.0F * scale);
  if (ImGui::SliderInt("##calibration_offset", &offset, -500, 500, "%d ms")) {
    context.calibration.offsetMs = static_cast<float>(offset);
  }
  const float buttonWidth = (panelWidth - 70.0F * scale) * 0.25F;
  const char* steps[] = {"-50", "-5", "+5", "+50"};
  const int deltas[] = {-50, -5, 5, 50};
  for (int index = 0; index < 4; ++index) {
    if (index > 0) ImGui::SameLine();
    if (ImGui::Button(steps[index], ImVec2(buttonWidth, 40.0F * scale))) {
      context.calibration.offsetMs =
          std::clamp(context.calibration.offsetMs + static_cast<float>(deltas[index]), -500.0F,
              500.0F);
    }
  }
  ImGui::Spacing();
  if (ImGui::Button("重置", ImVec2(panelWidth - 60.0F * scale, 42.0F * scale))) {
    context.calibration.offsetMs = 0.0F;
  }
  if (ImGui::Button("完成", ImVec2(panelWidth - 60.0F * scale, 46.0F * scale))) {
    context.calibration.offsetMs = static_cast<float>(offset);
    exitCalibration(context, true);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ui::endFullscreenWindow();
    return;
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  if (backButton(context, "##calibration_back", scale)) {
    exitCalibration(context, true);
    ui::endFullscreenWindow();
    return;
  }

  ui::endFullscreenWindow();
}

} // namespace rechigros::app
