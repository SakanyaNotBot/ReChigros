#pragma once

#include "rechigros/game/PrprEffect.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/fx/BloomPass.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::render::asset { class TextureCache; }
namespace rechigros::render::gl { class Framebuffer; }

namespace rechigros::render::fx {

// Uniform values a shader preset declares with a trailing `// %value%` comment.
using UniformDefaults = std::unordered_map<std::string, std::vector<float>>;

// Fullscreen storyboard shader chain. Shaders are resolved from the chart's own
// sources first and then from the packaged preset list; each active effect runs
// as one pass over a ping-pong pair of render targets.
class PrprChain final {
public:
  PrprChain() = default;
  ~PrprChain();
  PrprChain(const PrprChain&) = delete;
  PrprChain& operator=(const PrprChain&) = delete;

  // Registers the presets used by a chart. Custom sources win over presets.
  void prepare(const std::filesystem::path& assetRoot,
      const std::unordered_map<std::string, std::string>& chartSources,
      const std::vector<game::PrprEffect>& effects);
  void release() noexcept;

  // The chain is usable when it owns at least one single-pass program or a prepared
  // multi-stage bloom chain. A chart whose only effect is bloom has no single pass.
  [[nodiscard]] bool empty() const noexcept { return programs_.empty() && !bloom_.ready(); }

  // Runs every effect in `effects` over `input`, ping-ponging between `ping` and
  // `pong`. Returns the texture that holds the final result.
  [[nodiscard]] gl::Texture* apply(const std::vector<game::PrprEffect>& effects,
      gl::Texture* input, gl::Framebuffer& ping, gl::Framebuffer& pong, double chartTimeSec);

  // Runs the chain restricted to a sub-rectangle of the output, copying the rest
  // of the input through unchanged.
  [[nodiscard]] gl::Texture* applyStageViewport(const std::vector<game::PrprEffect>& effects,
      gl::Texture* input, gl::Framebuffer& ping, gl::Framebuffer& pong,
      const struct StageRect& rect, double chartTimeSec);

  // Binds the auxiliary textures the official presets expect.
  void setAuxTextures(gl::Texture* glitchMap, gl::Texture* glitchDogMap, gl::Texture* wireNoise,
      gl::Texture* displaceMap) noexcept;

private:
  struct Program {
    FullscreenPass pass;
    UniformDefaults defaults;
    bool valid{};
  };

  Program* resolve(const std::string& shaderKey);
  // Runs the multi-stage bloom chain for one `official_bloom` effect. Returns the
  // texture holding the result, or nullptr when the chain could not run.
  gl::Texture* applyBloom(const game::PrprEffect& effect, gl::Texture* input,
      gl::Framebuffer& output, double chartTimeSec);

  // Renders the procedural noise target a vignette pass samples through `noiseCol`.
  // Returns the texture to bind, or nullptr when the effect asks for no noise.
  gl::Texture* prepareVignetteNoise(const game::PrprEffect& effect, int screenWidth,
      int screenHeight, double chartTimeSec);
  void applyPass(const game::PrprEffect& effect, Program& program, gl::Texture* input,
      gl::Framebuffer& output, double chartTimeSec);

  std::filesystem::path assetRoot_;
  BloomPass bloom_;
  // Procedural noise generator for the vignette presets.
  FullscreenPass vignetteNoise_;
  bool vignetteNoiseReady_{};
  std::unique_ptr<gl::Framebuffer> noiseTarget_;
  // The noise animates from wall-clock time since the chain was prepared, so its phase
  // does not restart when a chart restarts.
  double noiseClockBaseSec_{};
  // Scratch target the bloom chain writes into before it becomes the chain input.
  std::unique_ptr<gl::Framebuffer> bloomTarget_;
  // Boxed because FullscreenPass owns GL handles and is intentionally not movable.
  std::unordered_map<std::string, std::unique_ptr<Program>> programs_;
  gl::Texture* glitchMap_{};
  gl::Texture* glitchDogMap_{};
  gl::Texture* wireNoise_{};
  gl::Texture* displaceMap_{};
};

// Sub-rectangle in pixels that a stage-scoped pass may write.
struct StageRect {
  float left{};
  float top{};
  float width{};
  float height{};
};

// Maps a preset key to its packaged file name, or nullptr for custom shaders.
[[nodiscard]] const char* presetFileForKey(const std::string& key) noexcept;

// Strips the version directive and hoists global initialisers into main, which is
// required by several presets that seed globals with non-constant expressions.
[[nodiscard]] std::string preparePresetSource(const std::string& source);

// Reads the `// %...%` uniform defaults out of a preset source.
[[nodiscard]] UniformDefaults parseUniformDefaults(const std::string& source);

} // namespace rechigros::render::fx
