#pragma once

#include <filesystem>
#include <string>

namespace rechigros::app {

// Metadata stored beside a chart pack. Empty strings and unset flags mean "not
// declared here"; the caller falls back to values embedded in the chart file.
struct ChartInfo {
  std::string name;
  std::string chart;
  std::string music;
  std::string illustration;
  std::string level;
  std::string difficulty;
  std::string composer;
  std::string charter;
  std::string illustrator;
  int chartOffsetMs{};
  bool hasChartOffset{};
  bool useAttachUiFix{};
  bool hasUseAttachUiFix{};
};

// Reads a JSON, YAML-like, key/value text or CSV metadata file. Returns an empty
// record when the file is missing, unreadable or not understood.
[[nodiscard]] ChartInfo readInfoFile(const std::filesystem::path& file);

// Reads the in-chart META block of a chart file back into a record.
[[nodiscard]] ChartInfo readInfoFromChartFile(const std::filesystem::path& chartFile);

// Builds the display label shown on the chart card, e.g. "IN Lv.15".
[[nodiscard]] std::string buildDifficultyLabel(const ChartInfo& info,
    const std::filesystem::path& chartFile);

// Collapses runs of ASCII spaces and replaces remaining spaces with en spaces.
[[nodiscard]] std::string normalizeDifficultyText(const std::string& text);

// Derives EZ/HD/IN/AT from the chart file name, or an empty string.
[[nodiscard]] std::string inferDifficultyPrefix(const std::filesystem::path& chartFile);

// Writes the canonical metadata file with a stable field order.
bool writeInfoJson(const std::filesystem::path& file, const ChartInfo& info);

[[nodiscard]] bool isChartCandidate(const std::filesystem::path& file);
[[nodiscard]] bool isAudioCandidate(const std::filesystem::path& file);
[[nodiscard]] bool isImageCandidate(const std::filesystem::path& file);

} // namespace rechigros::app
