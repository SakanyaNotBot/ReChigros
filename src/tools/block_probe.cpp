// Isolated probe for the screen-space block composite.
//
// The composite is the last stage of the frame and touches five render targets, so the
// only reliable way to tell a broken stage from a broken frame is to run it against a
// known scene outside the application. This tool creates a GL context, renders a
// synthetic scene into a texture, runs the mask, pre-pass and composite stages, and
// writes every intermediate target to a PNG so each stage can be checked on its own.
//
// It deliberately mirrors how the application owns the pass: textures come from the
// shared texture cache, zones come from the real block model, and mask geometry comes
// from the real overlay code. Only the scene is synthetic.

#include "rechigros/game/OfficialBlock.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/render/asset/TextureCache.hpp"
#include "rechigros/render/batch/SpriteBatch.hpp"
#include "rechigros/render/fx/BlockCompositePass.hpp"
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/FullscreenPass.hpp"
#include "rechigros/render/gl/Framebuffer.hpp"
#include "rechigros/render/gl/Texture.hpp"
#include "rechigros/render/math/Mat4.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <stb_image_write.h>

namespace {

using namespace rechigros;
using namespace rechigros::game;
using namespace rechigros::platform;
using namespace rechigros::render;
using namespace rechigros::render::asset;
using namespace rechigros::render::batch;
using namespace rechigros::render::fx;
using namespace rechigros::render::gl;
using namespace rechigros::render::math;

struct Options {
  std::filesystem::path assetRoot{"assets"};
  std::filesystem::path outputDirectory{"build/block-probe"};
  int width{1920};
  int height{1080};
  float chartTimeSec{2.0F};
  // Adds a synthetic HUD band to the scene so the composite resamples something that
  // does not move with the zone, like a score line.
  bool includeHud{true};
  // Runs only the composite pre-passes and stops: the fallback when a stage is suspect.
  bool prepassOnly{false};
};

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index] != nullptr ? argv[index] : "";
    const auto next = [&]() -> std::string {
      if (index + 1 >= argc || argv[index + 1] == nullptr) return {};
      return argv[++index];
    };
    if (flag == "--assets") {
      options.assetRoot = next();
    } else if (flag == "--out-dir") {
      options.outputDirectory = next();
    } else if (flag == "--width") {
      options.width = std::max(64, std::atoi(next().c_str()));
    } else if (flag == "--height") {
      options.height = std::max(64, std::atoi(next().c_str()));
    } else if (flag == "--time") {
      options.chartTimeSec = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--no-hud") {
      options.includeHud = false;
    } else if (flag == "--prepass-only") {
      options.prepassOnly = true;
    }
  }
  return options;
}

std::uint32_t checksum(const std::vector<std::uint8_t>& pixels) {
  std::uint32_t hash = 2166136261U;
  for (const auto value : pixels) {
    hash ^= value;
    hash *= 16777619U;
  }
  return hash;
}

// The frame state a mid-frame pass has to leave exactly as it found it. Restoring only
// some of it is what makes every later pass draw into the wrong target or at the wrong
// viewport, and it is the failure mode this probe is here to catch.
struct FrameState {
  GLint fbo{};
  GLint viewport[4]{};
  GLboolean blend{};
  GLboolean scissor{};
  GLboolean depth{};
  GLint blendSrcRgb{};
  GLint blendDstRgb{};
  GLint blendSrcAlpha{};
  GLint blendDstAlpha{};
  GLint blendEquationRgb{};
  GLint blendEquationAlpha{};
};

FrameState captureState() {
  FrameState state;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.fbo);
  glGetIntegerv(GL_VIEWPORT, state.viewport);
  state.blend = glIsEnabled(GL_BLEND);
  state.scissor = glIsEnabled(GL_SCISSOR_TEST);
  state.depth = glIsEnabled(GL_DEPTH_TEST);
  glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
  return state;
}

std::string describeState(const FrameState& state) {
  std::ostringstream out;
  out << "fbo=" << state.fbo << " viewport=" << state.viewport[0] << ',' << state.viewport[1]
      << ' ' << state.viewport[2] << 'x' << state.viewport[3] << " blend="
      << (state.blend ? "on" : "off") << " scissor=" << (state.scissor ? "on" : "off")
      << " depth=" << (state.depth ? "on" : "off") << " blendFunc=(" << state.blendSrcRgb << ','
      << state.blendDstRgb << ',' << state.blendSrcAlpha << ',' << state.blendDstAlpha
      << ") equation=(" << state.blendEquationRgb << ',' << state.blendEquationAlpha << ')';
  return out.str();
}

bool statesEqual(const FrameState& first, const FrameState& second) {
  return first.fbo == second.fbo && first.viewport[0] == second.viewport[0] &&
      first.viewport[1] == second.viewport[1] && first.viewport[2] == second.viewport[2] &&
      first.viewport[3] == second.viewport[3] && first.blend == second.blend &&
      first.scissor == second.scissor && first.depth == second.depth &&
      first.blendSrcRgb == second.blendSrcRgb && first.blendDstRgb == second.blendDstRgb &&
      first.blendSrcAlpha == second.blendSrcAlpha &&
      first.blendDstAlpha == second.blendDstAlpha &&
      first.blendEquationRgb == second.blendEquationRgb &&
      first.blendEquationAlpha == second.blendEquationAlpha;
}

// Percentage of colour channels that differ between two dumps by more than a rounding
// step. -1 means the pair could not be compared.
double frameDifference(const std::filesystem::path& firstPath,
    const std::filesystem::path& secondPath) {
  const auto first = asset::decodeImage(firstPath);
  const auto second = asset::decodeImage(secondPath);
  if (!first.valid() || !second.valid() || first.width != second.width ||
      first.height != second.height) {
    return -1.0;
  }
  long long differing = 0;
  long long total = 0;
  for (std::size_t index = 0; index + 3 < first.pixels.size(); index += 4) {
    ++total;
    const int delta = std::abs(static_cast<int>(first.pixels[index]) -
        static_cast<int>(second.pixels[index]));
    if (delta > 4) ++differing;
  }
  return total == 0 ? 0.0 : 100.0 * static_cast<double>(differing) / static_cast<double>(total);
}

// Reads a render target back and writes it top-down, the way a picture is normally
// viewed. The target is bottom-up, so the rows are flipped here. A mask keeps its
// coverage in the colour channels and leaves alpha at zero, so its dump can be forced
// opaque to make the coverage visible.
bool dumpTarget(const gl::Framebuffer& target, const std::filesystem::path& path,
    bool opaque = false) {
  if (!target.valid()) {
    std::cerr << "dump: target is not allocated for " << path << '\n';
    return false;
  }
  const int width = target.width();
  const int height = target.height();
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
      static_cast<std::size_t>(height) * 4U);
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const auto rowBytes = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> flipped(pixels.size());
  for (int row = 0; row < height; ++row) {
    std::copy_n(pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes, rowBytes,
        flipped.data() + static_cast<std::size_t>(row) * rowBytes);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (opaque) {
    for (std::size_t index = 3; index < flipped.size(); index += 4) flipped[index] = 255U;
  }
  if (stbi_write_png(path.string().c_str(), width, height, 4, flipped.data(),
          static_cast<int>(rowBytes)) == 0) {
    std::cerr << "dump: could not write " << path << '\n';
    return false;
  }
  std::cout << "  wrote " << path.filename().string() << " (" << width << 'x' << height
            << ", checksum " << checksum(flipped) << ")\n";
  return true;
}

bool dumpDefaultFramebuffer(int width, int height, const std::filesystem::path& path) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
      static_cast<std::size_t>(height) * 4U);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const auto rowBytes = static_cast<std::size_t>(width) * 4U;
  std::vector<std::uint8_t> flipped(pixels.size());
  for (int row = 0; row < height; ++row) {
    std::copy_n(pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes, rowBytes,
        flipped.data() + static_cast<std::size_t>(row) * rowBytes);
  }
  if (stbi_write_png(path.string().c_str(), width, height, 4, flipped.data(),
          static_cast<int>(rowBytes)) == 0) {
    std::cerr << "dump: could not write " << path << '\n';
    return false;
  }
  std::cout << "  wrote " << path.filename().string() << " (" << width << 'x' << height
            << ", checksum " << checksum(flipped) << ")\n";
  return true;
}

// A scene with fine detail, colour and a bright band: displacement, hue shifting and
// tearing are all obvious against it, and it is identical on every run.
std::vector<std::uint8_t> buildScenePixels(int width, int height, bool includeHud) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
      static_cast<std::size_t>(height) * 4U);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x)) * 4U;
      const float u = static_cast<float>(x) / static_cast<float>(std::max(1, width - 1));
      const float v = static_cast<float>(y) / static_cast<float>(std::max(1, height - 1));
      // Gradient background.
      std::uint8_t red = static_cast<std::uint8_t>(40.0F + 180.0F * u);
      std::uint8_t green = static_cast<std::uint8_t>(30.0F + 120.0F * v);
      std::uint8_t blue = static_cast<std::uint8_t>(90.0F + 120.0F * (1.0F - u * v));
      // Diagonal stripes: a displaced sample of these is unmistakable.
      const int stripe = (x + 2 * y) / 48;
      if ((stripe & 1) == 0) {
        red = static_cast<std::uint8_t>(std::min(255, red + 60));
        green = static_cast<std::uint8_t>(std::min(255, green + 50));
      }
      // Fine checkerboard.
      if (((x / 12) + (y / 12)) % 2 == 0) {
        red = static_cast<std::uint8_t>(std::min(255, red + 30));
        blue = static_cast<std::uint8_t>(std::min(255, blue + 30));
      }
      // A bright horizontal band and a few solid blocks, so hue shifts read clearly.
      if (y > height / 2 - 40 && y < height / 2 + 40) {
        red = 235;
        green = 220;
        blue = 180;
      }
      const int blockX = (x / 160) % 3;
      const int blockY = (y / 160) % 3;
      if (blockX == 1 && blockY == 1) {
        red = 60;
        green = 210;
        blue = 240;
      }
      if (includeHud && y > height - 120 && y < height - 80) {
        // A white band near the bottom edge stands in for the score line.
        red = green = blue = 245;
      }
      pixels[offset + 0] = red;
      pixels[offset + 1] = green;
      pixels[offset + 2] = blue;
      pixels[offset + 3] = 255;
    }
  }
  return pixels;
}

// Builds the zone set the probe validates with: a tall active strip at the right edge
// with a subtract hole in it, plus a warning zone on the left.
std::vector<game::OfficialBlock> buildZones(float chartTimeSec) {
  std::vector<game::OfficialBlock> blocks;

  game::OfficialBlock strip;
  strip.bottomLeftX = 0.86F;
  strip.bottomLeftY = 0.0F;
  strip.topRightX = 1.0F;
  strip.topRightY = 1.0F;
  strip.appearTime = 0.0F;
  strip.enableTime = 0.0F;
  strip.disableTime = 100000.0F;
  strip.disappearTime = 100000.0F;
  blocks.push_back(strip);

  game::OfficialBlock hole;
  hole.bottomLeftX = 0.90F;
  hole.bottomLeftY = 0.42F;
  hole.topRightX = 0.98F;
  hole.topRightY = 0.58F;
  hole.appearTime = 0.0F;
  hole.enableTime = 0.0F;
  hole.disableTime = 100000.0F;
  hole.disappearTime = 100000.0F;
  hole.isSubtract = true;
  blocks.push_back(hole);

  game::OfficialBlock warning;
  warning.bottomLeftX = 0.06F;
  warning.bottomLeftY = 0.30F;
  warning.topRightX = 0.30F;
  warning.topRightY = 0.62F;
  warning.appearTime = 0.0F;
  warning.enableTime = 100000.0F;
  warning.disableTime = 100000.0F;
  warning.disappearTime = 100000.0F;
  blocks.push_back(warning);

  return blocks;
}

} // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  std::error_code directoryError;
  std::filesystem::create_directories(options.outputDirectory, directoryError);
  if (directoryError) {
    std::cerr << "Could not create output directory: " << options.outputDirectory.string() << '\n';
    return 2;
  }

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 3;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_Window* window = SDL_CreateWindow("block-probe", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, options.width, options.height,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
  if (window == nullptr) {
    std::cerr << "No hidden GL window available: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 4;
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (context == nullptr) {
    std::cerr << "No GL context available: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }
  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
    std::cerr << "Failed to load OpenGL entry points\n";
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }
  std::cout << "GL " << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << " / "
            << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';

  int exitCode = 0;
  {
    const int width = options.width;
    const int height = options.height;
    std::cout << "Viewport " << width << 'x' << height << ", chart time "
              << options.chartTimeSec << " s\n";

    // Textures come from the shared cache, exactly as the application loads them.
    asset::TextureCache textures;
    textures.setAssetRoot(options.assetRoot);
    gl::Texture* displaceMap = textures.loadAsset("official_textures/BlockNoise1.png");
    gl::Texture* sparkMap = textures.loadAsset("official_textures/PointNoise.png");
    gl::Texture* noiseMap = textures.loadAsset("official_textures/FD_Noise_00000.png");
    if (displaceMap == nullptr || sparkMap == nullptr || noiseMap == nullptr) {
      std::cerr << "Missing block noise textures under " << options.assetRoot.string() << '\n';
      exitCode = 5;
    } else {
      displaceMap->setFilter(false);
      displaceMap->setWrapRepeat(true, true);
      sparkMap->setFilter(false);
      sparkMap->setWrapRepeat(true, true);
      noiseMap->setFilter(false);
      noiseMap->setWrapRepeat(true, true);
      std::cout << "Noise maps: " << displaceMap->width() << 'x' << displaceMap->height() << ", "
                << sparkMap->width() << 'x' << sparkMap->height() << ", "
                << noiseMap->width() << 'x' << noiseMap->height() << '\n';
    }

    // The scene texture and the target the composite writes to are different objects:
    // sampling the target being written is exactly the kind of plumbing mistake this
    // probe exists to catch.
    gl::Framebuffer sceneFbo;
    gl::Framebuffer outputFbo;
    if (exitCode == 0) {
      if (!sceneFbo.resize(width, height) || !outputFbo.resize(width, height)) {
        std::cerr << "Could not allocate the scene targets\n";
        exitCode = 6;
      }
    }

    if (exitCode == 0) {
      // 1) The synthetic scene, blitted into the scene target.
      const auto scenePixels = buildScenePixels(width, height, options.includeHud);
      gl::Texture sceneImage;
      sceneImage.uploadRgba(width, height, scenePixels.data());
      sceneImage.setFilter(false);
      sceneFbo.bind();
      glViewport(0, 0, width, height);
      glDisable(GL_BLEND);
      glDisable(GL_SCISSOR_TEST);
      glDisable(GL_DEPTH_TEST);
      glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
      glClear(GL_COLOR_BUFFER_BIT);
      fx::BlitPass::instance().ensure();
      fx::BlitPass::instance().draw(sceneImage, 1.0F, 1.0F, 1.0F, 1.0F, false);
      std::cout << "Stage 1: synthetic scene\n";
      dumpTarget(sceneFbo, options.outputDirectory / "01_scene.png");
    }

    if (exitCode == 0) {
      // 2) Zones -> masks, through the real overlay code.
      fx::BlockCompositePass pass;
      if (!pass.prepare(options.assetRoot, displaceMap, sparkMap, noiseMap)) {
        std::cerr << "BlockCompositePass::prepare failed\n";
        exitCode = 7;
      } else {
        pass.resize(width, height);
        pass.setStageRect(0.0F, 0.0F, static_cast<float>(width),
            static_cast<float>(height));
        auto blocks = buildZones(options.chartTimeSec);
        fx::BlockOverlay overlay;
        // Two updates: the warning zone's one-shot fade advances on the real delta.
        overlay.updateAll(blocks, options.chartTimeSec, 0.25F, 0.0F, 0.0F,
            static_cast<float>(width), static_cast<float>(height));
        overlay.updateAll(blocks, options.chartTimeSec, 0.25F, 0.0F, 0.0F,
            static_cast<float>(width), static_cast<float>(height));

        std::vector<fx::BlockMaskQuad> maskA;
        std::vector<fx::BlockMaskQuad> maskB;
        overlay.collectMaskQuads(blocks, options.chartTimeSec, maskA, maskB);
        std::cout << "Stage 2: zones -> masks (" << maskA.size() << " + " << maskB.size()
                  << " quads)\n";
        pass.rasteriseMasks(maskA, maskB);
        dumpTarget(pass.mask(0), options.outputDirectory / "02_mask_active.png", true);
        dumpTarget(pass.mask(1), options.outputDirectory / "03_mask_subtract.png", true);

        // 3) Compose / edge / glow pre-passes.
        std::cout << "Stage 3: pre-passes\n";
        pass.runPrepasses(options.chartTimeSec);
        dumpTarget(pass.composeTarget(), options.outputDirectory / "04_compose.png");
        dumpTarget(pass.effectTarget(), options.outputDirectory / "05_effect_edge_glow.png");
        dumpTarget(pass.pingTarget(0), options.outputDirectory / "06_ping0.png");
        dumpTarget(pass.pingTarget(1), options.outputDirectory / "07_ping1.png");

        if (!options.prepassOnly) {
          // 4) The composite over the scene, into an off-screen target.
          std::cout << "Stage 4: composite into an off-screen target\n";
          glDisable(GL_BLEND);
          outputFbo.bind();
          glViewport(0, 0, width, height);
          glClearColor(1.0F, 0.0F, 1.0F, 1.0F);
          glClear(GL_COLOR_BUFFER_BIT);
          pass.draw(sceneFbo.color(), options.chartTimeSec);
          dumpTarget(outputFbo, options.outputDirectory / "08_composite.png");

          // 5) The same composite straight onto the presented framebuffer, with the frame
          // state the application has at that point: premultiplied blending on, the
          // default framebuffer bound and the viewport set. The pass has to give every
          // one of those back, which is asserted before the picture is dumped.
          std::cout << "Stage 5: composite onto the default framebuffer\n";
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          glViewport(0, 0, width, height);
          glEnable(GL_BLEND);
          glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
          glClearColor(0.0F, 1.0F, 0.0F, 1.0F);
          glClear(GL_COLOR_BUFFER_BIT);
          const FrameState stateBefore = captureState();
          pass.draw(sceneFbo.color(), options.chartTimeSec);
          const FrameState stateAfter = captureState();
          std::cout << "  before: " << describeState(stateBefore) << '\n'
                    << "  after:  " << describeState(stateAfter) << '\n';
          if (!statesEqual(stateBefore, stateAfter)) {
            std::cerr << "  FAIL: the composite did not restore the frame state\n";
            exitCode = 8;
          } else {
            std::cout << "  PASS: framebuffer, viewport and blending restored\n";
          }
          dumpDefaultFramebuffer(width, height, options.outputDirectory / "09_presented.png");
          const double presentedDifference = frameDifference(
              options.outputDirectory / "08_composite.png",
              options.outputDirectory / "09_presented.png");
          std::cout << "  presented vs off-screen composite: "
                    << (presentedDifference < 0.0 ? std::string("not comparable")
                                                  : std::to_string(presentedDifference) + "%")
                    << " of channels differ\n";
          if (presentedDifference > 0.5) {
            std::cerr << "  FAIL: the presented composite differs from the off-screen one\n";
            exitCode = 9;
          }

          // 6) Blending really is usable again: a half-transparent black quad has to dim
          // the presented frame instead of replacing it, which is what a stale blend
          // state would do.
          std::cout << "Stage 6: drawing after the pass\n";
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          glViewport(0, 0, width, height);
          batch::SpriteBatch batch;
          batch.begin();
          batch.addQuadTopLeft(0.0F, 0.0F, static_cast<float>(width),
              static_cast<float>(height), 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F);
          batch.flush(math::Mat4::orthoTopLeft(static_cast<float>(width),
                          static_cast<float>(height)),
              textures.white(), nullptr);
          dumpDefaultFramebuffer(width, height, options.outputDirectory / "10_after_state.png");
          const double dimDifference = frameDifference(
              options.outputDirectory / "09_presented.png",
              options.outputDirectory / "10_after_state.png");
          std::cout << "  dimmed vs presented: " << dimDifference << "% of channels differ\n";
        }
      }
    }
    textures.clear();
  }

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
