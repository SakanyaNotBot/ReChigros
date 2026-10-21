#pragma once

#include <array>
#include <vector>

namespace rechigros::game { class OfficialBlock; }
namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::math { struct Mat4; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// Draws the official red zones ("block areas"). A zone is a noise-displaced,
// pixel-quantised silhouette with an inner noise field, sparks and a glow, and it looks
// different before and after it becomes active.
class BlockOverlay final {
public:
  // Appearance of one block, chosen by its phase.
  struct Style {
    float fillRed{}, fillGreen{}, fillBlue{}, fillAlpha{};
    float edgeRed{1.0F}, edgeGreen{1.0F}, edgeBlue{1.0F}, edgeAlpha{};
    float sparkRed{}, sparkGreen{}, sparkBlue{};
    bool glow{};
  };

  // Material values of the active and disabled looks.
  static constexpr float ACTIVE_FILL_R = 0.7132075F;
  static constexpr float ACTIVE_FILL_G = 0.235493F;
  static constexpr float ACTIVE_FILL_B = 0.235493F;
  static constexpr float ACTIVE_FILL_A = 0.667F;
  static constexpr float ACTIVE_EDGE_R = 1.0F;
  static constexpr float ACTIVE_EDGE_G = 0.3301886F;
  static constexpr float ACTIVE_EDGE_B = 0.3301886F;
  static constexpr float ACTIVE_EDGE_A = 0.8F;
  static constexpr float DISABLED_FILL_R = 0.497F;
  static constexpr float DISABLED_FILL_G = 0.138F;
  static constexpr float DISABLED_FILL_B = 0.138F;
  static constexpr float DISABLED_FILL_A = 0.4F;
  static constexpr float DISABLED_EDGE_A = 1.0F;
  static constexpr float GLOW_R = 1.0F;
  static constexpr float GLOW_G = 0.1792452F;
  static constexpr float GLOW_B = 0.1792452F;
  static constexpr float GLOW_INTENSITY = 0.8F;
  static constexpr float SPARK_ACTIVE_R = 1.0F;
  static constexpr float SPARK_ACTIVE_G = 0.2849057F;
  static constexpr float SPARK_ACTIVE_B = 0.2849057F;
  static constexpr float SPARK_DISABLED_R = 0.311F;
  static constexpr float SPARK_DISABLED_G = 0.078F;
  static constexpr float SPARK_DISABLED_B = 0.078F;
  // Quantisation step of the silhouette edge, in pixels per authored unit.
  static constexpr float BACKGROUND_PIXEL_SCALE = 6.0F;

  // Deterministic hash for the edge noise, sparks and row displacement.
  [[nodiscard]] static float blockHash(float x, float y, float seed) noexcept;

  // Resolves the look a block uses at `phase` (0 disabled, 1 ready, 2 active).
  [[nodiscard]] static Style styleFor(int phase, bool subtract, float fade) noexcept;

  // Emits one block. Returns the number of quads added.
  int draw(const game::OfficialBlock& block, float chartTimeSec, batch::SpriteBatch& target) const;

  // Advances every block to `chartTimeSec`, then emits the visible ones, flushing when the
  // batch fills. `blocks` may hold more than ten thousand entries, so the flush happens in
  // chunks. `deltaTimeSec` drives the one-shot show animation and must be the real frame
  // delta, clamped by the caller.
  void drawAll(std::vector<game::OfficialBlock>& blocks, float chartTimeSec, float deltaTimeSec,
      float stageX, float stageY, float stageWidth, float stageHeight,
      batch::SpriteBatch& target, const math::Mat4& projection, const gl::Texture& white) const;
};

} // namespace rechigros::render::fx
