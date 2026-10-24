#pragma once

#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/LineFrame.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <vector>

namespace rechigros::game {
class Chart;
} // namespace rechigros::game

namespace rechigros::render::gl {
class Texture;
} // namespace rechigros::render::gl

namespace rechigros::render::scene {

// Draws the chart-debug overlay: where each judge line sits, and where each of its notes lands on
// the play field, each labelled with its identity. It exists to make an authored chart's geometry
// visible against what the renderer actually draws, so a line or note that lands in the wrong
// place can be seen directly instead of inferred.
//
// Positions are derived with the same formulas the note pass uses, so the markers show where the
// renderer places things rather than a second, independent reading of the chart.
class ChartDebugOverlay final {
public:
  struct Style {
    // Half-size of a marker in stage-relative units. The reference sizes it from the stage
    // height, so the overlay keeps its proportion at any resolution.
    float markerFraction{0.005F};
    // Marker height as a multiple of the marker size.
    float textScale{6.0F};
  };

  // `frames` must hold the per-line states the renderer evaluated for this frame, in the same
  // order as `chart.judgeLineList`. `drawOrder` is the order the line labels number the lines
  // in, which is the order the reference shows.
  static void draw(batch::SpriteBatch& batch, const StageLayout& layout,
      const game::Chart& chart, const std::vector<LineFrame>& frames,
      const std::vector<int>& drawOrder, const Style& style, const gl::Texture& white,
      const math::Mat4& projection);
};

} // namespace rechigros::render::scene
