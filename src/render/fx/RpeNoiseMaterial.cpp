#include "RpeNoiseMaterial.hpp"

#include "rechigros/game/RpeCamera.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rechigros::render::fx {
namespace {

const char* kVertex = R"(
in vec2 aPosition;
out lowp vec4 color;
out highp vec2 fieldUV;
out highp vec2 sceneUV;
out highp vec2 displaceUV;
out highp vec2 sparkUV;
out highp vec2 disabledDisplaceUV;
out highp vec2 touchDisplaceUV;
out highp vec2 noiseUV;
out highp vec4 screenPos;
out highp float clipHalfWidth;
uniform vec3 uView;
void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0);
  color = vec4(1.0);
  fieldUV = aPosition * 0.5 + 0.5;
  sceneUV = fieldUV;
  displaceUV = fieldUV * vec2(0.8, 0.3);
  sparkUV = fieldUV * vec2(3.0, 1.2);
  disabledDisplaceUV = fieldUV * vec2(0.5, 0.2);
  touchDisplaceUV = fieldUV * vec2(0.55, 0.3);
  noiseUV = fieldUV * vec2(1.5, 1.46);
  screenPos = vec4(fieldUV, 0.0, 1.0);
  clipHalfWidth = uView.y * 0.888888896 / uView.x;
}
)";

void bind(const gl::ShaderProgram& program, int unit, const char* name, const gl::Texture& texture) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texture.id());
  program.set1i(name, unit);
}

int mirrorIndex(float value, int size) {
  int index = static_cast<int>(std::floor(value * size));
  const int period = std::max(1, size * 2);
  index %= period;
  if (index < 0) index += period;
  if (index >= size) index = period - 1 - index;
  return std::clamp(index, 0, size - 1);
}

float samplePlane(const std::vector<std::uint8_t>& plane, int width, int height, float x, float y) {
  const int px = std::clamp(static_cast<int>(x * width), 0, width - 1);
  const int py = std::clamp(static_cast<int>(y * height), 0, height - 1);
  return plane[py * width + px] / 255.0F;
}

struct Shape {
  std::array<game::RpeNoisePoint, 4> points;
  bool invert{};
  game::RpeNoiseState state{};
  game::RpeNoiseSample sample;
  const game::RpeNoiseArea* area{};
};

} // namespace

const std::vector<RpeNoiseTouchSample>& RpeNoiseTouchTimeline::update(
    const std::vector<RpeNoiseTouch>& touches, double seconds, double releaseDuration) {
  if (seconds < lastSeconds) reset();
  lastSeconds = seconds;
  for (auto& slot : slots_) {
    if (slot.id < 0) continue;
    const double progress = std::clamp((seconds - slot.start) / 0.1, 0.0, 1.0);
    slot.scale = static_cast<float>(slot.startScale + (slot.endScale - slot.startScale) * progress);
    if (!slot.held && seconds - slot.release > std::max(0.0, releaseDuration)) slot.id = -1;
  }
  for (const auto& touch : touches) {
    auto found = std::find_if(slots_.begin(), slots_.end(),
        [&](const Slot& slot) { return slot.id == touch.id; });
    if (found == slots_.end()) {
      found = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.id < 0; });
    }
    if (found == slots_.end()) continue;
    if (found->id != touch.id) {
      found->id = touch.id;
      found->start = seconds;
      found->startScale = 0.0F;
      found->endScale = 1.0F;
      found->scale = 0.0F;
    } else if (!found->held) {
      // The same finger came back before the release animation retired its slot. Grow it
      // from the current scale again; leaving endScale at zero would fade a held touch out.
      found->held = true;
      found->start = seconds;
      found->startScale = found->scale;
      found->endScale = 1.0F;
    }
    found->x = touch.x;
    found->y = touch.y;
    found->held = true;
    found->lastSeen = seconds;
  }
  for (auto& slot : slots_) {
    if (slot.id >= 0 && slot.lastSeen != seconds && slot.held) {
      slot.held = false;
      slot.release = seconds;
      slot.start = seconds;
      slot.startScale = slot.scale;
      slot.endScale = 0.0F;
    }
  }
  std::vector<const Slot*> ordered;
  for (const auto& slot : slots_) if (slot.id >= 0) ordered.push_back(&slot);
  std::sort(ordered.begin(), ordered.end(), [](const Slot* a, const Slot* b) { return a->id < b->id; });
  samples_.clear();
  for (const Slot* slot : ordered) {
    samples_.push_back({slot->x, slot->y,
        static_cast<float>(seconds - (slot->held ? slot->start : slot->release)), slot->scale,
        slot->held});
  }
  return samples_;
}

void RpeNoiseTouchTimeline::reset() {
  slots_ = {};
  samples_.clear();
  lastSeconds = -1.0e300;
}

RpeNoiseCoverage buildRpeNoiseCoverage(int width, int height,
    const std::vector<game::RpeNoiseArea>& areas, const std::string& layerId, double seconds,
    float stageX, float stageY, float stageWidth, float stageHeight, bool singleInversion,
    const game::RpeCameraFrame* camera, float cameraViewScale) {
  RpeNoiseCoverage result;
  result.width = std::max(1, width);
  result.height = std::max(1, height);
  width = result.width;
  height = result.height;
  const std::size_t count = static_cast<std::size_t>(width) * height;
  result.skin.resize(count * 4U, 0U);
  result.active.resize(count, 0U);
  result.base.resize(count * 4U, 0U);
  result.aux.resize(count * 4U, 0U);
  std::vector<Shape> shapes;
  for (const auto& area : areas) {
    if (area.layerId != layerId) continue;
    auto sample = game::RpeNoiseRuntime(area).sample(seconds);
    if (sample.state == game::RpeNoiseState::HiddenBefore ||
        sample.state == game::RpeNoiseState::HiddenAfter ||
        sample.scaleX == 0.0 || sample.scaleY == 0.0 ||
        area.left == area.right || area.bottom == area.top) continue;
    shapes.push_back({sample.points, area.invert, sample.state, sample, &area});
  }
  const int stride = width + 1;
  std::array<std::vector<int>, 1> normal, inverted;
  normal[0].resize(stride * height);
  inverted[0].resize(stride * height);
  std::array<std::vector<std::uint8_t>, 3> covered;
  for (auto& plane : covered) plane.resize(count);
  const bool perspective = camera != nullptr && camera->active;
  const double aspect = stageHeight > 0.0F ? stageWidth / stageHeight : 1.5;
  constexpr int kGroupOrder[3] = {1, 2, 0};
  for (const int group : kGroupOrder) {
    for (auto& plane : normal) std::fill(plane.begin(), plane.end(), 0);
    for (auto& plane : inverted) std::fill(plane.begin(), plane.end(), 0);
    for (const auto& shape : shapes) {
      const bool eligible = group == 0 ? shape.state == game::RpeNoiseState::Active
          : group == 1 ? shape.state != game::RpeNoiseState::Active
          : shape.state == game::RpeNoiseState::Ready;
      if (!eligible) continue;
      auto& diff = shape.invert ? inverted[0] : normal[0];
      if (perspective) {
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const auto point = camera->planeAt((x + 0.5F) / width * stageWidth,
                (y + 0.5F) / height * stageHeight, stageWidth, stageHeight, cameraViewScale);
            if (!point || !game::rpeNoiseContains(*shape.area, shape.sample,
                    {(*point)[0], (*point)[1]}, 0.0, aspect)) continue;
            ++diff[y * stride + x];
            --diff[y * stride + x + 1];
          }
        }
        continue;
      }
      std::array<game::RpeNoisePoint, 4> points;
      float low = std::numeric_limits<float>::max();
      float high = std::numeric_limits<float>::lowest();
      for (int i = 0; i < 4; ++i) {
        const auto& point = shape.points[i];
        points[i] = {stageX + stageWidth * 0.5F + static_cast<float>(point.x) * stageWidth / 1350.0F,
            stageY + stageHeight * 0.5F - static_cast<float>(point.y) * stageHeight / 900.0F};
        points[i].x = points[i].x * width / stageWidth;
        points[i].y = points[i].y * height / stageHeight;
        low = std::min(low, static_cast<float>(points[i].y));
        high = std::max(high, static_cast<float>(points[i].y));
      }
      const int firstY = std::clamp(static_cast<int>(std::ceil(low - 0.5F)), 0, height);
      const int lastY = std::clamp(static_cast<int>(std::ceil(high - 0.5F)), 0, height);
      for (int y = firstY; y < lastY; ++y) {
        float start = std::numeric_limits<float>::max();
        float end = std::numeric_limits<float>::lowest();
        const float py = y + 0.5F;
        for (int i = 0; i < 4; ++i) {
          const auto& a = points[i];
          const auto& b = points[(i + 1) % 4];
          if (!((a.y <= py && b.y > py) || (b.y <= py && a.y > py))) continue;
          const float x = a.x + (b.x - a.x) * (py - a.y) / (b.y - a.y);
          start = std::min(start, x);
          end = std::max(end, x);
        }
        if (start > end) continue;
        const int first = std::clamp(static_cast<int>(std::ceil(start - 0.5F)), 0, width);
        const int last = std::clamp(static_cast<int>(std::ceil(end - 0.5F)), 0, width);
        if (first >= last) continue;
        ++diff[y * stride + first];
        --diff[y * stride + last];
      }
    }
    for (int y = 0; y < height; ++y) {
      int n = 0, inv = 0;
      for (int x = 0; x < width; ++x) {
        n += normal[0][y * stride + x];
        inv += inverted[0][y * stride + x];
        const bool hasInversion = singleInversion ? inv == 1 : (inv & 1) != 0;
        const bool has = (n > 0) != hasInversion;
        const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
        if (n > 0) result.skin[pixel * 4U] |= 1U << (group * 2);
        if (hasInversion) result.skin[pixel * 4U] |= 2U << (group * 2);
        if (!has) continue;
        if (group == 0) {
          for (auto& plane : covered) plane[pixel] = 0;
          covered[0][pixel] = 255;
        } else if (group == 2) {
          covered[2][pixel] = 255;
          covered[1][pixel] = 255;
        } else {
          covered[1][pixel] = 255;
        }
      }
    }
  }
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    const auto i = pixel * 4U;
    result.active[pixel] = covered[0][pixel];
    result.base[i + 3] = covered[1][pixel];
    result.aux[i] = covered[2][pixel];
    result.aux[i + 2] = covered[1][pixel];
    result.skin[i + 3] = 255;
    result.empty = result.empty && covered[2][pixel] == 0 && covered[1][pixel] == 0 &&
        covered[0][pixel] == 0;
  }
  return result;
}

bool RpeNoiseMaterial::prepare(const std::filesystem::path& assetRoot, gl::Texture* displace,
    gl::Texture* spark, gl::Texture* noise) {
  release();
  displace_ = displace;
  spark_ = spark;
  noise_ = noise;
  if (displace_ == nullptr || spark_ == nullptr || noise_ == nullptr) return false;
  const auto bytes = platform::AssetManager::readBinary(assetRoot, "official_shaders/rpe_noise_material.glsl");
  const std::string source(bytes.begin(), bytes.end());
  if (source.empty() || !material_.build(kVertex, source)) return false;
  const auto displacementBytes = platform::AssetManager::readBinary(assetRoot,
      "official_textures/BlockNoise1.png");
  const auto hoverBytes = platform::AssetManager::readBinary(assetRoot,
      "official_textures/TouchHover.png");
  auto displacement = asset::decodeImageMemory(displacementBytes.data(), displacementBytes.size());
  auto hover = asset::decodeImageMemory(hoverBytes.data(), hoverBytes.size());
  if (!displacement.valid() || !hover.valid()) return false;
  noiseWidth_ = displacement.width;
  noiseHeight_ = displacement.height;
  displacementPixels_ = std::move(displacement.pixels);
  hoverWidth_ = hover.width;
  hoverHeight_ = hover.height;
  hoverPixels_ = std::move(hover.pixels);
  return FullscreenQuad::instance().ensure();
}

void RpeNoiseMaterial::release() {
  material_.reset();
  effect_.reset();
  aux_.reset();
  effectWidth_ = effectHeight_ = 0;
  displacementPixels_.clear();
  hoverPixels_.clear();
  noiseWidth_ = noiseHeight_ = hoverWidth_ = hoverHeight_ = 0;
}

void RpeNoiseMaterial::uploadParameters(double seconds) {
  static constexpr std::pair<const char*, float> floats[] = {
      {"_EdgeOpacity",0.8F},{"_FillStrength",0.667F},{"_FillOpacity",0.667F},
      {"_GlowIntensity",0.8F},{"_SparkMapOpacity",5.69F},{"_SparkHueShiftAmount",0.2F},
      {"_SparkDisplaceIntensity",2.39F},{"_DisplaceBlendIntensity",0.411F},
      {"_DisplaceSpeed",1.5F},{"_DisplaceStrength",0.15F},{"_TouchPosShine",0.0F},
      {"_TouchPosRadius",0.5F},{"_TouchPosSDFSmoothness",0.47F},{"_TouchPosSDFFalloff",0.41F},
      {"_BackgroundPixelScale",6.0F},{"uDisabledFillOpacity",0.4F},
      {"uDisabledSparkOpacity",3.5F},{"uDisabledSparkIntensity",2.29F},{"uDisabledSpeed",0.3F},
      {"_ShineSpeed",37.9F},{"_ShineBrightness",0.12F},{"_TouchDisplaceSpeed",2.9F},
      {"_TouchDisplaceStrength",0.08F},{"_NoiseEvoSpeed",0.03F},{"_NoiseDirChangeSpeed",60.0F},
      {"_NoiseDisplaceStrength",1.0F},{"_NoiseRadius",0.48F},{"_NoiseSmoothness",1.0F},
      {"_SDFCellSize",0.11F},{"_SDFSmoothness",0.63F},{"_SDFFalloff",0.34F},
      {"_SDFMoveSpeed",9.3F},{"_TouchBackgroundPixelScale",8.0F}};
  static constexpr float colors[][4] = {
      {1.0F,0.3301886F,0.3301886F,1.0F},{0.7132075F,0.235493F,0.235493F,1.0F},
      {1.0F,0.1792452F,0.1792452F,1.0F},{1.0F,1.0F,0.0F,0.0F},
      {0.497F,0.138F,0.138F,1.0F},{1.0F,1.0F,1.0F,1.0F},
      {1.0F,1.0F,0.0F,0.0F},{1.0F,0.0F,0.0F,1.0F},{1.0F,0.0F,0.0F,1.0F}};
  static constexpr const char* colorNames[] = {"_EdgeColor","_FillColor","_GlowColor",
      "_DisplaceDirection","uDisabledFillColor","_ShineColor","_TouchDisplaceDirection",
      "_NoiseTint","_TouchGlowColor"};
  for (const auto& [name, value] : floats) material_.set1f(name, value);
  for (std::size_t i = 0; i < std::size(colors); ++i) {
    material_.set4f(colorNames[i], colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
  }
  material_.set3f("_SparkTint", 1.0F, 0.2849057F, 0.2849057F);
  material_.set3f("uDisabledSparkTint", 0.3113208F, 0.0778302F, 0.0778302F);
  material_.set1i("uColored", 0);
  material_.set1f("uReadyExtra", 0.0F);
  const float time = static_cast<float>(seconds);
  material_.set4f("uUnityTime", time / 20.0F, time, time * 2.0F, time * 3.0F);
  material_.set3f("uView", static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_),
      static_cast<float>(viewportWidth_) / viewportHeight_);
  material_.set4f("_ScreenParams", static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_),
      1.0F + 1.0F / viewportWidth_, 1.0F + 1.0F / viewportHeight_);
  material_.set4f("_EffectRT_TexelSize", 1.0F / effectWidth_, 1.0F / effectHeight_,
      static_cast<float>(effectWidth_), static_cast<float>(effectHeight_));
}

gl::Texture* RpeNoiseMaterial::draw(gl::Texture& scene, gl::Framebuffer (&screens)[2],
    const RpeNoiseCoverage& coverage, double seconds, const std::vector<RpeNoiseTouchSample>& touches,
    int layerMask) {
  if (!valid() || coverage.empty) return &scene;
  const int width = coverage.width;
  const int height = coverage.height;
  if (effectWidth_ != width || effectHeight_ != height) {
    effectWidth_ = width;
    effectHeight_ = height;
    effect_.uploadRgba(width, height, nullptr);
    aux_.uploadRgba(width, height, nullptr);
    effect_.setFilter(true);
    aux_.setFilter(true);
  }
  effectPixels_ = coverage.base;
  current_.assign(width * height, 0U);
  const float dt = 0.70710678F * static_cast<float>(seconds) / 20.0F * 2.59F;
  for (int y = 0; y < height; ++y) {
    const float v = (y + 0.5F) / height;
    const int ny = mirrorIndex(dt + v * 1.02F, noiseHeight_);
    for (int x = 0; x < width; ++x) {
      const float u = (x + 0.5F) / width;
      const auto sample = [&](float sx) {
        const int nx = mirrorIndex(sx, noiseWidth_);
        return displacementPixels_[((noiseHeight_ - 1 - ny) * noiseWidth_ + nx) * 4] / 255.0F - 0.5F;
      };
      const float a = sample(dt + u * 2.13F);
      const float b = sample(-dt + u * 2.13F);
      const float sx = u + (0.70710678F * a - 0.70710678F * b) * 0.1F;
      const float sy = v + (0.70710678F * a + 0.70710678F * b) * 0.1F;
      current_[y * width + x] = static_cast<std::uint8_t>(std::lround(
          samplePlane(coverage.active, width, height, sx, sy) * 255.0F));
    }
  }
  const auto byte = [](float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  for (std::size_t index = 0; index < current_.size(); ++index) {
    effectPixels_[index * 4U] = current_[index];
  }
  float totalWeight = 0.0F;
  for (int pass = 0; pass < 6; ++pass) totalWeight += std::pow(6.0F - pass, 2.65F);
  for (int pass = 0; pass < 6; ++pass) {
    const float weight = std::pow(6.0F - pass, 2.65F) / totalWeight;
    if (weight < 0.01F) break;
    next_.resize(current_.size());
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        float maximum = 0.0F;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = std::clamp(x + dx, 0, width - 1);
            const int ny = std::clamp(y + dy, 0, height - 1);
            maximum = std::max(maximum, current_[ny * width + nx] / 255.0F);
          }
        }
        next_[y * width + x] = byte(maximum);
        const std::size_t index = static_cast<std::size_t>(y) * width + x;
        const float delta = maximum - current_[index] / 255.0F;
        if (pass == 0) effectPixels_[index * 4U + 1] = byte(delta);
        const float outside = 1.0F - effectPixels_[index * 4U] / 255.0F;
        effectPixels_[index * 4U + 2] = byte(weight * outside * delta +
            effectPixels_[index * 4U + 2] / 255.0F);
      }
    }
    std::swap(current_, next_);
  }
  auto aux = coverage.aux;
  const int maskWidth = width / 2;
  const int maskHeight = height / 2;
  const auto sampleSprite = [&](float u, float v, int channel) {
    const float x = u * hoverWidth_ - 0.5F;
    const float y = (1.0F - v) * hoverHeight_ - 0.5F;
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - std::floor(x);
    const float fy = y - std::floor(y);
    const auto pixel = [&](int px, int py) {
      px = std::clamp(px, 0, hoverWidth_ - 1);
      py = std::clamp(py, 0, hoverHeight_ - 1);
      return hoverPixels_[(py * hoverWidth_ + px) * 4 + channel] / 255.0F;
    };
    return (pixel(ix, iy) * (1.0F - fx) + pixel(ix + 1, iy) * fx) * (1.0F - fy) +
        (pixel(ix, iy + 1) * (1.0F - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
  };
  for (const auto& touch : touches) {
    if (!(touch.scale > 0.0F)) continue;
    const float size = 0.506F * touch.scale;
    const float aspect = static_cast<float>(viewportWidth_) / viewportHeight_;
    for (int y = 0; y < maskHeight; ++y) {
      const float v = (y + 0.5F) / maskHeight;
      for (int x = 0; x < maskWidth; ++x) {
        const float u = (x + 0.5F) / maskWidth;
        const float su = (u - touch.x) * aspect / size + 0.5F;
        const float sv = (v - touch.y) / size + 0.5F;
        if (su < 0.0F || su > 1.0F || sv < 0.0F || sv > 1.0F) continue;
        const float red = sampleSprite(su, sv, 0);
        const float alpha = sampleSprite(su, sv, 3);
        for (int dy = 0; dy < 2; ++dy) {
          for (int dx = 0; dx < 2; ++dx) {
            auto& output = aux[((y * 2 + dy) * width + x * 2 + dx) * 4U + 3];
            output = byte(red * alpha + output / 255.0F * (1.0F - alpha));
          }
        }
      }
    }
  }
  effect_.updateRgba(width, height, effectPixels_.data());
  aux_.updateRgba(width, height, aux.data());
  GLint previousFbo = 0;
  GLint previousViewport[4]{};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const bool blend = glIsEnabled(GL_BLEND) != GL_FALSE;
  const bool depth = glIsEnabled(GL_DEPTH_TEST) != GL_FALSE;
  const bool scissor = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
  GLint blendFactors[4]{};
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendFactors[0]);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendFactors[1]);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendFactors[2]);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendFactors[3]);
  screens[0].resize(viewportWidth_, viewportHeight_);
  screens[0].bind();
  glViewport(0, 0, viewportWidth_, viewportHeight_);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  BlitPass::instance().draw(scene, 1.0F, 1.0F, 1.0F, 1.0F, false);
  material_.use();
  uploadParameters(seconds);
  bind(material_, 0, "uDisplaceTex", *displace_);
  bind(material_, 1, "uSparkTex", *spark_);
  bind(material_, 2, "uMasks", effect_);
  bind(material_, 3, "uScene", scene);
  bind(material_, 4, "uAuxMasks", aux_);
  bind(material_, 5, "uNoiseTex", *noise_);
  bind(material_, 6, "uActiveColors", gl::whiteTexture());
  bind(material_, 7, "uDisabledColors", gl::whiteTexture());
  int shineCount = 0;
  float shinePositions[20]{};
  for (const auto& touch : touches) {
    if (!touch.held || shineCount >= 10) continue;
    shinePositions[shineCount * 2] = touch.x * static_cast<float>(viewportWidth_) / viewportHeight_;
    shinePositions[shineCount * 2 + 1] = touch.y;
    ++shineCount;
  }
  material_.set1i("_TouchPosCount", shineCount);
  material_.set2fv("_TouchPos", shineCount, shinePositions);
  material_.set1f("_TouchPosShine",
      (0.63F + 0.37F * (std::sin(static_cast<float>(seconds) * 43.0F) * 0.5F + 0.5F)) * 2.0F);
  const bool hasDisabled = std::any_of(coverage.base.begin(), coverage.base.end(),
      [](std::uint8_t value) { return value != 0; });
  if (hasDisabled && (layerMask & 1) != 0) {
    material_.set1i("uLayer", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    FullscreenQuad::instance().draw();
  }
  gl::Texture* result = &screens[0].color();
  const bool hasActive = std::any_of(coverage.active.begin(), coverage.active.end(),
      [](std::uint8_t value) { return value != 0; });
  const bool hasReady = std::any_of(coverage.aux.begin(), coverage.aux.end(),
      [](std::uint8_t value) { return value != 0; });
  if ((layerMask & 2) != 0 && (hasActive || hasReady || !touches.empty())) {
    screens[1].resize(viewportWidth_, viewportHeight_);
    screens[1].bind();
    glViewport(0, 0, viewportWidth_, viewportHeight_);
    glDisable(GL_BLEND);
    BlitPass::instance().draw(*result, 1.0F, 1.0F, 1.0F, 1.0F, false);
    material_.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    material_.set1i("uLayer", 3);
    FullscreenQuad::instance().draw();
    result = &screens[1].color();
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  glBlendFuncSeparate(static_cast<GLenum>(blendFactors[0]), static_cast<GLenum>(blendFactors[1]),
      static_cast<GLenum>(blendFactors[2]), static_cast<GLenum>(blendFactors[3]));
  if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
  return result;
}

} // namespace rechigros::render::fx
