#pragma once

#include "rechigros/render/math/Mat4.hpp"

#include <limits>
#include <string>

namespace rechigros::render::scene {

// Per-line evaluation result for a single frame. Mirrors the packed line record the
// reference batch builders consume, so the note pass reads one struct per line.
struct LineFrame {
  bool valid{};
  bool renderable{};
  bool noteValid{};
  bool hideLine{};
  bool hideNotes{};
  bool cover{};
  bool hasColorEvent{};

  float xNorm{0.5F};
  float yNorm{0.5F};
  float x{};
  float y{};
  float rotateDeg{};
  float cosLine{1.0F};
  float sinLine{0.0F};
  // Raw disappear-event alpha. Negative values encode line visibility modes:
  // -1 hides line and notes, -2 clips notes past the line, -100..-999 delays the
  // note appear time by (|value| - 100) * 3.2 beats.
  float alphaRaw{};
  float scaleX{1.0F};
  float scaleY{1.0F};
  float colorR{1.0F};
  float colorG{1.0F};
  float colorB{1.0F};
  float anchorX{0.5F};
  float anchorY{0.5F};
  float gifPosition{std::numeric_limits<float>::quiet_NaN()};
  float inclineSinr{std::numeric_limits<float>::quiet_NaN()};

  double floorPosition{};
  double bpm{120.0};

  // Storyboard text resolved for this frame; text events take priority over a texture.
  std::string text;
  // True when the line has text events at all, even if the current text is empty.
  bool hasTextEvents{};

  // Drawable distance range along the note travel axis, already padded for the cull test.
  float visibleMin{};
  float visibleMax{};
  float visiblePad{};
};

// Distances along the note travel axis that the current viewport can show.
void computeVisibleRange(float lineX, float lineY, float cosLine, float sinLine,
    bool revealActive, float revealScale,
    float stageWidth, float stageHeight, float scrollSpeed,
    const math::Rect& worldBounds,
    float& outMin, float& outMax) noexcept;

} // namespace rechigros::render::scene
