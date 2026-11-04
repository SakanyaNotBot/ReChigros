#pragma once

#include "rechigros/game/PlayResult.hpp"
#include "rechigros/game/ReplayLog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rechigros::app {

// One recorded touch sample of the legacy replay format. Only files written before the
// judgement-entry model use this shape; their playback still re-synthesises touches, so
// old and imported replays keep working.
struct ReplayTouchEvent {
  enum class Kind : int { Down = 0, Move = 1, Up = 2 };
  Kind kind{Kind::Down};
  std::int64_t pointerId{};
  float x{};
  float y{};
  double timeSec{};
};

struct ReplayMeta {
  std::string chartId;
  std::string songName;
  std::string difficulty;
  int score{};
  double accuracy{};
  int maxCombo{};
  int totalNotes{};
  int perfect{};
  int good{};
  int bad{};
  int miss{};
  // Timing statistics, so a playback can reproduce the settlement screen's histogram and
  // early/late split even when the live run collected nothing.
  int goodEarly{};
  int goodLate{};
  double stdDevMs{};
  std::array<int, game::PlayResult::JUDGEMENT_DETAIL_BIN_COUNT> judgementOffsetBins{};
  bool hasJudgementOffsetBins{};
  std::int64_t timestampMs{};
  // Modes used when the replay was RECORDED. They are display-only on playback: the viewer's
  // own settings decide how the chart is judged and drawn.
  bool autoplay{};
  bool challenge{};
  bool mirrorX{};
  bool practice{};
  float musicSpeed{1.0F};
  float scrollSpeed{1.0F};
  // Port-specific run offsets, kept so a saved replay can describe its run.
  int chartOffsetMs{};
  int audioOffsetMs{};

  [[nodiscard]] game::PlayResult toPlayResult() const;
};

// A replay is either the judgement-entry model (version 2, the default and the shape the
// reference records) or the legacy touch-sample model (version 1, read-only support so old
// and imported files still play).
struct ReplayData {
  int version{2};
  ReplayMeta meta;
  std::vector<game::ReplayJudgement> judgements;
  std::vector<ReplayTouchEvent> entries;

  [[nodiscard]] bool usesJudgementModel() const noexcept { return version >= 2; }
};

// Replay files are JSON documents inside a per-replay subdirectory.
[[nodiscard]] bool writeReplayFile(const std::filesystem::path& file, const ReplayData& data);

// Copies the final result of a run into a replay's metadata, so a saved replay records what
// the run actually scored instead of the values it started with.
void applyResultToReplayMeta(ReplayData& data, const game::PlayResult& result);
[[nodiscard]] std::optional<ReplayData> readReplayFile(const std::filesystem::path& file);

// The settlement result of a replay playback. The recorded statistics are authoritative, but
// the live offset histogram replaces the saved one whenever the live run collected data; an
// empty live histogram falls back to the saved bins.
[[nodiscard]] game::PlayResult mergeReplayPlaybackResult(const ReplayMeta& meta,
    const game::PlayResult& live);

struct ReplayInfo {
  std::string id;
  std::filesystem::path directory;
  std::filesystem::path file;
  ReplayMeta meta;
};

class ReplayStore final {
public:
  explicit ReplayStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

  // Persists the replay and returns the generated identifier. An empty string
  // means the write failed.
  [[nodiscard]] std::string save(const ReplayData& data);

  // Scans the store; returns saved replays newest first.
  [[nodiscard]] std::vector<ReplayInfo> list() const;

  [[nodiscard]] std::optional<ReplayInfo> find(const std::string& id) const;
  bool remove(const std::string& id);

private:
  std::filesystem::path root_;
};

} // namespace rechigros::app
