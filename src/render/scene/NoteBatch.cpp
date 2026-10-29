#include "rechigros/render/scene/NoteBatch.hpp"

#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Note.hpp"
#include "rechigros/game/NoteGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rechigros::render::scene {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kRpeHeight = 900.0F;
constexpr double kBeatSeconds = 60.0 / 32.0;

constexpr int kControlAlpha = 0;
constexpr int kControlScale = 1;
constexpr int kControlX = 2;
constexpr int kControlY = 3;
constexpr int kControlSkew = 4;

bool isFinite(float value) { return std::isfinite(value); }
bool isFinite(double value) { return std::isfinite(value); }

float clamp01(float value) {
  if (value < 0.0F) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}

// Editor formats store note alpha either normalised or in 0..255.
float normalizeNoteAlpha01(float alpha) {
  if (!isFinite(alpha)) return 1.0F;
  if (alpha > 1.5F || alpha < -2.5F) alpha /= 255.0F;
  return clamp01(alpha);
}

float computeNoteRenderAlpha(float noteAlphaRaw, float lineAlphaRaw, bool official) {
  const float noteAlpha = normalizeNoteAlpha01(noteAlphaRaw);
  if (noteAlpha <= 0.0F) return 0.0F;
  if (lineAlphaRaw < 0.0F) {
    if (official) return 0.0F;
    const int mode = static_cast<int>(std::floor(-lineAlphaRaw));
    if (mode == 1 || mode == 0) return 0.0F;
  }
  return noteAlpha;
}

// A disappear value below -100 delays the note appear time by 3.2-beat units.
double decodeAppearBeforeTicks(float lineAlphaRaw) {
  if (!(lineAlphaRaw < 0.0F)) return std::numeric_limits<double>::infinity();
  const int mode = static_cast<int>(std::floor(-lineAlphaRaw));
  if (mode >= 100 && mode < 1000) return static_cast<double>(mode - 100) * 3.2;
  return std::numeric_limits<double>::infinity();
}

bool isBeforeAppearTime(double noteGlobalBeat, double chartGlobalBeat, float lineAlphaRaw) {
  const double appearBeforeTicks = decodeAppearBeforeTicks(lineAlphaRaw);
  if (!isFinite(noteGlobalBeat) || !isFinite(chartGlobalBeat) || !isFinite(appearBeforeTicks)) {
    return false;
  }
  return chartGlobalBeat < noteGlobalBeat - appearBeforeTicks;
}

// The note control curves are evaluated on an RPE-pixel height, which couples the
// normalised y offset with the scroll-derived distance.
// The line incline event and the per-note skew control are both angles; combine
// them into one sine before evaluating the incline.
float combineInclineSinr(float baseSinr, float skewDeg) {
  const bool hasBase = isFinite(baseSinr);
  const bool hasSkew = isFinite(skewDeg) && std::abs(skewDeg) > 1.0e-6F;
  if (!hasBase && !hasSkew) return std::numeric_limits<float>::quiet_NaN();
  double angle = hasBase ? std::asin(std::max(-1.0F, std::min(1.0F, baseSinr))) : 0.0;
  if (hasSkew) angle += static_cast<double>(skewDeg) * kPi / 180.0;
  return static_cast<float>(std::sin(angle));
}

bool isAnyJudgement(int judgement) { return judgement >= 0 && judgement <= 3; }

float applyYControlScale(float baseFp, float yControl) {
  return isFinite(yControl) ? baseFp * yControl : baseFp;
}

float tintComponent(bool hasTint, float value) {
  if (!hasTint) return 1.0F;
  return isFinite(value) ? value : 1.0F;
}

// Exact AABB overlap of a rotated rectangle against the cull area.
bool isNoteRectInArea(float centerX, float centerY, float width, float height, float rotationDeg,
    const math::Rect& area) {
  if (!(width > 0.0F) || !(height > 0.0F)) return false;
  const float halfW = width * 0.5F;
  const float halfH = height * 0.5F;
  if (rotationDeg == 0.0F) {
    return !(centerX + halfW < area.left() || centerX - halfW > area.right() ||
             centerY + halfH < area.top() || centerY - halfH > area.bottom());
  }
  const double radians = static_cast<double>(rotationDeg) * kPi / 180.0;
  const auto cosAbs = static_cast<float>(std::abs(std::cos(radians)));
  const auto sinAbs = static_cast<float>(std::abs(std::sin(radians)));
  const float extentX = halfW * cosAbs + halfH * sinAbs;
  const float extentY = halfW * sinAbs + halfH * cosAbs;
  return !(centerX + extentX < area.left() || centerX - extentX > area.right() ||
           centerY + extentY < area.top() || centerY - extentY > area.bottom());
}

float evalControl(const game::JudgeLine& line, int type, float controlHeight) {
  return line.calcNoteControl(controlHeight, type, type == kControlSkew ? 0.0F : 1.0F);
}

struct LocalPlacement {
  float controlHeight{};
  float visualFp{};
  float transYFp{};
  float offsetX{};
  float shear{};
};

LocalPlacement localPlacement(const game::Note& note, const game::JudgeLine& line,
    const LineFrame& frame, const NotePassParams& params) {
  const float stageAspect = params.stageWidth / params.stageHeight;
  const double bpm = frame.bpm > 0.0 ? frame.bpm : 120.0;
  const double speed = note.speed * static_cast<double>(params.scrollSpeed);
  const float baseFp = static_cast<float>((note.floorPosition - frame.floorPosition) *
      game::PGR_HEIGHT * (kBeatSeconds / bpm) * params.stageHeight * speed);
  LocalPlacement placement;
  placement.controlHeight = game::rpeControlHeightFromBasePx(baseFp, note.yOffset, speed,
      stageAspect, params.stageHeight);
  placement.transYFp = note.yOffset * params.stageHeight *
      (params.officialFormat ? 1.0F : static_cast<float>(speed));
  const float yControl = params.officialFormat || note.isHold
      ? 1.0F : evalControl(line, kControlY, placement.controlHeight);
  placement.visualFp = params.officialFormat
      ? applyYControlScale(baseFp, yControl) + placement.transYFp
      : applyYControlScale(baseFp + placement.transYFp, yControl);
  placement.offsetX = note.positionX * game::PGR_WIDTH * params.stageWidth;
  const float xControl = evalControl(line, kControlX, placement.controlHeight);
  if (params.officialFormat) {
    const float skewControl = evalControl(line, kControlSkew, placement.controlHeight);
    const float inclineSin = combineInclineSinr(frame.inclineSinr, skewControl);
    if (isFinite(inclineSin)) {
      placement.offsetX *= game::calcInclineValue(inclineSin,
          applyYControlScale(baseFp, yControl), placement.transYFp, stageAspect,
          params.stageHeight);
    }
  } else if (isFinite(frame.inclineSinr) && !note.isHold) {
    const double angle = std::asin(std::clamp(frame.inclineSinr, -1.0F, 1.0F));
    const float shift = std::tan(note.positionX * (75.375F / 675.0F) *
        static_cast<float>(angle));
    placement.offsetX -= shift * placement.visualFp * (note.isAbove ? 1.0F : -1.0F);
  }
  if (isFinite(xControl) && xControl != 0.0F) placement.offsetX *= xControl;
  if (!params.officialFormat && !note.isHold) {
    const float skew = evalControl(line, kControlSkew, placement.controlHeight);
    placement.shear = std::tan(note.positionX * 75.375F * skew * kPi / 180.0F);
  }
  if (note.isHold && note.sect < params.chartTimeSec && !params.officialFormat) {
    placement.visualFp = placement.transYFp;
  }
  return placement;
}

void addQuadFlippable(batch::SpriteBatch& out, float centerX, float centerY, float width,
    float height, float rotationDeg, float red, float green, float blue, float alpha,
    float u0, float v0, float u1, float v1, bool flip) {
  if (flip) std::swap(u0, u1);
  out.addQuad(centerX, centerY, width, height, rotationDeg, red, green, blue, alpha, u0, v0, u1, v1);
}

void addQuadSelectFlippable(batch::SpriteBatch& out, float centerX, float centerY, float width,
    float height, float rotationDeg, float red, float green, float blue, float alpha,
    float u0, float v0, float u1, float v1, bool flip, float textureSelect) {
  if (flip) std::swap(u0, u1);
  out.addQuadSelect(centerX, centerY, width, height, rotationDeg, red, green, blue, alpha,
      u0, v0, u1, v1, textureSelect);
}

// First index in a time-sorted per-type note list whose note time is at or after `limitSec`.
// Scanning can then skip the notes whose fade window has already closed.
template <typename NoteAt>
std::size_t firstNoteAtOrAfter(NoteAt noteAt, std::size_t count, double limitSec) {
  std::size_t low = 0;
  std::size_t high = count;
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    if (noteAt(middle).sect < limitSec) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

} // namespace

void computeVisibleRange(float lineX, float lineY, float cosLine, float sinLine, bool revealActive,
    float revealScale, float stageWidth, float stageHeight, float scrollSpeed,
    const math::Rect& worldBounds, float& outMin, float& outMax) noexcept {
  // Notes travel along the line normal, so projecting the viewport onto that axis
  // culls rotated performance notes before their per-note AABB test.
  const float dirX = sinLine;
  const float dirY = -cosLine;
  float left = worldBounds.left() - lineX;
  float right = worldBounds.right() - lineX;
  float top = worldBounds.top() - lineY;
  float bottom = worldBounds.bottom() - lineY;
  const float scale = (isFinite(revealScale) && revealScale > 1.0e-6F) ? revealScale : 1.0F;
  const float marginW = revealActive ? stageWidth / scale / 12.0F : stageWidth / 12.0F;
  const float marginH = revealActive ? stageHeight / scale / 12.0F : stageHeight / 12.0F;
  left -= marginW;
  right += marginW;
  top -= marginH;
  bottom += marginH;
  const float p1 = left * dirX + top * dirY;
  const float p2 = right * dirX + top * dirY;
  const float p3 = left * dirX + bottom * dirY;
  const float p4 = right * dirX + bottom * dirY;
  float minimum = std::min(std::min(p1, p2), std::min(p3, p4));
  float maximum = std::max(std::max(p1, p2), std::max(p3, p4));
  const float oppositeMin = -maximum;
  const float oppositeMax = -minimum;
  if (oppositeMin < minimum) minimum = oppositeMin;
  if (oppositeMax > maximum) maximum = oppositeMax;
  if (scrollSpeed > 1.0e-6F && scrollSpeed < 1.0F) {
    const float factor = 1.0F / scrollSpeed;
    minimum *= factor;
    maximum *= factor;
  }
  outMin = minimum;
  outMax = maximum;
}

HoldUv buildHoldUv(int textureHeightPx, int tailSlicePx, int headSlicePx) noexcept {
  HoldUv uv;
  if (textureHeightPx <= 0) return uv;
  const int yTail0 = 0;
  const int yTail1 = std::min(textureHeightPx, std::max(0, tailSlicePx));
  const int yBody0 = yTail1;
  const int yBody1 = std::max(yBody0, textureHeightPx - std::max(0, headSlicePx));
  const int yHead0 = yBody1;
  const int yHead1 = textureHeightPx;

  const float padTail = (yTail1 - yTail0) > 1 ? 0.5F : 0.0F;
  const float padBody = (yBody1 - yBody0) > 1 ? 0.5F : 0.0F;
  const float padHead = (yHead1 - yHead0) > 1 ? 0.5F : 0.0F;
  const auto total = static_cast<float>(textureHeightPx);
  uv.tailV0 = (static_cast<float>(yTail0) + padTail) / total;
  uv.tailV1 = (static_cast<float>(yTail1) - padTail) / total;
  uv.bodyV0 = (static_cast<float>(yBody0) + padBody) / total;
  uv.bodyV1 = (static_cast<float>(yBody1) - padBody) / total;
  uv.headV0 = (static_cast<float>(yHead0) + padHead) / total;
  uv.headV1 = (static_cast<float>(yHead1) - padHead) / total;
  if (!(uv.tailV1 > uv.tailV0)) {
    uv.tailV0 = static_cast<float>(yTail0) / total;
    uv.tailV1 = static_cast<float>(yTail1) / total;
  }
  if (!(uv.bodyV1 > uv.bodyV0)) {
    uv.bodyV0 = static_cast<float>(yBody0) / total;
    uv.bodyV1 = static_cast<float>(yBody1) / total;
  }
  if (!(uv.headV1 > uv.headV0)) {
    uv.headV0 = static_cast<float>(yHead0) / total;
    uv.headV1 = static_cast<float>(yHead1) / total;
  }
  uv.tailHeightPx = static_cast<float>(yTail1 - yTail0);
  uv.headHeightPx = static_cast<float>(yHead1 - yHead0);
  return uv;
}

float highlightWidthScale(int normalWidth, int highlightWidth) noexcept {
  if (normalWidth <= 0 || highlightWidth <= 0) return 1.0F;
  const float ratio =
      static_cast<float>(highlightWidth) / static_cast<float>(normalWidth);
  return (std::isfinite(ratio) && ratio > 0.0F) ? ratio : 1.0F;
}

bool holdHasStarted(double noteStartSec, double chartTimeSec, bool officialFormat) noexcept {
  return officialFormat ? noteStartSec <= chartTimeSec : noteStartSec < chartTimeSec;
}

bool noteCoveredByLine(const game::Note& note, const LineFrame& frame, double chartTimeSec,
    bool officialFormat, float headFp) noexcept {
  if (!frame.cover) return false;
  (void)officialFormat;
  return note.sect > chartTimeSec && headFp < 0.0F;
}

float holdTailCapOffset(float headFp, float tailFp, float tailHeightPx, bool compact) noexcept {
  if (compact) return 0.0F;
  const float half = 0.5F * tailHeightPx;
  // A tail at or beyond the head keeps the historical outward offset; a tail that ended up
  // behind the head mirrors it so the cap still closes the body from the outside.
  return tailFp >= headFp ? half : -half;
}

float holdHeadClampMin(float headHeightPx, bool compact, bool clampToLine) noexcept {
  if (!clampToLine) return -std::numeric_limits<float>::infinity();
  return compact ? 0.0F : -headHeightPx * 0.5F;
}

void buildLineDrawOrder(const game::Chart& chart, std::vector<int>& out) {
  const auto& lines = chart.judgeLineList;
  const auto count = static_cast<int>(lines.size());
  out.resize(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) out[static_cast<std::size_t>(index)] = index;
  // Stable insertion sort by ascending z-order; higher orders paint over lower ones.
  for (int index = 1; index < count; ++index) {
    const int key = out[static_cast<std::size_t>(index)];
    const int keyZ = lines[static_cast<std::size_t>(key)]->zOrder;
    int previous = index - 1;
    while (previous >= 0) {
      const int otherZ =
          lines[static_cast<std::size_t>(out[static_cast<std::size_t>(previous)])]->zOrder;
      if (otherZ > keyZ) {
        out[static_cast<std::size_t>(previous + 1)] = out[static_cast<std::size_t>(previous)];
        --previous;
      } else {
        break;
      }
    }
    out[static_cast<std::size_t>(previous + 1)] = key;
  }
}

NoteHeadPose noteHeadPose(const game::Note& note, const LineFrame& frame,
    const NotePassParams& params) {
  NoteHeadPose pose;
  if (!frame.valid || !frame.renderable || note.master == nullptr) return pose;
  if (!(params.stageWidth > 1.0e-6F) || !(params.stageHeight > 1.0e-6F)) return pose;
  const auto& placement = localPlacement(note, *note.master, frame, params);
  const float atX = frame.x + placement.offsetX * frame.cosLine;
  const float atY = frame.y + placement.offsetX * frame.sinLine;
  const float dirX = note.isAbove ? frame.sinLine : -frame.sinLine;
  const float dirY = note.isAbove ? -frame.cosLine : frame.cosLine;
  pose.valid = true;
  pose.x = atX + placement.visualFp * dirX;
  pose.y = atY + placement.visualFp * dirY;
  pose.rotationDeg = frame.rotateDeg + (note.isAbove ? 0.0F : 180.0F);
  return pose;
}

void buildHeadBatch(batch::SpriteBatch& out, const std::vector<LineFrame>& frames,
    const std::vector<int>& drawOrder, const game::Chart& chart, int noteType,
    bool highlightVariant, float textureWidth, float textureHeight, const NotePassParams& params) {
  const auto& lines = chart.judgeLineList;
  const auto lineCount = static_cast<int>(lines.size());
  if (lineCount <= 0 || textureWidth <= 0.0F || textureHeight <= 0.0F) return;
  const float stageAspect =
      params.stageHeight > 1.0e-6F ? params.stageWidth / params.stageHeight : 16.0F / 9.0F;
  const bool reveal = params.revealHiddenObjects && params.revealHiddenFloorAlpha > 0.001F;

  for (const int lineIndex : drawOrder) {
    if (lineIndex < 0 || lineIndex >= lineCount) continue;
    const auto& line = *lines[static_cast<std::size_t>(lineIndex)];
    const auto& frame = frames[static_cast<std::size_t>(lineIndex)];
    if (!frame.renderable || !frame.noteValid) continue;
    if (frame.hideNotes && !reveal) continue;

    // Notes of one type are kept time-sorted by the runtime preparation, so this pass visits
    // only its own type and starts after the entries whose fade window has already closed.
    const std::vector<game::Note*>* typedNotes = nullptr;
    if (line.notesByType.size() > static_cast<std::size_t>(noteType)) {
      typedNotes = &line.notesByType[static_cast<std::size_t>(noteType)];
    }
    const std::size_t typedCount =
        typedNotes != nullptr ? typedNotes->size() : line.notes.size();
    const auto noteAt = [&](std::size_t index) -> const game::Note& {
      return typedNotes != nullptr ? *(*typedNotes)[index] : line.notes[index];
    };
    const double bpm = frame.bpm > 0.0 ? frame.bpm : 120.0;
    const double beatScale = static_cast<double>(game::PGR_HEIGHT) * (kBeatSeconds / bpm) *
        static_cast<double>(params.stageHeight);
    const std::size_t firstNote = firstNoteAtOrAfter(noteAt, typedCount,
        params.chartTimeSec - static_cast<double>(params.missFadeWindow));

    for (std::size_t noteIndex = firstNote; noteIndex < typedCount; ++noteIndex) {
      const auto& note = noteAt(noteIndex);
      if (note.isHold || note.type != noteType) continue;
      if (((note.morebets == 1) && params.multiPressHighlight) != highlightVariant) continue;
      if (isAnyJudgement(note.judgeResult)) continue;
      // Unjudged notes fade out over the good window once their time has passed.
      if (params.chartTimeSec - note.sect > params.missFadeWindow) continue;
      if (note.isFake && note.sect <= params.chartTimeSec) continue;

      const bool lineHiddenNote =
          reveal && frame.hideNotes;
      bool revealHidden = lineHiddenNote;
      if (!lineHiddenNote &&
          isBeforeAppearTime(note.rpeGlobalBeat, params.chartGlobalBeat, frame.alphaRaw)) {
        if (!reveal) continue;
        revealHidden = true;
      }

      const bool above = note.isAbove;
      const double speed = note.speed * static_cast<double>(params.scrollSpeed);
      const double baseFpD = (note.floorPosition - frame.floorPosition) * beatScale * speed;
      const auto baseFpPx = static_cast<float>(baseFpD);
      const float transYPx = note.yOffset * params.stageHeight *
          (params.officialFormat ? 1.0F : static_cast<float>(speed));
      // Without a y control curve and a per-note y offset the position used by the final cull
      // is exactly the cheap floor offset, so notes clearly outside the visible range can be
      // rejected before evaluating the control curves. Revealed hidden notes are the only
      // case that draws something outside the range.
      if (!reveal && line.noteYControls.empty() && note.yOffset == 0.0F) {
        if (baseFpPx < frame.visibleMin - frame.visiblePad
            || baseFpPx > frame.visibleMax + frame.visiblePad) {
          continue;
        }
      }
      const float controlHeight =
          game::rpeControlHeightFromBasePx(baseFpPx, note.yOffset, speed, stageAspect, params.stageHeight);
      const float yControl = params.officialFormat || note.isHold
          ? 1.0F : evalControl(line, kControlY, controlHeight);
      const float baseFpWithControl = applyYControlScale(baseFpPx, yControl);
      const float visualFp = params.officialFormat
          ? baseFpWithControl + transYPx
          : applyYControlScale(baseFpPx + transYPx, yControl);

      if (!lineHiddenNote && params.officialFormat && note.sect > params.chartTimeSec &&
          visualFp < -0.001F) {
        if (!reveal) continue;
        revealHidden = true;
      }

      float noteAlpha;
      if (lineHiddenNote) {
        noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      } else {
        noteAlpha = computeNoteRenderAlpha(note.alpha, frame.alphaRaw, params.officialFormat);
        if (normalizeNoteAlpha01(note.alpha) <= 0.0F ||
            (frame.alphaRaw < 0.0F && noteAlpha <= 0.0F)) {
          if (reveal) revealHidden = true;
        }
        noteAlpha *= params.frameGlobalAlpha;

        // A disappear value of exactly -2 hides notes that have crossed the line.
        if (frame.alphaRaw < 0.0F && noteAlpha > 0.0F) {
          const int mode = static_cast<int>(std::floor(-frame.alphaRaw));
          if (mode == 2) {
            if (above) {
              if (visualFp < -0.001F) {
                noteAlpha = 0.0F;
                if (reveal) revealHidden = true;
              }
            } else if (visualFp > 0.001F) {
              noteAlpha = 0.0F;
              if (reveal) revealHidden = true;
            }
          }
        }

        const float beforeControl = noteAlpha;
        noteAlpha *= evalControl(line, kControlAlpha, controlHeight);
        if (beforeControl > 0.0F && noteAlpha <= 0.0F && reveal) revealHidden = true;

        if (noteAlpha > 0.0F && note.visibleTime >= 0.0F &&
            (note.sect - params.chartTimeSec) > note.visibleTime) {
          noteAlpha = 0.0F;
          if (reveal) revealHidden = true;
        }
        if (note.sect <= params.chartTimeSec) {
          const auto progress =
              static_cast<float>((params.chartTimeSec - note.sect) / params.missFadeWindow);
          noteAlpha *= (1.0F - clamp01(progress));
        }
        if (revealHidden) noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      }

      if (noteAlpha <= 0.0F) {
        if (!revealHidden) continue;
        noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      }

      // Off screen, but later notes of this line may still be in range.
      if (visualFp < frame.visibleMin - frame.visiblePad ||
          visualFp > frame.visibleMax + frame.visiblePad) {
        continue;
      }

      const float size = (!isFinite(note.size) || note.size == 0.0F) ? 1.0F : note.size;
      const float xScale = (!isFinite(note.xScale) || note.xScale == 0.0F) ? 1.0F : note.xScale;
      float scaleControl = evalControl(line, kControlScale, controlHeight);
      if (!isFinite(scaleControl) || scaleControl == 0.0F) scaleControl = 1.0F;
      const float skewControl = evalControl(line, kControlSkew, controlHeight);
      const float inclineSinr = combineInclineSinr(frame.inclineSinr, skewControl);
      float inclineValue = 1.0F;
      if (params.officialFormat && isFinite(inclineSinr)) {
        inclineValue =
            game::calcInclineValue(inclineSinr, baseFpWithControl, transYPx, stageAspect, params.stageHeight);
      }

      // A highlight texture carries a different canvas size, so its width ratio restores
      // the note to the same drawn size as the normal variant.
      const float widthScale = params.widthScaleFor(note.type, highlightVariant && params.multiPressHighlight);
      const float baseWidth =
          params.noteWidthBase * size * widthScale * scaleControl * inclineValue;
      const float noteWidth = baseWidth * xScale;
      const float noteHeight = (baseWidth / textureWidth) * textureHeight;

      float offsetX = static_cast<float>(note.positionX * game::PGR_WIDTH * params.stageWidth);
      const float xControl = evalControl(line, kControlX, controlHeight);
      if (params.officialFormat) offsetX *= inclineValue;
      if (isFinite(xControl) && xControl != 0.0F) offsetX *= xControl;
      const float noteAtX = frame.x + offsetX * frame.cosLine;
      const float noteAtY = frame.y + offsetX * frame.sinLine;
      const float dirX = above ? frame.sinLine : -frame.sinLine;
      const float dirY = above ? -frame.cosLine : frame.cosLine;

      if (noteCoveredByLine(note, frame, params.chartTimeSec, params.officialFormat, visualFp)) continue;

      const float headX = noteAtX + visualFp * dirX;
      const float headY = noteAtY + visualFp * dirY;
      const float drawRotation = frame.rotateDeg + (above ? 0.0F : 180.0F);
      const float shear = params.officialFormat
          ? 0.0F : std::tan(note.positionX * 75.375F * skewControl * kPi / 180.0F);
      if (!isNoteRectInArea(headX, headY,
              std::abs(noteWidth) + std::abs(noteHeight * shear), std::abs(noteHeight),
              drawRotation, params.cullBounds)) {
        continue;
      }
      float u0 = params.flipU ? 1.0F : 0.0F;
      const float u1 = params.flipU ? 0.0F : 1.0F;
      out.addQuadSheared(headX, headY, noteWidth, noteHeight, drawRotation, shear,
          tintComponent(note.hasTint, note.tintR), tintComponent(note.hasTint, note.tintG),
          tintComponent(note.hasTint, note.tintB), noteAlpha, u0, 0.0F, u1, 1.0F);
    }
  }
}

void buildHoldBatch(batch::SpriteBatch& out, const std::vector<LineFrame>& frames,
    const std::vector<int>& drawOrder, const game::Chart& chart, bool highlightVariant,
    float textureWidth, float textureHeight, const HoldUv& uv, bool holdRepeat, bool holdKeepHead,
    bool holdCompact, float bodyTextureWidth, float bodyTextureHeight,
    const NotePassParams& params) {
  const auto& lines = chart.judgeLineList;
  const auto lineCount = static_cast<int>(lines.size());
  if (lineCount <= 0 || textureWidth <= 0.0F) return;
  const float stageAspect =
      params.stageHeight > 1.0e-6F ? params.stageWidth / params.stageHeight : 16.0F / 9.0F;
  const bool reveal = params.revealHiddenObjects && params.revealHiddenFloorAlpha > 0.001F;
  const bool repeatBody = holdRepeat && bodyTextureWidth > 0.0F && bodyTextureHeight > 0.0F;
  const float headHpx = uv.headHeightPx;
  const float tailHpx = uv.tailHeightPx;

  for (const int lineIndex : drawOrder) {
    if (lineIndex < 0 || lineIndex >= lineCount) continue;
    const auto& line = *lines[static_cast<std::size_t>(lineIndex)];
    const auto& frame = frames[static_cast<std::size_t>(lineIndex)];
    if (!frame.renderable || !frame.noteValid) continue;
    if (frame.hideNotes && !reveal) continue;

    // Holds are kept in the hold-type list, sorted by start time. A hold can only still be
    // on screen when it started at most `maxHoldDurationSec` before the current time.
    const std::vector<game::Note*>* typedNotes = nullptr;
    if (line.notesByType.size() > static_cast<std::size_t>(game::NOTE_HOLD)) {
      typedNotes = &line.notesByType[static_cast<std::size_t>(game::NOTE_HOLD)];
    }
    const std::size_t typedCount =
        typedNotes != nullptr ? typedNotes->size() : line.notes.size();
    const auto noteAt = [&](std::size_t index) -> const game::Note& {
      return typedNotes != nullptr ? *(*typedNotes)[index] : line.notes[index];
    };
    const std::size_t firstNote = typedNotes != nullptr
        ? firstNoteAtOrAfter(noteAt, typedCount,
              params.chartTimeSec - line.maxHoldDurationSec)
        : 0U;
    const double bpm = frame.bpm > 0.0 ? frame.bpm : 120.0;
    const double beatScale = static_cast<double>(game::PGR_HEIGHT) * (kBeatSeconds / bpm) *
        static_cast<double>(params.stageHeight);

    for (std::size_t noteIndex = firstNote; noteIndex < typedCount; ++noteIndex) {
      const auto& note = noteAt(noteIndex);
      if (!note.isHold) continue;
      if (((note.morebets == 1) && params.multiPressHighlight) != highlightVariant) continue;
      if (note.holdEndTime < params.chartTimeSec) continue;

      const bool lineHiddenNote = reveal && frame.hideNotes;
      bool revealHidden = lineHiddenNote;
      if (!lineHiddenNote &&
          isBeforeAppearTime(note.rpeGlobalBeat, params.chartGlobalBeat, frame.alphaRaw)) {
        if (!reveal) continue;
        revealHidden = true;
      }

      const bool above = note.isAbove;
      // Holds marked with the official speed flag scroll at the base rate.
      const double speed = (note.useOfficialSpeed ? 1.0 : note.speed) *
          static_cast<double>(params.scrollSpeed);
      const double speedForBody = speed;
      const double baseFpD = (note.floorPosition - frame.floorPosition) * beatScale * speed;
      const auto baseFpPx = static_cast<float>(baseFpD);
      const float transYPx = note.yOffset * params.stageHeight *
          (params.officialFormat ? 1.0F : static_cast<float>(speed));
      // Same fast cull as the head pass: with no y control curve and no y offset the head and
      // tail positions are plain floor offsets, so a hold entirely outside the visible range
      // can be rejected before evaluating the control curves or the collapse animation.
      if (!reveal && line.noteYControls.empty() && note.yOffset == 0.0F
          && note.sect > params.chartTimeSec) {
        float tailFastFp = baseFpPx;
        if (note.useOfficialSpeed) {
          tailFastFp = baseFpPx
              + static_cast<float>(note.holdLength * params.stageHeight * speedForBody);
        } else if (isFinite(note.holdEndFloorPosition)
            && std::abs(note.holdEndFloorPosition - note.floorPosition) > 1.0e-9) {
          const double tailBaseFpD = (note.holdEndFloorPosition - frame.floorPosition) *
              beatScale * speedForBody;
          tailFastFp = static_cast<float>(tailBaseFpD);
        }
        const float lowest = std::min(baseFpPx, tailFastFp);
        const float highest = std::max(baseFpPx, tailFastFp);
        if (highest < frame.visibleMin - frame.visiblePad
            || lowest > frame.visibleMax + frame.visiblePad) {
          continue;
        }
      }
      const float controlHeight =
          game::rpeControlHeightFromBasePx(baseFpPx, note.yOffset, speed, stageAspect, params.stageHeight);
      const float yControl = params.officialFormat || note.isHold
          ? 1.0F : evalControl(line, kControlY, controlHeight);
      const float baseFpWithControl = applyYControlScale(baseFpPx, yControl);
      const float visualFp = params.officialFormat
          ? baseFpWithControl + transYPx
          : applyYControlScale(baseFpPx + transYPx, yControl);
      const float signedLength = applyYControlScale(
          static_cast<float>(note.holdLength * params.stageHeight * speedForBody), yControl);
      float tailVisualFp = visualFp + signedLength;
      if (!note.useOfficialSpeed) {
        const double holdEndFloor = note.holdEndFloorPosition;
        if (isFinite(holdEndFloor) && std::abs(holdEndFloor - note.floorPosition) > 1.0e-9) {
          const double tailBaseFpD = (holdEndFloor - frame.floorPosition) *
              static_cast<double>(game::PGR_HEIGHT) * (kBeatSeconds / bpm) *
              static_cast<double>(params.stageHeight) * speedForBody;
          const auto tailBaseFpPx = static_cast<float>(tailBaseFpD);
          const float tailBaseWithControl = applyYControlScale(tailBaseFpPx, yControl);
          tailVisualFp = tailBaseWithControl + transYPx;
        }
      }

      float noteAlpha;
      if (lineHiddenNote) {
        noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      } else {
        noteAlpha = computeNoteRenderAlpha(note.alpha, frame.alphaRaw, params.officialFormat);
        if (normalizeNoteAlpha01(note.alpha) <= 0.0F ||
            (frame.alphaRaw < 0.0F && noteAlpha <= 0.0F)) {
          if (reveal) revealHidden = true;
        }
        if (params.officialFormat && note.speed == 0.0) {
          noteAlpha = 0.0F;
          if (reveal) revealHidden = true;
        }
        noteAlpha *= params.frameGlobalAlpha;
        if (note.judgeResult == 3) noteAlpha *= 0.5F;

        if (frame.alphaRaw < 0.0F && noteAlpha > 0.0F && note.sect > params.chartTimeSec) {
          const int mode = static_cast<int>(std::floor(-frame.alphaRaw));
          if (mode == 2) {
            const float topFp = std::max(visualFp, tailVisualFp);
            const float bottomFp = std::min(visualFp, tailVisualFp);
            if (above) {
              if (topFp < -0.001F) {
                noteAlpha = 0.0F;
                if (reveal) revealHidden = true;
              }
            } else if (bottomFp > 0.001F) {
              noteAlpha = 0.0F;
              if (reveal) revealHidden = true;
            }
          }
        }

        const float beforeControl = noteAlpha;
        noteAlpha *= evalControl(line, kControlAlpha, controlHeight);
        if (beforeControl > 0.0F && noteAlpha <= 0.0F && reveal) revealHidden = true;

        if (noteAlpha > 0.0F && note.visibleTime >= 0.0F &&
            (note.sect - params.chartTimeSec) > note.visibleTime) {
          noteAlpha = 0.0F;
          if (reveal) revealHidden = true;
        }
        if (revealHidden) noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      }

      if (noteAlpha <= 0.0F) {
        if (!revealHidden) continue;
        noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      }

      // Once the head is reached the body collapses towards the judge line.
      const bool headPassedLine = holdHasStarted(note.sect, params.chartTimeSec, params.officialFormat);
      float drawHeadFp;
      float drawTailFp;
      if (headPassedLine) {
        drawHeadFp = transYPx;
        if (note.useOfficialSpeed) {
          const double totalSec = note.secht;
          if (totalSec > 1.0e-9) {
            const double elapsed = params.chartTimeSec - note.sect;
            const double remaining = 1.0 - std::min(1.0, std::max(0.0, elapsed / totalSec));
            drawTailFp = drawHeadFp + signedLength * static_cast<float>(remaining);
          } else {
            drawTailFp = drawHeadFp;
          }
        } else {
          drawTailFp = tailVisualFp;
        }
      } else {
        drawHeadFp = visualFp;
        drawTailFp = tailVisualFp;
      }

      if (noteAlpha > 0.0F && frame.alphaRaw < 0.0F && headPassedLine) {
        const int mode = static_cast<int>(std::floor(-frame.alphaRaw));
        if (mode == 2) {
          const float topFp = std::max(drawHeadFp, drawTailFp);
          const float bottomFp = std::min(drawHeadFp, drawTailFp);
          if (above) {
            if (topFp < -0.001F) {
              noteAlpha = 0.0F;
              if (reveal) revealHidden = true;
            }
          } else if (bottomFp > 0.001F) {
            noteAlpha = 0.0F;
            if (reveal) revealHidden = true;
          }
        }
      }
      if (noteAlpha <= 0.0F) {
        if (!revealHidden) continue;
        noteAlpha = params.revealHiddenFloorAlpha * params.frameGlobalAlpha;
      }

      const float rangeMin = std::min(drawHeadFp, drawTailFp);
      const float rangeMax = std::max(drawHeadFp, drawTailFp);
      if (rangeMax < frame.visibleMin - frame.visiblePad ||
          rangeMin > frame.visibleMax + frame.visiblePad) {
        continue;
      }

      const float size = (!isFinite(note.size) || note.size == 0.0F) ? 1.0F : note.size;
      const float xScale = (!isFinite(note.xScale) || note.xScale == 0.0F) ? 1.0F : note.xScale;
      float scaleControl = evalControl(line, kControlScale, controlHeight);
      if (!isFinite(scaleControl) || scaleControl == 0.0F) scaleControl = 1.0F;
      const float skewControl = evalControl(line, kControlSkew, controlHeight);
      const float inclineSinr = combineInclineSinr(frame.inclineSinr, skewControl);
      float inclineValue = 1.0F;
      if (params.officialFormat && isFinite(inclineSinr)) {
        inclineValue =
            game::calcInclineValue(inclineSinr, baseFpWithControl, transYPx, stageAspect, params.stageHeight);
      }

      const float widthScale = params.widthScaleFor(note.type, highlightVariant && params.multiPressHighlight);
      const float baseWidth =
          params.noteWidthBase * size * widthScale * scaleControl * inclineValue;
      const float noteWidth = baseWidth * xScale;
      const float noteHeadHeight = (headHpx > 0.0F) ? (baseWidth / textureWidth) * headHpx : baseWidth;
      const float tailHeight = (tailHpx > 0.0F) ? (baseWidth / textureWidth) * tailHpx : noteHeadHeight;

      float offsetX = static_cast<float>(note.positionX * game::PGR_WIDTH * params.stageWidth);
      const float noteAtX = frame.x + offsetX * frame.cosLine;
      const float noteAtY = frame.y + offsetX * frame.sinLine;
      const float dirX = above ? frame.sinLine : -frame.sinLine;
      const float dirY = above ? -frame.cosLine : frame.cosLine;

      if (noteCoveredByLine(note, frame, params.chartTimeSec, params.officialFormat, visualFp)) continue;

      if (!headPassedLine && drawTailFp < drawHeadFp) {
        if (drawHeadFp >= 0.0F && drawTailFp < 0.0F) {
          drawTailFp = 0.0F;
        } else if (drawHeadFp <= 0.0F && drawTailFp > 0.0F) {
          drawTailFp = 0.0F;
        }
      }
      const float unclampedTailFp = drawTailFp;
      if (headPassedLine && drawTailFp < drawHeadFp) {
        drawTailFp = std::max(drawTailFp, 0.0F);
      }
      const bool hideKeptHeadDuringTailClamp =
          note.clicked && headPassedLine && unclampedTailFp < drawHeadFp;
      const bool tailClampedToJudgeLine =
          headPassedLine && unclampedTailFp < 0.0F && drawTailFp == 0.0F;
      const bool reverseHoldLocked =
          headPassedLine && speed < 0.0 && drawTailFp >= drawHeadFp && !tailClampedToJudgeLine;
      if (reverseHoldLocked) continue;

      const float drawRotation = frame.rotateDeg + (above ? 0.0F : 180.0F);
      const float red = tintComponent(note.hasTint, note.tintR);
      const float green = tintComponent(note.hasTint, note.tintG);
      const float blue = tintComponent(note.hasTint, note.tintB);

      // Head.
      const bool keepHead = holdKeepHead && !hideKeptHeadDuringTailClamp;
      constexpr float kHeadOffset = 0.4F;
      if (!headPassedLine || keepHead) {
        float headCenterFp = visualFp - noteHeadHeight * kHeadOffset;
        if (holdCompact) headCenterFp += noteHeadHeight * 0.5F;
        const float headClampMin = holdHeadClampMin(noteHeadHeight, holdCompact, true);
        const float headDrawFp = std::max(headClampMin, headCenterFp);
        const float headX = noteAtX + headDrawFp * dirX;
        const float headY = noteAtY + headDrawFp * dirY;
        if (isNoteRectInArea(headX, headY, noteWidth, noteHeadHeight, drawRotation,
                params.cullBounds)) {
          addQuadFlippable(out, headX, headY, noteWidth, noteHeadHeight, drawRotation,
              red, green, blue, noteAlpha, 0.0F, uv.headV0, 1.0F, uv.headV1, params.flipU);
        }
      }

      // Body.
      float bodyStartFp;
      float bodyEndFp;
      if (headPassedLine) {
        bodyStartFp = drawHeadFp;
        bodyEndFp = drawTailFp;
      } else {
        const float bodyHeadAdjust =
            holdCompact ? noteHeadHeight * 0.1F : (0.5F - kHeadOffset) * noteHeadHeight;
        bodyStartFp = visualFp + bodyHeadAdjust;
        bodyEndFp = drawTailFp;
      }
      const bool isReverseHold = headPassedLine && (drawTailFp < drawHeadFp);
      float drawStartFp;
      float drawEndFp;
      if (isReverseHold) {
        drawStartFp = std::min(bodyStartFp, bodyEndFp);
        drawEndFp = std::max(bodyStartFp, bodyEndFp);
        drawEndFp = std::min(drawEndFp, 0.0F);
      } else {
        drawStartFp = std::max(0.0F, std::min(bodyStartFp, bodyEndFp));
        drawEndFp = std::max(0.0F, std::max(bodyStartFp, bodyEndFp));
      }
      const float drawLength = drawEndFp - drawStartFp;
      if (drawLength > 0.01F) {
        const float bodyX = noteAtX + ((drawStartFp + drawEndFp) * 0.5F) * dirX;
        const float bodyY = noteAtY + ((drawStartFp + drawEndFp) * 0.5F) * dirY;
        if (isNoteRectInArea(bodyX, bodyY, noteWidth, drawLength, drawRotation, params.cullBounds)) {
          bool emittedRepeatBody = false;
          if (repeatBody) {
            const float bodyTileHeight = noteWidth * bodyTextureHeight / bodyTextureWidth;
            if (bodyTileHeight > 1.0F) {
              const float repeatV1 = drawLength / bodyTileHeight;
              addQuadSelectFlippable(out, bodyX, bodyY, noteWidth, drawLength, drawRotation,
                  red, green, blue, noteAlpha, 0.0F, 0.0F, 1.0F, repeatV1, params.flipU, 1.0F);
              emittedRepeatBody = true;
            }
          }
          if (!emittedRepeatBody) {
            addQuadFlippable(out, bodyX, bodyY, noteWidth, drawLength, drawRotation,
                red, green, blue, noteAlpha, 0.0F, uv.bodyV0, 1.0F, uv.bodyV1, params.flipU);
          }
        }
      }

      // Tail.
      const float tailOffset = params.officialFormat
          ? (holdCompact ? tailHeight * 0.5F : tailHeight)
          : holdTailCapOffset(drawHeadFp, drawTailFp, tailHeight, holdCompact) + tailHeight * 0.5F;
      float tailTipFp = drawTailFp + tailOffset;
      const float tailClamp = std::abs(tailOffset);
      if (!params.officialFormat) {
      } else if (!isReverseHold) {
        if (tailTipFp < tailClamp) tailTipFp = tailClamp;
      } else if (tailTipFp > -tailClamp) {
        tailTipFp = -tailClamp;
      }
      const float tailPosFp = tailTipFp - (tailHeight * 0.5F);
      const float tailX = noteAtX + tailPosFp * dirX;
      const float tailY = noteAtY + tailPosFp * dirY;
      if (isNoteRectInArea(tailX, tailY, noteWidth, tailHeight, drawRotation, params.cullBounds)) {
        addQuadFlippable(out, tailX, tailY, noteWidth, tailHeight, drawRotation,
            red, green, blue, noteAlpha, 0.0F, uv.tailV0, 1.0F, uv.tailV1, params.flipU);
      }
    }
  }
}

} // namespace rechigros::render::scene
