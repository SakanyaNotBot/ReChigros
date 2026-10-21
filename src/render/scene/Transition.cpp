#include "rechigros/render/scene/Transition.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rechigros::render::scene {
namespace {

// Keyframes of the decorative line expansion, sampled linearly. The 55-frame window
// carries 56 samples because both endpoints are included.
constexpr std::array<float, 56> kIntroLineScale = {
    0.0000000F, 0.0004785F, 0.0011947F, 0.0021729F,
    0.0034413F, 0.0050326F, 0.0069859F, 0.0093477F,
    0.0121742F, 0.0155344F, 0.0195135F, 0.0242201F,
    0.0297935F, 0.0364185F, 0.0443471F, 0.0539350F,
    0.0657090F, 0.0804966F, 0.0997056F, 0.1260097F,
    0.1653046F, 0.2320033F, 0.3332065F, 0.4175731F,
    0.4773408F, 0.5234603F, 0.5614561F, 0.5940762F,
    0.6228563F, 0.6487398F, 0.6723500F, 0.6941195F,
    0.7143646F, 0.7333211F, 0.7511719F, 0.7680620F,
    0.7841066F, 0.7994009F, 0.8140248F, 0.8280444F,
    0.8415143F, 0.8544856F, 0.8669972F, 0.8790865F,
    0.8907873F, 0.9021233F, 0.9131252F, 0.9238101F,
    0.9342016F, 0.9443144F, 0.9541675F, 0.9637740F,
    0.9731488F, 0.9823037F, 0.9912493F, 1.0000000F};

// Root scale of the level start UI, one sample per frame of its 40-frame window.
constexpr std::array<float, 41> kLevelStartRootScale = {
    1.1875000F, 1.1871370F, 1.1859880F, 1.1839500F,
    1.1809180F, 1.1767850F, 1.1714640F, 1.1649030F,
    1.1571190F, 1.1482240F, 1.1384360F, 1.1280640F,
    1.1174500F, 1.1069190F, 1.0967300F, 1.0870610F,
    1.0780130F, 1.0696320F, 1.0619220F, 1.0548630F,
    1.0484230F, 1.0425640F, 1.0372430F, 1.0324230F,
    1.0280630F, 1.0241300F, 1.0205910F, 1.0174170F,
    1.0145810F, 1.0120580F, 1.0098270F, 1.0078690F,
    1.0061640F, 1.0046970F, 1.0034540F, 1.0024200F,
    1.0015830F, 1.0009330F, 1.0004580F, 1.0001500F,
    1.0000000F};
constexpr float kLevelStartRootScaleStart = 1.1875F;

float clamp01(float value) {
  if (!(value > 0.0F)) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}

template <std::size_t N>
float sampleCurve(const std::array<float, N>& samples, float progress) {
  if constexpr (N == 0) {
    return clamp01(progress);
  } else if constexpr (N == 1) {
    return samples[0];
  } else {
    const float position = clamp01(progress) * static_cast<float>(N - 1);
    const auto index = static_cast<std::size_t>(std::floor(position));
    if (index >= N - 1) return samples[N - 1];
    const float fraction = position - static_cast<float>(index);
    return samples[index] + (samples[index + 1] - samples[index]) * fraction;
  }
}

} // namespace

float introLineScale(float progress) noexcept {
  return sampleCurve(kIntroLineScale, progress);
}

float introUiEase(float progress) noexcept {
  const float rootScale = sampleCurve(kLevelStartRootScale, progress);
  const float denominator = kLevelStartRootScaleStart - 1.0F;
  if (std::abs(denominator) < 1.0e-6F) return clamp01(progress);
  return clamp01((kLevelStartRootScaleStart - rootScale) / denominator);
}

float smoothStep01(float progress) noexcept {
  const float p = clamp01(progress);
  return p * p * (3.0F - 2.0F * p);
}

float outroDimMix(bool inOutro, float elapsedSec) noexcept {
  if (!inOutro) return 0.0F;
  return smoothStep01(elapsedSec / std::max(0.001F, UI_OUTRO_DURATION_SEC));
}

float outroLineCollapseScale(bool inOutro, float elapsedSec) noexcept {
  if (!inOutro) return 0.0F;
  const float local = (elapsedSec - OUTRO_LINE_COLLAPSE_START_SEC) /
      OUTRO_LINE_COLLAPSE_DURATION_SEC;
  if (local < 0.0F || local >= 1.0F) return 0.0F;
  return 1.0F - local;
}

bool outroLinesVisible(bool inOutro, float elapsedSec) noexcept {
  if (!inOutro) return true;
  // Before the collapse window the lines keep their full length; inside it they shrink;
  // afterwards they are gone.
  return elapsedSec < OUTRO_LINE_COLLAPSE_START_SEC + OUTRO_LINE_COLLAPSE_DURATION_SEC;
}

float introDimProgress(float introLineScaleValue, bool officialFormat) noexcept {
  const float decorHalfFactor = officialFormat ? 0.72F : (4000.0F / 1350.0F / 2.0F);
  const float reachScale = 0.5F / decorHalfFactor;
  if (reachScale >= 1.0F) return clamp01(introLineScaleValue);
  return clamp01(introLineScaleValue / reachScale);
}

float stageTextureAlpha(bool introStarted, float introLineScaleValue, float introDimMix,
    float backgroundDim, bool inOutro, float outroElapsedSec) noexcept {
  const float mix = introStarted ? introDimMix : 0.0F;
  const float dim = clamp01(backgroundDim);
  const float gameplayEnd = 0.5F * (1.0F - 0.7F * dim);
  const float target = INTRO_START_BRIGHTNESS + (gameplayEnd - INTRO_START_BRIGHTNESS) * mix;
  const float overlay = 0.7F * dim * mix;
  const float introAlpha = clamp01(target / std::max(0.05F, 1.0F - overlay));
  // The result page paints the artwork at full strength under its own dim.
  const float outroAlpha = 1.0F;
  return introAlpha + (outroAlpha - introAlpha) * outroDimMix(inOutro, outroElapsedSec);
}

float sideArtworkAlpha(bool introStarted, float introDimMix) noexcept {
  const float mix = introStarted ? introDimMix : 0.0F;
  const float target = INTRO_START_BRIGHTNESS + (SIDE_FACTOR - INTRO_START_BRIGHTNESS) * mix;
  const float overlay = (1.0F - SIDE_FACTOR) * mix;
  return clamp01(target / std::max(0.05F, 1.0F - overlay));
}

float backgroundDimAlpha(bool introStarted, float introDimMix, float backgroundDim,
    bool inOutro, float outroElapsedSec) noexcept {
  const float mix = introStarted ? introDimMix : 0.0F;
  const float base = 0.7F * clamp01(backgroundDim) * mix;
  const float outro = 1.0F - OUTRO_BG_TRANSMISSION;
  return base + (outro - base) * outroDimMix(inOutro, outroElapsedSec);
}

float sideDimAlpha(bool introStarted, float introDimMix, bool inOutro,
    float outroElapsedSec) noexcept {
  const float mix = introStarted ? introDimMix : 0.0F;
  const float base = clamp01(1.0F - SIDE_FACTOR) * mix;
  const float outro = 1.0F - OUTRO_BG_TRANSMISSION;
  return base + (outro - base) * outroDimMix(inOutro, outroElapsedSec);
}

} // namespace rechigros::render::scene
