#pragma once

#include "rechigros/game/GameConstants.hpp"
#include "rechigros/render/fx/HitEffect.hpp"
#include "rechigros/render/math/Mat4.hpp"

#include <vector>

namespace rechigros::game { class Chart; }
namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// The autoplay hit burst.
//
// The debris of one hit is rolled once while the chart is prepared and stored on the
// chart's click-effect list, so an autoplay run and a replay of it show the same burst.
// That is a different source from the manual-hit bursts that `HitEffectField` spawns at
// judgement time, and the two never run together: autoplay draws these, manual play draws
// those.
class ClickEffectField final {
public:
  // Integral of the debris drag curve over 0..u, in prefab units.
  [[nodiscard]] static float dragIntegral(float u) noexcept;
  // Radial distance one particle has reached at animation progress `p`, in prefab units:
  // the drag law integrated with Simpson's rule, because the fitted curve has no closed
  // form.
  [[nodiscard]] static float particleTravel(float speed, float p) noexcept;
  // Size envelope of the debris: it swells, then shrinks away.
  [[nodiscard]] static float particleSize(float p) noexcept;

  // Draws every live burst. `musicSpeed` back-dates a burst so a slow frame does not
  // restart it, and `musicSpeed` is also the practice speed.
  void draw(game::Chart& chart, batch::SpriteBatch& out, const math::Mat4& projection,
      gl::Texture& sheet, const gl::Texture& whiteTexture, double chartTimeSec,
      double visualTimeSec, float stageX, float stageY, float stageWidth, float stageHeight,
      float noteWidth, const HitEffectAtlas& atlas, const HitEffectStyle& style,
      const HitEffectSkin& skin, bool mirrorU);

  // Drops the bookkeeping so a seek or a restart replays the bursts from scratch.
  void reset() noexcept { cursor_ = 0; }

private:
  // Index of the first burst that can still be on screen. Entries are time-sorted.
  int cursor_{};
};

} // namespace rechigros::render::fx
