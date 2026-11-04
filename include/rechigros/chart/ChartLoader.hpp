#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string>
#include <memory>

namespace rechigros::chart {
class ChartLoader final {
public:
  // Loads `path` and applies the companion files of its pack. `assetRoot` locates the
  // shared level-effect table; an empty value skips level-effect matching. Level effects are
  // selected from the chart's normalised digest, and with `storyMode` off only the Message
  // chart keeps its effect, so the caller passes the setting it is about to play under.
  // `songName` is the display name the caller already knows from the pack metadata.
  static std::unique_ptr<game::Chart> loadFromFile(const std::filesystem::path& path,
      const std::filesystem::path& assetRoot = {}, const std::string& songName = {},
      bool storyMode = true);
};
}
