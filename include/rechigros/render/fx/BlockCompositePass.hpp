#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"

#include <filesystem>
#include <vector>

namespace rechigros::render::fx {

// One rectangle a zone contributes to a mask target. Positions are screen pixels with
// the origin at the top left, matching how the zones are laid out. Several rectangles
// accumulate into the same texel, which is how a hole written twice cancels.
struct BlockMaskQuad {
  float centerX{};
  float centerY{};
  float width{};
  float height{};
  float rotationDeg{};
  float red{};
  float green{};
  float blue{};
  float alpha{};
};

// The screen-space composite that makes the picture look torn inside a red zone.
//
// The caller rasterises the visible zones into two mask targets, and two pre-passes turn
// those masks into a coverage target and an edge/glow target at reduced resolution. The
// composite then replaces the scene inside the covered area with a displaced, tinted and
// spark-overlaid resample of it, and leaves everything else untouched. The scene being
// resampled is a colour copy of the finished frame, so it must be a texture rather than
// the target the composite writes to.
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
  // Reduced resolution of every intermediate target: the masks and the coverage target
  // are sampled point-wise, the edge/glow target bilinearly.
  static constexpr int MASK_DIVISOR = 8;
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
  // Size of a reduced target for a screen dimension, never below one texel.
  [[nodiscard]] static int reducedSize(int screenPixels, int divisor) noexcept;

  // Loads both shaders. The three noise textures are owned by the caller's texture cache.
  [[nodiscard]] bool prepare(
      const std::filesystem::path& assetRoot, gl::Texture* displace, gl::Texture* spark,
      gl::Texture* noise) noexcept;
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }
  // True once resize() has allocated the intermediate targets. A prepared pass that has
  // not been given a viewport yet cannot draw, so callers gate on both.
  [[nodiscard]] bool allocated() const noexcept {
    return compose_.valid() && effect_.valid() && maskFbo_[0].valid();
  }

  // Allocates the masks and the reduced targets for a viewport of this size.
  void resize(int width, int height) noexcept;

  // Rows the composite may treat as the play field, in screen pixels. Pixels outside it
  // are left untouched, which keeps the composite from distorting a letterbox. Defaults
  // to the whole viewport.
  void setStageRect(float left, float top, float right, float bottom) noexcept;

  // Mask targets the caller rasterises the zones into: 0 accumulates the visible layer,
  // 1 accumulates the subtract and ready layers. Both are cleared by rasteriseMasks().
  [[nodiscard]] gl::Framebuffer& mask(int index) noexcept { return maskFbo_[index]; }
  void clearMasks() noexcept;

  // Rasterises both masks in one call. `maskA` quads accumulate into the R, G and B
  // channels of the first mask, `maskB` quads into the R, G and B channels of the
  // second; the caller decides which channel each zone writes by filling the colour.
  void rasteriseMasks(const std::vector<BlockMaskQuad>& maskAQuads,
      const std::vector<BlockMaskQuad>& maskBQuads) noexcept;

  // Runs the compose, edge and glow pre-passes for this frame. Kept separate from the
  // composite draw so a diagnostic caller can inspect the intermediate targets.
  void runPrepasses(float chartTimeSec);

  // A finger held over the block area, in the shader's own space: `x` is a fraction of the
  // width and `y` a fraction of the height measured from the bottom, which is what the
  // shipped hover shader expects.
  struct TouchPoint {
    float x{};
    float y{};
  };

  // The reference keeps a point alive while the finger stays down and clears it on release,
  // so the glow does not fade as the finger moves. Callers pass the live held points and the
  // wall clock the shine wave runs on; both default to the no-touch state.
  struct TouchState {
    TouchPoint points[10]{};
    int count{};
    double realTimeSec{};
  };

  // Composites `scene` over the currently bound target. The caller's framebuffer binding
  // and viewport are restored before returning, so this may run mid-frame.
  void draw(gl::Texture& scene, float chartTimeSec);
  void draw(gl::Texture& scene, float chartTimeSec, const TouchState& touch);

  // The composite program, exposed so a probe can assert its uniforms resolve.
  [[nodiscard]] gl::ShaderProgram& compositeProgram() noexcept { return composite_.program(); }

  // Intermediate targets, exposed so a standalone probe can dump each stage.
  [[nodiscard]] gl::Framebuffer& composeTarget() noexcept { return compose_; }
  [[nodiscard]] gl::Framebuffer& effectTarget() noexcept { return effect_; }
  [[nodiscard]] gl::Framebuffer& pingTarget(int index) noexcept { return ping_[index]; }

private:
  void uploadMaterials() const;
  void ensureMaskRasteriser() noexcept;
  // Loads the hover sprite and builds its program once per asset root. Returns false when
  // the sprite is missing, which leaves the zone quads as the only mask content.
  bool ensureHoverProgram(const std::filesystem::path& assetRoot) noexcept;

  FullscreenPass prepass_;
  FullscreenPass composite_;
  // Raw-colour additive quad program used only to fill the mask targets. The sprite
  // batch cannot be reused: it premultiplies its colour by alpha, while a mask carries
  // its coverage in the colour channels and uses alpha as a separate weight.
  gl::ShaderProgram maskProgram_;
  unsigned int maskVao_{};
  unsigned int maskVbo_{};
  // Reused between frames: a chapter-level chart can hand over dozens of quads per frame
  // and allocating the vertex staging buffer each time would churn the heap mid-frame.
  std::vector<float> maskVertices_;
  gl::Framebuffer maskFbo_[2];
  gl::Framebuffer compose_;
  gl::Framebuffer ping_[2];
  gl::Framebuffer effect_;
  gl::Texture* displaceMap_{};
  gl::Texture* sparkMap_{};
  gl::Texture* noiseMap_{};
  gl::Texture* touchMap_{};
  std::filesystem::path assetRoot_;
  std::vector<float> hoverVertices_;
  bool hoverReady_{};
  // A soft sprite with its own alpha, used for the hover channel instead of a constant.
  gl::Texture hoverTexture_;
  gl::ShaderProgram hoverProgram_;
  unsigned int hoverVao_{};
  int width_{};
  int height_{};
  float stageLeft_{};
  float stageTop_{};
  float stageRight_{};
  float stageBottom_{};
  bool ready_{};
};

} // namespace rechigros::render::fx
