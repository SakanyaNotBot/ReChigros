#include "rechigros/render/scene/ChartDebugOverlay.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/NoteGeometry.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::scene {
namespace {

// One note's centre on the play field, in stage pixels. The derivation mirrors the reference's
// debug readout, which applies the same control curves the note pass uses, so a marker lands
// where the note is drawn.
struct NoteAnchor {
  float centerX{};
  float centerY{};
  bool onScreen{};
};

float controlOr(const game::JudgeLine& line, int controlIndex, float controlHeight,
    float fallback) {
  const float value = line.calcNoteControl(controlHeight, controlIndex, fallback);
  return std::isfinite(value) ? value : fallback;
}

NoteAnchor noteAnchor(const game::Note& note, const game::JudgeLine& line, const LineFrame& frame,
    const StageLayout& layout, float scrollSpeed) {
  NoteAnchor anchor;
  const float stageWidth = layout.stage.width;
  const float stageHeight = layout.stage.height;
  if (!(stageHeight > 0.0F) || !(stageWidth > 0.0F)) return anchor;
  const float stageAspect = stageWidth / stageHeight;
  const double bpm = frame.bpm > 0.0 ? frame.bpm : 120.0;
  const double beatScale = static_cast<double>(game::PGR_HEIGHT) * (60.0 / bpm)
      * static_cast<double>(stageHeight);
  const double speed = (note.useOfficialSpeed ? 1.0 : note.speed)
      * static_cast<double>(scrollSpeed);
  const double baseFp = (note.floorPosition - frame.floorPosition) * beatScale * speed;
  const auto baseFpPx = static_cast<float>(baseFp);
  const float transYPx = note.yOffset * stageHeight;
  const float controlHeight = game::rpeControlHeightFromBasePx(baseFpPx, note.yOffset, speed,
      stageAspect, stageHeight);
  const float yControl = controlOr(line, game::JudgeLine::NOTE_CTRL_Y, controlHeight, 1.0F);
  const float baseFpWithControl = baseFpPx * yControl;
  const float visualFp = baseFpWithControl + transYPx;

  const float skewControl = controlOr(line, game::JudgeLine::NOTE_CTRL_SKEW, controlHeight, 0.0F);
  const float inclineSinr = game::combineInclineSinr(frame.inclineSinr, skewControl);
  float inclineValue = 1.0F;
  if (std::isfinite(inclineSinr)) {
    inclineValue = game::calcInclineValue(inclineSinr, baseFpWithControl, transYPx, stageAspect,
        stageHeight);
  }
  float offsetX = static_cast<float>(note.positionX * game::PGR_WIDTH * stageWidth);
  offsetX *= inclineValue;
  // The reference applies the X control only to notes that are not holds.
  if (!note.isHold) {
    const float xControl = controlOr(line, game::JudgeLine::NOTE_CTRL_X, controlHeight, 1.0F);
    if (xControl != 0.0F) offsetX *= xControl;
  }

  const float lineX = layout.stage.x + frame.xNorm * stageWidth;
  const float lineY = layout.stage.y + frame.yNorm * stageHeight;
  const float alongX = lineX + offsetX * frame.cosLine;
  const float alongY = lineY + offsetX * frame.sinLine;
  const float dirX = note.isAbove ? frame.sinLine : -frame.sinLine;
  const float dirY = note.isAbove ? -frame.cosLine : frame.cosLine;
  anchor.centerX = alongX + visualFp * dirX;
  anchor.centerY = alongY + visualFp * dirY;
  anchor.onScreen = anchor.centerX >= layout.stage.x && anchor.centerX <= layout.stage.right()
      && anchor.centerY >= layout.stage.y && anchor.centerY <= layout.stage.bottom();
  return anchor;
}

} // namespace

void ChartDebugOverlay::draw(batch::SpriteBatch& batch, const StageLayout& layout,
    const game::Chart& chart, const std::vector<LineFrame>& frames,
    const std::vector<int>& drawOrder, const Style& style, const gl::Texture& white,
    const math::Mat4& projection) {
  if (!white.valid() || !(layout.stage.width > 0.0F) || !(layout.stage.height > 0.0F)) return;
  const float marker = layout.stage.height * style.markerFraction;
  if (!(marker > 0.0F)) return;

  const std::size_t lineCount = chart.judgeLineList.size();
  if (frames.size() < lineCount) return;

  batch.begin();
  for (std::size_t order = 0; order < drawOrder.size(); ++order) {
    const int index = drawOrder[order];
    if (index < 0 || static_cast<std::size_t>(index) >= lineCount) continue;
    const auto& line = *chart.judgeLineList[static_cast<std::size_t>(index)];
    // A line bound to an interface element has no play-field position of its own.
    if (line.attachUiElementId != 0) continue;
    const auto& frame = frames[static_cast<std::size_t>(index)];
    if (!frame.valid) continue;

    const float lineX = layout.stage.x + frame.xNorm * layout.stage.width;
    const float lineY = layout.stage.y + frame.yNorm * layout.stage.height;
    // The line marker is violet, so it reads as chrome rather than as gameplay.
    batch.addQuad(lineX, lineY, marker * 4.0F, marker * 4.0F, 0.0F, 0.8F, 0.2F, 1.0F, 1.0F,
        0.0F, 0.0F, 1.0F, 1.0F);

    // A line whose notes are hidden draws none.
    if (frame.hideNotes) continue;
    for (const auto& note : line.notes) {
      const NoteAnchor anchor = noteAnchor(note, line, frame, layout, 1.0F);
      if (!anchor.onScreen) continue;
      // The marker is dimmed for a note the current moment does not show, so an overlay of a
      // dense chart stays readable.
      const float alpha = note.alpha <= 0.0F ? 0.45F : 1.0F;
      // Lime, the reference's note colour for this overlay.
      batch.addQuad(anchor.centerX, anchor.centerY, marker * 6.0F, marker * 6.0F, 0.0F, 0.2F,
          1.0F, 0.2F, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
    }
  }
  batch.flush(projection, white, nullptr);
}

} // namespace rechigros::render::scene
