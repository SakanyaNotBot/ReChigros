#pragma once

#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/OfficialBlock.hpp"
#include "rechigros/game/PrprEffect.hpp"
#include "rechigros/game/ClickEffectItem.hpp"
#include "rechigros/game/OfficialLevelEffects.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game {
class Chart final {
public:
  double offset{};
  int formatVersion{}, rpeVersion{160};
  std::shared_ptr<BpmTimeline> bpmTimeline;
  std::filesystem::path packDir;
  std::vector<PrprEffect> prprEffects;
  std::vector<PrprVideo> prprVideos;
  std::array<int, 8> attachUiLineIndex{-1,-1,-1,-1,-1,-1,-1,-8};
  bool useAttachUiFix{true};
  std::unordered_map<int, std::array<float,6>> uiTransforms;
  std::unordered_map<std::string,std::string> prprShaderSources;
  std::vector<std::unique_ptr<JudgeLine>> judgeLineList;
  std::vector<Note*> allNotesSorted;
  std::vector<ClickEffectItem> clickEffectsSorted;
  // Active red-zone geometry. Empty for every format except official charts that ship
  // blockAreaList; GameplaySession evaluates it to suppress blocked touches.
  std::vector<OfficialBlock> blockAreaList;
  double chartEndTimeSec{};
  // Chart identity used to match a level effect: the normalized chart digest and the
  // display song name. Both are optional; an empty value simply never matches.
  std::string normalizedDigest;
  std::string songName;
  // Level effect selected for this chart, or null when none applies.
  std::shared_ptr<const OfficialLevelEffects> levelEffects;

  void updateUiTransform(int id,float x,float y,float rotation,float sx,float sy,float alpha);
  void prepareRuntime();
  void resetRuntime() noexcept;
};
}
