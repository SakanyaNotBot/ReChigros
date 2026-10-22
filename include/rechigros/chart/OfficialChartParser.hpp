#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string_view>

namespace rechigros::chart {
class OfficialChartParser final {
public:
  static game::Chart parse(const std::filesystem::path& file);
  static game::Chart parse(std::string_view jsonText);

  // Large official charts are dominated by the judge-line and block-area arrays: a
  // chapter-level chart can carry a 16 MiB document whose remaining fields are tiny. This
  // entry point slices those two arrays out of the raw text, parses the (small) remainder,
  // then parses and converts the array elements on a worker pool, preserving the authored
  // order. `workerCount` of zero picks a count from the hardware; a value of one forces the
  // sequential path. Any structural surprise falls back to the regular parser, so the
  // result is always identical to parse().
  static game::Chart parseParallel(std::string_view jsonText, unsigned workerCount = 0);
};
}
