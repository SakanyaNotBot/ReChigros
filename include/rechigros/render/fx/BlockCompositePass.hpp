#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"

#include <filesystem>

namespace rechigros::render::fx {

// The screen-space composite that makes the picture look torn inside a red zone.
//
// A zone is rasterised into two full-resolution masks by the caller, then two pre-passes
// turn those masks into a coverage target and an edge/glow target at reduced resolution.
// The composite replaces the scene inside the covered area with a displaced, tinted and
// spark-overlaid resample of it, and leaves everything else untouched.
//
// The shaders ship as assets, so this class only drives them: their sources are loaded
// unchanged and the shader program rewrites the legacy spellings for the active context.
class BlockCompositePass final {
public:
  // Number of ping-pong dilation steps that build the outer glow.
  static constexpr int GLOW_PASSES = 5;
  // Coverage gate applied to the subtract mask, as the shipped material thresholds it.
  static constexpr float GATE_LOW = 0.09F;
  static constexpr float GATE_HIGH = 0.12F;
  // One sprite writes 0.1 into a mask; two overlapping sprites write 0.2.
  static constexpr float MASK_WRITE_ALPHA = 0.1F;
  // Reduced resolution of the coverage target, and of the edge/glow target.
  static constexpr int COMPOSE_DIVISOR = 8;
  static constexpr int EFFECT_DIVISOR = 4;
  // The material animates on chart time divided by this value.
  static constexpr float TIME_DIVISOR = 20.0F;
  // Diagonal direction of the noise displacement.
  static constexpr float DISPLACE_DIRECTION = 0.70710678F;

  // Weight of one glow ring; ring 0 is the outermost and carries the least weight.
  [[nodiscard]] static float glowRingWeight(int pass) noexcept;
  // Coverage gate: 1 where the mask is at least GATE_LOW, -1 where it reaches GATE_HIGH,
  // 0 between. A hole written twice therefore cancels instead of being dropped.
  [[nodiscard]] static float gate(float value) noexcept;

  // Loads both shaders. The two noise textures are owned by the caller's texture cache.
  [[nodiscard]] bool prepare(
      const std::filesystem::path& assetRoot, gl::Texture* displace, gl::Texture* spark,
      gl::Texture* noise) noexcept;
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Allocates the masks and the reduced targets for a viewport of this size.
  void resize(int width, int height) noexcept;

  // Mask targets the caller rasterises the zones into: 0 accumulates the visible zones,
  // 1 accumulates the subtract zones. Both are cleared here before drawing.
  [[nodiscard]] gl::Framebuffer& mask(int index) noexcept { return maskFbo_[index]; }
  void clearMasks() noexcept;

  // Runs the pre-passes and composites `scene` over the currently bound target. The
  // viewport must already be set to the scene size.
  void draw(gl::Texture& scene, float chartTimeSec);

private:
  void uploadMaterials() const;
  void runPrepasses(float chartTimeSec);

  FullscreenPass prepass_;
  FullscreenPass composite_;
  gl::Framebuffer maskFbo_[2];
  gl::Framebuffer compose_;
  gl::Framebuffer ping_[2];
  gl::Framebuffer effect_;
  gl::Texture* displaceMap_{};
  gl::Texture* sparkMap_{};
  gl::Texture* noiseMap_{};
  int width_{};
  int height_{};
  bool ready_{};
};

} // namespace rechigros::render::fx
