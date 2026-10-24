#include "rechigros/app/Settings.hpp"

#include "rechigros/platform/FileSystem.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string_view>

namespace rechigros::app {
namespace {

using Values = std::map<std::string, std::string>;

std::string toString(float value) {
  std::ostringstream stream;
  stream.precision(6);
  stream << value;
  return stream.str();
}

float parseFloat(const std::string& value, float fallback) {
  try {
    std::size_t consumed = 0;
    const float parsed = std::stof(value, &consumed);
    return consumed == 0 ? fallback : parsed;
  } catch (...) {
    return fallback;
  }
}

int parseInt(const std::string& value, int fallback) {
  try {
    std::size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed);
    return consumed == 0 ? fallback : parsed;
  } catch (...) {
    return fallback;
  }
}

bool parseBool(const std::string& value, bool fallback) {
  if (value == "1" || value == "true") return true;
  if (value == "0" || value == "false") return false;
  return fallback;
}

// The stage aspect accepts zero ("follow the window") or a finite ratio clamped to the
// range the settings slider can produce. A non-finite value is repaired to "follow window".
float sanitizeAspectRatio(float value) {
  if (!std::isfinite(value)) return 0.0F;
  if (value == 0.0F) return 0.0F;
  return std::max(0.5F, std::min(value, 5.0F));
}

} // namespace

bool Settings::load(const std::filesystem::path& file) {
  const std::string content = platform::readTextFile(file);
  if (content.empty()) return false;
  Values values;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto separator = line.find('=');
    if (separator == std::string::npos || separator == 0) continue;
    values[line.substr(0, separator)] = line.substr(separator + 1);
  }
  const auto number = [&values](const char* key, float fallback) {
    const auto iterator = values.find(key);
    return iterator == values.end() ? fallback : parseFloat(iterator->second, fallback);
  };
  const auto integer = [&values](const char* key, int fallback) {
    const auto iterator = values.find(key);
    return iterator == values.end() ? fallback : parseInt(iterator->second, fallback);
  };
  const auto boolean = [&values](const char* key, bool fallback) {
    const auto iterator = values.find(key);
    return iterator == values.end() ? fallback : parseBool(iterator->second, fallback);
  };

  keyScale = number("keyScale", keyScale);
  backgroundDim = number("backgroundDim", backgroundDim);
  aspectRatio = number("aspectRatio", aspectRatio);
  scrollSpeed = number("scrollSpeed", scrollSpeed);
  judgeLineWidth = number("judgeLineWidth", judgeLineWidth);
  musicVolume = number("musicVolume", musicVolume);
  soundEffectVolume = number("soundEffectVolume", soundEffectVolume);
  musicSpeed = number("musicSpeed", musicSpeed);
  audioOffsetMs = integer("audioOffsetMs", audioOffsetMs);
  hitOffsetIndicatorMode = integer("hitOffsetIndicatorMode", hitOffsetIndicatorMode);
  apfcIndicator = boolean("apfcIndicator", apfcIndicator);
  judgementDetailDistribution = boolean("judgementDetailDistribution", judgementDetailDistribution);
  realtimeAccuracy = boolean("realtimeAccuracy", realtimeAccuracy);
  scoreJumpEffect = boolean("scoreJumpEffect", scoreJumpEffect);
  multiPressHighlight = boolean("multiPressHighlight", multiPressHighlight);
  mirrorX = boolean("mirrorX", mirrorX);
  autoplay = boolean("autoplay", autoplay);
  challengeMode = boolean("challengeMode", challengeMode);
  practiceMode = boolean("practiceMode", practiceMode);
  lowResolutionMode = boolean("lowResolutionMode", lowResolutionMode);
  antialias = boolean("antialias", antialias);
  showFps = boolean("showFps", showFps);
  showDebugInfo = boolean("showDebugInfo", showDebugInfo);
  disableChartShader = boolean("disableChartShader", disableChartShader);
  chartReveal = boolean("chartReveal", chartReveal);
  storyMode = boolean("storyMode", storyMode);
  replayEnabled = boolean("replayEnabled", replayEnabled);
  uiScale = number("uiScale", uiScale);
  const auto skin = values.find("selectedSkin");
  if (skin != values.end()) selectedSkin = skin->second;

  // Clamp everything that feeds the mixer or the play field scaling directly.
  aspectRatio = sanitizeAspectRatio(aspectRatio);
  keyScale = std::max(0.7F, std::min(keyScale, 1.5F));
  backgroundDim = std::max(0.0F, std::min(backgroundDim, 1.0F));
  scrollSpeed = std::max(0.1F, std::min(scrollSpeed, 5.0F));
  judgeLineWidth = std::max(0.5F, std::min(judgeLineWidth, 2.0F));
  musicVolume = std::max(0.0F, std::min(musicVolume, 5.0F));
  soundEffectVolume = std::max(0.0F, std::min(soundEffectVolume, 5.0F));
  musicSpeed = std::max(0.5F, std::min(musicSpeed, 2.0F));
  audioOffsetMs = std::max(-500, std::min(audioOffsetMs, 500));
  hitOffsetIndicatorMode = std::max(0, std::min(hitOffsetIndicatorMode, 2));
  // The interface scale is clamped to what the design canvas can absorb: below 0.85 the rows
  // start colliding, above 2.0 a wide list no longer fits its clipped viewport.
  uiScale = std::max(0.85F, std::min(uiScale, 2.0F));
  // Revealing chart detail only makes sense with the automatic demonstration, so the
  // reference turns autoplay on when reveal is enabled.
  if (chartReveal && !autoplay) autoplay = true;
  return true;
}

bool Settings::save(const std::filesystem::path& file) const {
  std::ostringstream stream;
  const auto write = [&stream](const char* key, const std::string& value) {
    stream << key << '=' << value << '\n';
  };
  write("keyScale", toString(keyScale));
  write("uiScale", toString(uiScale));
  write("backgroundDim", toString(backgroundDim));
  write("aspectRatio", toString(aspectRatio));
  write("scrollSpeed", toString(scrollSpeed));
  write("judgeLineWidth", toString(judgeLineWidth));
  write("musicVolume", toString(musicVolume));
  write("soundEffectVolume", toString(soundEffectVolume));
  write("musicSpeed", toString(musicSpeed));
  write("audioOffsetMs", std::to_string(audioOffsetMs));
  write("hitOffsetIndicatorMode", std::to_string(hitOffsetIndicatorMode));
  const auto writeBool = [&write](const char* key, bool value) {
    write(key, value ? "1" : "0");
  };
  writeBool("apfcIndicator", apfcIndicator);
  writeBool("judgementDetailDistribution", judgementDetailDistribution);
  writeBool("realtimeAccuracy", realtimeAccuracy);
  writeBool("scoreJumpEffect", scoreJumpEffect);
  writeBool("multiPressHighlight", multiPressHighlight);
  writeBool("mirrorX", mirrorX);
  writeBool("autoplay", autoplay);
  writeBool("challengeMode", challengeMode);
  writeBool("practiceMode", practiceMode);
  writeBool("lowResolutionMode", lowResolutionMode);
  writeBool("antialias", antialias);
  writeBool("showFps", showFps);
  writeBool("showDebugInfo", showDebugInfo);
  writeBool("disableChartShader", disableChartShader);
  writeBool("chartReveal", chartReveal);
  writeBool("storyMode", storyMode);
  writeBool("replayEnabled", replayEnabled);
  write("selectedSkin", selectedSkin);
  return platform::writeTextFile(file, stream.str());
}

} // namespace rechigros::app
