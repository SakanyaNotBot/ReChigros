#pragma once

#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <array>
#include <string>

namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::text { class TextRenderer; }

namespace rechigros::render::scene {

// Score/combo/accuracy state mirrored into the HUD widgets.
struct HudValues {
  int score{};
  int combo{};
  int maxCombo{};
  double accuracy{1.0};
  double progress{0.0};
};

// Colours used by the HUD, split out so a skin can override them later.
struct HudColors {
  float textR{1.0F}, textG{1.0F}, textB{1.0F}, textA{1.0F};
  float scoreR{1.0F}, scoreG{1.0F}, scoreB{1.0F};
};

// Draws the in-game HUD: pause button, progress bar, score, combo and accuracy.
// Text quads are flushed as they are emitted because each string owns its own
// texture; `projection` is the same matrix the caller uses for the frame.
void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, float alpha,
    const math::Mat4& projection);

} // namespace rechigros::render::scene
