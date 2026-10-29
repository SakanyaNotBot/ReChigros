#pragma once

#include "rechigros/game/HoldBodyBurst.hpp"
#include "rechigros/render/math/Mat4.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace rechigros::game { class Chart; class Note; }
namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }

namespace rechigros::render::fx {

// Atlas geometry for the hit-effect spritesheet: a row-major grid of equally sized
// frames. The column and row counts come from the resource pack.
struct HitEffectAtlas {
  // Defaults used when the resource pack does not specify a grid.
  int columns{4};
  int rows{4};
  [[nodiscard]] int frames() const noexcept { return columns * rows; }
};

// Judgement colours used by the burst. These are the skin's note colours, not the
// HUD palette, and each judgement carries its own alpha.
struct HitEffectSkin {
  float perfectRed{1.0F};
  float perfectGreen{164.0F / 255.0F};
  float perfectBlue{107.0F / 255.0F};
  float perfectAlpha{1.0F};
  float goodRed{107.0F / 255.0F};
  float goodGreen{188.0F / 255.0F};
  float goodBlue{1.0F};
  float goodAlpha{1.0F};
};

// Tuning values that come from the skin configuration.
struct HitEffectStyle {
  float spriteScale{1.583F};
  float sizeScale{1.0F};
  float duration{0.5F};
  bool rotateSheet{false};
  bool tinted{true};
  bool showParticles{true};
};

// One spawned burst. Positions are already in stage pixels.
struct HitEffectInstance {
  double startSec{};
  float x{};
  float y{};
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  float alpha{1.0F};
  float rotateDeg{};
  std::array<float, 4> dirX{};
  std::array<float, 4> dirY{};
  std::array<float, 4> radius0{};
  std::array<float, 4> speed{};
  int parts{4};
};

// Detects freshly judged notes and keeps the resulting bursts alive until they
// expire. One instance per renderer; `update` must run once per frame.
class HitEffectField {
public:
  void setStyle(const HitEffectStyle& style) noexcept { style_ = style; }
  [[nodiscard]] const HitEffectStyle& style() const noexcept { return style_; }

  void setSkin(const HitEffectSkin& skin) noexcept { skin_ = skin; }
  [[nodiscard]] const HitEffectSkin& skin() const noexcept { return skin_; }

  void setAtlas(const HitEffectAtlas& atlas) noexcept { atlas_ = atlas; }
  [[nodiscard]] const HitEffectAtlas& atlas() const noexcept { return atlas_; }

  // Scans every note for a judgement that appeared since the previous call and
  // spawns a burst at the note head. `chartTimeSec` positions the burst, while
  // `visualTimeSec` drives its animation; `musicSpeed` back-dates a burst to the
  // moment its note was actually judged so a slow frame does not restart it.
  void update(game::Chart& chart, double chartTimeSec, double visualTimeSec,
      float stageX, float stageY, float stageWidth, float stageHeight, float noteWidth,
      float musicSpeed = 1.0F, bool skipJudgedNotes = false);

  void clear() noexcept;

  // Draws the spritesheet frames and the particle debris. `whiteTexture` must be a
  // 1x1 opaque white texture used for the debris quads. `mirrorU` swaps the sheet's
  // horizontal UVs so a mirrored play field does not show mirrored artwork.
  void draw(batch::SpriteBatch& out, const math::Mat4& projection, gl::Texture& sheet,
      const gl::Texture& whiteTexture, double visualTimeSec, bool mirrorU = false) const;

  // Spawns one burst for a held note's body, at the point where that note currently meets
  // its judge line. The gameplay session supplies the bursts (one every half beat while a
  // hold is active), so the timing loop lives there and this only reproduces the look.
  void spawnHoldBody(game::Chart& chart, const game::HoldBodyBurst& burst,
      double chartTimeSec, double visualTimeSec, float stageX, float stageY, float stageWidth,
      float stageHeight, float noteWidth);

  // Spawns the burst for a hold head hit at the position the renderer resolved for the
  // judged frame (where the head was when it was hit, which is above the line for an early
  // hit and on the line once the head has arrived), like the reference head effect.
  void spawnHoldHead(game::Chart& chart, const game::HoldHeadBurst& burst,
      double chartTimeSec, double visualTimeSec, float x, float y, float rotationDeg,
      float noteWidth, float musicSpeed = 1.0F);

  [[nodiscard]] bool empty() const noexcept { return effects_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return effects_.size(); }

private:
  // Last judgement seen per note slot, so a re-armed note fires again.
  struct TrackedNote {
    const void* note{};
    int judgement{-1};
  };

  void spawn(float x, float y, double startSec, float red, float green, float blue, float alpha,
      int parts, float lineRotDeg);
  // Spawns a burst for one note at `startSec`, using the note's current line geometry.
  void spawnBurstForNote(const game::Note& note, int judgement, double chartTimeSec,
      double startSec, float stageX, float stageY, float stageWidth, float stageHeight);

  HitEffectStyle style_{};
  HitEffectSkin skin_{};
  HitEffectAtlas atlas_{};
  std::vector<HitEffectInstance> effects_;
  std::vector<TrackedNote> tracked_;
  std::uint32_t randomState_{0x9E3779B9U};
  // Note width from the most recent update; all burst sizes are relative to it.
  float noteWidth_{1.0F};

  [[nodiscard]] float nextRandom() noexcept;
};

} // namespace rechigros::render::fx
