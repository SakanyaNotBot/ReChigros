#include "rechigros/render/fx/RpeNoisePass.hpp"

#include "RpeNoiseMaterial.hpp"

#include "rechigros/game/RpeCamera.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace rechigros::render::fx {
namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

std::vector<float> numberArray(const nlohmann::json& value) {
  if (value.is_number()) return {value.get<float>()};
  if (!value.is_array()) return {};
  std::vector<float> result;
  for (const auto& item : value) {
    if (!item.is_number()) return {};
    result.push_back(item.get<float>());
  }
  return result;
}

bool parseUniformDefault(const std::string& line, std::string& name, std::vector<float>& values) {
  const auto uniformAt = line.find("uniform");
  const auto marker = line.find("// %");
  if (uniformAt == std::string::npos || marker == std::string::npos) return false;
  std::istringstream declaration(line.substr(uniformAt + 7, marker - uniformAt - 7));
  std::string type;
  declaration >> type >> name;
  if (name.empty()) return false;
  name.resize(name.find_first_of("[;"));
  const auto end = line.find('%', marker + 4);
  if (end == std::string::npos) return false;
  std::istringstream valuesText(line.substr(marker + 4, end - marker - 4));
  std::string item;
  while (std::getline(valuesText, item, ',')) {
    char* tail = nullptr;
    const double value = std::strtod(item.c_str(), &tail);
    if (tail == item.c_str()) return false;
    values.push_back(static_cast<float>(value));
  }
  return !values.empty();
}

template<typename Channel>
std::string packedTextureReads(std::string source,
    const std::unordered_map<std::string, std::string>& inputs, const Channel& channel) {
  std::string helpers;
  for (const auto& [name, target] : inputs) {
    const auto spec = channel.targets.find(target);
    if (spec == channel.targets.end() || spec->second.channels == 4) continue;
    const std::string function = "rpenPacked_" + name;
    for (std::size_t at = 0; (at = source.find("texture2D", at)) != std::string::npos;) {
      std::size_t cursor = at + 9;
      while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
      if (cursor >= source.size() || source[cursor++] != '(') { at += 9; continue; }
      while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
      if (source.compare(cursor, name.size(), name) != 0) { at += 9; continue; }
      cursor += name.size();
      while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
      if (cursor >= source.size() || source[cursor] != ',') { at += 9; continue; }
      source.replace(at, cursor - at + 1, function + "(");
      at += function.size() + 1;
    }
    helpers += "vec4 " + function + "(vec2 p) { vec4 c = texture2D(" + name +
        ", p); return " + (spec->second.channels == 1 ? "vec4(c.r, 0.0, 0.0, 1.0);" :
            "vec4(c.rg, 0.0, 1.0);") + " }\n";
  }
  if (helpers.empty()) return source;
  const auto lastUniform = source.rfind("uniform");
  const auto insertAt = lastUniform == std::string::npos ? 0 : source.find(';', lastUniform);
  source.insert(insertAt == std::string::npos ? 0 : insertAt + 1, "\n" + helpers);
  return source;
}

} // namespace

bool invertedNoiseCoveredForTest(int overlapCount, bool single) noexcept {
  return single ? overlapCount == 1 : (overlapCount & 1) != 0;
}

struct RpeNoisePass::TouchTimelines {
  std::unordered_map<std::string, RpeNoiseTouchTimeline> entries;
};

RpeNoisePass::RpeNoisePass() = default;

RpeNoisePass::~RpeNoisePass() { release(); }

bool RpeNoisePass::ready() const noexcept {
  return material_ != nullptr && material_->valid();
}

void RpeNoisePass::release() noexcept {
  resources_.clear();
  if (timelines_) timelines_->entries.clear();
  material_.reset();
  maskPixels_.reset();
  maskData_.clear();
}

void RpeNoisePass::resize(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  if (material_) material_->setViewport(width_, height_);
  for (auto& resource : resources_) {
    if (resource && resource->material) resource->material->setViewport(width_, height_);
  }
}

void RpeNoisePass::clearTarget(gl::Framebuffer& target, bool opaqueAlpha) {
  target.bind();
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0F, 0.0F, 0.0F, opaqueAlpha ? 1.0F : 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
}

bool RpeNoisePass::loadChannel(const std::filesystem::path& root,
    const std::string& mainSource, const nlohmann::json* source, Channel& result,
    std::unordered_map<std::string, std::unique_ptr<gl::ShaderProgram>>& programs,
    std::unordered_map<std::string, gl::Texture>& images) {
  if (source == nullptr) return false;
  const auto& config = *source;
  if (const auto mask = config.find("blockMask"); mask != config.end() && mask->is_object()) {
    result.blockMaskScaleX = std::max(0.001F, mask->value("scaleX", 1.0F));
    result.blockMaskScaleY = std::max(0.001F, mask->value("scaleY", 1.0F));
  }
  if (const auto targets = config.find("targets"); targets != config.end() && targets->is_object()) {
    for (const auto& [name, value] : targets->items()) {
      if (!value.is_object()) continue;
      TargetSpec spec;
      spec.scaleX = std::max(0.001F, value.value("scaleX", 1.0F));
      spec.scaleY = std::max(0.001F, value.value("scaleY", 1.0F));
      const auto format = value.value("format", "rgba8");
      spec.channels = format == "r8" ? 1 : format == "rg8" ? 2 : 4;
      spec.linear = value.value("filter", "nearest") == "linear";
      spec.noClear = value.value("noClear", false);
      result.targets[name] = spec;
    }
  }
  const auto passes = config.find("passes");
  if (passes == config.end() || !passes->is_array()) return false;
  for (const auto& passValue : *passes) {
    if (!passValue.is_object()) continue;
    const std::string shader = passValue.value("shader", "");
    const std::string output = passValue.value("output", "");
    if (shader.empty() || output.empty()) return false;
    const auto sourceFile = root / shader;
    auto& program = programs[shader];
    if (!program) program = std::make_unique<gl::ShaderProgram>();
    const std::string passSource = readText(sourceFile);
    Pass pass;
    pass.program = program.get();
    pass.output = output.substr(0, output.find('.'));
    if (output.find('.') != std::string::npos && output.size() > output.find('.') + 1) {
      pass.component = output[output.find('.') + 1];
    }
    if (const auto inputs = passValue.find("inputs"); inputs != passValue.end() && inputs->is_object()) {
      for (const auto& [name, value] : inputs->items()) {
        if (value.is_string()) pass.inputs[name] = value.get<std::string>();
      }
    }
    if (const auto constants = passValue.find("constants");
        constants != passValue.end() && constants->is_object()) {
      for (const auto& [name, value] : constants->items()) {
        auto values = numberArray(value);
        if (!values.empty()) pass.constants[name] = std::move(values);
      }
    }
    if (!program->build(fullscreenVertexSource(),
            packedTextureReads(passSource, pass.inputs, result))) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[render] noise skin %s: %s",
          sourceFile.string().c_str(), program->log().c_str());
      return false;
    }
    std::istringstream lines(passSource);
    std::string line;
    while (std::getline(lines, line)) {
      const auto textureAt = line.find("@texture");
      if (textureAt == std::string::npos) continue;
      std::istringstream declaration(line.substr(0, textureAt));
      std::string qualifier, type, name;
      declaration >> qualifier >> type >> name;
      if (qualifier != "uniform" || type != "sampler2D" || name.empty()) continue;
      name.resize(name.find_first_of("[;"));
      std::istringstream options(line.substr(textureAt + 8));
      std::string path;
      options >> path;
      ImageSpec image;
      image.name = name;
      image.path = sourceFile.parent_path() / path;
      std::string option;
      while (options >> option) {
        const auto separator = option.find('=');
        if (separator == std::string::npos) continue;
        const auto key = option.substr(0, separator);
        const auto value = option.substr(separator + 1);
        if (key == "wrap") {
          image.repeat = value == "repeat";
          image.mirror = value == "mirror";
        } else if (key == "filter") {
          image.linear = value == "linear";
        } else if (key == "colorspace") {
          image.srgb = value == "srgb";
        } else if (key == "alphaMode") {
          image.premultiply = value == "premult";
        }
      }
      auto decoded = asset::decodeImage(image.path);
      if (!decoded.valid()) return false;
      for (std::size_t index = 0; index + 3 < decoded.pixels.size(); index += 4) {
        if (image.srgb) {
          for (int channel = 0; channel < 3; ++channel) {
            double value = decoded.pixels[index + channel] / 255.0;
            value = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            decoded.pixels[index + channel] = static_cast<std::uint8_t>(std::clamp(value * 255.0, 0.0, 255.0));
          }
        }
        if (image.premultiply) {
          const auto alpha = decoded.pixels[index + 3];
          for (int channel = 0; channel < 3; ++channel) {
            decoded.pixels[index + channel] = static_cast<std::uint8_t>(
                (static_cast<unsigned int>(decoded.pixels[index + channel]) * alpha + 127U) / 255U);
          }
        }
      }
      std::vector<std::uint8_t> flipped(decoded.pixels.size());
      const auto rowBytes = static_cast<std::size_t>(decoded.width) * 4U;
      for (int row = 0; row < decoded.height; ++row) {
        std::copy_n(decoded.pixels.data() + static_cast<std::size_t>(decoded.height - 1 - row) * rowBytes,
            rowBytes, flipped.data() + static_cast<std::size_t>(row) * rowBytes);
      }
      gl::Texture texture;
      if (!texture.uploadRgba(decoded.width, decoded.height, flipped.data())) return false;
      texture.setFilter(image.linear);
      texture.setWrapRepeat(image.repeat, image.repeat);
      if (image.mirror) texture.setWrapMirrored(true, true);
      images[name] = std::move(texture);
      pass.images.push_back(image);
    }
    std::istringstream uniformLines(mainSource);
    while (std::getline(uniformLines, line)) {
      std::string name;
      std::vector<float> values;
      if (parseUniformDefault(line, name, values)) pass.parameters.push_back(name);
    }
    result.passes.push_back(std::move(pass));
  }
  return !result.passes.empty();
}

std::unique_ptr<RpeNoisePass::Resource> RpeNoisePass::loadResource(
    const game::RpeNoiseLayer& layer) {
  auto resource = std::make_unique<Resource>();
  resource->layerId = layer.id;
  resource->skin = layer.skin;
  if (!layer.skin.empty()) {
    const auto root = packDirectory_ / layer.skin;
    const auto mainSource = readText(root / "main.glsl");
    const auto configPath = root / "config.json";
    nlohmann::json config;
    if (std::filesystem::exists(configPath)) {
      const auto text = readText(configPath);
      config = nlohmann::json::parse(text, nullptr, false);
      if (config.is_discarded()) return resource;
    }
    if (!mainSource.empty()) {
      std::istringstream lines(mainSource);
      std::string line;
      while (std::getline(lines, line)) {
        std::string name;
        std::vector<float> values;
        if (parseUniformDefault(line, name, values)) resource->parameterDefaults[name] = std::move(values);
      }
    }
    if (config.is_object()) {
      if (config.contains("passes")) {
        loadChannel(root, mainSource, &config, resource->normal, resource->programs, resource->images);
      } else if (const auto normal = config.find("normal"); normal != config.end()) {
        loadChannel(root, mainSource, &*normal, resource->normal, resource->programs, resource->images);
      }
      if (const auto low = config.find("lowPerformance"); low != config.end()) {
        resource->hasLow = loadChannel(root, mainSource, &*low, resource->low,
            resource->programs, resource->images);
      }
    }
    if (resource->normal.passes.empty()) {
      nlohmann::json fallback;
      fallback["targets"] = nlohmann::json::object();
      fallback["passes"] = nlohmann::json::array();
      fallback["passes"].push_back({{"shader", "main.glsl"}, {"output", "$screen"}});
      loadChannel(root, mainSource, &fallback, resource->normal, resource->programs,
          resource->images);
    }
  } else {
    resource->normal.blockMaskScaleX = 1.0F;
    resource->normal.blockMaskScaleY = 1.0F;
  }
  return resource;
}

bool RpeNoisePass::prepare(const std::filesystem::path& packDirectory,
    const std::vector<game::RpeNoiseLayer>& layers, const std::filesystem::path& assetRoot,
    gl::Texture* displace, gl::Texture* spark, gl::Texture* noise) {
  release();
  packDirectory_ = packDirectory;
  material_ = std::make_unique<RpeNoiseMaterial>();
  timelines_ = std::make_unique<TouchTimelines>();
  material_->prepare(assetRoot, displace, spark, noise);
  material_->setViewport(width_, height_);
  for (const auto& layer : layers) {
    resources_.push_back(loadResource(layer));
    resources_.back()->material = std::make_unique<RpeNoiseMaterial>();
    resources_.back()->material->prepare(assetRoot, displace, spark, noise);
    resources_.back()->material->setViewport(width_, height_);
  }
  return ready();
}

gl::Texture* RpeNoisePass::run(Resource& resource, const Channel& channel, gl::Texture& input,
    const game::RpeNoiseLayer& layer, double seconds,
    const std::vector<RpeNoiseTouchSample>& touches) {
  if (!resource.screens[0].valid()) resource.screens[0].resize(width_, height_);
  if (!resource.screens[1].valid()) resource.screens[1].resize(width_, height_);
  const bool repeated = resource.lastSeconds == seconds && resource.lastParameters == layer.parameters;
  BlitPass::instance().ensure();
  for (const auto& [name, spec] : channel.targets) {
    auto& target = resource.targets[name];
    const bool created = !target;
    if (!target) target = std::make_unique<Target>();
    target->spec = spec;
    const int targetWidth = std::max(1, static_cast<int>(width_ * spec.scaleX));
    const int targetHeight = std::max(1, static_cast<int>(height_ * spec.scaleY));
    target->front.resize(targetWidth, targetHeight);
    target->back.resize(targetWidth, targetHeight);
    target->front.color().setFilter(spec.linear);
    target->back.color().setFilter(spec.linear);
    if (created) {
      clearTarget(target->front, spec.channels < 4);
      clearTarget(target->back, spec.channels < 4);
    }
    if (!spec.noClear) {
      clearTarget(target->front, spec.channels < 4);
      continue;
    }
    auto& history = resource.history[name];
    if (!history) history = std::make_unique<gl::Framebuffer>();
    history->resize(targetWidth, targetHeight);
    auto& source = repeated ? *history : target->front;
    auto& destination = repeated ? target->front : *history;
    destination.bind();
    glViewport(0, 0, targetWidth, targetHeight);
    glDisable(GL_BLEND);
    BlitPass::instance().draw(source.color(), 1.0F, 1.0F, 1.0F, 1.0F, false);
  }
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  resource.screens[0].bind();
  glViewport(0, 0, width_, height_);
  BlitPass::instance().draw(input, 1.0F, 1.0F, 1.0F, 1.0F, false);
  int screenIndex = 0;
  gl::Texture* current = &resource.screens[0].color();
  if (channel.passes.empty()) return current;
  for (const auto& pass : channel.passes) {
    gl::Framebuffer* target = nullptr;
    if (pass.output != "$screen") {
      const auto found = resource.targets.find(pass.output);
      if (found != resource.targets.end()) target = &found->second->back;
    }
    gl::Framebuffer& destination = target != nullptr
        ? *target : resource.screens[1 - screenIndex];
    destination.bind();
    glViewport(0, 0, destination.width(), destination.height());
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (target != nullptr) {
      auto found = resource.targets.find(pass.output);
      if (pass.component != '\0') {
        glDisable(GL_BLEND);
        BlitPass::instance().draw(found->second->front.color(), 1.0F, 1.0F, 1.0F, 1.0F, false);
      }
    }
    pass.program->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current->id());
    pass.program->set1i("screenTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskPixels_->id());
    pass.program->set1i("blockMask", 1);
    pass.program->set2f("screenSize", static_cast<float>(width_), static_cast<float>(height_));
    pass.program->set2f("screenTextureSize", static_cast<float>(width_), static_cast<float>(height_));
    pass.program->set2f("blockMaskSize", static_cast<float>(maskPixels_->width()),
        static_cast<float>(maskPixels_->height()));
    pass.program->set2f("targetSize", static_cast<float>(destination.width()),
        static_cast<float>(destination.height()));
    pass.program->set2f("targetTexelSize", 1.0F / destination.width(), 1.0F / destination.height());
    pass.program->set1f("time", static_cast<float>(seconds));
    pass.program->set1i("touchCount", std::min(32, static_cast<int>(touches.size())));
    float touchPoints[128]{};
    for (std::size_t index = 0; index < touches.size() && index < 32; ++index) {
      touchPoints[index * 4] = touches[index].x;
      touchPoints[index * 4 + 1] = touches[index].y;
      touchPoints[index * 4 + 2] = touches[index].age;
      touchPoints[index * 4 + 3] = touches[index].held ? 1.0F : 0.0F;
    }
    pass.program->set4fv("touchPoints", std::min(32, static_cast<int>(touches.size())), touchPoints);
    glActiveTexture(GL_TEXTURE0);
    int unit = 2;
    for (const auto& [name, inputName] : pass.inputs) {
      gl::Texture* texture = current;
      if (inputName != "$screen") {
        const auto found = resource.targets.find(inputName);
        if (found == resource.targets.end()) continue;
        texture = &found->second->front.color();
      }
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, texture->id());
      pass.program->set1i(name.c_str(), unit);
      pass.program->set2f((name + "Size").c_str(), static_cast<float>(texture->width()),
          static_cast<float>(texture->height()));
      ++unit;
    }
    for (const auto& image : pass.images) {
      const auto found = resource.images.find(image.name);
      if (found == resource.images.end()) continue;
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, found->second.id());
      pass.program->set1i(image.name.c_str(), unit);
      pass.program->set2f((image.name + "Size").c_str(),
          static_cast<float>(found->second.width()), static_cast<float>(found->second.height()));
      ++unit;
    }
    for (const auto& [name, values] : resource.parameterDefaults) {
      const auto override = layer.parameters.find(name);
      const auto& selected = override == layer.parameters.end() ? values : override->second;
      if (selected.size() >= 4) pass.program->set4f(name.c_str(), selected[0], selected[1], selected[2], selected[3]);
      else if (selected.size() == 2) pass.program->set2f(name.c_str(), selected[0], selected[1]);
      else if (!selected.empty()) pass.program->set1f(name.c_str(), selected[0]);
    }
    for (const auto& [name, values] : pass.constants) {
      if (values.size() >= 4) pass.program->set4f(name.c_str(), values[0], values[1], values[2], values[3]);
      else if (values.size() == 2) pass.program->set2f(name.c_str(), values[0], values[1]);
      else if (!values.empty()) pass.program->set1f(name.c_str(), values[0]);
    }
    if (pass.component != '\0') {
      glColorMask(pass.component == 'r', pass.component == 'g', pass.component == 'b',
          pass.component == 'a');
    }
    FullscreenQuad::instance().draw();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (target != nullptr) {
      auto found = resource.targets.find(pass.output);
      std::swap(found->second->front, found->second->back);
    } else {
      screenIndex = 1 - screenIndex;
      current = &resource.screens[screenIndex].color();
    }
  }
  return current;
}

gl::Texture* RpeNoisePass::draw(gl::Texture& scene,
    const std::vector<game::RpeNoiseArea>& areas,
    const std::vector<game::RpeNoiseLayer>& layers, double chartTimeSec, float stageX,
    float stageY, float stageWidth, float stageHeight, bool singleInversion,
    const std::vector<RpeNoiseTouch>& touches,
    const game::RpeCameraFrame* camera, float cameraViewScale, int layerMask) {
  if (!ready() || width_ <= 0 || height_ <= 0) return &scene;
  GLint oldFbo = 0;
  GLint oldViewport[4]{};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
  glGetIntegerv(GL_VIEWPORT, oldViewport);
  const bool blend = glIsEnabled(GL_BLEND) != GL_FALSE;
  const bool scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
  const bool depth = glIsEnabled(GL_DEPTH_TEST) != GL_FALSE;
  gl::Texture* current = &scene;
  for (std::size_t index = 0; index < layers.size() && index < resources_.size(); ++index) {
    if (std::none_of(areas.begin(), areas.end(), [&](const auto& area) {
          return area.layerId == layers[index].id && chartTimeSec >= area.appear &&
              chartTimeSec < area.disappear;
        })) continue;
    auto& resource = *resources_[index];
    if (resource.lastSeconds > -1.0e299 &&
        (chartTimeSec < resource.lastSeconds - 1.0e-9 ||
         std::abs(chartTimeSec - resource.lastSeconds) > 1.0 ||
         resource.lastParameters != layers[index].parameters)) {
      for (auto& [name, target] : resource.targets) {
        clearTarget(target->front, target->spec.channels < 4);
        clearTarget(target->back, target->spec.channels < 4);
      }
      resource.history.clear();
      timelines_->entries[layers[index].id].reset();
    }
    const Channel& channel = layers[index].channel == "low" && resource.hasLow
        ? resource.low : resource.normal;
    const bool custom = !resource.skin.empty() && !channel.passes.empty();
    double release = 0.1;
    if (const auto found = resource.parameterDefaults.find("touchReleaseDuration");
        found != resource.parameterDefaults.end() && !found->second.empty()) {
      release = std::max(0.0F, found->second.front());
    }
    if (const auto found = layers[index].parameters.find("touchReleaseDuration");
        found != layers[index].parameters.end() && !found->second.empty()) {
      release = std::max(0.0F, found->second.front());
    }
    std::vector<RpeNoiseTouch> normalizedTouches;
    normalizedTouches.reserve(touches.size());
    for (const auto& touch : touches) {
      normalizedTouches.push_back({touch.id, touch.x / width_, 1.0F - touch.y / height_});
    }
    const auto& timeline = timelines_->entries[layers[index].id].update(normalizedTouches,
        chartTimeSec, release);
    if (custom && (layerMask & 2) != 0) {
      const int maskWidth = std::max(1, static_cast<int>(width_ * channel.blockMaskScaleX));
      const int maskHeight = std::max(1, static_cast<int>(height_ * channel.blockMaskScaleY));
      auto coverage = buildRpeNoiseCoverage(maskWidth, maskHeight, areas, layers[index].id,
          chartTimeSec, stageX, stageY, stageWidth, stageHeight, true, camera,
          cameraViewScale);
      maskData_ = std::move(coverage.skin);
      if (!maskPixels_) maskPixels_ = std::make_unique<gl::Texture>();
      if (maskPixels_->width() != maskWidth || maskPixels_->height() != maskHeight) {
        maskPixels_->uploadRgba(maskWidth, maskHeight, maskData_.data());
      } else {
        maskPixels_->updateRgba(maskWidth, maskHeight, maskData_.data());
      }
      maskPixels_->setFilter(false);
      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_DEPTH_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      current = run(resource, channel, *current, layers[index], chartTimeSec, timeline);
    } else {
      auto coverage = buildRpeNoiseCoverage(std::max(1, width_ / 8) * 2,
          std::max(1, height_ / 8) * 2, areas, layers[index].id, chartTimeSec, stageX, stageY,
          stageWidth, stageHeight, singleInversion, camera, cameraViewScale);
      current = resource.material->draw(*current, resource.screens, coverage, chartTimeSec, timeline, layerMask);
    }
    resource.lastSeconds = chartTimeSec;
    resource.lastParameters = layers[index].parameters;
    resource.lastOutput = current;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
  glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
  if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
  if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  return current;
}

} // namespace rechigros::render::fx
