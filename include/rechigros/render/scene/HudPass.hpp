#pragma once

#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <string>

namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }
namespace rechigros::render::text { class TextRenderer; }

namespace rechigros::render::scene {

// Score/combo/accuracy state mirrored into the HUD widgets.
struct HudValues {
  int score{};
  int combo{};
  int maxCombo{};
  double accuracy{1.0};
  double progress{0.0};
  // Bottom-left and bottom-right captions.
  std::string songName;
  std::string levelName;
  // The accuracy readout is only shown when the setting asks for it.
  bool showAccuracy{true};
};

// Colours used by the HUD, split out so a skin can override them later.
struct HudColors {
  float textR{1.0F}, textG{1.0F}, textB{1.0F}, textA{1.0F};
  float scoreR{1.0F}, scoreG{1.0F}, scoreB{1.0F};
};

// Optional artwork. A null texture falls back to a flat quad so the layout stays
// verifiable without the assets.
struct HudTextures {
  const gl::Texture* pause{};
  const gl::Texture* timerLine{};
};

// Canvas unit in pixels: the HUD is authored on a 1000-unit tall canvas.
[[nodiscard]] float hudUnit(const StageLayout& layout) noexcept;
// HUD x offset in canvas units, measured from the stage centre.
[[nodiscard]] float hudX(const StageLayout& layout, float uiX) noexcept;
// HUD y offset in canvas units, measured upwards from the stage centre.
[[nodiscard]] float hudY(const StageLayout& layout, float uiY) noexcept;

// Draws the in-game HUD: pause button, progress bar, song and level captions,
// score, accuracy and combo. Every position derives from the anchored canvas, so
// the layout follows the play field on any aspect ratio. Text quads are flushed as
// they are emitted because each string owns its own texture; `projection` is the
// frame's matrix.
void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, const HudTextures& textures, float alpha,
    const math::Mat4& projection);

// Rect the HUD occupies, used to decide whether stage clipping is required.
[[nodiscard]] math::Rect hudBounds(const StageLayout& layout) noexcept;

} // namespace rechigros::render::scene
