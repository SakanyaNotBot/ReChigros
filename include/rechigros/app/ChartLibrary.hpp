#pragma once

#include "rechigros/app/InfoFile.hpp"
#include "rechigros/game/PlayResult.hpp"

#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::app {

// One installed chart pack. `id` is the directory name beneath the packs root.
struct ChartRecord {
  std::string id;
  std::filesystem::path rootDir;
  std::filesystem::path chartFile;
  std::filesystem::path musicFile;
  std::filesystem::path bgFile;
  std::filesystem::path infoFile;
  ChartInfo info;
  std::string songName;
  std::string difficulty;
  int chartOffsetMs{};
  int bestScore{};
  // Best accuracy scaled by 10000 (987600 = 98.76%), or -1 when never played.
  int bestAcc{-1};
  std::int64_t lastPlayedAtMs{};
  std::int64_t modifiedAtMs{};

  [[nodiscard]] bool complete() const noexcept;
  // Grade index used by the grade artwork: 0 F, 1 C, 2 B, 3 A, 4 S, 5 V, 6 FC, 7 AP.
  [[nodiscard]] int gradeIndex() const noexcept;
  [[nodiscard]] int bestGradeIndex() const noexcept;
  [[nodiscard]] const char* gradeName() const noexcept;
  [[nodiscard]] const char* bestGradeName() const noexcept;
};

class ChartLibrary final {
public:
  ChartLibrary(std::filesystem::path packsRoot, std::filesystem::path scoreFile);

  [[nodiscard]] const std::filesystem::path& packsRoot() const noexcept { return packsRoot_; }
  [[nodiscard]] const std::filesystem::path& scoreFile() const noexcept { return scoreFile_; }

  // Rebuilds the index when needed and returns the sorted records.
  [[nodiscard]] const std::vector<ChartRecord>& records(bool force = false);
  [[nodiscard]] std::optional<ChartRecord> find(const std::string& id);
  [[nodiscard]] std::optional<ChartRecord> loadRecord(const std::string& id);
  [[nodiscard]] std::string findIdForPath(const std::filesystem::path& path);

  struct ImportResult {
    ChartRecord record;
    bool imported{};
  };
  // Extracts and installs one package. Throws std::runtime_error on failure;
  // `requireComplete` rejects packs without chart, audio or illustration.
  [[nodiscard]] ImportResult importZip(const std::filesystem::path& archive,
      bool requireComplete = true);
  // Installs a pack that already exists as an unpacked directory.
  [[nodiscard]] ImportResult importDirectory(const std::filesystem::path& directory);

  bool remove(const std::vector<std::string>& ids);
  bool updateInfo(const std::string& id, const std::string& name, const std::string& difficulty,
      int chartOffsetMs);

  void recordBestResult(const std::string& id, const game::PlayResult& result);
  void recordPlayTime(const std::string& id, std::int64_t timestampMs);

  void invalidate() noexcept { loaded_ = false; }

  // Warm the index on a worker thread. The first screen does not need the pack list, and
  // the scan reads and parses every package, so running it before the first frame turns
  // launch into a multi-second black screen. Callers that need the records still go
  // through records(), which waits for this warm-up when it is already running.
  void warmUpAsync();

private:
  struct ScoreEntry {
    int bestScore{};
    int bestAcc{-1};
    std::int64_t lastPlayedAtMs{};
  };

  void rebuildIndex();
  [[nodiscard]] std::optional<ChartRecord> loadRecordInternal(const std::string& id);
  [[nodiscard]] ImportResult installDirectory(const std::filesystem::path& temporary,
      const std::string& id, const std::string& fallbackName, bool requireComplete);
  void loadScores();
  void saveScores() const;
  [[nodiscard]] ScoreEntry scoreFor(const std::string& id) const;
  void sortRecords();

  std::filesystem::path packsRoot_;
  std::filesystem::path scoreFile_;
  std::vector<ChartRecord> records_;
  std::unordered_map<std::string, ScoreEntry> scores_;
  bool loaded_{};
  bool scoresLoaded_{};
  // In-flight warm-up, joined by records() before it touches the index.
  std::shared_ptr<std::shared_future<void>> warmUp_;
  std::mutex indexMutex_;
};

// Formats one score with the fixed seven digit width used throughout the UI.
[[nodiscard]] std::string formatScore(int score);
// Formats an accuracy fraction as "98.76%".
[[nodiscard]] std::string formatAccuracy(double accuracy);
// Maps a run result to the grade artwork index.
[[nodiscard]] int gradeIndexForScore(int score, bool fullCombo);

// Sidecar discovery for a chart file that is played outside the library (command line, drag
// and drop, batch captures). Only regular files that sit directly in `directory` are
// considered: those runs have no pack root, so a neighbouring directory's metadata or
// artwork must never be mistaken for this chart's own. The pack path in `detectPack` stays
// recursive, matching the reference's pack layout search.
[[nodiscard]] std::filesystem::path findSidecarInfoFile(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path findSidecarMusicFile(const std::filesystem::path& directory);
[[nodiscard]] std::filesystem::path findSidecarImageFile(const std::filesystem::path& directory);

} // namespace rechigros::app
