#pragma once

#include <array>
#include <cmath>

namespace rechigros::render::math {

// Column-major 4x4 matrix, matching the layout the GL uniform upload expects.
struct Mat4 {
  std::array<float, 16> m{1.0F, 0.0F, 0.0F, 0.0F,
                          0.0F, 1.0F, 0.0F, 0.0F,
                          0.0F, 0.0F, 1.0F, 0.0F,
                          0.0F, 0.0F, 0.0F, 1.0F};

  [[nodiscard]] const float* data() const noexcept { return m.data(); }

  static Mat4 identity() noexcept { return {}; }

  // Screen-space orthographic projection with the origin at the top-left corner
  // and y growing downwards, so chart geometry can be authored in pixels.
  static Mat4 orthoTopLeft(float width, float height) noexcept {
    Mat4 out;
    out.m[0] = 2.0F / width;
    out.m[5] = -2.0F / height;
    out.m[10] = -1.0F;
    out.m[12] = -1.0F;
    out.m[13] = 1.0F;
    out.m[14] = 0.0F;
    out.m[15] = 1.0F;
    return out;
  }

  static Mat4 translation(float x, float y) noexcept {
    Mat4 out;
    out.m[12] = x;
    out.m[13] = y;
    return out;
  }

  static Mat4 scaling(float x, float y) noexcept {
    Mat4 out;
    out.m[0] = x;
    out.m[5] = y;
    return out;
  }

  // Rotation about +Z; a positive angle turns clockwise on screen because y points down.
  static Mat4 rotationDegrees(float degrees) noexcept {
    const double radians = static_cast<double>(degrees) * 3.14159265358979323846 / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    Mat4 out;
    out.m[0] = c;
    out.m[1] = s;
    out.m[4] = -s;
    out.m[5] = c;
    return out;
  }

  // Reflection about the vertical line x = centerX.
  static Mat4 mirrorX(float centerX) noexcept {
    Mat4 out;
    out.m[0] = -1.0F;
    out.m[12] = 2.0F * centerX;
    return out;
  }

  [[nodiscard]] Mat4 operator*(const Mat4& rhs) const noexcept {
    Mat4 out;
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        float sum = 0.0F;
        for (int k = 0; k < 4; ++k) {
          sum += m[k * 4 + row] * rhs.m[column * 4 + k];
        }
        out.m[column * 4 + row] = sum;
      }
    }
    return out;
  }
};

// Axis-aligned rectangle in screen pixels.
struct Rect {
  float x{}, y{}, width{}, height{};

  [[nodiscard]] float left() const noexcept { return x; }
  [[nodiscard]] float top() const noexcept { return y; }
  [[nodiscard]] float right() const noexcept { return x + width; }
  [[nodiscard]] float bottom() const noexcept { return y + height; }
  [[nodiscard]] bool empty() const noexcept { return width <= 0.0F || height <= 0.0F; }
};

} // namespace rechigros::render::math
