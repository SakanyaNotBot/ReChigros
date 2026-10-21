#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string>
#include <memory>

namespace rechigros::chart {
class ChartLoader final {
public:
  // Loads `path` and applies the companion files of its pack. `assetRoot` locates the
  // shared level-effect table; an empty value skips level-effect matching. `songName` is
  // the display name the caller already knows from the chart's metadata, used to match a
  // level effect when the chart file itself does not identify one.
  static std::unique_ptr<game::Chart> loadFromFile(const std::filesystem::path& path,
      const std::filesystem::path& assetRoot = {}, const std::string& songName = {});
};
}
