#pragma once

#include <filesystem>
#include <string>

namespace rechigros::app {

// Persistent user preferences. Every field has a defined default so a missing or
// partially written file still yields a playable configuration.
struct Settings {
  // Play field. The stage aspect defaults to the reference's 16:9; zero means "follow the
  // window" and is only written after the user picks that option.
  float keyScale{1.0F};
  float backgroundDim{0.25F};
  float aspectRatio{16.0F / 9.0F};
  float scrollSpeed{1.0F};
  float judgeLineWidth{1.0F};

  // Audio.
  float musicVolume{1.0F};
  float soundEffectVolume{1.0F};
  float musicSpeed{1.0F};
  int audioOffsetMs{};

  // Judgement display.
  // Judgement display. The offset indicator is disabled by default like the reference; the
  // user enables it from the settings page.
  int hitOffsetIndicatorMode{};
  bool apfcIndicator{};
  bool judgementDetailDistribution{};
  bool realtimeAccuracy{};
  bool scoreJumpEffect{};
  bool multiPressHighlight{true};

  // Session defaults.
  bool mirrorX{};
  bool autoplay{};
  bool challengeMode{};
  bool practiceMode{};
  bool lowResolutionMode{};
  bool antialias{};
  bool showFps{};
  bool showDebugInfo{};
  bool disableChartShader{};
  bool chartReveal{};
  bool storyMode{true};
  // The reference starts with replay recording off.
  bool replayEnabled{false};

  // Interface scale. The layout is authored on a fixed design canvas, so on a small phone
  // or a high-density desktop the rows end up physically small. This multiplies the density
  // units the interface uses, which enlarges every row, glyph and control together without
  // changing their proportions. The reference has no such setting because it only ships at
  // one density; the port exposes it so the picture stays readable on other screens.
  float uiScale{1.0F};

  std::string selectedSkin;

  [[nodiscard]] bool load(const std::filesystem::path& file);
  [[nodiscard]] bool save(const std::filesystem::path& file) const;
};

} // namespace rechigros::app
