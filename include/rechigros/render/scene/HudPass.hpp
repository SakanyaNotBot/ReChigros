#pragma once

#include "rechigros/render/math/Mat4.hpp"
#include "rechigros/render/scene/StageLayout.hpp"

#include <array>
#include <string>

namespace rechigros::render::batch { class SpriteBatch; }
namespace rechigros::render::gl { class Texture; }
namespace rechigros::render::text { class TextRenderer; }

namespace rechigros::render::scene {

// Canvas-space anchors. The HUD is authored on a 1000-unit tall play field whose width
// follows the stage aspect ratio; x grows right from the stage centre, y grows up.
[[nodiscard]] float hudUnit(const StageLayout& layout) noexcept;
[[nodiscard]] float hudX(const StageLayout& layout, float uiX) noexcept;
[[nodiscard]] float hudY(const StageLayout& layout, float uiY) noexcept;

// Element ids of the attach-UI mapping. They are the ids the chart format uses to
// connect a judge line's animation state to one HUD widget.
enum class HudElement : int {
  Pause = 1,
  ComboNumber = 2,
  ComboLabel = 3,
  Score = 4,
  Bar = 5,
  Name = 6,
  Level = 7,
};
inline constexpr int kHudElementCount = 8;

// One widget's attach-UI state for the current frame, already converted to the HUD's
// coordinate space. A widget without an attached line keeps the default transform.
struct HudElementTransform {
  bool valid{};
  float translateX{};
  float translateY{};
  float rotateDeg{};
  float scaleX{1.0F};
  float scaleY{1.0F};
  float alpha{1.0F};
  float red{1.0F};
  float green{1.0F};
  float blue{1.0F};
  // Text a chart attaches to an element (the combo label can be overridden).
  std::string text;
};

struct HudTransforms {
  std::array<HudElementTransform, kHudElementCount> element{};
  // Charts from the fixed-format era place the scale pivot on the element centre.
  bool useFix{true};
};

// Live values mirrored into the HUD widgets.
struct HudValues {
  int score{};
  int combo{};
  int maxCombo{};
  double accuracy{1.0};
  double playheadSec{};
  double totalDurationSec{};
  // Captions and the combo label variant for the run's mode.
  std::string songName;
  std::string levelName;
  std::string comboLabel{"COMBO"};
  // The accuracy readout follows a gameplay setting.
  bool showAccuracy{true};
  // Combo widgets also have a chart-driven blocked display; false hides them.
  bool showCombo{true};
  // Optional catch-up animation of the score and accuracy readouts.
  bool scoreJumpEffect{false};
  // Normalised progress of the 40-frame start and exit animations (1 and 0 when idle).
  float introProgress{1.0F};
  float outroProgress{0.0F};
  // Wall clock used by the pause-button ring feedback.
  double visualTimeSec{};
  // Seconds since the pause button was last touched; negative when idle.
  double pauseRingAgeSec{-1.0F};
  // Frame-rate readout (reference: bottom-left "N FPS" plate).
  float fps{};
  bool showFps{false};
};

struct HudColors {
  float textR{1.0F}, textG{1.0F}, textB{1.0F}, textA{1.0F};
  float scoreR{1.0F}, scoreG{1.0F}, scoreB{1.0F};
};

// Optional artwork. A null texture falls back to a flat quad so the layout stays
// verifiable without the assets.
struct HudTextures {
  const gl::Texture* pause{};
  const gl::Texture* timerLine{};
  const gl::Texture* noteRing{};
  // Pointer glyph of the judgement offset dial.
  const gl::Texture* hitOffsetArrow{};
  // Solid white texture used by the dial's line primitives.
  const gl::Texture* white{};
};

// Judgement-offset indicator state for the current frame, mirrored from the judgement
// engine. The reference shows one of two shapes: a 60-degree dial (sector) or a
// horizontal line; both accumulate short-lived per-judgement marks and a pointer.
struct HudHitOffsetMark {
  bool active{};
  float angleDeg{};
  int judgement{};
  float age{};
};

struct HudHitOffset {
  // 0 disabled, 1 sector dial, 2 horizontal line.
  int mode{};
  bool suppressed{}; // autoplay or a non-replay run never shows it
  float perfectWindowSec{0.080F};
  float goodWindowSec{0.180F};
  float badWindowSec{0.220F};
  float pointerAngleDeg{};
  // Per-judgement marks of the last 0.9 s, with their normalised age.
  std::array<HudHitOffsetMark, 64> marks{};
  int markCount{};
};

// Draws the in-game HUD exactly where the play field's HUD canvas places it: pause
// button and progress bar, song title bottom-left, difficulty bottom-right, score
// top-right, optional accuracy, and the centre combo number/label. `alpha` scales the
// whole overlay. Each string owns its own texture, so quads are flushed per string.
void drawHud(batch::SpriteBatch& batch, text::TextRenderer& text, const StageLayout& layout,
    const HudValues& values, const HudColors& colors, const HudTextures& textures,
    const HudTransforms& transforms, const HudHitOffset& hitOffset, float alpha,
    const math::Mat4& projection);

} // namespace rechigros::render::scene
