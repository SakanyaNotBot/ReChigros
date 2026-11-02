#pragma once

#include "rechigros/game/RpeCamera.hpp"
#include "rechigros/render/math/Mat4.hpp"

namespace rechigros::render::scene {

struct StageLayout;

struct RpeCameraProjection {
  bool active{};
  bool followChart{};
  float viewScale{1.0F};
  math::Mat4 matrix{};
  math::Rect worldBounds{-1000000.0F, -1000000.0F, 2000000.0F, 2000000.0F};
};

[[nodiscard]] RpeCameraProjection resolveRpeCamera(const game::RpeCameraFrame& camera,
    const StageLayout& layout) noexcept;

} // namespace rechigros::render::scene
