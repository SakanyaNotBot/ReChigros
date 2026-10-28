#include "rechigros/render/fx/RpeNoisePass.hpp"

#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace rechigros::render::fx {
namespace {

const char* kDefaultFragment = R"(
uniform sampler2D screenTexture;
uniform sampler2D blockMask;
uniform vec2 screenSize;
uniform float time;
in vec2 uv;
out vec4 fragmentColor;
float bitValue(float word, int bit) {
  float divisor = pow(2.0, float(bit));
  return mod(floor(word / divisor), 2.0);
}
void main() {
  ivec2 size = textureSize(blockMask, 0);
  ivec2 pixel = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
  vec4 mask = texelFetch(blockMask, pixel, 0);
  float word = floor(mask.r * 255.0 + 0.5);
  float activeMask = max(bitValue(word, 0), bitValue(word, 1));
  float otherMask = max(bitValue(word, 2), bitValue(word, 3));
  float readyMask = max(bitValue(word, 4), bitValue(word, 5));
  float coverage = max(max(activeMask, otherMask), readyMask);
  float activation = activeMask > 0.5 ? 1.0 : (readyMask > 0.5 ? mask.g : 0.0);
  vec3 appearance = vec3(127.0, 35.0, 35.0) / 255.0;
  vec3 activeColor = vec3(182.0, 60.0, 60.0) / 255.0;
  vec3 edgeColor = vec3(255.0, 84.0, 84.0) / 255.0;
  vec3 color = mix(appearance, activeColor, activation);
  float alpha = (0.4 + (0.667 - 0.4) * activation) * coverage;
  float edge = 0.0;
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    ivec2 neighbour = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
    float neighbourWord = floor(texelFetch(blockMask, neighbour, 0).r * 255.0 + 0.5);
    float neighbourCoverage = max(max(bitValue(neighbourWord, 0), bitValue(neighbourWord, 1)),
        max(bitValue(neighbourWord, 2), bitValue(neighbourWord, 3)));
    edge = max(edge, abs(neighbourCoverage - coverage));
  }
  edge *= activation;
  vec4 scene = texture(screenTexture, uv);
  vec3 result = mix(scene.rgb, color, clamp(alpha, 0.0, 1.0));
  result = mix(result, edgeColor, clamp(edge, 0.0, 1.0));
  fragmentColor = vec4(result, 1.0);
}
)";

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

std::string parameterType(const std::string& type) {
  if (type == "float") return "float";
  return "";
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

} // namespace

RpeNoisePass::~RpeNoisePass() { release(); }

bool RpeNoisePass::ensureDefaultProgram() {
  return defaultProgram_.valid() || (defaultProgram_.build(fullscreenVertexSource(), kDefaultFragment) &&
      FullscreenQuad::instance().ensure());
}

void RpeNoisePass::release() noexcept {
  resources_.clear();
  layerIds_.clear();
  defaultProgram_.reset();
  maskPixels_.reset();
  mask_.reset();
  maskData_.clear();
}

void RpeNoisePass::resize(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
}

void RpeNoisePass::clearTarget(gl::Framebuffer& target) {
  target.bind();
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
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
    auto program = std::make_unique<gl::ShaderProgram>();
    if (!program->build(fullscreenVertexSource(), readText(sourceFile).c_str())) return false;
    programs[shader] = std::move(program);
    Pass pass;
    pass.program = programs[shader].get();
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
    std::istringstream lines(readText(sourceFile));
    std::string line;
    while (std::getline(lines, line)) {
      const auto textureAt = line.find("@texture");
      if (textureAt == std::string::npos) continue;
      std::istringstream declaration(line.substr(0, textureAt));
      std::string type, name;
      declaration >> type >> name;
      if (type != "sampler2D" || name.empty()) continue;
      std::istringstream options(line.substr(textureAt + 8));
      std::string path;
      options >> path;
      ImageSpec image;
      image.name = name;
      image.path = root / path;
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
      const auto normal = config.find("normal");
      if (normal != config.end()) {
        loadChannel(root, mainSource, &*normal, resource->normal, resource->programs, resource->images);
      }
      if (const auto low = config.find("lowPerformance"); low != config.end()) {
        resource->hasLow = loadChannel(root, mainSource, &*low, resource->low,
            resource->programs, resource->images);
      }
    }
    if (resource->normal.passes.empty()) {
      Channel fallback;
      fallback.passes.push_back({});
      const auto source = root / "main.glsl";
      auto program = std::make_unique<gl::ShaderProgram>();
      if (program->build(fullscreenVertexSource(), readText(source).c_str())) {
        resource->programs["main.glsl"] = std::move(program);
        fallback.passes.front().program = resource->programs["main.glsl"].get();
        fallback.passes.front().output = "$screen";
        if (config.is_object()) {
          if (const auto mask = config.find("blockMask"); mask != config.end() && mask->is_object()) {
            fallback.blockMaskScaleX = std::max(0.001F, mask->value("scaleX", 1.0F));
            fallback.blockMaskScaleY = std::max(0.001F, mask->value("scaleY", 1.0F));
          }
        }
        resource->normal = std::move(fallback);
      }
    }
  } else {
    resource->normal.blockMaskScaleX = 0.125F;
    resource->normal.blockMaskScaleY = 0.125F;
  }
  return resource;
}

bool RpeNoisePass::prepare(const std::filesystem::path& packDirectory,
    const std::vector<game::RpeNoiseLayer>& layers) {
  release();
  packDirectory_ = packDirectory;
  if (!ensureDefaultProgram()) return false;
  for (const auto& layer : layers) {
    resources_.push_back(loadResource(layer));
    layerIds_.push_back(layer.id);
  }
  return true;
}

void RpeNoisePass::uploadMask(const std::vector<game::RpeNoiseArea>& areas, double seconds,
    float, float, float, float stageHeight) {
  std::fill(maskData_.begin(), maskData_.end(), 0U);
  const int maskWidth = maskPixels_ != nullptr ? maskPixels_->width() : 1;
  const int maskHeight = maskPixels_ != nullptr ? maskPixels_->height() : 1;
  const float scale = stageHeight / 900.0F;
  struct Entry {
    const game::RpeNoiseArea* area;
    game::RpeNoiseSample sample;
  };
  std::vector<Entry> entries;
  entries.reserve(areas.size());
  for (const auto& area : areas) {
    auto sample = game::RpeNoiseRuntime(area).sample(seconds);
    if (sample.state != game::RpeNoiseState::HiddenBefore &&
        sample.state != game::RpeNoiseState::HiddenAfter) {
      entries.push_back({&area, sample});
    }
  }
  const auto inside = [](const game::RpeNoiseSample& shape, double x, double y) {
    bool positive = false;
    bool negative = false;
    for (int corner = 0; corner < 4; ++corner) {
      const auto& a = shape.points[static_cast<std::size_t>(corner)];
      const auto& b = shape.points[static_cast<std::size_t>((corner + 1) % 4)];
      const double cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
      positive = positive || cross > 0.0;
      negative = negative || cross < 0.0;
    }
    return !(positive && negative);
  };
  for (int y = 0; y < maskHeight; ++y) {
    const float screenY = (static_cast<float>(y) + 0.5F) * height_ / maskHeight;
    const double worldY = (height_ * 0.5 - screenY) / scale;
    for (int x = 0; x < maskWidth; ++x) {
      const float screenX = (static_cast<float>(x) + 0.5F) * width_ / maskWidth;
      const double worldX = (screenX - width_ * 0.5) / scale;
      int normal[3]{};
      int inverted[3]{};
      double activation[3]{};
      for (const auto& entry : entries) {
        if (!inside(entry.sample, worldX, worldY)) continue;
        const bool active = entry.sample.state == game::RpeNoiseState::Active;
        const bool ready = entry.sample.state == game::RpeNoiseState::Ready;
        const bool eligible[3]{active, !active, ready};
        for (int group = 0; group < 3; ++group) {
          if (!eligible[group]) continue;
          if (entry.area->invert) ++inverted[group];
          else ++normal[group];
          activation[group] = std::max(activation[group], entry.sample.activationMix);
        }
      }
      const std::size_t pixel = (static_cast<std::size_t>(y) * maskWidth + x) * 4U;
      int word = 0;
      int strongest = 0;
      for (int group = 0; group < 3; ++group) {
        if (normal[group] > 0) word |= 1 << (group * 2);
        if (inverted[group] == 1) word |= 1 << (group * 2 + 1);
        if (normal[group] > 0 || inverted[group] == 1) {
          strongest = std::max(strongest, static_cast<int>(std::lround(activation[group] * 255.0)));
        }
      }
      if (word == 0) continue;
      maskData_[pixel] = static_cast<std::uint8_t>(word);
      maskData_[pixel + 1U] = static_cast<std::uint8_t>(strongest);
      maskData_[pixel + 2U] = 255;
      maskData_[pixel + 3U] = 255;
    }
  }
  if (maskPixels_ == nullptr) maskPixels_ = std::make_unique<gl::Texture>();
  if (maskPixels_->width() != maskWidth || maskPixels_->height() != maskHeight) {
    maskPixels_->uploadRgba(maskWidth, maskHeight, maskData_.data());
  } else {
    maskPixels_->updateRgba(maskWidth, maskHeight, maskData_.data());
  }
}

gl::Texture* RpeNoisePass::run(Resource& resource, const Channel& channel, gl::Texture& input,
    const game::RpeNoiseLayer& layer, double seconds) {
  if (!resource.screens[0].valid()) {
    resource.screens[0].resize(width_, height_);
    resource.screens[1].resize(width_, height_);
  }
  for (auto& [name, target] : resource.targets) (void)name;
  for (const auto& [name, spec] : channel.targets) {
    auto& target = resource.targets[name];
    if (!target) target = std::make_unique<Target>();
    target->spec = spec;
    const int targetWidth = std::max(1, static_cast<int>(width_ * spec.scaleX));
    const int targetHeight = std::max(1, static_cast<int>(height_ * spec.scaleY));
    target->front.resize(targetWidth, targetHeight);
    target->back.resize(targetWidth, targetHeight);
    target->front.color().setFilter(spec.linear);
    target->back.color().setFilter(spec.linear);
    if (!spec.noClear) clearTarget(target->front);
  }
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  resource.screens[0].bind();
  BlitPass::instance().ensure();
  BlitPass::instance().draw(input, 1.0F, 1.0F, 1.0F, 1.0F, false);
  int screenIndex = 0;
  gl::Texture* current = &resource.screens[0].color();
  if (channel.passes.empty()) {
    resource.screens[1].bind();
    glViewport(0, 0, width_, height_);
    defaultProgram_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, current->id());
    defaultProgram_.set1i("screenTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskPixels_->id());
    defaultProgram_.set1i("blockMask", 1);
    defaultProgram_.set2f("screenSize", static_cast<float>(width_), static_cast<float>(height_));
    defaultProgram_.set1f("time", static_cast<float>(seconds));
    FullscreenQuad::instance().draw();
    return &resource.screens[1].color();
  }
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
    pass.program->set1i("touchCount", 0);
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
      pass.program->set1i(name.c_str(), unit++);
    }
    for (const auto& image : pass.images) {
      const auto found = resource.images.find(image.name);
      if (found == resource.images.end()) continue;
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, found->second.id());
      pass.program->set1i(image.name.c_str(), unit++);
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
    float stageY, float stageWidth, float stageHeight) {
  if (!defaultProgram_.valid() || width_ <= 0 || height_ <= 0) return &scene;
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
    if (resource.lastSeconds > -1.0e299 && chartTimeSec < resource.lastSeconds - 1.0e-9) {
      for (auto& [name, target] : resource.targets) {
        clearTarget(target->front);
        clearTarget(target->back);
      }
    }
    const Channel& channel = layers[index].channel == "low" && resource.hasLow
        ? resource.low : resource.normal;
    const int maskWidth = std::max(1, static_cast<int>(width_ * channel.blockMaskScaleX));
    const int maskHeight = std::max(1, static_cast<int>(height_ * channel.blockMaskScaleY));
    maskData_.assign(static_cast<std::size_t>(maskWidth) * maskHeight * 4U, 0U);
    if (!maskPixels_) maskPixels_ = std::make_unique<gl::Texture>();
    if (maskPixels_->width() != maskWidth || maskPixels_->height() != maskHeight) {
      maskPixels_->uploadRgba(maskWidth, maskHeight, maskData_.data());
    }
    std::vector<game::RpeNoiseArea> layerAreas;
    for (const auto& area : areas) {
      if (area.layerId == layers[index].id) layerAreas.push_back(area);
    }
    uploadMask(layerAreas, chartTimeSec, stageX, stageY, stageWidth, stageHeight);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    current = run(resource, channel, *current, layers[index], chartTimeSec);
    resource.lastSeconds = chartTimeSec;
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
