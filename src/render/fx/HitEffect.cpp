#include "rechigros/render/fx/HitEffect.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::fx {
namespace {

// Burst geometry, all relative to one note width.
constexpr float kPrefabToNoteWidth = 1.35F / 2.1758F;
constexpr float kParticleSize = 0.3F * kPrefabToNoteWidth;
// Debris spawn ring and speed range.
constexpr float kSpawnRadius = 0.2F;
constexpr float kSpawnThickness = 0.2F;
constexpr float kSpeedMin = 15.0F;
constexpr float kSpeedMax = 35.0F;
// Drag constant of the fitted particle-speed law.
constexpr float kDragK = 2.657F;
constexpr float kParticleLifetime = 0.5F;
// Simpson step count for the analytical particle travel.
constexpr int kTravelSteps = 24;
constexpr int kMaxLiveEffects = 512;
constexpr float kPi = 3.14159265358979323846F;

float clamp01(float value) {
  if (!(value > 0.0F)) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}

// Fitted drag term of the particle-speed law, normalised to unit progress.
float dragIntegral(float u) {
  if (u <= 0.0F) return 0.0F;
  if (u > 1.0F) u = 1.0F;
  return ((0.05F * u - 0.4666667F) * u + 1.1F) * u * u;
}

// Radial distance a particle has reached at progress `p`, in prefab units. This is
// the integral of the fitted speed law integrated with Simpson's rule; the closed
// form is not available because the law is a fitted polynomial.
float particleTravel(float speed, float p) {
  const float step = p / static_cast<float>(kTravelSteps);
  float sum = 0.0F;
  for (int stepIndex = 0; stepIndex <= kTravelSteps; ++stepIndex) {
    const float u = static_cast<float>(stepIndex) * step;
    const float weight = (stepIndex == 0 || stepIndex == kTravelSteps)
        ? 1.0F
        : (((stepIndex & 1) == 1) ? 4.0F : 2.0F);
    const float speedNow = 1.0F / speed + kDragK * dragIntegral(u);
    if (speedNow <= 1.0e-6F) continue;
    sum += weight / speedNow;
  }
  return sum * step / 3.0F * kParticleLifetime;
}

// Size envelope of the debris: grows in, then shrinks away.
float particleSize(float p) {
  return ((0.2078F * p - 1.6524F) * p + 1.6399F) * p + 0.4988F;
}

bool isPerfect(int judgement) noexcept { return judgement == 0; }
bool isGood(int judgement) noexcept { return judgement == 1; }

} // namespace

float HitEffectField::nextRandom() noexcept {
  // Xorshift keeps the debris spread deterministic without pulling in <random>.
  auto state = randomState_;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  randomState_ = state;
  return static_cast<float>(state & 0xFFFFFFU) / static_cast<float>(0x1000000U);
}

void HitEffectField::clear() noexcept {
  effects_.clear();
  tracked_.clear();
}

void HitEffectField::spawn(float x, float y, double startSec, float red, float green, float blue,
    float alpha, int parts, float lineRotDeg) {
  if (effects_.size() >= static_cast<std::size_t>(kMaxLiveEffects)) return;

  HitEffectInstance effect;
  effect.startSec = startSec;
  effect.x = x;
  effect.y = y;
  effect.red = red;
  effect.green = green;
  effect.blue = blue;
  effect.alpha = alpha;
  effect.rotateDeg = lineRotDeg;
  effect.parts = std::clamp(parts, 1, 4);

  // Every part gets an independent angle, speed and spawn radius; the radius is
  // reduced by a random fraction of the ring thickness.
  for (int part = 0; part < 4; ++part) {
    const auto slot = static_cast<std::size_t>(part);
    const float degrees = nextRandom() * 360.0F;
    const float radians = degrees * kPi / 180.0F;
    effect.dirX[slot] = std::cos(radians);
    effect.dirY[slot] = std::sin(radians);
    effect.speed[slot] = kSpeedMin + nextRandom() * (kSpeedMax - kSpeedMin);
    effect.radius0[slot] = kSpawnRadius * (1.0F - kSpawnThickness * nextRandom());
  }
  effects_.push_back(effect);
}

void HitEffectField::update(game::Chart& chart, double chartTimeSec, double visualTimeSec,
    float stageX, float stageY, float stageWidth, float stageHeight, float noteWidth,
    float musicSpeed) {
  noteWidth_ = noteWidth;

  // Expire finished bursts first so the spawn budget reflects live effects only.
  effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                     [&](const HitEffectInstance& effect) {
                       return visualTimeSec - effect.startSec >
                           static_cast<double>(style_.duration);
                     }),
      effects_.end());

  if (tracked_.size() != chart.allNotesSorted.size()) tracked_.resize(chart.allNotesSorted.size());

  const float stageAspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : 16.0F / 9.0F;
  const double speed = std::max(0.001, static_cast<double>(musicSpeed));

  for (std::size_t index = 0; index < chart.allNotesSorted.size(); ++index) {
    game::Note* note = chart.allNotesSorted[index];
    auto& tracked = tracked_[index];
    if (note == nullptr) {
      tracked.note = nullptr;
      tracked.judgement = -1;
      continue;
    }
    tracked.note = note;
    const int judgement = note->judgeResult;
    if (!isPerfect(judgement) && !isGood(judgement)) {
      tracked.judgement = judgement;
      continue;
    }
    if (tracked.judgement == judgement) continue; // already emitted for this state
    tracked.judgement = judgement;
    if (note->master == nullptr) continue;
    // Hold notes get their head burst from the session event stream, which the renderer
    // places where the head was when it was hit; an on-line burst from this scan would be
    // wrong for an early hit and would duplicate the event burst.
    if (note->isHold) continue;

    // Very late sweeps at load time must not produce bursts.
    if (note->sect > chartTimeSec + 0.5) continue;

    // The burst starts at the moment the note was judged on the visual clock, so a
    // slow frame or a playhead jump does not restart it.
    const double judgedAt = std::isfinite(note->judgeTimeSec) ? note->judgeTimeSec : note->sect;
    const double lag = std::max(0.0, (chartTimeSec - judgedAt) / speed);
    spawnBurstForNote(*note, judgement, chartTimeSec, visualTimeSec - lag, stageX, stageY,
        stageWidth, stageHeight);
  }
}

void HitEffectField::spawnBurstForNote(const game::Note& note, int judgement,
    double chartTimeSec, double startSec, float stageX, float stageY, float stageWidth,
    float stageHeight) {
  if (note.master == nullptr) return;
  const float stageAspect = stageHeight > 1.0e-6F ? stageWidth / stageHeight : 16.0F / 9.0F;
  const auto& line = *note.master;
  const auto state = const_cast<game::JudgeLine&>(line).fillState(chartTimeSec, stageAspect);
  const float lineX = stageX + state.xNorm * stageWidth;
  const float lineY = stageY + state.yNorm * stageHeight;
  const double radians = static_cast<double>(state.rotateDeg) * kPi / 180.0;
  const float cosLine = static_cast<float>(std::cos(radians));
  const float sinLine = static_cast<float>(std::sin(radians));
  const float lineRotDeg = state.rotateDeg + (note.isAbove ? 0.0F : 180.0F);

  // The burst sits on the judge line at the note's lane offset, lifted by the
  // note's vertical offset the same way the note head is.
  const float offsetX = static_cast<float>(note.positionX * game::PGR_WIDTH * stageWidth);
  const float transYPx = static_cast<float>(note.yOffset * stageHeight);
  const float dirX = note.isAbove ? sinLine : -sinLine;
  const float dirY = note.isAbove ? -cosLine : cosLine;

  const float x = lineX + offsetX * cosLine + transYPx * dirX;
  const float y = lineY + offsetX * sinLine + transYPx * dirY;

  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
  int parts = 4;
  if (isPerfect(judgement)) {
    red = skin_.perfectRed;
    green = skin_.perfectGreen;
    blue = skin_.perfectBlue;
    alpha = skin_.perfectAlpha;
    parts = 4;
  } else {
    red = skin_.goodRed;
    green = skin_.goodGreen;
    blue = skin_.goodBlue;
    alpha = skin_.goodAlpha;
    parts = 3;
  }
  // A per-note hit-effect tint overrides the skin colour entirely.
  if (note.hasHitEffectTint) {
    red = note.hitEffectR;
    green = note.hitEffectG;
    blue = note.hitEffectB;
  }
  spawn(x, y, startSec, red, green, blue, alpha, parts, lineRotDeg);
}

void HitEffectField::spawnHoldBody(game::Chart& chart, const game::HoldBodyBurst& burst,
    double chartTimeSec, double visualTimeSec, float stageX, float stageY, float stageWidth,
    float stageHeight, float noteWidth) {
  noteWidth_ = noteWidth;
  if (burst.noteIndex < 0
      || static_cast<std::size_t>(burst.noteIndex) >= chart.allNotesSorted.size()) {
    return;
  }
  const game::Note* note = chart.allNotesSorted[static_cast<std::size_t>(burst.noteIndex)];
  if (note == nullptr) return;
  // A body burst is drawn exactly like a head burst of the same judgement, at the position
  // the hold occupies when the timer fires. Back-date it to the moment it happened so a
  // long frame does not restart the animation.
  const double lag = std::max(0.0, chartTimeSec - burst.timeSec);
  spawnBurstForNote(*note, burst.perfect ? 0 : 1, chartTimeSec, visualTimeSec - lag, stageX,
      stageY, stageWidth, stageHeight);
}

void HitEffectField::spawnHoldHead(game::Chart& chart, const game::HoldHeadBurst& burst,
    double chartTimeSec, double visualTimeSec, float x, float y, float rotationDeg,
    float noteWidth, float musicSpeed) {
  noteWidth_ = noteWidth;
  if (burst.noteIndex < 0
      || static_cast<std::size_t>(burst.noteIndex) >= chart.allNotesSorted.size()) {
    return;
  }
  const game::Note* note = chart.allNotesSorted[static_cast<std::size_t>(burst.noteIndex)];
  if (note == nullptr) return;
  // The reference back-dates the burst by (current chart time - event time) / speed so a
  // slow frame does not restart the animation.
  const double speed = std::max(0.001, static_cast<double>(musicSpeed));
  const double lag = std::max(0.0, (chartTimeSec - burst.timeSec) / speed);
  const int judgement = burst.perfect ? 0 : 1;
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
  int parts = 4;
  if (isPerfect(judgement)) {
    red = skin_.perfectRed;
    green = skin_.perfectGreen;
    blue = skin_.perfectBlue;
    alpha = skin_.perfectAlpha;
    parts = 4;
  } else {
    red = skin_.goodRed;
    green = skin_.goodGreen;
    blue = skin_.goodBlue;
    alpha = skin_.goodAlpha;
    parts = 3;
  }
  if (note->hasHitEffectTint) {
    red = note->hitEffectR;
    green = note->hitEffectG;
    blue = note->hitEffectB;
  }
  spawn(x, y, visualTimeSec - lag, red, green, blue, alpha, parts, rotationDeg);
}

void HitEffectField::draw(batch::SpriteBatch& out, const math::Mat4& projection,
    gl::Texture& sheet, const gl::Texture& whiteTexture, double visualTimeSec,
    bool mirrorU) const {
  if (effects_.empty()) return;
  const int frames = atlas_.frames();
  if (frames <= 0 || !sheet.valid()) return;

  const float duration = std::max(1.0e-3F, style_.duration);
  const float effectSize = noteWidth_ * style_.spriteScale * style_.sizeScale;

  // Spritesheet animation.
  out.begin();
  const float halfU = 0.5F / static_cast<float>(sheet.width());
  const float halfV = 0.5F / static_cast<float>(sheet.height());
  for (const auto& effect : effects_) {
    const float age = static_cast<float>(visualTimeSec - effect.startSec);
    if (age < 0.0F || age > duration) continue;
    const float progress = clamp01(age / duration);

    int frame = static_cast<int>(std::floor(progress * static_cast<float>(frames)));
    frame = std::clamp(frame, 0, frames - 1);
    const int column = frame % atlas_.columns;
    const int row = frame / atlas_.columns;

    const float u0 = static_cast<float>(column) / static_cast<float>(atlas_.columns) + halfU;
    const float u1 =
        static_cast<float>(column + 1) / static_cast<float>(atlas_.columns) - halfU;
    const float v0 = static_cast<float>(row) / static_cast<float>(atlas_.rows) + halfV;
    const float v1 = static_cast<float>(row + 1) / static_cast<float>(atlas_.rows) - halfV;
    // A reflected play field mirrors the artwork too; swapping the frame's horizontal
    // UVs undoes that without moving the frame inside its sheet cell.
    const float drawU0 = mirrorU ? u1 : u0;
    const float drawU1 = mirrorU ? u0 : u1;

    // The sheet is only rotated when the skin asks for it and the note carries an
    // explicit tint; that is the same gate the reference applies.
    const float rotation = style_.rotateSheet ? effect.rotateDeg : 0.0F;
    const bool applyTint = style_.tinted;
    out.addQuad(effect.x, effect.y, effectSize, effectSize, rotation,
        applyTint ? effect.red : 1.0F, applyTint ? effect.green : 1.0F,
        applyTint ? effect.blue : 1.0F, effect.alpha, drawU0, v0, drawU1, v1);
  }
  if (!out.empty()) out.flush(projection, sheet, nullptr);

  // Debris quads, drawn with the shared white texture.
  if (!style_.showParticles) return;
  const float worldPx = noteWidth_ * kPrefabToNoteWidth;
  const float baseSize = noteWidth_ * kParticleSize * style_.sizeScale;
  out.begin();
  for (const auto& effect : effects_) {
    const float age = static_cast<float>(visualTimeSec - effect.startSec);
    if (age < 0.0F || age > duration) continue;
    const float progress = clamp01(age / duration);
    const float alpha = 1.0F - progress;
    const float size = baseSize * particleSize(progress);
    for (int part = 0; part < effect.parts; ++part) {
      const auto slot = static_cast<std::size_t>(part);
      const float travel = particleTravel(effect.speed[slot], progress);
      const float radius = (effect.radius0[slot] + travel) * worldPx * style_.sizeScale;
      out.addQuad(effect.x + radius * effect.dirX[slot], effect.y + radius * effect.dirY[slot],
          size, size, 0.0F, effect.red, effect.green, effect.blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
    }
  }
  if (!out.empty()) out.flush(projection, whiteTexture, nullptr);
}

} // namespace rechigros::render::fx
