#include "rechigros/render/scene/RpeCameraProjection.hpp"

#include "rechigros/render/scene/StageLayout.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::scene {

RpeCameraProjection resolveRpeCamera(const game::RpeCameraFrame& camera,
    const StageLayout& layout) noexcept {
  RpeCameraProjection result;
  result.active = camera.active;
  result.followChart = camera.followChart;
  result.viewScale = layout.stage.height / 900.0F;
  if (!camera.active || !(result.viewScale > 0.0F)) return result;

  constexpr double pi = 3.14159265358979323846;
  const auto axes = camera.basis();
  const auto& right = axes[0];
  const auto& up = axes[1];
  const auto& forward = axes[2];
  const double scale = result.viewScale;
  const double width = layout.viewWidth;
  const double height = layout.viewHeight;
  const double focal = 450.0 / std::tan(camera.fov * pi / 360.0);
  const double horizontal = 2.0 * focal * scale / width;
  const double vertical = 2.0 * focal * scale / height;
  const double a = (game::RPE_CAMERA_FAR + game::RPE_CAMERA_NEAR)
      / (game::RPE_CAMERA_FAR - game::RPE_CAMERA_NEAR);
  const double b = -2.0 * game::RPE_CAMERA_FAR * game::RPE_CAMERA_NEAR
      / (game::RPE_CAMERA_FAR - game::RPE_CAMERA_NEAR);
  const double originX = -width * 0.5 / scale - camera.x;
  const double originY = height * 0.5 / scale - camera.y;
  const double originZ = -camera.z;
  const double u = right[0] * originX + right[1] * originY + right[2] * originZ;
  const double v = up[0] * originX + up[1] * originY + up[2] * originZ;
  const double depth = forward[0] * originX + forward[1] * originY + forward[2] * originZ;
  const auto row = [&result](int index, double x, double y, double z, double w) {
    result.matrix.m[static_cast<std::size_t>(index)] = static_cast<float>(x);
    result.matrix.m[static_cast<std::size_t>(4 + index)] = static_cast<float>(y);
    result.matrix.m[static_cast<std::size_t>(8 + index)] = static_cast<float>(z);
    result.matrix.m[static_cast<std::size_t>(12 + index)] = static_cast<float>(w);
  };
  row(0, horizontal * right[0] / scale, -horizontal * right[1] / scale, 0.0, horizontal * u);
  row(1, vertical * up[0] / scale, -vertical * up[1] / scale, 0.0, vertical * v);
  row(2, a * forward[0] / scale, -a * forward[1] / scale, 0.0, a * depth + b);
  row(3, forward[0] / scale, -forward[1] / scale, 0.0, depth);

  float left = 0.0F, top = 0.0F, rightBound = 0.0F, bottom = 0.0F;
  bool valid = true;
  const std::array<std::array<double, 2>, 4> corners{{
      {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}}};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    const auto point = camera.planeAt(corners[index][0], corners[index][1], width, height, scale);
    if (!point) {
      valid = false;
      break;
    }
    const float x = static_cast<float>(width * 0.5 + (*point)[0] * scale);
    const float y = static_cast<float>(height * 0.5 - (*point)[1] * scale);
    if (index == 0) {
      left = rightBound = x;
      top = bottom = y;
    } else {
      left = std::min(left, x);
      rightBound = std::max(rightBound, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  if (valid) result.worldBounds = {left, top, rightBound - left, bottom - top};
  result.active = std::all_of(result.matrix.m.begin(), result.matrix.m.end(),
      [](float value) { return std::isfinite(value); });
  return result;
}

} // namespace rechigros::render::scene
