#pragma once

#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/LineFrame.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <string>
#include <vector>

namespace rechigros::game { class Chart; class JudgeLine; }
namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::scene {

// Judge-line geometry in units of the stage height / stage width.
inline constexpr float LINE_WIDTH_OFFICIAL = 0.0075F;
inline constexpr float LINE_LENGTH_OFFICIAL = 2.88F;
inline constexpr float LINE_LENGTH_EDITOR = 4000.0F / 1350.0F / 2.0F;
inline constexpr float LINE_WIDTH_EDITOR = 1.0F / 180.0F;

// Colour and alpha assigned to plain judge lines; the AP/FC indicator reuses it.
struct LineTint {
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
};

// Per-frame inputs for the line pass that are not part of the chart itself.
struct LinePassParams {
  double chartTimeSec{};
  bool officialFormat{false};
  bool revealHiddenObjects{false};
  float revealFloorAlpha{};
  float frameGlobalAlpha{1.0F};
  // Extra width multiplier applied to the line thickness.
  float thickness{1.0F};
  // Screen-space X scale used by the outro collapse.
  float outroScaleX{1.0F};
  // Indices of textures and line text resolved by the caller.
  const std::vector<gl::Texture*>* fileTextures{};
};

// Fills one LineFrame per chart line for the current time.
void evaluateLines(const game::Chart& chart, const StageLayout& layout, double chartTimeSec,
    float scrollSpeed, bool blockNotes, bool revealHiddenObjects, float revealScale,
    const math::Rect& worldBounds, float visibleRangePad, LinePassParams params,
    std::vector<LineFrame>& out);

// Squared distance used to decide whether a line's decorated sprite is on screen.
[[nodiscard]] bool lineSpriteVisible(const math::Rect& stage, float centerX, float centerY,
    float width, float height, float rotationDeg) noexcept;

// Draws one plain judge line as a rotated quad.
void drawPlainLine(batch::SpriteBatch& batch, float centerX, float centerY, float halfLength,
    float width, float rotationDeg, const LineTint& tint, float alpha) noexcept;

// Draws the decorative intro line that grows from the stage centre.
void drawIntroLine(batch::SpriteBatch& batch, const StageLayout& layout, bool officialFormat,
    float scale, float thickness, const LineTint& tint) noexcept;

} // namespace rechigros::render::scene
