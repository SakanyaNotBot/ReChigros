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

// One DimensionSlash light-line keyframe: [time, x, y, lineDir, slashDir].
struct LevelEffectSlashKey {
  float time{};
  float x{};
  float y{};
  float lineDir{};
  float slashDir{};
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

  // Selects the effect for a chart from its normalised digest. Returns null when the chart is
  // not whitelisted, or when it is not the Message chart and story mode is off. The result is
  // a per-chart copy because the challenge state it carries advances while the chart plays.
  // `songName` is accepted for reporting only; it never selects an effect.
  static std::shared_ptr<OfficialLevelEffects> forChart(
      const OfficialLevelEffects& table, const std::string& normalizedDigest,
      const std::string& songName, bool storyMode = true);

  // Selects an effect by its song entry name. This exists for diagnostics that drive one
  // effect chain on a synthetic chart; the product selects effects through `forChart`, which
  // is digest-only, because a chart's own metadata can name an official song without being
  // that chart.
  static std::shared_ptr<OfficialLevelEffects> forSongName(
      const OfficialLevelEffects& table, const std::string& songName);

  [[nodiscard]] const std::string& effectName() const noexcept { return effectName_; }
  [[nodiscard]] const std::string& songKey() const noexcept { return songKey_; }
  [[nodiscard]] bool hideHud() const noexcept { return hideHud_; }
  // True when the level effect ducks the audio while a red zone is held.
  [[nodiscard]] bool lowPassOnBlock() const noexcept { return lowPassOnBlock_; }
  [[nodiscard]] bool valid() const noexcept { return !effectName_.empty(); }

  // Evaluates a named AnimationDirector track. Returns 0 when the track is absent.
  [[nodiscard]] float track(const std::string& name, float timeSec) const;
  [[nodiscard]] bool hasTrack(const std::string& name) const;
  [[nodiscard]] const std::vector<LevelEffectKey>* trackKeys(const std::string& name) const;

  // Reads a scalar field of the effect's `scalars` table.
  [[nodiscard]] float scalar(const std::string& field, float fallback) const;
  // Reads a numeric `params` entry of one pass of the effect's `settings` table.
  [[nodiscard]] float settingFloat(const std::string& pass, const std::string& name,
      float fallback) const;

  // Number of DimensionSlash light lines this effect drives (0 when it has none).
  [[nodiscard]] int slashCount() const;
  // Slash `index` (1-based) at `timeSec` written as {x, y, lineDir, slashDir}; false when
  // that slash has no keyframes.
  [[nodiscard]] bool slashAt(int index, float timeSec, std::array<float, 4>& out) const;

  // Builds the storyboard effect list for this level effect, spanning the whole chart so
  // each pass idles to identity through its own zero-valued tracks.
  [[nodiscard]] std::vector<PrprEffect> buildPrprEffects() const;

  // Evaluates an AnimationDirector keyframe list at `timeSec`. Exposed for tests.
  [[nodiscard]] static float evaluateTrack(const std::vector<LevelEffectKey>& keys,
      float timeSec);

  // --- Challenge state (Ametrine / Petrichor level mods) ---

  [[nodiscard]] bool isChallengeFailed() const noexcept { return challengeFailed_; }
  // Resets for a chart restart; the mod prefab is recreated per level.
  void resetChallenge() noexcept;
  // A Bad or Miss inside the authored window fails the Ametrine check; a Miss while a
  // finger holds a red zone fails the Petrichor one.
  void onBadOrMiss(float chartTimeSec, bool isMiss);
  void onBlockStateChanged(bool touching) noexcept { touchingBlock_ = touching; }
  // Advances the Ametrine vignette ramp by a real-time delta.
  void updateAmetrineVignette(float chartTimeSec, float deltaTimeSec);
  [[nodiscard]] float ametrineVignette() const noexcept { return ametrineVignette_; }

private:
  using TrackTable = std::unordered_map<std::string, std::vector<LevelEffectKey>>;
  using SlashTable = std::unordered_map<std::string, std::vector<LevelEffectSlashKey>>;
  using ScalarTable = std::unordered_map<std::string, float>;
  using SettingTable = std::unordered_map<std::string, float>;
  struct SongEntry {
    std::string effect;
    bool hideHud{};
    bool lowPassOnBlock{true};
  };
  struct EffectData {
    TrackTable tracks;
    SlashTable slashes;
    ScalarTable scalars;
    SettingTable settings;
  };

  // Digest to song id, from the shipped whitelist.
  std::unordered_map<std::string, std::string> digestToSong_;
  std::unordered_map<std::string, SongEntry> songs_;
  std::unordered_map<std::string, EffectData> effects_;
  // The selected effect, copied out of the table by `forChart`.
  std::string effectName_;
  std::string songKey_;
  bool hideHud_{};
  bool lowPassOnBlock_{true};
  TrackTable tracks_;
  SlashTable slashes_;
  ScalarTable scalars_;
  SettingTable settings_;

  bool challengeFailed_{};
  bool touchingBlock_{};
  float ametrineVignette_{};
};

} // namespace rechigros::game
