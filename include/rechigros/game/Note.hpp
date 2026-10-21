#pragma once

#include <limits>
#include <vector>

namespace rechigros::game {
class JudgeLine;

struct Note {
  int type{};
  double time{};
  double holdTime{};
  double speed{1.0};
  double positionX{};
  double rpeGlobalBeat{std::numeric_limits<double>::quiet_NaN()};
  bool isFake{false};
  float size{1.0F};
  float xScale{1.0F};
  float alpha{1.0F};
  float judgeArea{1.0F};
  float visibleTime{-1.0F};
  float yOffset{};
  bool hasTint{false};
  float tintR{1.0F}, tintG{1.0F}, tintB{1.0F};
  bool hasHitEffectTint{false};
  float hitEffectR{1.0F}, hitEffectG{1.0F}, hitEffectB{1.0F};
  bool useOfficialSpeed{false};

  bool isAbove{false};
  bool isHold{false};
  double sect{}, secht{}, holdEndTime{}, holdLength{}, floorPosition{}, holdEndFloorPosition{};
  bool clicked{false};
  int judgeResult{-1};
  double judgeDiffSec{};
  double judgeTimeSec{std::numeric_limits<double>::quiet_NaN()};
  bool preJudge{false}, dragPrimed{false}, holdActive{false}, holdPerfect{false};
  bool holdPreJudge{false}, holdFxActive{false};
  double holdUpTimeSec{std::numeric_limits<double>::infinity()};
  double holdDiffSec{};
  double holdFxAtSec{std::numeric_limits<double>::quiet_NaN()};
  int morebets{};
  JudgeLine* master{nullptr};
  std::vector<Note*> nearNotes;
  long long holdTapTimeMs{};
  int holdStatus{};
  bool holdBroken{false};
  int safeFrame{};
  bool isJudged{false};
  long long badTimeMs{};
  bool scored{false};
  double statOffset{};
  bool offsetIndicatorShown{false};
  bool judgementDetailOffsetRecorded{false};
  int frameCount{};
  int sortedIndex{-1};
  double cachedAppearSec{std::numeric_limits<double>::quiet_NaN()};
  double lastAppearBeforeBeats{std::numeric_limits<double>::quiet_NaN()};
};
} // namespace rechigros::game

      