#pragma once

#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/Texture.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace rechigros::render::fx {

// Tuning values of one bloom pass, all driven by the effect's animated tracks.
struct BloomSettings {
  float brightness{0.0F};
  float threshold{0.308F};
  float radius{1.1F};
  float intensity{1.85F};
  float sampleRate{0.45F};
  int iterations{3};
};

// Four-stage DualKawase bloom: luminance prefilter, iterative down-sampling,
// iterative up-sampling, then a composite that screen-blends the blurred result onto
// the source. The intermediate targets are pooled and reused across frames.
class BloomPass final {
public:
  BloomPass() = default;
  ~BloomPass() = default;
  BloomPass(const BloomPass&) = delete;
  BloomPass& operator=(const BloomPass&) = delete;

  // Compiles the four programs. Safe to call repeatedly; a failed compile leaves the
  // pass unusable and `ready()` reports false.
  bool prepare(const std::filesystem::path& assetRoot);
  void release() noexcept;
  [[nodiscard]] bool ready() const noexcept { return ready_; }

  // Runs the whole chain from `input` into `output`. When the settings ask for no
  // bloom the input is blitted through unchanged, matching the reference's
  // `radius * brightness > 0` gate.
  void apply(const BloomSettings& settings, const gl::Texture& input, gl::Framebuffer& output,
      double chartTimeSec);

private:
  struct Stage {
    FullscreenPass pass;
    bool valid{};
  };

  // Render target reused across frames, keyed only by size.
  struct Target {
    gl::Framebuffer fbo;
    int width{};
    int height{};
  };

  [[nodiscard]] Target* acquire(int width, int height, const Target* exclude);
  void releaseTargets() noexcept;
  [[nodiscard]] bool compile(Stage& stage, const char* fileName);

  Stage luminance_;
  Stage down_;
  Stage up_;
  Stage composite_;
  std::vector<std::unique_ptr<Target>> pool_;
  std::filesystem::path assetRoot_;
  bool ready_{};
};

} // namespace rechigros::render::fx
