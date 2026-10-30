#pragma once

#include <limits>
#include <string>
#include <vector>

namespace rechigros::game {
class JudgeLine;
struct Note;

class NearNoteRange {
public:
  class Iterator {
  public:
    Note* operator*() const { return (*group_)[index_]; }
    Iterator& operator++() {
      ++index_;
      skip();
      return *this;
    }
    bool operator==(const Iterator& other) const noexcept {
      return group_ == other.group_ && index_ == other.index_;
    }

  private:
    friend class NearNoteRange;
    const std::vector<Note*>* group_{};
    std::size_t ownerIndex_{};
    std::size_t index_{};

    void skip();
  };

  [[nodiscard]] Iterator begin() const;
  [[nodiscard]] Iterator end() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const { return size() == 0; }
  void clear() noexcept { group_ = nullptr; ownerIndex_ = 0; }

private:
  friend class Chart;
  const std::vector<Note*>* group_{};
  std::size_t ownerIndex_{};
};

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
  std::string hitSound;
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
  // Real seconds accumulated since the last hold-body burst. The reference resets it every
  // frame while the hold has not started and again right after a burst, which is what makes
  // the bursts travel with a hold that slides along its judge line.
  double holdFxTimerSec{};
  int morebets{};
  JudgeLine* master{nullptr};
  NearNoteRange nearNotes;
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

      