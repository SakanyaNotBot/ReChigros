// Standalone smoke path for the renderer: loads a chart, renders a handful of
// offscreen frames and writes a PNG, so the render stack is testable without an
// interactive window.

#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/game/OfficialLevelEffects.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/render/GameRenderer.hpp"
#include "rechigros/render/asset/TextureCache.hpp"

#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <nlohmann/json.hpp>

#include <vector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

#ifdef RECHIGROS_GLES
# ifdef __APPLE__
#  include <OpenGLES/ES3/gl.h>
# else
#  include <GLES3/gl3.h>
# endif
#else
# include <glad/glad.h>
#endif

namespace {

std::string chartTitle(const std::filesystem::path& chartPath) {
  try {
    std::ifstream input(chartPath, std::ios::binary);
    const auto root = nlohmann::json::parse(input, nullptr, false);
    const auto meta = root.find("META");
    if (meta == root.end() || !meta->is_object()) return {};
    const auto name = meta->find("name");
    return name != meta->end() && name->is_string() ? name->get<std::string>() : std::string{};
  } catch (...) {
    return {};
  }
}

struct Options {
  std::filesystem::path chartPath;
  std::filesystem::path outputPath{"render-smoke.png"};
  std::filesystem::path assetRoot{"assets"};
  std::filesystem::path backgroundImage;
  int frames{3};
  int width{1280};
  int height{720};
  double chartTime{4.0};
  double duration{};
  std::string songName;
  std::string levelName;
  std::string skinDirectory;
  // When set, the rendered frame is compared against this PNG and the difference is
  // printed, so a renderer input that silently has no effect is caught.
  std::string comparePath;
  float backgroundDim{0.0F};
  float keyScale{1.0F};
  // Forces one note into the bad judgement so the bad-effect path can be exercised.
  bool forceBad{false};
  bool mirrorX{false};
  float scrollSpeed{1.0F};
  // Renderer settings mirroring the settings screen; defaults keep the reference look.
  float judgeLineWidth{1.0F};
  bool lowResolutionMode{false};
  bool antialias{false};
  bool disableChartShader{false};
  bool multiPressHighlight{true};
  bool storyMode{true};
  bool chartReveal{false};
  bool pauseMenu{false};
  float pauseElapsedSec{0.3F};
  float outroProgress{0.0F};
  double outroElapsed{std::numeric_limits<double>::quiet_NaN()};
  // Draws the frame-rate readout so the HUD number composer can be captured.
  bool showFps{false};
  bool lineState{};
  // Drops the hold-body bursts so a frame can be compared with and without them.
  bool holdBodyBursts{true};
  double holdHitTime{std::numeric_limits<double>::quiet_NaN()};
  bool capture{false};
  bool captureYuv{false};
  bool animate{false};
  bool apfc{false};
  // Suppresses the perfect/good burst so a bad copy can be compared on its own.
  bool noHitEffects{false};
};

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index] != nullptr ? argv[index] : "";
    const auto next = [&]() -> std::string {
      if (index + 1 >= argc || argv[index + 1] == nullptr) return {};
      return argv[++index];
    };
    if (flag == "--chart") {
      options.chartPath = next();
    } else if (flag == "--out") {
      options.outputPath = next();
    } else if (flag == "--assets") {
      options.assetRoot = next();
    } else if (flag == "--frames") {
      options.frames = std::max(1, std::atoi(next().c_str()));
    } else if (flag == "--width") {
      options.width = std::max(16, std::atoi(next().c_str()));
    } else if (flag == "--height") {
      options.height = std::max(16, std::atoi(next().c_str()));
    } else if (flag == "--time") {
      options.chartTime = std::atof(next().c_str());
    } else if (flag == "--duration") {
      options.duration = std::atof(next().c_str());
    } else if (flag == "--background") {
      options.backgroundImage = next();
    } else if (flag == "--skin") {
      options.skinDirectory = next();
    } else if (flag == "--song") {
      options.songName = next();
    } else if (flag == "--level") {
      options.levelName = next();
    } else if (flag == "--compare") {
      options.comparePath = next();
    } else if (flag == "--force-bad") {
      options.forceBad = true;
    } else if (flag == "--key-scale") {
      options.keyScale = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--dim") {
      options.backgroundDim = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--mirror") {
      options.mirrorX = true;
    } else if (flag == "--scroll") {
      options.scrollSpeed = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--pause") {
      options.pauseMenu = true;
    } else if (flag == "--pause-elapsed") {
      options.pauseElapsedSec = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--outro") {
      options.outroProgress = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--outro-elapsed") {
      options.outroElapsed = std::atof(next().c_str());
    } else if (flag == "--show-fps") {
      options.showFps = true;
    } else if (flag == "--line-state") {
      options.lineState = true;
    } else if (flag == "--no-hold-bursts") {
      options.holdBodyBursts = false;
    } else if (flag == "--hold-hit-time") {
      options.holdHitTime = std::atof(next().c_str());
    } else if (flag == "--judge-line-width") {
      options.judgeLineWidth = static_cast<float>(std::atof(next().c_str()));
    } else if (flag == "--low-res") {
      options.lowResolutionMode = true;
    } else if (flag == "--antialias") {
      options.antialias = true;
    } else if (flag == "--no-chart-shader") {
      options.disableChartShader = true;
    } else if (flag == "--no-multi-press") {
      options.multiPressHighlight = false;
    } else if (flag == "--no-story-mode") {
      options.storyMode = false;
    } else if (flag == "--chart-reveal") {
      options.chartReveal = true;
    } else if (flag == "--capture") {
      options.capture = true;
    } else if (flag == "--capture-yuv") {
      options.captureYuv = true;
    } else if (flag == "--animate") {
      options.animate = true;
    } else if (flag == "--apfc") {
      options.apfc = true;
    } else if (flag == "--no-hit-effects") {
      options.noHitEffects = true;
    }
  }
  return options;
}
} // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  if (options.chartPath.empty()) {
    std::cerr << "--render-smoke requires --chart <path>\n";
    return 2;
  }

  // A hidden window is enough: the renderer draws into its own framebuffer and only
  // reads the result back, so no visible UI is needed.
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 3;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_Window* window = SDL_CreateWindow("render-smoke", options.width, options.height, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
  if (window == nullptr) {
    // A dummy video driver has no GL; report it instead of pretending to have rendered.
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
#ifndef RECHIGROS_GLES
  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
    std::cerr << "Failed to load OpenGL entry points\n";
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }
#endif

  int exitCode = 0;
  try {
    auto chart = rechigros::chart::ChartLoader::loadFromFile(options.chartPath, options.assetRoot,
        options.songName);
    // A smoke run names the level effect it wants to exercise. The product selects an effect
    // from the chart's digest alone, so a fixture chart can never match a whitelisted chart;
    // the explicit name is what lets the matrix drive each effect chain on a synthetic chart.
    if (!options.songName.empty() && chart->levelEffects == nullptr) {
      if (const auto table = rechigros::game::OfficialLevelEffects::load(options.assetRoot)) {
        if (auto effect = rechigros::game::OfficialLevelEffects::forSongName(*table,
                options.songName)) {
          chart->levelEffects = effect;
          auto passes = effect->buildPrprEffects();
          chart->prprEffects.insert(chart->prprEffects.end(), passes.begin(), passes.end());
        }
      }
    }
    std::cout << "Loaded chart: " << chart->judgeLineList.size() << " lines, "
              << chart->allNotesSorted.size() << " notes, "
              << chart->prprEffects.size() << " storyboard effects, end="
              << chart->chartEndTimeSec << '\n';
    for (const auto& effect : chart->prprEffects) {
      std::cout << "  effect shader=" << effect.shader << " [" << effect.startTime << ", "
                << effect.endTime << "] vars=" << effect.variables.size() << "\n";
    }
    if (options.lineState) {
      std::cout << std::fixed << std::setprecision(6);
      for (std::size_t index = 0; index < chart->judgeLineList.size(); ++index) {
        const auto& line = chart->judgeLineList[index];
        if (line == nullptr) continue;
        const auto state = line->fillState(options.chartTime, 16.0F / 9.0F);
        std::cout << "line-state " << index << ' '
                  << (state.xNorm - 0.5F) * 1350.0F << ' ' << (0.5F - state.yNorm) * 900.0F << ' '
                  << state.rotateDeg << ' ' << state.alpha * 255.0F << ' '
                  << state.scaleX << ' ' << state.scaleY << ' '
                  << state.colorR * 255.0F << ' ' << state.colorG * 255.0F << ' '
                  << state.colorB * 255.0F << '\n';
      }
    }

    rechigros::render::GameRenderer renderer;
    if (!renderer.initialize(options.assetRoot)) {
      std::cerr << "Renderer initialization failed (asset root: " << options.assetRoot.string()
                << ")\n";
      exitCode = 5;
    } else {
      // A skin directory overrides the material settings the built-in pack provides.
      if (!options.skinDirectory.empty()) renderer.setSkinDirectory(options.skinDirectory);
      if (!options.backgroundImage.empty()) renderer.setBackgroundImage(options.backgroundImage);
      // Driving the session in autoplay first gives judged notes, so the smoke frame
      // exercises the hit-effect pass instead of an untouched chart.
      rechigros::game::GameplaySession session;
      session.setViewport({static_cast<float>(options.width), static_cast<float>(options.height),
          16.0F / 9.0F, 1.0F, 380.0F});
      session.setChart(chart.get());
      session.setAutoplay(true);
      std::vector<rechigros::game::JudgementEvent> judgementEvents;
      const double startTime = std::max(0.0, options.chartTime - 2.0);
      for (double step = startTime; step <= options.chartTime; step += 1.0 / 60.0) {
        session.update(step, 1.0F / 60.0F);
        const auto frameEvents = session.takeJudgementEvents();
        judgementEvents.insert(judgementEvents.end(), frameEvents.begin(), frameEvents.end());
      }
      std::cout << "Autoplay drove " << chart->allNotesSorted.size() << " notes, "
                << session.judgement().counts()[0] << " perfect\n";

      // Forcing a note into the bad judgement exercises the bad-effect path without a
      // simulated mistimed touch.
      if (options.forceBad && !chart->allNotesSorted.empty()) {
        chart->allNotesSorted.front()->judgeResult =
            static_cast<int>(rechigros::game::JudgementEngine::Judgement::Bad);
      }

      rechigros::render::RenderFrameInput input;
      input.judgementEvents = std::move(judgementEvents);
      input.judgementEventsAuthoritative = !options.forceBad;
      input.chartTimeSec = options.chartTime;
      input.playheadSec = options.chartTime;
      input.visualTimeSec = options.chartTime;
      input.hud.visible = true;
      input.hud.score = session.judgement().score();
      input.hud.combo = session.judgement().combo();
      input.hud.accuracy = session.judgement().realtimeAccuracy();
      input.hud.maxCombo = session.judgement().maxCombo();
      input.hud.perfect = session.judgement().counts()[0];
      input.hud.good = session.judgement().counts()[1];
      input.hud.bad = session.judgement().counts()[2];
      input.hud.miss = session.judgement().counts()[3];
      // The progress bar follows the music position, so the smoke frame reports the current
      // playhead against the chart length.
      input.hud.playheadSec = options.chartTime;
      input.hud.totalDurationSec = options.duration > 0.0
          ? options.duration : std::max(1.0, chart->chartEndTimeSec);
      input.hud.comboLabel = "COMBO";
      input.transition.inOutro = std::isfinite(options.outroElapsed);
      input.transition.outroElapsedSec = std::isfinite(options.outroElapsed)
          ? static_cast<float>(std::max(0.0, options.outroElapsed)) : 0.0F;
      input.hud.outroProgress = input.transition.inOutro
          ? static_cast<float>(std::clamp(input.transition.outroElapsedSec / (40.0F / 60.0F),
              0.0F, 1.0F))
          : options.outroProgress;
      input.hud.showFps = options.showFps;
      input.hud.fps = 144.0F;
      input.apfcIndicator = options.apfc;
      input.backgroundDim = options.backgroundDim;
      input.keyScale = options.keyScale;
      input.mirrorX = options.mirrorX;
      input.scrollSpeed = options.scrollSpeed;
      input.judgeLineWidth = options.judgeLineWidth;
      input.lowResolutionMode = options.lowResolutionMode;
      input.antialias = options.antialias;
      input.disableChartShader = options.disableChartShader;
      input.multiPressHighlight = options.multiPressHighlight;
      input.storyMode = options.storyMode;
      input.chartReveal = options.chartReveal;
      input.showHitParticles = !options.noHitEffects;
      input.songName = options.songName.empty() ? chartTitle(options.chartPath) : options.songName;
      input.levelName = options.levelName;
      // Hold-body bursts produced while the session was driven to this chart time. The
      // renderer draws one burst per entry, so a hold frame can be inspected headlessly.
      input.holdBodyBursts = session.takeHoldBodyBursts();
      input.holdHeadBursts = session.takeHoldHeadBursts();
      if (!options.holdBodyBursts) {
        input.holdBodyBursts.clear();
        input.holdHeadBursts.clear();
      }
      if (std::isfinite(options.holdHitTime)) {
        input.holdBodyBursts.clear();
        input.holdHeadBursts.clear();
        for (std::size_t index = 0; index < chart->allNotesSorted.size(); ++index) {
          if (!chart->allNotesSorted[index]->isHold) continue;
          rechigros::game::HoldHeadBurst burst;
          burst.noteIndex = static_cast<int>(index);
          burst.timeSec = options.holdHitTime;
          burst.perfect = true;
          input.holdHeadBursts.push_back(burst);
          break;
        }
      }
      if (options.pauseMenu) {
        input.pause.mode = rechigros::render::PauseMode::Menu;
        input.pause.elapsedSec = options.pauseElapsedSec;
      }

      bool written = false;
      if (options.captureYuv) {
        if (renderer.beginCapture(input, chart.get(), options.width, options.height)) {
          std::vector<std::uint8_t> planes;
          for (int frame = 0; frame < options.frames; ++frame) {
            planes.clear();
            written = renderer.captureFrameYuv(input, chart.get(), &session.judgement(), planes);
            if (!written) break;
          }
          if (written) {
            std::ofstream output(options.outputPath, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(planes.data()),
                static_cast<std::streamsize>(planes.size()));
            written = output.good();
          }
          renderer.endCapture();
        }
      } else if (options.capture) {
        if (options.animate) {
          const double first = options.chartTime - static_cast<double>(options.frames - 1) / 60.0;
          input.chartTimeSec = first;
          input.playheadSec = first;
          input.visualTimeSec = first;
        }
        if (renderer.beginCapture(input, chart.get(), options.width, options.height)) {
          std::vector<std::uint8_t> pixels;
          for (int frame = 0; frame < options.frames; ++frame) {
            if (options.animate) {
              const double time = options.chartTime - static_cast<double>(options.frames - 1 - frame) / 60.0;
              input.chartTimeSec = time;
              input.playheadSec = time;
              input.visualTimeSec = time;
            }
            pixels.clear();
            written = renderer.captureFrame(input, chart.get(), &session.judgement(), pixels);
            if (!written) break;
          }
          if (written) {
            written = stbi_write_png(options.outputPath.string().c_str(), options.width,
                options.height, 4, pixels.data(), options.width * 4) != 0;
          }
          renderer.endCapture();
        }
      } else {
        written = renderer.renderOffscreen(input, chart.get(), &session.judgement(),
            options.outputPath, options.frames, options.width, options.height);
      }
      if (written) {
        std::cout << "Wrote " << options.outputPath.string() << " (" << options.width << 'x'
                  << options.height << ", " << options.frames << " frames)\n";
        if (!options.comparePath.empty()) {
          // Comparing two rendered frames is how a renderer input that silently has no
          // effect gets caught: an honoured input must change the picture.
          const auto rendered = rechigros::render::asset::decodeImage(options.outputPath);
          const auto reference = rechigros::render::asset::decodeImage(options.comparePath);
          if (!rendered.valid() || !reference.valid() ||
              rendered.width != reference.width || rendered.height != reference.height) {
            std::cerr << "Cannot compare: images missing or of different size\n";
            exitCode = 7;
          } else {
            long long differing = 0;
            long long sampled = 0;
            const auto stride = static_cast<std::size_t>(rendered.width) * 4;
            for (int row = 0; row < rendered.height; row += 5) {
              for (std::size_t column = 0; column < stride; column += 20) {
                const auto offset = static_cast<std::size_t>(row) * stride + column;
                ++sampled;
                const int delta = std::abs(static_cast<int>(rendered.pixels[offset]) -
                    static_cast<int>(reference.pixels[offset]));
                if (delta > 4) ++differing;
              }
            }
            const double percent = sampled == 0
                ? 0.0
                : 100.0 * static_cast<double>(differing) / static_cast<double>(sampled);
            std::cout << "Compare " << options.comparePath << ": " << std::fixed
                      << std::setprecision(2) << percent << "% of sampled pixels differ\n";
          }
        }
      } else {
        std::cerr << "Failed to write " << options.outputPath.string() << '\n';
        exitCode = 6;
      }
      renderer.shutdown();
    }
  } catch (const std::exception& error) {
    std::cerr << "Chart load failed: " << error.what() << '\n';
    exitCode = 7;
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
