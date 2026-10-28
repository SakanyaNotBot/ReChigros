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

namespace rechigros::render::fx {

class RpeNoisePass final {
public:
  RpeNoisePass() = default;
  ~RpeNoisePass();
  RpeNoisePass(const RpeNoisePass&) = delete;
  RpeNoisePass& operator=(const RpeNoisePass&) = delete;

  bool prepare(const std::filesystem::path& packDirectory,
      const std::vector<game::RpeNoiseLayer>& layers);
  void release() noexcept;
  void resize(int width, int height);
  [[nodiscard]] bool ready() const noexcept { return defaultProgram_.valid(); }

  gl::Texture* draw(gl::Texture& scene, const std::vector<game::RpeNoiseArea>& areas,
      const std::vector<game::RpeNoiseLayer>& layers, double chartTimeSec, float stageX,
      float stageY, float stageWidth, float stageHeight);

private:
  struct TargetSpec {
    float scaleX{1.0F};
    float scaleY{1.0F};
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
    gl::Framebuffer screens[2];
    gl::Framebuffer mask;
    std::string signature;
    double lastSeconds{-1.0e300};
    gl::Texture* lastOutput{};
  };
  struct LayerState {
    std::unique_ptr<Resource> resource;
    std::string skin;
  };

  bool ensureDefaultProgram();
  std::unique_ptr<Resource> loadResource(const game::RpeNoiseLayer& layer);
  bool loadChannel(const std::filesystem::path& root, const std::string& mainSource,
      const nlohmann::json* channel, Channel& result,
      std::unordered_map<std::string, std::unique_ptr<gl::ShaderProgram>>& programs,
      std::unordered_map<std::string, gl::Texture>& images);
  void uploadMask(const std::vector<game::RpeNoiseArea>& areas, double seconds,
      float stageX, float stageY, float stageWidth, float stageHeight);
  gl::Texture* run(Resource& resource, const Channel& channel, gl::Texture& input,
      const game::RpeNoiseLayer& layer, double seconds);
  void drawQuad(gl::Texture* texture);
  static void clearTarget(gl::Framebuffer& target);

  std::filesystem::path packDirectory_;
  int width_{};
  int height_{};
  std::vector<std::unique_ptr<Resource>> resources_;
  std::vector<std::string> layerIds_;
  gl::Framebuffer mask_;
  gl::ShaderProgram defaultProgram_;
  std::unique_ptr<gl::Texture> maskPixels_;
  std::vector<std::uint8_t> maskData_;
};

} // namespace rechigros::render::fx
