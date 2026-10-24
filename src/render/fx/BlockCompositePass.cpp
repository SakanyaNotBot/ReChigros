#include "rechigros/render/fx/BlockCompositePass.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace rechigros::render::fx {
namespace {

const char* const kPrepassShader = "official_shaders/block_prepass.glsl";
const char* const kCompositeShader = "official_shaders/block_composite.glsl";

// Material values the shipped shader reads from its uniform arrays. The rows follow the
// order the shader declares them in: compose transform and strength, then the active and
// disabled looks, then the spark and shine controls.
constexpr float kMatParams[8][4] = {
    {0.8F, 0.667F, 0.667F, 0.8F},
    {5.69F, 0.2F, 2.39F, 0.411F},
    {1.5F, 0.15F, 6.0F, 37.9F},
    {0.12F, 0.0F, 0.5F, 0.47F},
    {0.41F, 0.03F, 60.0F, 1.0F},
    {0.48F, 1.0F, 0.11F, 0.63F},
    {0.34F, 9.3F, 0.0F, 0.0F},
    {8.0F, 0.0F, 0.0F, 0.0F},
};

constexpr float kMatColors[7][4] = {
    {1.0F, 0.3301886F, 0.3301886F, 1.0F},
    {0.7132075F, 0.235493F, 0.235493F, 1.0F},
    {1.0F, 0.1792452F, 0.1792452F, 1.0F},
    {1.0F, 0.2849057F, 0.2849057F, 1.0F},
    {0.497F, 0.138F, 0.138F, 0.4F},
    {1.0F, 0.0F, 0.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 1.0F},
};

constexpr float kMatExtra[5][4] = {
    {2.13F, 1.02F, 2.59F, 0.1F},
    {0.8F, 0.3F, 1.5F, 0.15F},
    {3.0F, 1.2F, 0.5F, 0.2F},
    {0.3F, 2.29F, 0.55F, 0.3F},
    {2.9F, 0.08F, 1.5F, 1.46F},
};

// Raw-colour additive quads. The sprite batch cannot fill the masks: it multiplies its
// colour by alpha and every mask texel keeps its coverage in the colour channels.
const char* const kMaskVertexSource = R"(
in vec2 aPosition;
in vec4 aColor;
out vec4 vColor;
void main() {
  vColor = aColor;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

const char* const kMaskFragmentSource = R"(
in vec4 vColor;
out vec4 fragmentColor;
void main() {
  fragmentColor = vColor;
}
)";

// The hover channel is a sprite with its own alpha: the shipped shader writes only the
// sprite's alpha into the second mask, multiplied by the quad's colour alpha. The reference
// uses a separate program for this rather than the mask program, so the coverage is a
// picture of the touched spot rather than a flat rectangle.
const char* const kHoverFragmentSource = R"(
in vec4 vColor;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 fragmentColor;
void main() {
  fragmentColor = vec4(0.0, 0.0, 0.0, texture(uTex, vUv).a * vColor.a);
}
)";

const char* const kHoverVertexSource = R"(
in vec2 aPosition;
in vec4 aColor;
in vec2 aUv;
out vec4 vColor;
out vec2 vUv;
void main() {
  vColor = aColor;
  vUv = aUv;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

[[nodiscard]] std::string readShader(const std::filesystem::path& assetRoot, const char* relative) {
  const auto bytes = platform::AssetManager::readBinary(assetRoot, relative);
  return std::string(bytes.begin(), bytes.end());
}

void bindTexture(int unit, const char* sampler, const gl::Texture* texture,
    const gl::ShaderProgram& program) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, texture != nullptr ? texture->id() : 0);
  program.set1i(sampler, unit);
}

// Appends one rotated quad as two triangles. Positions arrive in screen pixels with the
// origin at the top left and leave as clip space, one texel per vertex.
void appendQuad(std::vector<float>& out, const BlockMaskQuad& quad, int width, int height) {
  constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;
  const float radians = quad.rotationDeg * kDegToRad;
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  const float halfWidth = quad.width * 0.5F;
  const float halfHeight = quad.height * 0.5F;
  const float corners[4][2] = {
      {-halfWidth, -halfHeight},
      {halfWidth, -halfHeight},
      {-halfWidth, halfHeight},
      {halfWidth, halfHeight}};
  const float safeWidth = static_cast<float>(std::max(1, width));
  const float safeHeight = static_cast<float>(std::max(1, height));
  float clip[4][2];
  for (int corner = 0; corner < 4; ++corner) {
    const float localX = corners[corner][0];
    const float localY = corners[corner][1];
    const float x = quad.centerX + localX * cosine - localY * sine;
    const float y = quad.centerY + localX * sine + localY * cosine;
    clip[corner][0] = x * 2.0F / safeWidth - 1.0F;
    clip[corner][1] = 1.0F - y * 2.0F / safeHeight;
  }
  const int order[6] = {0, 1, 2, 2, 1, 3};
  for (const int index : order) {
    out.push_back(clip[index][0]);
    out.push_back(clip[index][1]);
    out.push_back(quad.red);
    out.push_back(quad.green);
    out.push_back(quad.blue);
    out.push_back(quad.alpha);
  }
}

} // namespace

float BlockCompositePass::glowRingWeight(int pass) noexcept {
  if (pass < 0 || pass >= GLOW_PASSES) return 0.0F;
  // The chain weights each ring by its remaining reach over the sum of the reach values,
  // so the innermost ring carries the most weight.
  float sum = 0.0F;
  for (int ring = 1; ring <= GLOW_PASSES; ++ring) {
    sum += static_cast<float>(ring);
  }
  if (!(sum > 0.0F)) return 0.0F;
  return static_cast<float>(GLOW_PASSES - pass) / sum;
}

float BlockCompositePass::gate(float value) noexcept {
  if (value >= GATE_HIGH) return -1.0F;
  if (value >= GATE_LOW) return 1.0F;
  return 0.0F;
}

int BlockCompositePass::reducedSize(int screenPixels, int divisor) noexcept {
  if (divisor <= 0) return std::max(1, screenPixels);
  return std::max(1, screenPixels / divisor);
}

bool BlockCompositePass::prepare(
    const std::filesystem::path& assetRoot, gl::Texture* displace, gl::Texture* spark,
    gl::Texture* noise) noexcept {
  // Keep the allocated targets across a rebuild: a caller that prepared once and then
  // re-prepared (a chart switch) must not lose its viewport size.
  const int previousWidth = width_;
  const int previousHeight = height_;
  release();
  displaceMap_ = displace;
  sparkMap_ = spark;
  noiseMap_ = noise;
  assetRoot_ = assetRoot;
  const std::string prepass = readShader(assetRoot, kPrepassShader);
  const std::string composite = readShader(assetRoot, kCompositeShader);
  if (prepass.empty() || composite.empty()) return false;
  // The sources are already written against the legacy ES spelling; the program rewrites
  // them for the active context.
  if (!prepass_.build(prepass.c_str())) return false;
  if (!composite_.build(composite.c_str())) return false;
  ensureMaskRasteriser();
  ready_ = true;
  if (previousWidth > 0 && previousHeight > 0) resize(previousWidth, previousHeight);
  return true;
}

void BlockCompositePass::release() noexcept {
  ready_ = false;
  prepass_.release();
  composite_.release();
  maskProgram_.reset();
  if (maskVao_ != 0) glDeleteVertexArrays(1, &maskVao_);
  if (maskVbo_ != 0) glDeleteBuffers(1, &maskVbo_);
  maskVao_ = 0;
  maskVbo_ = 0;
  for (auto& target : maskFbo_) target.reset();
  compose_.reset();
  for (auto& target : ping_) target.reset();
  effect_.reset();
  width_ = 0;
  height_ = 0;
  stageLeft_ = stageTop_ = stageRight_ = stageBottom_ = 0.0F;
}

void BlockCompositePass::ensureMaskRasteriser() noexcept {
  if (maskVao_ != 0 && maskVbo_ != 0 && maskProgram_.valid()) return;
  if (!maskProgram_.valid() && !maskProgram_.build(kMaskVertexSource, kMaskFragmentSource)) return;
  if (maskVao_ == 0) glGenVertexArrays(1, &maskVao_);
  if (maskVbo_ == 0) glGenBuffers(1, &maskVbo_);
  if (maskVao_ == 0 || maskVbo_ == 0) return;
  glBindVertexArray(maskVao_);
  glBindBuffer(GL_ARRAY_BUFFER, maskVbo_);
  const int position = maskProgram_.attribute("aPosition");
  const int color = maskProgram_.attribute("aColor");
  constexpr int kStride = 6 * static_cast<int>(sizeof(float));
  if (position >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(position));
    glVertexAttribPointer(static_cast<GLuint>(position), 2, GL_FLOAT, GL_FALSE, kStride, nullptr);
  }
  if (color >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(color));
    glVertexAttribPointer(static_cast<GLuint>(color), 4, GL_FLOAT, GL_FALSE, kStride,
        reinterpret_cast<const void*>(2 * sizeof(float)));
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void BlockCompositePass::resize(int width, int height) noexcept {
  // Allocating a target leaves it bound and the caller is usually mid-frame, so the
  // previous binding and viewport are restored before returning.
  GLint previousFbo = 0;
  GLint previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  // Every mask and the coverage target are screen/8 with point filtering: that reduced
  // resolution is what turns the zone outline into the staircase the material shows.
  const int maskWidth = reducedSize(width_, MASK_DIVISOR);
  const int maskHeight = reducedSize(height_, MASK_DIVISOR);
  for (auto& target : maskFbo_) {
    target.resize(maskWidth, maskHeight);
    target.color().setFilter(false);
  }
  compose_.resize(reducedSize(width_, COMPOSE_DIVISOR), reducedSize(height_, COMPOSE_DIVISOR));
  compose_.color().setFilter(false);
  for (auto& target : ping_) {
    target.resize(reducedSize(width_, EFFECT_DIVISOR), reducedSize(height_, EFFECT_DIVISOR));
    target.color().setFilter(false);
  }
  effect_.resize(reducedSize(width_, EFFECT_DIVISOR), reducedSize(height_, EFFECT_DIVISOR));
  effect_.color().setFilter(true);
  setStageRect(0.0F, 0.0F, static_cast<float>(width_), static_cast<float>(height_));
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
      previousViewport[3]);
}

void BlockCompositePass::setStageRect(float left, float top, float right, float bottom) noexcept {
  stageLeft_ = left;
  stageTop_ = top;
  stageRight_ = right;
  stageBottom_ = bottom;
}

void BlockCompositePass::clearMasks() noexcept {
  GLint previousFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  for (auto& target : maskFbo_) {
    if (!target.valid()) continue;
    target.bind();
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFbo));
}

void BlockCompositePass::rasteriseMasks(const std::vector<BlockMaskQuad>& maskAQuads,
    const std::vector<BlockMaskQuad>& maskBQuads) noexcept {
  if (!maskFbo_[0].valid() || !maskFbo_[1].valid()) return;
  ensureMaskRasteriser();
  if (!maskProgram_.valid() || maskVao_ == 0 || maskVbo_ == 0) return;

  // The masks are additive: overlapping writes accumulate and the shader then runs the
  // sum through the low/high gate. Blend, scissor and depth state belong to the frame
  // the composite runs in, so every one of them is put back before returning.
  GLint previousFbo = 0;
  GLint previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
  const GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
  GLint blendState[6] = {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO, GL_FUNC_ADD, GL_FUNC_ADD};
  glGetIntegerv(GL_BLEND_SRC_RGB, &blendState[0]);
  glGetIntegerv(GL_BLEND_DST_RGB, &blendState[1]);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendState[2]);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &blendState[3]);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendState[4]);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendState[5]);

  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_ONE, GL_ONE);

  auto& vertices = maskVertices_;
  vertices.clear();
  vertices.reserve(maskAQuads.size() * 6U * 6U + maskBQuads.size() * 6U * 6U);
  // These are float indices into the interleaved buffer; glDrawArrays takes vertex indices,
  // and every vertex is kFloatsPerVertex floats. Using the float index directly skipped the
  // first two thirds of the second mask's geometry, so any quad whose range started past the
  // first mask read the wrong vertices — the hover channel written into the second mask was
  // silently dropped this way.
  constexpr std::size_t kFloatsPerVertex = 6;
  const std::size_t firstAFloat = vertices.size();
  for (const auto& quad : maskAQuads) appendQuad(vertices, quad, width_, height_);
  const std::size_t firstBFloat = vertices.size();
  for (const auto& quad : maskBQuads) appendQuad(vertices, quad, width_, height_);
  const std::size_t firstA = firstAFloat / kFloatsPerVertex;
  const std::size_t firstB = firstBFloat / kFloatsPerVertex;
  const std::size_t vertexCountA = firstB - firstA;
  const std::size_t vertexCountB = vertices.size() / kFloatsPerVertex - firstB;

  // Hover quads carry a non-zero alpha in the second mask. They are emitted separately with
  // UVs so the shipped sprite supplies the coverage picture; the zone quads keep alpha zero
  // and never enter this path.
  hoverVertices_.clear();
  for (const auto& quad : maskBQuads) {
    if (quad.alpha <= 0.0F) continue;
    const float halfW = quad.width * 0.5F;
    const float halfH = quad.height * 0.5F;
    const float left = quad.centerX - halfW;
    const float right = quad.centerX + halfW;
    const float top = quad.centerY - halfH;
    const float bottom = quad.centerY + halfH;
    const auto pushVertex = [&](float x, float y, float u, float v) {
      hoverVertices_.push_back(x * 2.0F / std::max(1.0F, static_cast<float>(width_)) - 1.0F);
      hoverVertices_.push_back(1.0F - y * 2.0F / std::max(1.0F, static_cast<float>(height_)));
      hoverVertices_.push_back(0.0F);
      hoverVertices_.push_back(0.0F);
      hoverVertices_.push_back(0.0F);
      hoverVertices_.push_back(quad.alpha);
      hoverVertices_.push_back(u);
      hoverVertices_.push_back(v);
    };
    const float corners[4][2] = {{left, top}, {right, top}, {left, bottom}, {right, bottom}};
    const float uvs[4][2] = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}};
    const int order[6] = {0, 1, 2, 2, 1, 3};
    for (const int index : order) {
      pushVertex(corners[index][0], corners[index][1], uvs[index][0], uvs[index][1]);
    }
  }

  maskProgram_.use();
  glBindVertexArray(maskVao_);
  glBindBuffer(GL_ARRAY_BUFFER, maskVbo_);
  if (!vertices.empty()) {
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
        vertices.data(), GL_STREAM_DRAW);
  }
  // Every target is cleared even when it receives no geometry: the composite samples both.
  for (auto& target : maskFbo_) {
    if (!target.valid()) continue;
    target.bind();
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  const auto drawMask = [this](int index, std::size_t firstVertex, std::size_t vertexCount) {
    if (vertexCount == 0 || !maskFbo_[index].valid()) return;
    maskFbo_[index].bind();
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex),
        static_cast<GLsizei>(vertexCount));
  };
  drawMask(0, firstA, vertexCountA);
  drawMask(1, firstB, vertexCountB);

  // The hover channel is drawn into the second mask after the zone quads, using the shipped
  // sprite's alpha rather than a constant. Quads that carry a hover coverage are emitted by
  // BlockOverlay::appendHoverQuads; they are drawn with the sprite here so the composite's
  // hover term reads a picture of the touched spot instead of a flat rectangle.
  if (!hoverVertices_.empty() && ensureHoverProgram(assetRoot_)) {
    maskFbo_[1].bind();
    glViewport(0, 0, maskFbo_[1].width(), maskFbo_[1].height());
    hoverProgram_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hoverTexture_.id());
    glUniform1i(hoverProgram_.uniform("uTex"), 0);
    glBindVertexArray(hoverVao_);
    glBindBuffer(GL_ARRAY_BUFFER, maskVbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(hoverVertices_.size() * sizeof(float)),
        hoverVertices_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(hoverVertices_.size() / 8));
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  glBlendEquationSeparate(static_cast<GLenum>(blendState[4]), static_cast<GLenum>(blendState[5]));
  glBlendFuncSeparate(static_cast<GLenum>(blendState[0]), static_cast<GLenum>(blendState[1]),
      static_cast<GLenum>(blendState[2]), static_cast<GLenum>(blendState[3]));
  if (blendWasOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (depthWasOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (scissorWasOn) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

bool BlockCompositePass::ensureHoverProgram(const std::filesystem::path& assetRoot) noexcept {
  if (hoverReady_) return hoverProgram_.valid() && hoverTexture_.valid();
  hoverReady_ = true;
  if (!hoverProgram_.build(kHoverVertexSource, kHoverFragmentSource)) return false;
  if (hoverVao_ == 0) glGenVertexArrays(1, &hoverVao_);
  glBindVertexArray(hoverVao_);
  const int position = hoverProgram_.attribute("aPosition");
  const int color = hoverProgram_.attribute("aColor");
  const int uv = hoverProgram_.attribute("aUv");
  if (position >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(position));
    glVertexAttribPointer(static_cast<GLuint>(position), 2, GL_FLOAT, GL_FALSE,
        static_cast<GLsizei>(8 * sizeof(float)), nullptr);
  }
  if (color >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(color));
    glVertexAttribPointer(static_cast<GLuint>(color), 4, GL_FLOAT, GL_FALSE,
        static_cast<GLsizei>(8 * sizeof(float)),
        reinterpret_cast<const void*>(2 * sizeof(float)));
  }
  if (uv >= 0) {
    glEnableVertexAttribArray(static_cast<GLuint>(uv));
    glVertexAttribPointer(static_cast<GLuint>(uv), 2, GL_FLOAT, GL_FALSE,
        static_cast<GLsizei>(8 * sizeof(float)),
        reinterpret_cast<const void*>(6 * sizeof(float)));
  }
  glBindVertexArray(0);
  int width = 0;
  int height = 0;
  int channels = 0;
  const auto bytes = platform::AssetManager::readBinary(assetRoot,
      "official_textures/TouchHover.png");
  unsigned char* pixels = bytes.empty() ? nullptr
      : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
          &channels, 4);
  if (pixels == nullptr) return false;
  hoverTexture_.uploadRgba(width, height, pixels);
  stbi_image_free(pixels);
  hoverTexture_.setFilter(true);
  return hoverTexture_.valid();
}

void BlockCompositePass::uploadMaterials() const {
  auto& program = const_cast<gl::ShaderProgram&>(composite_.program());
  program.use();
  glUniform4fv(program.uniform("matParams[0]"), 8, &kMatParams[0][0]);
  glUniform4fv(program.uniform("matColors[0]"), 7, &kMatColors[0][0]);
  glUniform4fv(program.uniform("matExtra[0]"), 5, &kMatExtra[0][0]);
}

void BlockCompositePass::runPrepasses(float chartTimeSec) {
  if (!prepass_.valid() || !compose_.valid() || !effect_.valid()) return;
  GLint previousFbo = 0;
  GLint previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const GLboolean blendWasOn = glIsEnabled(GL_BLEND);

  auto& program = prepass_.program();
  program.use();
  program.set1f("time", chartTimeSec / TIME_DIVISOR);
  program.set2f("displaceDirection", DISPLACE_DIRECTION, DISPLACE_DIRECTION);
  program.set4f("composeParams", kMatExtra[0][0], kMatExtra[0][1], kMatExtra[0][2],
      kMatExtra[0][3]);
  program.set2f("dilateTexel", 1.0F / static_cast<float>(std::max(1, effect_.width())),
      1.0F / static_cast<float>(std::max(1, effect_.height())));

  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  // Coverage of both zone layers, displaced for the visible ones and plain for the
  // warning silhouette.
  compose_.bind();
  glViewport(0, 0, compose_.width(), compose_.height());
  program.set1i("passMode", 0);
  bindTexture(0, "displaceMap", displaceMap_, program);
  bindTexture(1, "maskA", &maskFbo_[0].color(), program);
  bindTexture(2, "maskB", &maskFbo_[1].color(), program);
  bindTexture(3, "composeRT", &compose_.color(), program);
  FullscreenQuad::instance().draw();

  // One dilation step produces the outline of the covered area.
  effect_.bind();
  glViewport(0, 0, effect_.width(), effect_.height());
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  program.set1i("passMode", 2);
  FullscreenQuad::instance().draw();

  // The glow accumulates weighted rings outside the covered area. The chain ping-pongs
  // between two targets and the last step writes only the accumulated channel into the
  // effect target, so the outline in R survives.
  const int last = GLOW_PASSES - 1;
  for (int pass = 0; pass < GLOW_PASSES; ++pass) {
    gl::Framebuffer& source = pass == 0 ? compose_ : ping_[(pass % 2) == 1 ? 0 : 1];
    gl::Framebuffer& destination = pass < last ? ping_[pass % 2 == 0 ? 0 : 1] : effect_;
    destination.bind();
    glViewport(0, 0, destination.width(), destination.height());
    program.set1i("passMode", 3);
    program.set1f("passWeight", glowRingWeight(pass));
    program.set1f("glowFirstPass", pass == 0 ? 1.0F : 0.0F);
    bindTexture(4, "pingTex", &source.color(), program);
    bindTexture(3, "composeRT", &compose_.color(), program);
    if (pass == last) glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    FullscreenQuad::instance().draw();
    if (pass == last) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  if (blendWasOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void BlockCompositePass::draw(gl::Texture& scene, float chartTimeSec) {
  draw(scene, chartTimeSec, TouchState{});
}

void BlockCompositePass::draw(gl::Texture& scene, float chartTimeSec, const TouchState& touch) {
  if (!ready_ || !composite_.valid() || !scene.valid()) return;
  if (!compose_.valid() || !effect_.valid()) return;
  if (!FullscreenQuad::instance().ensure()) return;

  // Everything the frame had bound before this call is restored at the end: the composite
  // runs in the middle of a frame, and a stale binding here is what makes the following
  // passes draw into a reduced-size target instead of the screen.
  GLint previousFbo = 0;
  GLint previousViewport[4] = {0, 0, 1, 1};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
  const GLboolean scissorWasOn = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
  GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
  // GL_COLOR_WRITEMASK is the query name for the glColorMask state on both the desktop
  // and the ES flavour; the write mask is restored because the glow chain toggles it.
  glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);

  // The pre-passes change the bound target; they restore it themselves, and the composite
  // below re-binds the caller's target explicitly.
  runPrepasses(chartTimeSec);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  auto& program = composite_.program();
  program.use();
  uploadMaterials();
  program.set1f("time", chartTimeSec / TIME_DIVISOR);
  program.set1f("timeY", chartTimeSec);
  program.set2f("screenSize", static_cast<float>(width_), static_cast<float>(height_));
  program.set4f("stageRect", stageLeft_, stageTop_, stageRight_, stageBottom_);
  program.set2f("displaceDirection", DISPLACE_DIRECTION, DISPLACE_DIRECTION);
  // Touch hover state. The reference uploads the live held points, and a shine that runs on
  // real time rather than the chart clock so a paused chart does not freeze the glow.
  const int touchCount = std::clamp(touch.count, 0, 10);
  program.set1i("touchCount", touchCount);
  if (touchCount > 0) {
    float positions[20]{};
    for (int index = 0; index < touchCount; ++index) {
      positions[index * 2] = touch.points[index].x;
      positions[index * 2 + 1] = touch.points[index].y;
    }
    program.set2fv("touchPos", touchCount, positions);
  }
  // Shine wave: (low + (1 - low) * (sin(speed * realTime) * 0.5 + 0.5)) * brightness,
  // with the reference's speed 43, low 0.63 and brightness 2.0.
  const float wave = static_cast<float>(std::sin(43.0F * touch.realTimeSec)) * 0.5F + 0.5F;
  program.set1f("touchShineValue", (0.63F + (1.0F - 0.63F) * wave) * 2.0F);

  bindTexture(0, "screenTexture", &scene, program);
  bindTexture(1, "displaceMap", displaceMap_, program);
  bindTexture(2, "sparkMap", sparkMap_, program);
  bindTexture(3, "composeRT", &compose_.color(), program);
  bindTexture(4, "effectRT", &effect_.color(), program);
  bindTexture(5, "maskA", &maskFbo_[0].color(), program);
  bindTexture(6, "maskB", &maskFbo_[1].color(), program);
  bindTexture(7, "noiseMap", noiseMap_, program);
  FullscreenQuad::instance().draw();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
  if (blendWasOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (depthWasOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (scissorWasOn) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

} // namespace rechigros::render::fx
