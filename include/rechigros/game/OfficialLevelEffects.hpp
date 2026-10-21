#pragma once

#include "rechigros/game/PrprEffect.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {

// One AnimationDirector keyframe: [time, value, stepping, endValue, startValue, ease].
struct LevelEffectKey {
  float time{};
  float value{};
  bool stepping{};
  float endValue{};
  float startValue{};
  int easeType{};
  // AnimationCurve weights, only meaningful when `easeType` is 15.
  std::array<float, 4> curveWeights{};
};

// Per-song level effect driven by `official_effects/chapter9_phase2.json`.
//
// Every track is an AnimationDirector curve evaluated with the official ease table, and
// `buildPrprEffects` turns the effect's pass list into the storyboard chain the renderer
// consumes. A chart is matched to an effect by its normalized chart digest (the shipped
// whitelist) or, failing that, by its song name.
class OfficialLevelEffects final {
public:
  // Loads the shared JSON file. Returns null when it is missing or unreadable.
  static std::shared_ptr<const OfficialLevelEffects> load(
      const std::filesystem::path& assetRoot);

  // Selects the effect for a chart. Returns null when no effect applies.
  static std::shared_ptr<const OfficialLevelEffects> forChart(
      const OfficialLevelEffects& table, const std::string& normalizedDigest,
      const std::string& songName);

  [[nodiscard]] const std::string& effectName() const noexcept { return effectName_; }
  [[nodiscard]] bool hideHud() const noexcept { return hideHud_; }
  [[nodiscard]] bool valid() const noexcept { return !effectName_.empty(); }

  // Evaluates a named AnimationDirector track. Returns 0 when the track is absent.
  [[nodiscard]] float track(const std::string& name, float timeSec) const;
  [[nodiscard]] bool hasTrack(const std::string& name) const;

  // Reads a scalar field of the effect's `scalars` table.
  [[nodiscard]] float scalar(const std::string& field, float fallback) const;
  // Reads a numeric setting of the effect's `settings` table, addressed as
  // "<pass>/<name>".
  [[nodiscard]] float settingFloat(const std::string& pass, const std::string& name,
      float fallback) const;

  // Builds the storyboard effect list for this level effect, spanning the whole chart so
  // each pass idles to identity through its own zero-valued tracks.
  [[nodiscard]] std::vector<PrprEffect> buildPrprEffects() const;

  // Evaluates an AnimationDirector keyframe list at `timeSec`. Exposed for tests.
  [[nodiscard]] static float evaluateTrack(const std::vector<LevelEffectKey>& keys,
      float timeSec);

private:
  using TrackTable = std::unordered_map<std::string, std::vector<LevelEffectKey>>;
  using ScalarTable = std::unordered_map<std::string, float>;
  using SettingTable = std::unordered_map<std::string, float>;
  struct EffectData {
    TrackTable tracks;
    ScalarTable scalars;
    SettingTable settings;
    bool hideHud{};
  };

  // Identity to effect name, from the shipped whitelist.
  std::unordered_map<std::string, std::string> digestToEffect_;
  std::unordered_map<std::string, std::string> songToEffect_;
  std::unordered_map<std::string, EffectData> effects_;
  // The selected effect, copied out of the table by `forChart`.
  std::string effectName_;
  bool hideHud_{};
  TrackTable tracks_;
  ScalarTable scalars_;
  SettingTable settings_;
};

} // namespace rechigros::game
