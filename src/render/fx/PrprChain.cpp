#include "rechigros/render/fx/PrprChain.hpp"

#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/platform/AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace rechigros::render::fx {
namespace {

// Seconds from a monotonic clock; used for the noise phase, which must not restart when a
// chart restarts.
double monotonicSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}

std::string lowercase(std::string text) {
  for (auto& character : text) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return text;
}

void replaceAllText(std::string& text, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  for (std::size_t position = 0; (position = text.find(from, position)) != std::string::npos;) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
}

std::string adaptRpeShader(std::string body) {
  if (body.find("CC_Texture0") == std::string::npos &&
      body.find("v_texCoord") == std::string::npos) return body;
  const bool portable = body.find("uniform sampler2D screenTexture") != std::string::npos;
  const std::string coordinate = portable ? "uv" : "v_texCoord";
  const std::string origin = portable ? "vec2(0.0)" : "vec2(0.0, 0.0333)";
  const std::string size = portable ? "vec2(1.0)" : "vec2(0.7031, 1.0 / 1.2)";
  replaceAllText(body, "CC_Texture0", "u_texture");
  replaceAllText(body, "screenTexture", "u_texture");
  replaceAllText(body, "screenSize", "resolution");
  replaceAllText(body, "uniform sampler2D u_texture;", "");
  for (const std::string declaration : {
      "varying vec2 " + coordinate + ";",
      "varying highp vec2 " + coordinate + ";",
      "varying mediump vec2 " + coordinate + ";",
      "varying lowp vec2 " + coordinate + ";"}) {
    replaceAllText(body, declaration, "");
  }
  replaceAllText(body, coordinate, "rpeCoordinate");
  replaceAllText(body, "texture2DProj", "rpeSampleProj");
  replaceAllText(body, "texture2D", "rpeSample");
  replaceAllText(body, "void main", "void rpeMain");
  return "precision mediump float;\n"
      "uniform sampler2D u_texture;\n"
      "varying vec2 v_texCoord;\n"
      "uniform vec2 rpeRectMin;\n"
      "uniform vec2 rpeRectMax;\n"
      "vec2 rpeCoordinate;\n"
      "vec4 rpeSample(sampler2D source, vec2 coordinate) {\n"
      "  vec2 local = (coordinate - " + origin + ") / " + size + ";\n"
      "  if (local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) return vec4(0.0);\n"
      "  return texture2D(source, mix(rpeRectMin, rpeRectMax, local));\n"
      "}\n"
      "vec4 rpeSampleProj(sampler2D source, vec4 coordinate) { return rpeSample(source, coordinate.xy / coordinate.w); }\n"
      "vec4 rpeSampleProj(sampler2D source, vec3 coordinate) { return rpeSample(source, coordinate.xy / coordinate.z); }\n" +
      body +
      "\nvoid main() {\n"
      "  if (v_texCoord.x < rpeRectMin.x || v_texCoord.x > rpeRectMax.x || "
      "v_texCoord.y < rpeRectMin.y || v_texCoord.y > rpeRectMax.y) gl_FragColor = vec4(0.0);\n"
      "  else {\n"
      "    rpeCoordinate = " + origin + " + (v_texCoord - rpeRectMin) / "
      "(rpeRectMax - rpeRectMin) * " + size + ";\n"
      "    rpeMain();\n"
      "  }\n"
      "}\n";
}

// The prpr vertex shader folds UVScale into the sampled coordinate.
const char* kPrprVertex = R"(
in vec2 aPosition;
out vec2 uv;
out vec2 v_texCoord;
out vec4 v_fragmentColor;
uniform vec2 UVScale;
void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0);
  uv = (aPosition * 0.5 + 0.5 - vec2(0.5)) * UVScale + vec2(0.5);
  v_texCoord = uv;
  v_fragmentColor = vec4(1.0);
}
)";

void setUniformArray(gl::ShaderProgram& program, const std::string& name,
    const std::vector<float>& values) {
  const int location = program.uniform(name.c_str());
  if (location < 0 || values.empty()) return;
  switch (values.size()) {
    case 1: glUniform1f(location, values[0]); break;
    case 2: glUniform2f(location, values[0], values[1]); break;
    case 3: glUniform3f(location, values[0], values[1], values[2]); break;
    default: glUniform4f(location, values[0], values[1], values[2], values[3]); break;
  }
}

// Evaluates a prpr variable at the given time. Constant values return directly;
// animated streams are interpolated between their surrounding keys.
bool isActiveAt(const game::PrprEffect& effect, double chartTimeSec) {
  return chartTimeSec >= effect.startTime && chartTimeSec < effect.endTime;
}

// Converts a stage rectangle given in top-left screen pixels into the scissor box the
// passes need: bottom-left origin, clipped to the target, and rejected when it cannot
// cover a whole pixel.
bool stageViewportRect(const StageRect& rect, int targetWidth, int targetHeight, int& outX,
    int& outY, int& outWidth, int& outHeight) {
  const int safeWidth = std::max(1, targetWidth);
  const int safeHeight = std::max(1, targetHeight);
  int x = static_cast<int>(std::lround(rect.left));
  int y = static_cast<int>(std::lround(static_cast<float>(safeHeight) -
      (rect.top + rect.height)));
  int width = static_cast<int>(std::lround(rect.width));
  int height = static_cast<int>(std::lround(rect.height));
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (x + width > safeWidth) width = safeWidth - x;
  if (y + height > safeHeight) height = safeHeight - y;
  if (width <= 0 || height <= 0) return false;
  outX = x;
  outY = y;
  outWidth = width;
  outHeight = height;
  return true;
}
} // namespace

const char* presetFileForKey(const std::string& key) noexcept {
  static const std::unordered_map<std::string, const char*> presets = {
      {"chromatic", "chromatic.glsl"},
      {"circleBlur", "circle_blur.glsl"},
      {"circle_blur", "circle_blur.glsl"},
      {"fisheye", "fisheye.glsl"},
      {"glitch", "glitch.glsl"},
      {"grayscale", "grayscale.glsl"},
      {"noise", "noise.glsl"},
      {"pixel", "pixel.glsl"},
      {"radialBlur", "radial_blur.glsl"},
      {"radial_blur", "radial_blur.glsl"},
      {"shockwave", "shockwave.glsl"},
      {"vignette", "vignette.glsl"},
      {"official_glitch", "official_glitch.glsl"},
      {"official_glitch_dog", "official_glitch_dog.glsl"},
      {"official_rgb_shift", "official_rgb_shift.glsl"},
      {"official_vignette_plus", "official_vignette_plus.glsl"},
      {"official_lens_distortion", "official_lens_distortion.glsl"},
      {"official_esc", "official_esc.glsl"},
      {"official_light_band", "official_light_band.glsl"},
      {"official_dimension_slash", "official_dimension_slash.glsl"},
      {"official_simple_vignette", "official_simple_vignette.glsl"},
      {"official_kaleido", "official_kaleido.glsl"},
      {"official_ds_grid", "official_ds_grid.glsl"},
      {"official_vignette_noise", "official_vignette_noise.glsl"},
      {"official_bloom_lum", "official_bloom_lum.glsl"},
      {"official_bloom_down", "official_bloom_down.glsl"},
      {"official_bloom_up", "official_bloom_up.glsl"},
      {"official_bloom_composite", "official_bloom_composite.glsl"},
  };
  const auto it = presets.find(key);
  return it == presets.end() ? nullptr : it->second;
}

UniformDefaults parseUniformDefaults(const std::string& source) {
  UniformDefaults defaults;
  // Presets annotate defaults with a trailing comment: uniform float power; // %0.03%
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    const auto uniformPos = line.find("uniform ");
    if (uniformPos == std::string::npos) continue;
    const auto marker = line.find("// %");
    if (marker == std::string::npos) continue;
    const auto endMarker = line.find('%', marker + 4);
    if (endMarker == std::string::npos) continue;

    std::istringstream declaration(line.substr(uniformPos + 8, marker - uniformPos - 8));
    std::string type;
    std::string name;
    declaration >> type >> name;
    if (name.empty()) continue;
    const auto bracket = name.find_first_of("[;");
    if (bracket != std::string::npos) name.resize(bracket);

    std::vector<float> values;
    std::istringstream valueStream(line.substr(marker + 4, endMarker - marker - 4));
    std::string token;
    while (std::getline(valueStream, token, ',')) {
      token = trim(token);
      if (token.empty()) continue;
      values.push_back(static_cast<float>(std::strtod(token.c_str(), nullptr)));
    }
    if (values.empty()) continue;
    if (type == "float" && values.size() > 1) values.resize(1);
    if (type == "vec2" && values.size() > 2) values.resize(2);
    if (type == "vec4" && values.size() > 4) values.resize(4);
    defaults.emplace(name, std::move(values));
  }
  return defaults;
}

std::string preparePresetSource(const std::string& source) {
  // Drop any version directive: the context version is decided at build time.
  std::string body;
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto trimmed = trim(line);
    if (trimmed.rfind("#version", 0) == 0) continue;
    body += line;
    body.push_back('\n');
  }

  // Presets may seed globals with non-constant expressions; those initialisers are
  // hoisted into main so the shader compiles on strict drivers.
  const auto mainPos = body.find("void main");
  if (mainPos == std::string::npos) return body;
  std::string prefix = body.substr(0, mainPos);
  std::string suffix = body.substr(mainPos);

  static const char* kTypes[] = {"mat2", "mat3", "mat4", "vec2", "vec3", "vec4", "float", "int",
      "bool"};
  std::string newPrefix;
  std::string initialisers;
  int braceDepth = 0;
  std::istringstream prefixStream(prefix);
  std::string prefixLine;
  while (std::getline(prefixStream, prefixLine)) {
    bool hoisted = false;
    if (braceDepth == 0) {
      const auto trimmed = trim(prefixLine);
      // Only a plain declaration can be hoisted. A line carrying a parenthesis, a brace or
      // a block close is a function signature or a one-line definition, and treating it as
      // an initialiser would splice the surrounding code into a bogus statement.
      const bool isPlainDeclaration = !trimmed.empty() && trimmed.rfind("const ", 0) != 0 &&
          trimmed.find('(') == std::string::npos && trimmed.find('{') == std::string::npos &&
          trimmed.find('}') == std::string::npos;
      if (isPlainDeclaration) {
        for (const char* type : kTypes) {
          const std::string prefixToken = std::string(type) + " ";
          if (trimmed.rfind(prefixToken, 0) != 0) continue;
          const auto equal = trimmed.find('=');
          const auto semicolon = trimmed.rfind(';');
          if (equal == std::string::npos || semicolon == std::string::npos || semicolon < equal) {
            continue;
          }
          std::string declaration = trimmed.substr(0, equal);
          std::string expression = trimmed.substr(equal + 1, semicolon - equal - 1);
          newPrefix += declaration + ";\n";
          declaration.erase(0, declaration.find(' ') + 1);
          const auto identifierEnd = declaration.find_first_of(" \t[");
          std::string identifier = declaration.substr(0, identifierEnd);
          initialisers += identifier + " = " + expression + ";\n";
          hoisted = true;
          break;
        }
      }
    }
    if (!hoisted) newPrefix += prefixLine + "\n";
    for (const char character : prefixLine) {
      if (character == '{') ++braceDepth;
      else if (character == '}') braceDepth = std::max(0, braceDepth - 1);
    }
  }

  if (initialisers.empty()) return body;
  const auto mainBrace = suffix.find('{');
  if (mainBrace == std::string::npos) return body;
  suffix.insert(mainBrace + 1, "\n" + initialisers);
  return newPrefix + suffix;
}

PrprChain::~PrprChain() { release(); }

void PrprChain::release() noexcept {
  programs_.clear();
  bloom_.release();
  bloomTarget_.reset();
  vignetteNoise_.release();
  vignetteNoiseReady_ = false;
  noiseTarget_.reset();
  noiseClockBaseSec_ = 0.0;
  frozenNoiseClock_ = false;
}

gl::Texture* PrprChain::prepareVignetteNoise(const game::PrprEffect& effect, int screenWidth,
    int screenHeight, double chartTimeSec) {
  if (!vignetteNoiseReady_) return nullptr;

  // Noise is only generated while the pass asks for a non-zero brightness.
  float brightness = 0.0F;
  if (const auto it = effect.variables.find("vignetteNoiseBrightness");
      it != effect.variables.end()) {
    const auto values = it->second.evaluate(chartTimeSec);
    if (!values.empty()) brightness = values.front();
  }
  if (!(brightness > 0.0F)) return nullptr;

  float sampleRate = 0.5F;
  if (const auto it = effect.variables.find("vignetteNoiseSampleRate");
      it != effect.variables.end()) {
    const auto values = it->second.evaluate(chartTimeSec);
    if (!values.empty() && values.front() > 0.0F) sampleRate = values.front();
  }

  const int noiseWidth = std::max(1, static_cast<int>(sampleRate * static_cast<float>(screenWidth)));
  const int noiseHeight = std::max(1, static_cast<int>(sampleRate * static_cast<float>(screenHeight)));
  if (!noiseTarget_) noiseTarget_ = std::make_unique<gl::Framebuffer>();
  if (!noiseTarget_->valid() || noiseTarget_->width() != noiseWidth ||
      noiseTarget_->height() != noiseHeight) {
    if (!noiseTarget_->resize(noiseWidth, noiseHeight)) {
      noiseTarget_.reset();
      return nullptr;
    }
  }

  float saturation = 0.0F;
  if (const auto it = effect.variables.find("vignetteNoiseSaturation");
      it != effect.variables.end()) {
    const auto values = it->second.evaluate(chartTimeSec);
    if (!values.empty()) saturation = values.front();
  }

  noiseTarget_->bind();
  glViewport(0, 0, noiseWidth, noiseHeight);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_BLEND);
  auto& shader = vignetteNoise_.program();
  shader.use();
  shader.set1i("screenTexture", 0);
  shader.set1i("u_texture", 0);
  shader.set1i("u_texture_legacy", 0);
  shader.set2f("rpeRectMin", 0.0F, 0.0F);
  shader.set2f("rpeRectMax", 1.0F, 1.0F);
  shader.set2f("UVScale", 1.0F, 1.0F);
  const double noiseTimeSec = frozenNoiseClock_
      ? chartTimeSec + 3.0
      : monotonicSeconds() - noiseClockBaseSec_;
  shader.set1f("time", static_cast<float>(noiseTimeSec));
  shader.set1f("noiseBrightness", brightness);
  shader.set1f("noiseSaturation", saturation);
  (void)FullscreenQuad::instance().ensure();
  FullscreenQuad::instance().draw();
  return &noiseTarget_->color();
}

gl::Texture* PrprChain::applyBloom(const game::PrprEffect& effect, gl::Texture* input,
    gl::Framebuffer& output, double chartTimeSec) {
  if (input == nullptr || !output.valid()) return nullptr;
  if (!bloom_.prepare(assetRoot_)) return nullptr;
  if (!bloomTarget_ || bloomTarget_->width() != output.width() ||
      bloomTarget_->height() != output.height()) {
    bloomTarget_ = std::make_unique<gl::Framebuffer>();
    if (!bloomTarget_->resize(output.width(), output.height())) {
      bloomTarget_.reset();
      return nullptr;
    }
  }

  // Each value falls back to the preset default when the effect does not carry it.
  const auto scalar = [&](const char* name, float fallback) {
    const auto it = effect.variables.find(name);
    if (it == effect.variables.end()) return fallback;
    const auto values = it->second.evaluate(chartTimeSec);
    return values.empty() ? fallback : values.front();
  };

  fx::BloomSettings settings;
  settings.brightness = scalar("bloomBrightness", 0.0F);
  settings.threshold = scalar("bloomThreshold", 0.308F);
  settings.radius = scalar("bloomRadius", 1.1F);
  settings.intensity = scalar("bloomIntensity", 1.85F);
  settings.sampleRate = scalar("bloomSampleRate", 0.45F);
  settings.iterations = static_cast<int>(scalar("bloomIteration", 3.0F));
  bloom_.apply(settings, *input, *bloomTarget_, chartTimeSec);
  return &bloomTarget_->color();
}

void PrprChain::setAuxTextures(gl::Texture* glitchMap, gl::Texture* glitchDogMap,
    gl::Texture* wireNoise, gl::Texture* displaceMap) noexcept {
  glitchMap_ = glitchMap;
  glitchDogMap_ = glitchDogMap;
  wireNoise_ = wireNoise;
  displaceMap_ = displaceMap;
}

void PrprChain::prepare(const std::filesystem::path& assetRoot,
    const std::unordered_map<std::string, std::string>& chartSources,
    const std::vector<game::PrprEffect>& effects) {
  assetRoot_ = assetRoot;
  programs_.clear();

  std::vector<std::string> keys;
  for (const auto& effect : effects) {
    const std::string key = trim(effect.shader);
    if (key.empty()) continue;
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
  }

  // Live playback starts the noise phase here; offscreen capture derives it from chart time.
  noiseClockBaseSec_ = monotonicSeconds();
  if (const auto bytes = platform::AssetManager::readBinary(assetRoot_,
          "prpr_shaders/official_vignette_noise.glsl"); !bytes.empty()) {
    std::string source(bytes.begin(), bytes.end());
    vignetteNoiseReady_ = vignetteNoise_.build(preparePresetSource(source).c_str());
    if (!vignetteNoiseReady_) {
      std::fprintf(stderr, "[chain] vignette noise compile failed: %s\n",
          vignetteNoise_.program().log().c_str());
    }
    if (vignetteNoiseReady_) {
      vignetteNoise_.program().use();
      vignetteNoise_.program().set2f("UVScale", 1.0F, 1.0F);
    }
  }

  // The bloom chain has its own four programs; compile them up front when requested.
  if (std::find(keys.begin(), keys.end(), std::string("official_bloom")) != keys.end()) {
    bloom_.prepare(assetRoot_);
  }

  for (const auto& key : keys) {
    std::string source;
    // A pack may reference its own shader with or without the leading slash the format
    // allows, so both spellings are tried before falling back to the bundled presets.
    if (const auto it = chartSources.find(key); it != chartSources.end()) {
      source = it->second;
    } else if (key.size() > 1 && key.front() == '/') {
      if (const auto it = chartSources.find(key.substr(1)); it != chartSources.end()) {
        source = it->second;
      }
    }
    if (source.empty()) {
      if (const char* fileName = presetFileForKey(key); fileName != nullptr) {
        const auto bytes = platform::AssetManager::readBinary(assetRoot_, "prpr_shaders/" +
            std::string(fileName));
        if (!bytes.empty()) source.assign(bytes.begin(), bytes.end());
      }
    }
    if (source.empty() && key.front() != '/') {
      const auto bytes = platform::AssetManager::readBinary(assetRoot_, "prpr_shaders/" + key + ".glsl");
      if (!bytes.empty()) source.assign(bytes.begin(), bytes.end());
    }
    if (source.empty() && key.front() != '/') {
      const auto bytes = platform::AssetManager::readBinary(assetRoot_, "prpr_shaders/pr/" + key + ".glsl");
      if (!bytes.empty()) source.assign(bytes.begin(), bytes.end());
    }
    // A key that is itself a shader file name is also tried directly against the presets,
    // which is how a chart can name "vignette.glsl" rather than the preset key "vignette".
    if (source.empty() && !key.empty() && key.front() != '/' &&
        key.find(".glsl") != std::string::npos) {
      const auto bytes = platform::AssetManager::readBinary(assetRoot_, "prpr_shaders/" + key);
      if (!bytes.empty()) source.assign(bytes.begin(), bytes.end());
    }
    if (source.empty()) continue;

    auto program = std::make_unique<Program>();
    program->defaults = parseUniformDefaults(source);
    program->valid = program->pass.build(preparePresetSource(source).c_str());
    if (program->valid) {
      // The prpr vertex shader declares UVScale; every pass must set it to (1,1)
      // or the sample collapses into the centre texel.
      program->pass.program().use();
      program->pass.program().set2f("UVScale", 1.0F, 1.0F);
    }
    if (!program->valid) std::fprintf(stderr, "[chain] %s compile failed: %s\n", key.c_str(), program->pass.program().log().c_str());
    programs_.emplace(key, std::move(program));
  }
}

PrprChain::Program* PrprChain::resolve(const std::string& shaderKey) {
  auto it = programs_.find(trim(shaderKey));
  if (it != programs_.end() && it->second->valid) return it->second.get();
  // Preset names may be spelled with or without their file extension.
  std::string normalized = trim(shaderKey);
  const auto dot = normalized.find_last_of('.');
  if (dot != std::string::npos) normalized.resize(dot);
  it = programs_.find(normalized);
  if (it != programs_.end() && it->second->valid) return it->second.get();
  return nullptr;
}

void PrprChain::applyPass(const game::PrprEffect& effect, Program& program, gl::Texture* input,
    gl::Framebuffer& output, double chartTimeSec) {
  if (input == nullptr || !output.valid()) return;

  // The noise target is rendered before the output is bound: rendering it changes the bound
  // framebuffer and the viewport, so it cannot run between binding the output and drawing.
  gl::Texture* noise = prepareVignetteNoise(effect, output.width(), output.height(),
      chartTimeSec);

  output.bind();
  glViewport(0, 0, output.width(), output.height());
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  // Shader output replaces the target content; blending would double-composite it.
  glDisable(GL_BLEND);

  auto& shader = program.pass.program();
  shader.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input->id());
  shader.set1i("screenTexture", 0);
  shader.set1i("u_texture", 0);
  shader.set1i("u_texture_legacy", 0);
  shader.set2f("rpeRectMin", 0.0F, 0.0F);
  shader.set2f("rpeRectMax", 1.0F, 1.0F);
  shader.set2f("UVScale", 1.0F, 1.0F);
  shader.set1f("time", static_cast<float>(chartTimeSec));
  shader.set2f("screenSize", static_cast<float>(input->width()), static_cast<float>(input->height()));
  shader.set2f("resolution", static_cast<float>(input->width()), static_cast<float>(input->height()));

  // Auxiliary samplers used by the chapter-9 presets.
  if (glitchMap_ != nullptr) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glitchMap_->id());
    shader.set1i("glitchMap", 1);
    shader.set1i("glitchDogMap", 1);
  }
  if (wireNoise_ != nullptr) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, wireNoise_->id());
    shader.set1i("wireNoise", 2);
  }
  if (displaceMap_ != nullptr) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, displaceMap_->id());
    shader.set1i("displaceMap", 3);
  }
  // The vignette presets sample a procedural noise target regenerated each frame.
  if (noise != nullptr) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, noise->id());
    shader.set1i("noiseCol", 4);
  }
  glActiveTexture(GL_TEXTURE0);

  for (const auto& [name, values] : program.defaults) {
    setUniformArray(shader, name, values);
  }
  for (const auto& [name, variable] : effect.variables) {
    setUniformArray(shader, name, variable.evaluate(chartTimeSec));
  }

  FullscreenQuad::instance().draw();
  glEnable(GL_BLEND);
}

void PrprChain::applyPassScissored(const game::PrprEffect& effect, Program& program,
    gl::Texture* input, gl::Framebuffer& output, double chartTimeSec, int x, int y, int width,
    int height) {
  if (input == nullptr || !output.valid()) return;

  // The copy of the previous result has to be complete before the scissor goes on, so the
  // area outside the stage keeps the unprocessed picture.
  output.bind();
  glViewport(0, 0, output.width(), output.height());
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  BlitPass::instance().ensure();
  BlitPass::instance().draw(*input, 1.0F, 1.0F, 1.0F, 1.0F, false);

  const GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
  const bool blendWasOn = glIsEnabled(GL_BLEND) != GL_FALSE;
  // Clear only the stage region: the copied surround has to survive.
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);

  gl::Texture* noise = prepareVignetteNoise(effect, output.width(), output.height(),
      chartTimeSec);
  // The noise target is rendered into its own framebuffer, which would drop the scissor.
  output.bind();
  glViewport(0, 0, output.width(), output.height());
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glDisable(GL_BLEND);

  auto& shader = program.pass.program();
  shader.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input->id());
  shader.set1i("screenTexture", 0);
  shader.set1i("u_texture", 0);
  shader.set1i("u_texture_legacy", 0);
  shader.set2f("rpeRectMin", 0.0F, 0.0F);
  shader.set2f("rpeRectMax", 1.0F, 1.0F);
  // The quad still covers the whole target; the scissor decides which fragments survive,
  // so the coordinates stay in the same 0..1 space as the full-frame passes.
  shader.set2f("UVScale", 1.0F, 1.0F);
  shader.set1f("time", static_cast<float>(chartTimeSec));
  shader.set2f("screenSize", static_cast<float>(input->width()),
      static_cast<float>(input->height()));

  if (glitchMap_ != nullptr) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glitchMap_->id());
    shader.set1i("glitchMap", 1);
    shader.set1i("glitchDogMap", 1);
  }
  if (wireNoise_ != nullptr) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, wireNoise_->id());
    shader.set1i("wireNoise", 2);
  }
  if (displaceMap_ != nullptr) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, displaceMap_->id());
    shader.set1i("displaceMap", 3);
  }
  if (noise != nullptr) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, noise->id());
    shader.set1i("noiseCol", 4);
  }
  glActiveTexture(GL_TEXTURE0);

  for (const auto& [name, values] : program.defaults) {
    setUniformArray(shader, name, values);
  }
  for (const auto& [name, variable] : effect.variables) {
    setUniformArray(shader, name, variable.evaluate(chartTimeSec));
  }

  FullscreenQuad::instance().draw();

  if (!scissorWasOn) glDisable(GL_SCISSOR_TEST);
  if (blendWasOn) glEnable(GL_BLEND);
}

gl::Texture* PrprChain::apply(const std::vector<game::PrprEffect>& effects, gl::Texture* input,
    gl::Framebuffer& ping, gl::Framebuffer& pong, double chartTimeSec) {
  gl::Texture* current = input;
  bool usePing = true;
  for (const auto& effect : effects) {
    if (!isActiveAt(effect, chartTimeSec)) continue;
    gl::Framebuffer& target = usePing ? ping : pong;
    if (!target.valid()) continue;

    // Bloom is a multi-stage chain, not a single fullscreen pass.
    if (trim(effect.shader) == "official_bloom") {
      if (gl::Texture* result = applyBloom(effect, current, target, chartTimeSec)) {
        current = result;
        usePing = !usePing;
      }
      continue;
    }

    Program* program = resolve(effect.shader);
    if (program == nullptr) continue;
    applyPass(effect, *program, current, target, chartTimeSec);
    current = &target.color();
    usePing = !usePing;
  }
  return current;
}

gl::Texture* PrprChain::applyStageViewport(const std::vector<game::PrprEffect>& effects,
    gl::Texture* input, gl::Framebuffer& ping, gl::Framebuffer& pong, const StageRect& rect,
    double chartTimeSec) {
  if (input == nullptr) return input;

  // Stage-scoped effects cover the play field only; everything outside it has to keep the
  // unprocessed scene. Each pass therefore copies its input into the target in full and
  // then draws the shader with the scissor restricted to the stage rectangle.
  int viewportX = 0;
  int viewportY = 0;
  int viewportWidth = 0;
  int viewportHeight = 0;
  if (!stageViewportRect(rect, ping.width(), ping.height(), viewportX, viewportY,
          viewportWidth, viewportHeight)) {
    // A stage that cannot cover a pixel means there is nothing to process.
    return input;
  }

  gl::Texture* current = input;
  bool usePing = true;
  for (const auto& effect : effects) {
    if (!isActiveAt(effect, chartTimeSec)) continue;
    gl::Framebuffer& target = usePing ? ping : pong;
    if (!target.valid()) continue;

    if (trim(effect.shader) == "official_bloom") {
      // Bloom is a multi-stage chain that always runs full-screen; scoping it would need a
      // second copy of the whole chain, and no shipped stage-scoped effect is a bloom.
      if (gl::Texture* result = applyBloom(effect, current, target, chartTimeSec)) {
        current = result;
        usePing = !usePing;
      }
      continue;
    }

    Program* program = resolve(effect.shader);
    if (program == nullptr) continue;
    applyPassScissored(effect, *program, current, target, chartTimeSec, viewportX, viewportY,
        viewportWidth, viewportHeight);
    current = &target.color();
    usePing = !usePing;
  }
  return current;
}

} // namespace rechigros::render::fx
