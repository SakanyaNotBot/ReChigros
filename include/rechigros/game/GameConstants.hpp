#pragma once

namespace rechigros::game {
inline constexpr double PGR_BEAT = 1.875;
inline constexpr float PGR_WIDTH = 0.05625F;
inline constexpr float PGR_HEIGHT = 0.6F;
inline constexpr int NOTE_TAP = 1;
inline constexpr int NOTE_DRAG = 2;
inline constexpr int NOTE_HOLD = 3;
inline constexpr int NOTE_FLICK = 4;
inline constexpr float PERFECT_COLOR[3] = {1.0F, 236.0F / 255.0F, 160.0F / 255.0F};
inline constexpr float GOOD_COLOR[3] = {180.0F / 255.0F, 225.0F / 255.0F, 1.0F};
inline constexpr float RPE_WIDTH = 1350.0F;
inline constexpr float RPE_HEIGHT = 900.0F;
inline constexpr float HEIGHT_RATIO = 0.83175F;
inline constexpr float SPEED_RATIO = 10.0F / 45.0F / HEIGHT_RATIO;

// Hit-effect particle burst, from the shipped "perfect" prefab: four particles on a ring of
// radius 0.2 (thickness 0.2, so 0.16..0.20) at random angles, with a start speed drawn
// uniformly from 15..35 prefab units per second.
inline constexpr float HIT_FX_PARTICLE_SPEED_MIN = 15.0F;
inline constexpr float HIT_FX_PARTICLE_SPEED_MAX = 35.0F;
inline constexpr float HIT_FX_PARTICLE_SPAWN_RADIUS = 0.2F;
inline constexpr float HIT_FX_PARTICLE_SPAWN_THICKNESS = 0.2F;
}
