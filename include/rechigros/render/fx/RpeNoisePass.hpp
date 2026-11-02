#pragma once

#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/ShaderProgram.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/game/RpeNoiseArea.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rechigros::game { struct RpeCameraFrame; }

namespace rechigros::render::fx {

struct RpeNoiseTouch;
struct RpeNoiseTouchSample;
class RpeNoiseMaterial;

[[nodiscard]] bool invertedNoiseCoveredForTest(int overlapCount, bool single = false) noexcept;

class RpeNoisePass final {
public:
  RpeNoisePass();
  ~RpeNoisePass();
  RpeNoisePass(const RpeNoisePass&) = delete;
  RpeNoisePass& operator=(const RpeNoisePass&) = delete;

  bool prepare(const std::filesystem::path& packDirectory,
      const std::vector<game::RpeNoiseLayer>& layers, const std::filesystem::path& assetRoot,
      gl::Texture* displace, gl::Texture* spark, gl::Texture* noise);
  void release() noexcept;
  void resize(int width, int height);
  [[nodiscard]] bool ready() const noexcept;

  gl::Texture* draw(gl::Texture& scene, const std::vector<game::RpeNoiseArea>& areas,
      const std::vector<game::RpeNoiseLayer>& layers, double chartTimeSec, float stageX,
      float stageY, float stageWidth, float stageHeight, bool singleInversion,
      const std::vector<RpeNoiseTouch>& touches,
      const game::RpeCameraFrame* camera = nullptr, float cameraViewScale = 1.0F,
      int layerMask = 3);

private:
  struct TargetSpec {
    float scaleX{1.0F};
    float scaleY{1.0F};
    int channels{4};
    bool linear{};
    bool noClear{};
  };
  struct Target {
    TargetSpec spec;
    gl::Framebuffer front;
    gl::Framebuffer back;
  };
  struct ImageSpec {
    std::string name;
    std::filesystem::path path;
    bool linear{true};
    bool repeat{};
    bool mirror{};
    bool srgb{};
    bool premultiply{};
  };
  struct Pass {
    gl::ShaderProgram* program{};
    std::string output;
    char component{};
    std::unordered_map<std::string, std::string> inputs;
    std::unordered_map<std::string, std::vector<float>> constants;
    std::vector<ImageSpec> images;
    std::vector<std::string> parameters;
  };
  struct Channel {
    float blockMaskScaleX{1.0F};
    float blockMaskScaleY{1.0F};
    std::unordered_map<std::string, TargetSpec> targets;
    std::vector<Pass> passes;
  };
  struct Resource {
    std::string layerId;
    std::string skin;
    Channel normal;
    Channel low;
    bool hasLow{};
    std::unordered_map<std::string, std::unique_ptr<gl::ShaderProgram>> programs;
    std::unordered_map<std::string, gl::Texture> images;
    std::unordered_map<std::string, std::vector<float>> parameterDefaults;
    std::unordered_map<std::string, std::unique_ptr<Target>> targets;
    std::unordered_map<std::string, std::unique_ptr<gl::Framebuffer>> history;
    std::unordered_map<std::string, std::vector<float>> lastParameters;
    gl::Framebuffer screens[2];
    std::unique_ptr<RpeNoiseMaterial> material;
    double lastSeconds{-1.0e300};
    gl::Texture* lastOutput{};
  };
  struct TouchTimelines;

  std::unique_ptr<Resource> loadResource(const game::RpeNoiseLayer& layer);
  bool loadChannel(const std::filesystem::path& root, const std::string& mainSource,
      const nlohmann::json* channel, Channel& result,
      std::unordered_map<std::string, std::unique_ptr<gl::ShaderProgram>>& programs,
      std::unordered_map<std::string, gl::Texture>& images);
  gl::Texture* run(Resource& resource, const Channel& channel, gl::Texture& input,
      const game::RpeNoiseLayer& layer, double seconds,
      const std::vector<RpeNoiseTouchSample>& touches);
  static void clearTarget(gl::Framebuffer& target, bool opaqueAlpha = false);

  std::filesystem::path packDirectory_;
  int width_{};
  int height_{};
  std::vector<std::unique_ptr<Resource>> resources_;
  std::unique_ptr<RpeNoiseMaterial> material_;
  std::unique_ptr<TouchTimelines> timelines_;
  std::unique_ptr<gl::Texture> maskPixels_;
  std::vector<std::uint8_t> maskData_;
};

} // namespace rechigros::render::fx
