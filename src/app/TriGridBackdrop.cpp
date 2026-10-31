#include "rechigros/app/TriGridBackdrop.hpp"

#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace rechigros::app {
namespace {

// Shared vertex stage: a full-viewport triangle strip with matching texture
// coordinates (top-left origin).
constexpr const char* kVertexShaderBody = R"GLSL(
in vec2 aPosition;
in vec2 aUv;
out vec2 vUv;
void main() {
  vUv = aUv;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

// Background pass: cover-fit the blurred artwork and apply the breath, vignette
// and scanline terms.
constexpr const char* kBackgroundShaderBody = R"GLSL(
uniform sampler2D uBg;
uniform vec2 uResolution;
uniform vec2 uBgSize;
uniform float uTime;
in vec2 vUv;
out vec4 fragColor;
vec2 coverBgUv(vec2 uv, float aspect, float bgAspect) {
  vec2 p = uv - 0.5;
  if (aspect > bgAspect) p.y *= bgAspect / aspect;
  else p.x *= aspect / bgAspect;
  return clamp(p + 0.5, 0.0, 1.0);
}
void main() {
  float aspect = max(uResolution.x / max(uResolution.y, 1.0), 0.01);
  float bgAspect = uBgSize.x / max(uBgSize.y, 1.0);
  vec3 bg = texture(uBg, coverBgUv(vUv, aspect, bgAspect)).rgb;
  float breath = 0.5 + 0.5 * sin(-1.57079633 + uTime * 1.5);
  vec2 p = (vUv - 0.5) * vec2(aspect, 1.0);
  float vignette = clamp(1.08 - dot(p, p) * 0.20, 0.60, 1.0);
  float line8 = pow(0.5 + 0.5 * cos(6.2831853 * gl_FragCoord.y / 8.0), 4.5);
  bg *= (0.52 + 0.26 * breath) * vignette * (0.82 + 0.14 * line8);
  fragColor = vec4(bg, 1.0);
}
)GLSL";

// Grid pass: bend-warped sampling of the triangle mask (R = grid, G = glow,
// B = face) with a five-tap blur, then the moving band and scanline shading.
constexpr const char* kGridShaderBody = R"GLSL(
uniform sampler2D uBase;
uniform vec2 uResolution;
uniform float uTime;
in vec2 vUv;
out vec4 fragColor;
vec3 sampleTriMask(vec2 uv, vec2 px) {
  vec3 c = texture(uBase, uv).rgb * 0.40;
  c += texture(uBase, uv + vec2( px.x, 0.0)).rgb * 0.15;
  c += texture(uBase, uv + vec2(-px.x, 0.0)).rgb * 0.15;
  c += texture(uBase, uv + vec2(0.0,  px.y)).rgb * 0.15;
  c += texture(uBase, uv + vec2(0.0, -px.y)).rgb * 0.15;
  return c;
}
void main() {
  float aspect = max(uResolution.x / max(uResolution.y, 1.0), 0.01);
  vec2 center = vec2(0.5);
  vec2 view = vec2(vUv.x - 0.5, 0.5 - vUv.y);
  vec2 world = view * vec2(aspect * 10.0, 10.0);
  vec2 screen = view * vec2(aspect, 1.0) * 2.0;
  float bend = 1.0 + 0.262 * dot(screen, screen) / (2.56 * 2.56);
  vec2 warpedWorld = world / bend;
  vec2 texUv = warpedWorld * vec2(0.27, 0.44) + center;
  vec2 samplePx = vec2(aspect * 10.0 * 0.27 / max(uResolution.x, 1.0), 10.0 * 0.44 / max(uResolution.y, 1.0)) * 0.70;
  vec3 avgMask = sampleTriMask(texUv, samplePx);
  float gridMask = clamp(avgMask.r, 0.0, 1.0);
  float faceMask = clamp(avgMask.b, 0.0, 1.0);
  float glowMask = clamp(avgMask.g, 0.0, 1.0);
  float band = clamp((sin(vUv.y * 1.6 + uTime * 1.318 - 0.70) - 0.80) * 5.8, 0.0, 1.0);
  vec3 color = vec3(0.012, 0.018, 0.026);
  color += vec3(0.72, 0.98, 1.0) * gridMask * (0.34 + band * 0.72);
  color += vec3(0.52, 0.82, 1.0) * faceMask * band * 0.20;
  color += vec3(0.15, 0.72, 1.0) * glowMask * band * 3.3;
  vec2 vignettePos = vec2(screen.x * aspect, screen.y);
  float vignette = clamp(1.06 - dot(vignettePos, vignettePos) * 0.046, 0.84, 1.0);
  float line8 = pow(0.5 + 0.5 * cos(6.2831853 * gl_FragCoord.y / 8.0), 4.5);
  float scanline = 0.78 + 0.14 * line8;
  color *= vignette * scanline;
  float alpha = clamp(gridMask * 0.24 + dot(color, vec3(0.3333333)) * 0.28, 0.0, 0.78);
  fragColor = vec4(color, alpha);
}
)GLSL";

constexpr float kQuadVertices[] = {
    -1.0F, -1.0F, 0.0F, 1.0F,
    1.0F, -1.0F, 1.0F, 1.0F,
    -1.0F, 1.0F, 0.0F, 0.0F,
    1.0F, 1.0F, 1.0F, 0.0F,
};

std::string shaderHeader() {
#ifdef RECHIGROS_GLES
  return "#version 300 es\nprecision highp float;\n";
#else
  return "#version 330 core\n";
#endif
}

bool isPowerOfTwo(int value) {
  return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

TriGridBackdrop::~TriGridBackdrop() {
  release();
}

unsigned int TriGridBackdrop::buildProgram(const char* vertexSource,
    const char* fragmentSource) {
  const auto compile = [](GLenum type, const char* source) -> GLuint {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
      GLint length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
      std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
      glGetShaderInfoLog(shader, length, nullptr, log.data());
      std::cerr << "Backdrop shader compile failed: " << log << '\n';
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  };
  const GLuint vertexShader = compile(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, fragmentSource);
  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader != 0) glDeleteShader(vertexShader);
    if (fragmentShader != 0) glDeleteShader(fragmentShader);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glBindAttribLocation(program, 0, "aPosition");
  glBindAttribLocation(program, 1, "aUv");
  glLinkProgram(program);
  GLint status = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  if (status == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    std::cerr << "Backdrop program link failed: " << log << '\n';
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

TriGridBackdrop::Image TriGridBackdrop::decodeAsset(const std::filesystem::path& assetRoot,
    const char* relativePath) {
  Image image;
  const auto bytes = platform::AssetManager::readBinary(assetRoot, relativePath);
  if (bytes.empty()) return image;
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* pixels = stbi_load_from_memory(bytes.data(),
      static_cast<int>(bytes.size()), &width, &height, &channels, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    if (pixels != nullptr) stbi_image_free(pixels);
    return image;
  }
  image.width = width;
  image.height = height;
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  image.pixels.assign(pixels, pixels + count);
  stbi_image_free(pixels);
  return image;
}

bool TriGridBackdrop::uploadTexture(Image& image, Texture& texture) {
  if (!image.valid()) return false;
  GLuint id = 0;
  glGenTextures(1, &id);
  if (id == 0) return false;
  glBindTexture(GL_TEXTURE_2D, id);
  const bool wrapRepeat = isPowerOfTwo(image.width) && isPowerOfTwo(image.height);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
      wrapRepeat ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
      wrapRepeat ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, image.pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  texture.id = id;
  texture.width = image.width;
  texture.height = image.height;
  // The decoded copy has served its purpose; holding 14 MB of pixels next to the texture
  // would double the memory the backdrop needs.
  image = Image{};
  return true;
}

bool TriGridBackdrop::loadTexture(const std::filesystem::path& assetRoot,
    const char* relativePath, Texture& texture) {
  Image image = decodeAsset(assetRoot, relativePath);
  return uploadTexture(image, texture);
}

void TriGridBackdrop::beginPrepare(const std::filesystem::path& assetRoot) {
  if (decodeStarted_ || ready_) return;
  decodeStarted_ = true;
  prepareFailed_ = false;
  decodeDone_.store(false, std::memory_order_release);
  const std::filesystem::path root = assetRoot;
  // The decode is the expensive half of the preparation and it touches no OpenGL state, so
  // it runs while the window and the interface are still being set up. The upload that
  // remains on the render thread is the only cost the first frame pays.
  decodeThread_ = std::thread([this, root] {
    Image mask = decodeAsset(root, "res/TriGridBase.png");
    Image background = decodeAsset(root, "res/background_blur.png");
    {
      std::lock_guard<std::mutex> lock{decodeMutex_};
      decodedMask_ = std::move(mask);
      decodedBackground_ = std::move(background);
    }
    decodeDone_.store(true, std::memory_order_release);
  });
}

void TriGridBackdrop::destroyTexture(Texture& texture) noexcept {
  if (texture.id != 0) {
    const GLuint id = texture.id;
    glDeleteTextures(1, &id);
  }
  texture = Texture{};
}

bool TriGridBackdrop::createProgramsAndGeometry() {
  const std::string header = shaderHeader();
  const std::string vertexSource = header + kVertexShaderBody;
  const std::string gridSource = header + kGridShaderBody;
  const std::string backgroundSource = header + kBackgroundShaderBody;
  gridProgram_ = buildProgram(vertexSource.c_str(), gridSource.c_str());
  backgroundProgram_ = buildProgram(vertexSource.c_str(), backgroundSource.c_str());
  if (gridProgram_ == 0 || backgroundProgram_ == 0) {
    release();
    return false;
  }
  gridBaseLocation_ = glGetUniformLocation(gridProgram_, "uBase");
  gridResolutionLocation_ = glGetUniformLocation(gridProgram_, "uResolution");
  gridTimeLocation_ = glGetUniformLocation(gridProgram_, "uTime");
  backgroundTextureLocation_ = glGetUniformLocation(backgroundProgram_, "uBg");
  backgroundResolutionLocation_ = glGetUniformLocation(backgroundProgram_, "uResolution");
  backgroundSizeLocation_ = glGetUniformLocation(backgroundProgram_, "uBgSize");
  backgroundTimeLocation_ = glGetUniformLocation(backgroundProgram_, "uTime");

  glGenVertexArrays(1, &vertexArray_);
  glGenBuffers(1, &vertexBuffer_);
  glBindVertexArray(vertexArray_);
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(4 * sizeof(float)),
      reinterpret_cast<const void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(4 * sizeof(float)),
      reinterpret_cast<const void*>(2 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

bool TriGridBackdrop::prepare(const std::filesystem::path& assetRoot) {
  release();
  beginPrepare(assetRoot);
  if (decodeThread_.joinable()) decodeThread_.join();
  return finishPrepare();
}

bool TriGridBackdrop::finishPrepare() {
  if (ready_) return true;
  if (prepareFailed_) return false;
  if (!decodeStarted_ || !decodeDone_.load(std::memory_order_acquire)) return false;
  if (decodeThread_.joinable()) decodeThread_.join();
  if (!createProgramsAndGeometry()) {
    prepareFailed_ = true;
    return false;
  }
  bool uploaded = false;
  {
    std::lock_guard<std::mutex> lock{decodeMutex_};
    uploaded = uploadTexture(decodedMask_, triGridMask_)
        && uploadTexture(decodedBackground_, blurredBackground_);
  }
  if (!uploaded) {
    release();
    prepareFailed_ = true;
    return false;
  }
  ready_ = true;
  return true;
}

void TriGridBackdrop::release() noexcept {
  if (decodeThread_.joinable()) decodeThread_.join();
  decodeStarted_ = false;
  decodeDone_.store(false, std::memory_order_release);
  prepareFailed_ = false;
  {
    std::lock_guard<std::mutex> lock{decodeMutex_};
    decodedMask_ = Image{};
    decodedBackground_ = Image{};
  }
  if (gridProgram_ != 0) glDeleteProgram(gridProgram_);
  if (backgroundProgram_ != 0) glDeleteProgram(backgroundProgram_);
  if (vertexBuffer_ != 0) glDeleteBuffers(1, &vertexBuffer_);
  if (vertexArray_ != 0) glDeleteVertexArrays(1, &vertexArray_);
  destroyTexture(triGridMask_);
  destroyTexture(blurredBackground_);
  gridProgram_ = 0;
  backgroundProgram_ = 0;
  vertexArray_ = 0;
  vertexBuffer_ = 0;
  gridBaseLocation_ = -1;
  gridResolutionLocation_ = -1;
  gridTimeLocation_ = -1;
  backgroundTextureLocation_ = -1;
  backgroundResolutionLocation_ = -1;
  backgroundSizeLocation_ = -1;
  backgroundTimeLocation_ = -1;
  ready_ = false;
}

void TriGridBackdrop::drawQuad() const {
  glBindVertexArray(vertexArray_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

void TriGridBackdrop::render(int width, int height, double timeSec,
    unsigned int sourceTexture, bool hasSource, int sourceWidth, int sourceHeight) {
  if (!ready_ || width <= 0 || height <= 0) return;

  // Save the state the interface pass and the ImGui backend expect to inherit.
  GLint previousProgram = 0;
  GLint previousVertexArray = 0;
  GLint previousActiveTexture = GL_TEXTURE0;
  GLint previousTexture0 = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
  const GLboolean wasBlend = glIsEnabled(GL_BLEND);
  const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
  const GLboolean wasScissor = glIsEnabled(GL_SCISSOR_TEST);

  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);

  unsigned int backgroundColor = blurredBackground_.id;
  int backgroundWidth = blurredBackground_.width;
  int backgroundHeight = blurredBackground_.height;
  if (hasSource && sourceTexture != 0) {
    backgroundColor = sourceTexture;
    // Texture dimensions are supplied by the caller: OpenGL ES exposes no
    // portable way to query them from the driver.
    backgroundWidth = sourceWidth > 0 ? sourceWidth : 1;
    backgroundHeight = sourceHeight > 0 ? sourceHeight : 1;
  }

  const float time = static_cast<float>(timeSec);
  const float resolutionX = static_cast<float>(width);
  const float resolutionY = static_cast<float>(height);

  // Opaque artwork pass.
  glDisable(GL_BLEND);
  glUseProgram(backgroundProgram_);
  glUniform2f(backgroundResolutionLocation_, resolutionX, resolutionY);
  glUniform2f(backgroundSizeLocation_, static_cast<float>(backgroundWidth),
      static_cast<float>(backgroundHeight));
  glUniform1f(backgroundTimeLocation_, time);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, backgroundColor);
  glUniform1i(backgroundTextureLocation_, 0);
  drawQuad();

  // Additive grid pass over the artwork.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glUseProgram(gridProgram_);
  glUniform2f(gridResolutionLocation_, resolutionX, resolutionY);
  glUniform1f(gridTimeLocation_, time);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, triGridMask_.id);
  glUniform1i(gridBaseLocation_, 0);
  drawQuad();
  glBlendFunc(GL_ONE, GL_ZERO);
  glDisable(GL_BLEND);

  // Restore the inherited state.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
  glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  glUseProgram(static_cast<GLuint>(previousProgram));
  glBindVertexArray(static_cast<GLuint>(previousVertexArray));
  if (wasBlend != GL_FALSE) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (wasDepth != GL_FALSE) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (wasCull != GL_FALSE) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
  if (wasScissor != GL_FALSE) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

} // namespace rechigros::app
