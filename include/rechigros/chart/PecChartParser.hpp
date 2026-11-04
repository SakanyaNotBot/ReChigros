#pragma once

#include "rechigros/game/Chart.hpp"

#include <string_view>

namespace rechigros::chart {
class PecChartParser final {
public:
  static game::Chart parse(std::string_view content);
};
}

