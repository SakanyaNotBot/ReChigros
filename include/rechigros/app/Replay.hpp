#pragma once

#include "rechigros/game/PlayResult.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rechigros::app {

// One recorded touch sample. Timestamps use the chart clock; positions are
// normalised against the play field so a replay survives a different window size.
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
  std::int64_t timestampMs{};
  bool autoplay{};
  bool challenge{};
  bool mirrorX{};
  bool practice{};
  float musicSpeed{1.0F};
  float scrollSpeed{1.0F};
  int chartOffsetMs{};
  int audioOffsetMs{};
};

struct ReplayData {
  ReplayMeta meta;
  std::vector<ReplayTouchEvent> entries;
};

// Replay files are JSON documents inside a per-replay subdirectory.
[[nodiscard]] bool writeReplayFile(const std::filesystem::path& file, const ReplayData& data);

// Copies the final result of a run into a replay's metadata, so a saved replay records what
// the run actually scored instead of the values it started with.
void applyResultToReplayMeta(ReplayData& data, const game::PlayResult& result);
[[nodiscard]] std::optional<ReplayData> readReplayFile(const std::filesystem::path& file);

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
