#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string_view>

namespace rechigros::chart {
class RpeChartParser final {
public:
  static game::Chart parse(const std::filesystem::path& path);
  static game::Chart parse(std::string_view text,
      const std::filesystem::path& baseDirectory = {});
};
}
