#pragma once

#include <filesystem>
#include <string>

namespace rechigros::app {

// Persistent user preferences. Every field has a defined default so a missing or
// partially written file still yields a playable configuration.
struct Settings {
  // Play field.
  float keyScale{1.0F};
  float backgroundDim{0.25F};
  float aspectRatio{0.0F};
  float scrollSpeed{1.0F};
  float judgeLineWidth{1.0F};

  // Audio.
  float musicVolume{1.0F};
  float soundEffectVolume{1.0F};
  float musicSpeed{1.0F};
  int audioOffsetMs{};

  // Judgement display.
  int hitOffsetIndicatorMode{1};
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
  bool replayEnabled{true};

  std::string selectedSkin;

  [[nodiscard]] bool load(const std::filesystem::path& file);
  [[nodiscard]] bool save(const std::filesystem::path& file) const;
};

} // namespace rechigros::app
