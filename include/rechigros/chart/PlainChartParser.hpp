#pragma once

#include "rechigros/game/Chart.hpp"

#include <filesystem>
#include <string_view>

namespace rechigros::chart {

// Parser for the plain chart schema: a JSON object whose fields are bound by name.
//
// This is the shape an editor writes when it does not stamp the official header, so the
// object may carry any subset of the model's fields. Two consequences are deliberate and
// match the reference: an absent `formatVersion` stays 0, which makes the runtime take the
// non-official hold geometry branch, and no official-parser coercion runs (no bpm clamp, no
// isCover/invertRotation overrides, no coordinate re-packing, no block areas). Unknown
// fields are ignored rather than rejected.
class PlainChartParser final {
public:
  static game::Chart parse(std::string_view json);
  static game::Chart parse(const std::filesystem::path& path);
};

} // namespace rechigros::chart
