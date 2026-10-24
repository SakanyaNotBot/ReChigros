#include "rechigros/render/fx/ClickEffectField.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/ClickEffectItem.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/fx/HitEffect.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::fx {
namespace {
constexpr float kPi = 3.14159265358979323846F;
// Simpson step count of the travel integral.
constexpr int kTravelSteps = 24;
// Burst geometry relative to one note width.
constexpr float kPrefabToNoteWidth = 1.35F / 2.1758F;
constexpr float kParticleSize = 0.3F * kPrefabToNoteWidth;
// Drag constant of the fitted particle-speed law.
constexpr float kDragK = 2.657F;
constexpr float kParticleLifetime = 0.5F;

float clamp01(float value) {
  if (!(value > 0.0F)) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}
} // namespace

float ClickEffectField::dragIntegral(float u) noexcept {
  if (u <= 0.0F) return 0.0F;
  if (u > 1.0F) u = 1.0F;
  return ((0.05F * u - 0.4666667F) * u + 1.1F) * u * u;
}

float ClickEffectField::particleTravel(float speed, float p) noexcept {
  const float step = p / static_cast<float>(kTravelSteps);
  float sum = 0.0F;
  for (int index = 0; index <= kTravelSteps; ++index) {
    const float u = static_cast<float>(index) * step;
    const float weight = (index == 0 || index == kTravelSteps)
        ? 1.0F
        : (((index & 1) == 1) ? 4.0F : 2.0F);
    const float speedNow = 1.0F / speed + kDragK * dragIntegral(u);
    if (speedNow <= 1.0e-6F) continue;
    sum += weight / speedNow;
  }
  return sum * step / 3.0F * kParticleLifetime;
}

float ClickEffectField::particleSize(float p) noexcept {
  return ((0.2078F * p - 1.6524F) * p + 1.6399F) * p + 0.4988F;
}

void ClickEffectField::draw(game::Chart& chart, batch::SpriteBatch& out,
    const math::Mat4& projection, gl::Texture& sheet, const gl::Texture& whiteTexture,
    double chartTimeSec, double visualTimeSec, float stageX, float stageY, float stageWidth,
    float stageHeight, float noteWidth, const HitEffectAtlas& atlas, const HitEffectStyle& style,
    const HitEffectSkin& skin, bool mirrorU) {
  auto& effects = chart.clickEffectsSorted;
  if (effects.empty() || !sheet.valid()) return;

  const int frames = std::max(1, atlas.frames());
  const int columns = std::max(1, atlas.columns);
  const int rows = std::max(1, atlas.rows);
  const double duration = std::max(1.0e-3F, style.duration);
  const float effectSize = noteWidth * style.spriteScale * style.sizeScale;
  const float stageAspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : 16.0F / 9.0F;

  // Entries are time-sorted, so the ones whose animation has finished form a prefix.
  // A burst that never started keeps the prefix pinned until it is reached.
  while (cursor_ < static_cast<int>(effects.size())) {
    const game::ClickEffectItem& item = effects[static_cast<std::size_t>(cursor_)];
    if (item.timeSec > chartTimeSec) break;
    if (!item.animStartCached) break;
    if ((visualTimeSec - item.animStartSec) <= duration) break;
    ++cursor_;
  }

  // Spritesheet frames. One flush per burst keeps the drawn order identical to the list.
  for (int index = cursor_; index < static_cast<int>(effects.size()); ++index) {
    game::ClickEffectItem& item = effects[static_cast<std::size_t>(index)];
    if (item.timeSec > chartTimeSec) break;
    if (!item.animStartCached) {
      item.animStartCached = true;
      item.animStartSec = static_cast<float>(visualTimeSec);
    }
    const double elapsed = visualTimeSec - item.animStartSec;
    if (elapsed < 0.0 || elapsed > duration) continue;
    if (item.note == nullptr || item.note->master == nullptr) continue;

    const float progress = clamp01(static_cast<float>(elapsed / duration));
    int frame = static_cast<int>(std::floor(progress * static_cast<float>(frames)));
    frame = std::clamp(frame, 0, frames - 1);
    const int column = frame % columns;
    const int row = frame / columns;
    const float halfU = 0.5F / static_cast<float>(sheet.width());
    const float halfV = 0.5F / static_cast<float>(sheet.height());
    const float u0 = static_cast<float>(column) / static_cast<float>(columns) + halfU;
    const float u1 = static_cast<float>(column + 1) / static_cast<float>(columns) - halfU;
    const float v0 = static_cast<float>(row) / static_cast<float>(rows) + halfV;
    const float v1 = static_cast<float>(row + 1) / static_cast<float>(rows) - halfV;

    if (!item.positionCached) {
      const game::Note& note = *item.note;
      const auto state = note.master->fillState(item.timeSec, stageAspect);
      const float lineX = stageX + state.xNorm * stageWidth;
      const float lineY = stageY + state.yNorm * stageHeight;
      const double radians = static_cast<double>(state.rotateDeg) * kPi / 180.0;
      const float cosLine = static_cast<float>(std::cos(radians));
      const float sinLine = static_cast<float>(std::sin(radians));
      item.cachedLineRotDeg = state.rotateDeg + (note.isAbove ? 0.0F : 180.0F);
      const float offsetX = static_cast<float>(note.positionX * game::PGR_WIDTH * stageWidth);
      const float transYPx = static_cast<float>(note.yOffset * stageHeight);
      const float dirX = note.isAbove ? sinLine : -sinLine;
      const float dirY = note.isAbove ? -cosLine : cosLine;
      item.cachedScreenX = lineX + offsetX * cosLine + transYPx * dirX;
      item.cachedScreenY = lineY + offsetX * sinLine + transYPx * dirY;
      item.positionCached = true;
    }

    // A hold keeps the perfect palette once its head was hit perfectly.
    const bool perfectPalette = !item.note->isHold || item.note->holdPerfect;
    const float baseR = perfectPalette ? skin.perfectRed : skin.goodRed;
    const float baseG = perfectPalette ? skin.perfectGreen : skin.goodGreen;
    const float baseB = perfectPalette ? skin.perfectBlue : skin.goodBlue;
    const float alpha = perfectPalette ? skin.perfectAlpha : skin.goodAlpha;
    // A per-note tint replaces the palette entirely.
    const bool forceTint = item.note->hasHitEffectTint;
    const float tintR = (style.tinted || forceTint)
        ? (forceTint ? item.note->hitEffectR : baseR) : 1.0F;
    const float tintG = (style.tinted || forceTint)
        ? (forceTint ? item.note->hitEffectG : baseG) : 1.0F;
    const float tintB = (style.tinted || forceTint)
        ? (forceTint ? item.note->hitEffectB : baseB) : 1.0F;
    const float rotation = style.rotateSheet ? item.cachedLineRotDeg : 0.0F;

    // The sheet is uploaded top-down, so the mirror is undone in the horizontal axis only.
    out.begin();
    out.addQuad(item.cachedScreenX, item.cachedScreenY, effectSize, effectSize, rotation, tintR,
        tintG, tintB, alpha, mirrorU ? u1 : u0, v0, mirrorU ? u0 : u1, v1);
    out.flush(projection, sheet, nullptr);
  }

  if (!style.showParticles) return;
  const float worldPx = noteWidth * kPrefabToNoteWidth;
  const float baseSize = noteWidth * kParticleSize * style.sizeScale;
  out.begin();
  for (int index = cursor_; index < static_cast<int>(effects.size()); ++index) {
    const game::ClickEffectItem& item = effects[static_cast<std::size_t>(index)];
    if (item.timeSec > chartTimeSec) break;
    if (!item.animStartCached || !item.positionCached) continue;
    const double elapsed = visualTimeSec - item.animStartSec;
    if (elapsed < 0.0 || elapsed > duration) continue;
    if (item.note == nullptr) continue;

    const float progress = clamp01(static_cast<float>(elapsed / duration));
    const float alpha = 1.0F - progress;
    const float size = baseSize * particleSize(progress);
    const bool perfectPalette = !item.note->isHold || item.note->holdPerfect;
    const float red = item.note->hasHitEffectTint
        ? item.note->hitEffectR : (perfectPalette ? skin.perfectRed : skin.goodRed);
    const float green = item.note->hasHitEffectTint
        ? item.note->hitEffectG : (perfectPalette ? skin.perfectGreen : skin.goodGreen);
    const float blue = item.note->hasHitEffectTint
        ? item.note->hitEffectB : (perfectPalette ? skin.perfectBlue : skin.goodBlue);

    for (int part = 0; part < item.numOfParts; ++part) {
      const auto slot = static_cast<std::size_t>(part);
      const float travel = particleTravel(item.effectSpeed[slot], progress);
      const float radius = (item.effectR0[slot] + travel) * worldPx * style.sizeScale;
      out.addQuad(item.cachedScreenX + radius * item.effectDirX[slot],
          item.cachedScreenY + radius * item.effectDirY[slot], size, size, 0.0F, red, green, blue,
          alpha, 0.0F, 0.0F, 1.0F, 1.0F);
    }
  }
  if (!out.empty()) out.flush(projection, whiteTexture, nullptr);
}

} // namespace rechigros::render::fx
