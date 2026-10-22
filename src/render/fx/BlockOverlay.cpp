#include "rechigros/render/fx/BlockOverlay.hpp"

#include "rechigros/game/OfficialBlock.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <algorithm>
#include <cmath>

namespace rechigros::render::fx {
namespace {

// Below this a zone is too small to read and is skipped.
constexpr float kMinimumDrawableExtent = 1.0F;
// The smallest extent a zone needs before it is worth rasterising into a mask.
constexpr float kMinimumMaskExtent = 0.5F;
// The silhouette never needs more than this many columns.
constexpr int kMaximumColumns = 96;
constexpr int kMinimumColumns = 4;
// The inner noise field is capped independently of the edge columns.
constexpr int kMaximumNoiseColumns = 28;
constexpr int kMinimumNoiseRows = 3;
constexpr int kMaximumNoiseRows = 16;
// Only noise this bright shows through, and it is remapped over the remaining range.
constexpr float kNoiseCutoff = 0.86F;

// Emits one axis-aligned rectangle addressed by its top-left corner.
void emitRect(batch::SpriteBatch& target, float x, float y, float width, float height, float red,
    float green, float blue, float alpha) {
  if (!(width > 0.0F) || !(height > 0.0F) || !(alpha > 0.001F)) return;
  target.addQuadTopLeft(x, y, width, height, red, green, blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
}

// Appends one zone rectangle to a mask list. The coverage lives in the colour channels,
// so alpha stays zero and the mask can accumulate several zones per texel.
void appendMaskQuad(std::vector<BlockMaskQuad>& out, const game::OfficialBlock& block,
    float red, float green, float blue) {
  BlockMaskQuad quad;
  quad.centerX = block.centerX;
  quad.centerY = block.centerY;
  quad.width = block.halfW * 2.0F;
  quad.height = block.halfH * 2.0F;
  quad.rotationDeg = block.rotationDeg;
  quad.red = red;
  quad.green = green;
  quad.blue = blue;
  quad.alpha = 0.0F;
  out.push_back(quad);
}

} // namespace

float BlockOverlay::blockHash(float x, float y, float seed) noexcept {
  const float value = std::sin(x * 127.1F + y * 311.7F + seed * 74.7F) * 43758.5453F;
  return value - std::floor(value);
}

BlockOverlay::Style BlockOverlay::styleFor(int phase, bool subtract, float fade) noexcept {
  const bool active = phase == 2;
  Style style;
  // A subtract zone erases the scene instead of tinting it, so it carries no colour of
  // its own; only its alpha matters.
  style.fillRed = subtract ? 0.0F : (active ? ACTIVE_FILL_R : DISABLED_FILL_R);
  style.fillGreen = subtract ? 0.0F : (active ? ACTIVE_FILL_G : DISABLED_FILL_G);
  style.fillBlue = subtract ? 0.0F : (active ? ACTIVE_FILL_B : DISABLED_FILL_B);
  style.fillAlpha = (subtract ? (active ? 0.94F : 0.6F)
                              : (active ? ACTIVE_FILL_A : DISABLED_FILL_A)) * fade;
  style.edgeRed = active ? ACTIVE_EDGE_R : 1.0F;
  style.edgeGreen = active ? ACTIVE_EDGE_G : 1.0F;
  style.edgeBlue = active ? ACTIVE_EDGE_B : 1.0F;
  style.edgeAlpha = (active ? ACTIVE_EDGE_A : DISABLED_EDGE_A) * fade;
  style.sparkRed = active ? SPARK_ACTIVE_R : SPARK_DISABLED_R;
  style.sparkGreen = active ? SPARK_ACTIVE_G : SPARK_DISABLED_G;
  style.sparkBlue = active ? SPARK_ACTIVE_B : SPARK_DISABLED_B;
  style.glow = active && GLOW_INTENSITY > 0.001F;
  return style;
}

void BlockOverlay::rebuildWindow(const std::vector<game::OfficialBlock>& blocks,
    float chartTimeSec) const {
  window_.data = blocks.data();
  window_.size = blocks.size();
  window_.order.resize(blocks.size());
  for (std::size_t index = 0; index < blocks.size(); ++index) window_.order[index] = index;
  std::stable_sort(window_.order.begin(), window_.order.end(),
      [&blocks](std::size_t first, std::size_t second) {
        return blocks[first].appearTime < blocks[second].appearTime;
      });
  window_.active.clear();
  window_.released.clear();
  window_.cursor = 0;
  // A jump in time can leave arbitrary runtime state behind, so the whole list returns to
  // its reset state and the live entries below start their animation again.
  auto& mutableBlocks = const_cast<std::vector<game::OfficialBlock>&>(blocks);
  for (auto& block : mutableBlocks) block.resetRuntime();
  while (window_.cursor < window_.order.size()
      && blocks[window_.order[window_.cursor]].appearTime <= chartTimeSec) {
    window_.active.push_back(window_.order[window_.cursor]);
    ++window_.cursor;
  }
  std::sort(window_.active.begin(), window_.active.end());
  window_.lastTimeSec = chartTimeSec;
}

void BlockOverlay::advanceWindow(const std::vector<game::OfficialBlock>& blocks,
    float chartTimeSec) const {
  if (window_.data != blocks.data() || window_.size != blocks.size()
      || chartTimeSec < window_.lastTimeSec) {
    rebuildWindow(blocks, chartTimeSec);
    return;
  }
  if (chartTimeSec == window_.lastTimeSec) return;
  window_.lastTimeSec = chartTimeSec;
  // Entries become live in appear-time order, so the cursor only moves forward.
  bool added = false;
  while (window_.cursor < window_.order.size()
      && blocks[window_.order[window_.cursor]].appearTime <= chartTimeSec) {
    window_.active.push_back(window_.order[window_.cursor]);
    ++window_.cursor;
    added = true;
  }
  if (added) std::sort(window_.active.begin(), window_.active.end());
  // Entries that leave their window return to the reset state, exactly like a pass that
  // visits every authored entry would leave them.
  auto& mutableBlocks = const_cast<std::vector<game::OfficialBlock>&>(blocks);
  window_.released.clear();
  std::erase_if(window_.active, [&mutableBlocks, chartTimeSec, this](std::size_t index) {
    const auto& block = mutableBlocks[index];
    if (chartTimeSec < block.appearTime || chartTimeSec >= block.disappearTime) {
      this->window_.released.push_back(index);
      return true;
    }
    return false;
  });
  for (const std::size_t index : window_.released) mutableBlocks[index].resetRuntime();
}

void BlockOverlay::updateAll(std::vector<game::OfficialBlock>& blocks, float chartTimeSec,
    float deltaTimeSec, float stageX, float stageY, float stageWidth, float stageHeight) const {
  if (blocks.empty()) return;
  // Only the zones inside their appear/disappear window need their event evaluation run;
  // a chapter-level chart can author more than ten thousand entries while a handful are
  // live at any instant.
  advanceWindow(blocks, chartTimeSec);
  for (const std::size_t index : window_.active) {
    blocks[index].update(chartTimeSec, stageX, stageY, stageWidth, stageHeight, deltaTimeSec);
  }
}

void BlockOverlay::collectMaskQuads(const std::vector<game::OfficialBlock>& blocks,
    float chartTimeSec, std::vector<BlockMaskQuad>& maskA,
    std::vector<BlockMaskQuad>& maskB) const {
  // The mask layout the shipped material expects: the first mask holds the active layer
  // (R normal, G subtract) plus the warning silhouette in B with its fade as the value;
  // the second holds the subtract layer (R, and B while the zone is ready) plus the ready
  // highlight in G. Overlapping subtract zones add, and the composite's gate then turns a
  // doubly written texel into a hole instead of a visible zone.
  advanceWindow(blocks, chartTimeSec);
  for (const std::size_t index : window_.active) {
    const auto& block = blocks[index];
    if (!block.visible) continue;
    if (block.halfW * 2.0F <= kMinimumMaskExtent || block.halfH * 2.0F <= kMinimumMaskExtent) {
      continue;
    }
    const int phase = block.phase(chartTimeSec);
    if (phase == 2) {
      // Active layer, one channel per kind so a hole is a separate mask value.
      if (block.isSubtract) {
        appendMaskQuad(maskA, block, 0.0F, BlockCompositePass::MASK_WRITE_ALPHA, 0.0F);
      } else {
        appendMaskQuad(maskA, block, 1.0F, 0.0F, 0.0F);
      }
      continue;
    }
    if (!block.isSubtract) {
      // Warning silhouette, drawn at the true position rather than the displaced one.
      const float fade = block.showProgress();
      if (fade > 0.002F) appendMaskQuad(maskA, block, 0.0F, 0.0F, fade);
      if (phase == 1) appendMaskQuad(maskB, block, 0.0F, block.showProgress(), 0.0F);
      continue;
    }
    // Subtract zones that are not active are collected in the second mask.
    const float ready = phase == 1 ? block.showProgress() : 0.0F;
    appendMaskQuad(maskB, block, BlockCompositePass::MASK_WRITE_ALPHA, 0.0F, BlockCompositePass::MASK_WRITE_ALPHA * ready);
  }
}

int BlockOverlay::draw(const game::OfficialBlock& block, float chartTimeSec,
    batch::SpriteBatch& target) const {
  const int phase = block.phase(chartTimeSec);
  const bool active = phase == 2;

  const float width = block.halfW * 2.0F;
  const float height = block.halfH * 2.0F;
  if (width <= kMinimumDrawableExtent || height <= kMinimumDrawableExtent) return 0;

  // The block's appearance fades with the one-shot show animation; a fully active zone is
  // already at full strength.
  const float fade = active ? 1.0F : block.showProgress();
  if (!(fade > 0.002F)) return 0;
  const Style style = styleFor(phase, block.isSubtract, fade);

  // The silhouette is rasterised at a fixed pixel scale so the edge reads as a staircase.
  const float smaller = std::min(width, height);
  float step = std::max(6.0F, smaller / BACKGROUND_PIXEL_SCALE / 3.0F);
  const int columns = std::clamp(static_cast<int>(std::lround(width / step)), kMinimumColumns,
      kMaximumColumns);
  step = width / static_cast<float>(columns);

  const float left = block.centerX - width * 0.5F;
  const float top = block.centerY - height * 0.5F;
  const float bottom = block.centerY + height * 0.5F;
  const float edgeWidth = std::max(1.5F, smaller * 0.012F);

  // Edge height and per-column horizontal displacement.
  std::vector<float> edgeY(static_cast<std::size_t>(columns) + 1, bottom);
  std::vector<float> displaceX(static_cast<std::size_t>(columns) + 1, 0.0F);
  const float cell = std::max(1.0F, smaller * 0.22F);
  const float falloff = std::max(4.0F, smaller * 0.06F);
  for (int column = 0; column <= columns; ++column) {
    const float x = left + static_cast<float>(column) * step;
    const float noise = blockHash(std::floor(x / cell), 0.0F, 0.5F);
    const float noiseB = blockHash(std::floor(x / cell) * 0.5F, 1.0F, 1.5F);
    edgeY[static_cast<std::size_t>(column)] = bottom - noise * falloff;
    const float rowNoise = blockHash(std::floor(x / (step * 4.0F)),
        std::floor(chartTimeSec * 1.5F), 2.5F);
    displaceX[static_cast<std::size_t>(column)] =
        (rowNoise - 0.5F) * step * 1.6F * (0.35F + 0.65F * noiseB);
  }

  int quads = 0;
  const auto emit = [&target, &quads](float x, float y, float w, float h, float red, float green,
                       float blue, float alpha) {
    if (!(w > 0.0F) || !(h > 0.0F) || !(alpha > 0.001F)) return;
    if (target.addQuadTopLeft(x, y, w, h, red, green, blue, alpha, 0.0F, 0.0F, 1.0F, 1.0F)) {
      ++quads;
    }
  };

  if (style.glow) {
    // Two stacked halos widen the soft edge of an active zone.
    const float pad = std::max(6.0F, smaller * 0.05F);
    const float alpha = 0.16F * GLOW_INTENSITY * fade;
    target.addQuad(block.centerX, block.centerY, width + pad * 2.0F, height + pad * 2.0F,
        block.rotationDeg, GLOW_R, GLOW_G, GLOW_B, alpha * 0.5F, 0.0F, 0.0F, 1.0F, 1.0F);
    target.addQuad(block.centerX, block.centerY, width + pad, height + pad, block.rotationDeg,
        GLOW_R, GLOW_G, GLOW_B, alpha, 0.0F, 0.0F, 1.0F, 1.0F);
    quads += 2;
  }

  // Inner noise field: a sparse grid of bright cells above the silhouette edge.
  const int noiseColumns = std::min(columns, kMaximumNoiseColumns);
  const int noiseRows = std::clamp(
      static_cast<int>(std::lround(height / std::max(step, 12.0F))), kMinimumNoiseRows,
      kMaximumNoiseRows);
  const float noiseStepX = width / static_cast<float>(noiseColumns);
  const float noiseStepY = height / static_cast<float>(noiseRows);
  const float noisePhase = std::floor(chartTimeSec * 1.8F);
  if (!block.isSubtract) {
    for (int row = 0; row < noiseRows; ++row) {
      for (int column = 0; column < noiseColumns; ++column) {
        const float noise = blockHash(static_cast<float>(column), static_cast<float>(row),
            noisePhase);
        if (noise < kNoiseCutoff) continue;
        const float alpha = (active ? 0.20F : 0.10F) *
            (0.4F + 0.6F * (noise - kNoiseCutoff) / (1.0F - kNoiseCutoff)) * fade;
        const float x = left + static_cast<float>(column) * noiseStepX;
        const float y = top + static_cast<float>(row) * noiseStepY;
        const int edgeColumn = std::min(columns,
            static_cast<int>(static_cast<float>(column) * (static_cast<float>(columns) /
                static_cast<float>(noiseColumns))));
        if (y > edgeY[static_cast<std::size_t>(edgeColumn)]) continue;
        emit(x, y, noiseStepX, noiseStepY, 1.0F, 0.0F, 0.0F, alpha);
      }
    }
  }

  // Silhouette: one column at a time, then the top edge and both side edges.
  for (int column = 0; column < columns; ++column) {
    const float x0 = left + static_cast<float>(column) * step +
        displaceX[static_cast<std::size_t>(column)];
    const float x1 = left + static_cast<float>(column + 1) * step +
        displaceX[static_cast<std::size_t>(column + 1)];
    const float bottomY = std::min(edgeY[static_cast<std::size_t>(column)],
        edgeY[static_cast<std::size_t>(column + 1)]);
    if (bottomY <= top) continue;
    const float segmentWidth = std::max(1.0F, x1 - x0);
    emit(x0, top, segmentWidth, bottomY - top, style.fillRed, style.fillGreen, style.fillBlue,
        style.fillAlpha);
    emit(x0, bottomY - edgeWidth, segmentWidth, edgeWidth, style.edgeRed, style.edgeGreen,
        style.edgeBlue, style.edgeAlpha);
    if (column == columns - 1) {
      emit(x1 - edgeWidth, top, edgeWidth, bottomY - top, style.edgeRed, style.edgeGreen,
          style.edgeBlue, style.edgeAlpha);
    }
    if (column == 0) {
      emit(x0, top, edgeWidth, bottomY - top, style.edgeRed, style.edgeGreen, style.edgeBlue,
          style.edgeAlpha);
    }
  }
  emit(left, top, width, edgeWidth, style.edgeRed, style.edgeGreen, style.edgeBlue,
      style.edgeAlpha);

  // Sparks: short-lived points that shimmer across the zone.
  const int sparks = block.isSubtract ? 0 : (active ? 14 : 6);
  const float sparkPhase = std::floor(chartTimeSec * 6.0F);
  for (int index = 0; index < sparks; ++index) {
    const float sparkX = blockHash(static_cast<float>(index), 3.0F, sparkPhase);
    const float sparkY = blockHash(static_cast<float>(index), 7.0F, sparkPhase);
    const float sparkScale = blockHash(static_cast<float>(index), 11.0F, sparkPhase);
    const float x = left + sparkX * width;
    const float y = top + sparkY * (height * 0.85F);
    const float size = std::max(2.0F, std::min(step, height * 0.03F) * (0.4F + sparkScale));
    const float alpha = (active ? 0.5F : 0.25F) * (0.3F + 0.7F * sparkScale) * fade;
    emit(x, y, size, size, style.sparkRed, style.sparkGreen, style.sparkBlue, alpha);
  }
  return quads;
}

void BlockOverlay::drawVisible(const std::vector<game::OfficialBlock>& blocks,
    float chartTimeSec, batch::SpriteBatch& target, const math::Mat4& projection,
    const gl::Texture& white) const {
  // Only the zones inside their window are candidates; `updateAll` has already refreshed
  // that small list. The batch is still flushed in chunks because a burst of zones can be
  // live at once and the batch has a fixed capacity.
  constexpr int kFlushThreshold = batch::MAX_BATCH_SPRITES / 2;
  for (const std::size_t index : window_.active) {
    const auto& block = blocks[index];
    if (!block.visible) continue;
    if (target.size() >= kFlushThreshold) {
      target.flush(projection, white, nullptr);
      target.begin();
    }
    draw(block, chartTimeSec, target);
  }
  if (!target.empty()) target.flush(projection, white, nullptr);
}

void BlockOverlay::drawAll(std::vector<game::OfficialBlock>& blocks, float chartTimeSec,
    float deltaTimeSec, float stageX, float stageY, float stageWidth, float stageHeight,
    batch::SpriteBatch& target, const math::Mat4& projection, const gl::Texture& white) const {
  if (blocks.empty()) return;
  updateAll(blocks, chartTimeSec, deltaTimeSec, stageX, stageY, stageWidth, stageHeight);
  drawVisible(blocks, chartTimeSec, target, projection, white);
}

} // namespace rechigros::render::fx
