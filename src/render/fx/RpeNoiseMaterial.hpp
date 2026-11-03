#pragma once

#include "rechigros/game/RpeNoiseArea.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/ShaderProgram.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game { struct RpeCameraFrame; }

namespace rechigros::render::fx {

struct RpeNoiseTouch {
  std::int64_t id{-1};
  float x{};
  float y{};
};

struct RpeNoiseTouchSample {
  float x{};
  float y{};
  float age{};
  float scale{1.0F};
  bool held{};
};

class RpeNoiseTouchTimeline final {
public:
  const std::vector<RpeNoiseTouchSample>& update(const std::vector<RpeNoiseTouch>& touches,
      double seconds, double releaseDuration);
  [[nodiscard]] const std::vector<RpeNoiseTouchSample>& samples() const noexcept {
    return samples_;
  }
  void reset();

private:
  struct Slot {
    std::int64_t id{-1};
    float x{}, y{};
    double start{}, release{}, lastSeen{};
    float startScale{}, endScale{}, scale{};
    bool held{};
  };
  std::array<Slot, 32> slots_;
  std::vector<RpeNoiseTouchSample> samples_;
  double lastSeconds{-1.0e300};
};

struct RpeNoiseCoverage {
  int width{}, height{};
  bool empty{true};
  std::vector<std::uint8_t> skin;
  std::vector<std::uint8_t> active;
  std::vector<std::uint8_t> base;
  std::vector<std::uint8_t> aux;
};

RpeNoiseCoverage buildRpeNoiseCoverage(int width, int height,
    const std::vector<game::RpeNoiseArea>& areas, const std::string& layerId, double seconds,
    float stageX, float stageY, float stageWidth, float stageHeight, bool singleInversion,
    const game::RpeCameraFrame* camera, float cameraViewScale);

class RpeNoiseMaterial final {
public:
  bool prepare(const std::filesystem::path& assetRoot, gl::Texture* displace,
      gl::Texture* spark, gl::Texture* noise);
  void release();
  void setViewport(int width, int height) {
    viewportWidth_ = std::max(1, width);
    viewportHeight_ = std::max(1, height);
  }
  bool valid() const noexcept { return material_.valid() && noiseWidth_ > 0; }
  gl::Texture* draw(gl::Texture& scene, gl::Framebuffer (&screens)[2],
      const RpeNoiseCoverage& coverage, double seconds,
      const std::vector<RpeNoiseTouchSample>& touches, int layerMask = 3);

private:
  void uploadParameters(double seconds);
  gl::ShaderProgram material_;
  gl::Texture* displace_{};
  gl::Texture* spark_{};
  gl::Texture* noise_{};
  std::vector<std::uint8_t> displacementPixels_;
  int noiseWidth_{}, noiseHeight_{};
  std::vector<std::uint8_t> hoverPixels_;
  int hoverWidth_{}, hoverHeight_{};
  gl::Texture effect_, aux_;
  int effectWidth_{}, effectHeight_{};
  std::vector<std::uint8_t> effectPixels_, current_, next_;
  int viewportWidth_{}, viewportHeight_{};
};

} // namespace rechigros::render::fx
