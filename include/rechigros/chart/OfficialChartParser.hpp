#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string_view>

namespace rechigros::chart {
class OfficialChartParser final {
public:
  static game::Chart parse(const std::filesystem::path& file);
  static game::Chart parse(std::string_view jsonText);
};
}

