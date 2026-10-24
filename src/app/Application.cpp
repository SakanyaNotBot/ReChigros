#include "Application.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/CaptureHarness.hpp"
#include "ImportDialogs.hpp"
#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/TriGridBackdrop.hpp"
#include "rechigros/app/UiLayout.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/app/UiText.hpp"
#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/platform/AssetManager.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/GameRenderer.hpp"
#include "rechigros/render/gl/Gl.hpp"
#include "rechigros/render/gl/GlDebug.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

namespace rechigros::app {
namespace {

// A drop or picker answer waits for this many rendered frames before its import starts, so
// the platform window has settled and the import does not run during the launch transition.
constexpr int kDropStableFrames = 45;

std::filesystem::path pathFromUtf8(const char* value) {
  if (value == nullptr || *value == '\0') return {};
  std::u8string utf8;
  while (*value) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(*value++)));
  return std::filesystem::path(utf8);
}

#ifdef RECHIGROS_GLES
// GLES has no core timer query: the elapsed-time query is an extension whose entry points
// have to be resolved at run time. The desktop path uses the core GL 3.3 query directly.
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif
struct GpuTimerApi {
  void (*genQueries)(int, unsigned int*){};
  void (*deleteQueries)(int, const unsigned int*){};
  void (*beginQuery)(unsigned int, unsigned int){};
  void (*endQuery)(unsigned int){};
  void (*getQueryObjectuiv)(unsigned int, unsigned int, unsigned int*){};
  void (*getQueryObjectui64v)(unsigned int, unsigned int, unsigned long long*){};
  bool available{};
};

GpuTimerApi& gpuTimerApi() {
  static GpuTimerApi api;
  return api;
}
#endif

} // namespace

Application::Application(int argc, char** argv) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index] != nullptr ? argv[index] : "";
    if (argument == "--frames") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        frameLimit_ = std::max(0, std::atoi(argv[++index]));
        batchMode_ = frameLimit_ > 0;
      }
    } else if (argument == "--expect-result") {
      expectResult_ = true;
    } else if (argument == "--expect-pause") {
      expectPause_ = true;
    } else if (argument == "--practice") {
      practiceOverride_ = true;
    } else if (argument == "--challenge") {
      challengeOverride_ = true;
    } else if (argument == "--mirror") {
      mirrorOverride_ = true;
    } else if (argument == "--autoplay") {
      autoplayOverride_ = true;
    } else if (argument == "--mistime") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        batchMistimeMs_ = std::atof(argv[++index]);
      }
    } else if (argument == "--practice-perfect") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        practicePerfectMs_ = std::atof(argv[++index]);
      }
    } else if (argument == "--practice-good") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        practiceGoodMs_ = std::atof(argv[++index]);
      }
    } else if (argument == "--replay-playback") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        replayPlaybackPath_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--record-replay") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        recordReplayPath_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--capture-frame") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        capturePath_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--capture-time") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        captureTimeSec_ = std::atof(argv[++index]);
      }
    } else if (argument == "--capture-at-frame") {
      // A capture usually waits for the opening animation to finish; this pins the exact
      // frame, which is what a screen comparison of a late gameplay state needs.
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        captureAtFrame_ = std::max(0, std::atoi(argv[++index]));
      }
    } else if (argument == "--frame-stats") {
      // Records the wall interval and the pre-swap time of every frame, which separates a
      // slow renderer from a stalled presentation path.
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        frameStatsPath_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--aspect") {
      // Overrides the stage aspect ratio a capture screen shows; a narrow value reproduces
      // the letterboxed play field with its blurred side columns.
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        aspectRatioOverride_ = static_cast<float>(std::atof(argv[++index]));
      }
    } else if (argument == "--data-root") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        dataRootOverride_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--swap-interval") {
      // Present pacing experiment: 0 disables vsync, 1 keeps the normal pacing, -1 selects
      // adaptive vsync where the platform supports it.
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        swapInterval_ = std::atoi(argv[++index]);
      }
    } else if (argument == "--no-imgui-skip") {
      imguiSkipIdle_ = false;
    } else if (argument == "--imgui-skip") {
      imguiSkipIdle_ = true;
    } else if (argument == "--screen") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        screenSetup_ = argv[++index];
      }
    } else if (argument == "--record") {
      // Records the chart argument to this path and exits, so a recording can be produced without
      // driving the interface. The same code path the button uses runs; only the trigger differs.
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        recordOutputPath_ = pathFromUtf8(argv[++index]);
      }
    } else if (argument == "--fixed-delta") {
      fixedDelta_ = true;
    } else if (argument == "--gl-debug") {
      glDebug_ = true;
    } else if (argument == "--window") {
      if (index + 1 < argc && argv[index + 1] != nullptr) {
        int width = 0;
        int height = 0;
        if (std::sscanf(argv[++index], "%dx%d", &width, &height) == 2 && width >= 160
            && height >= 90) {
          windowWidthOverride_ = width;
          windowHeightOverride_ = height;
        }
      }
    } else if (!argument.empty() && argument[0] != '-' && initialPath_.empty()) {
      initialPath_ = pathFromUtf8(argument.c_str());
    }
  }
}

Application::~Application() {
  shutdown();
}

bool Application::initialize() {
  if (!replayPlaybackPath_.empty() && initialPath_.empty()) {
    std::cerr << "--replay-playback needs a chart path argument\n";
    return false;
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
    return false;
  }
  // The diagnostic switch has to be on before the first draw so the first failing pass
  // is the one reported, rather than one inherited from an earlier frame.
  render::gl::setErrorLoggingEnabled(glDebug_);
#ifdef RECHIGROS_GLES
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__ANDROID__) || defined(__IPHONEOS__)
  windowFlags |= SDL_WINDOW_FULLSCREEN;
#else
  // Automated runs exercise the real GL context and event loop without putting a
  // window on the user's desktop.
  if (batchMode_) windowFlags |= SDL_WINDOW_HIDDEN;
#endif
  window_ = SDL_CreateWindow("ReChigros", windowWidthOverride_, windowHeightOverride_, windowFlags);
  if (window_ == nullptr) {
    std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
    return false;
  }

  // Window icon, decoded straight from the application assets.
  {
    const auto iconPath = platform::assetRoot() / "app" / "icon.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    auto* pixels = stbi_load(iconPath.string().c_str(), &width, &height, &channels, 4);
    if (pixels != nullptr) {
      if (auto* icon = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels,
              width * 4)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_DestroySurface(icon);
      }
      stbi_image_free(pixels);
    }
  }

  context_ = SDL_GL_CreateContext(window_);
  if (context_ == nullptr) {
    std::cerr << "SDL_GL_CreateContext: " << SDL_GetError() << '\n';
    return false;
  }
#ifndef RECHIGROS_GLES
  if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
    std::cerr << "Failed to load OpenGL entry points\n";
    return false;
  }
#endif
  // Reported for a device bring-up run: the driver and language strings decide which
  // shading features can be relied on.
  render::gl::logContextInfo();
  SDL_GL_SetSwapInterval(swapInterval_);
  {
    int appliedInterval = swapInterval_;
    SDL_GL_GetSwapInterval(&appliedInterval);
    appliedSwapInterval_ = appliedInterval;
  }
  std::cout << "present: swap interval requested=" << swapInterval_
            << " applied=" << appliedSwapInterval_ << '\n';
  initGpuTimer();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  ui::applyTheme();

  // The display scale has to be known before the font atlas is built: a glyph atlas
  // rasterised at a single small size and then magnified by the interface scale is the main
  // source of blurry text. Rasterising at the display density keeps the glyphs crisp at
  // every scale the layout uses.
  displayDpi_ = 380.0F;
  {
    // The play field scales some geometry by the display density, and the density is only
    // meaningful when it is a positive finite number. Each source is tried in turn so a
    // platform report of zero or a not-a-number cannot reach the renderer: the window
    // content scale first, then the drawable pixel ratio, then the reference density.
    const float scale = SDL_GetWindowDisplayScale(window_);
    if (std::isfinite(scale) && scale > 0.0F) {
      displayDpi_ = 96.0F * scale;
    } else {
      int windowWidth = 0;
      int windowHeight = 0;
      int pixelWidth = 0;
      int pixelHeight = 0;
      SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
      SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight);
      if (windowWidth > 0 && windowHeight > 0 && pixelWidth > 0) {
        const float ratio = static_cast<float>(pixelWidth) / static_cast<float>(windowWidth);
        if (std::isfinite(ratio) && ratio > 0.0F) displayDpi_ = 96.0F * ratio;
      }
    }
    if (!std::isfinite(displayDpi_) || displayDpi_ <= 0.0F) displayDpi_ = 380.0F;
  }
  // Density is driven by the interface scale, not the raw display DPI: the layout grows
  // its text by the interface scale, and a glyph atlas built at the base size is then
  // magnified by that factor. Rasterising at the same factor removes the magnification.
  // Horizontal oversampling is dropped to 1 so the larger atlas keeps roughly the same
  // size as before.
  int windowWidth = 0;
  int windowHeight = 0;
  SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
  const float interfaceScale = std::clamp(
      std::min(static_cast<float>(std::max(1, windowWidth)) / 731.0F,
          static_cast<float>(std::max(1, windowHeight)) / 415.0F),
      1.0F, 2.0F);
  // Interface labels are drawn through the glyph cache below, which rasterises at the final
  // pixel size. This atlas only serves the immediate-mode widgets (text fields, tooltips), so
  // it is built at a fixed density instead of following the window scale; that keeps the
  // embedded CJK range in a 2048-square atlas at every window size.
  const float fontDensity = 1.75F;
  {
    // A packaged build keeps its assets inside the package rather than on disk, so the
    // font is materialised to a readable path first.
    const auto fontPath =
        platform::AssetManager::materialize(platform::assetRoot(), "res/font.ttf");
    ImFont* font = nullptr;
    if (!fontPath.empty() && platform::fileExists(fontPath)) {
      ImFontConfig config;
      // Rasterise denser without changing the reported metrics, so the layout keeps its
      // design sizes while the glyph texture carries more detail.
      config.RasterizerDensity = fontDensity;
      config.OversampleH = 1;
      config.OversampleV = 1;
      config.PixelSnapH = false;
      // The common range covers most of the interface but leaves a few characters out. The
      // extras below are the ones the screens actually use, so they are appended rather than
      // switching to the full range, which would multiply the atlas size. A glyph range list
      // is a run of (first, last) pairs ended by a lone zero in the first slot.
      //
      // The list must outlive the atlas build: the loader keeps the pointer rather than
      // copying the values, so it is held statically.
      static std::vector<ImWchar> ranges;
      ranges.clear();
      static const ImWchar kExtraGlyphs[] = {0x8F74, 0x8F74, 0x9891, 0x9891, 0};
      const ImWchar* common = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
      for (std::size_t index = 0; common[index] != 0; index += 2) {
        ranges.push_back(common[index]);
        ranges.push_back(common[index + 1]);
      }
      for (std::size_t index = 0; kExtraGlyphs[index] != 0; index += 2) {
        ranges.push_back(kExtraGlyphs[index]);
        ranges.push_back(kExtraGlyphs[index + 1]);
      }
      ranges.push_back(0);
      font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 20.0F, &config,
          ranges.data());
    }
    if (font == nullptr) io.Fonts->AddFontDefault();
  }
  if (batchMode_) {
    // Reported once so a blurry-text report can be traced to the atlas resolution.
    unsigned char* atlasPixels = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
    std::cout << "font: dpi=" << displayDpi_ << " density=" << fontDensity
              << " atlas=" << atlasWidth << "x" << atlasHeight << '\n';
  }
  if (!ImGui_ImplSDL3_InitForOpenGL(window_, context_)) {
    std::cerr << "ImGui SDL3 backend initialization failed\n";
    return false;
  }
#ifdef RECHIGROS_GLES
  ImGui_ImplOpenGL3_Init("#version 300 es");
#else
  ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
  imguiReady_ = true;

  audio_ = std::make_unique<audio::AudioEngine>();
  const auto assetRoot = platform::assetRoot();
  audio_->initialize();
  audio_->loadSoundEffects(assetRoot);

  renderer_ = std::make_unique<render::GameRenderer>();
  if (!renderer_->initialize(assetRoot)) {
    std::cerr << "Renderer initialization failed (asset root: " << assetRoot.string() << ")\n";
  }
  // Interface glyph cache. Every label is rasterised at the pixel size it is finally drawn
  // at, which keeps text sharp at any window size and density.
  ui::fontCache().initialize(assetRoot);

  // GPU menu backdrop. When its shaders or textures fail to load, the interface
  // keeps the simple ImGui backdrop instead. The environment switch exists so the
  // fallback path can be verified without touching the asset tree.
  const char* disableBackdrop = std::getenv("RECHIGROS_DISABLE_BACKDROP");
  if (disableBackdrop != nullptr && *disableBackdrop != '\0') {
    std::cerr << "GPU backdrop disabled by environment; using the fallback background\n";
  } else {
    backdrop_ = std::make_unique<TriGridBackdrop>();
    if (!backdrop_->prepare(assetRoot)) {
      std::cerr << "GPU backdrop unavailable; using the fallback background\n";
      backdrop_->release();
      backdrop_.reset();
    }
  }
  ui::setBackdropSuppressed(backdrop_ != nullptr && backdrop_->ready());

  app_ = std::make_unique<AppContext>(
      dataRootOverride_.empty() ? platform::userDataRoot() / "app" : dataRootOverride_);
  app_->audio = audio_.get();
  app_->renderer = renderer_.get();
  app_->displayDpi = displayDpi_;
  app_->settingsDirty = false;
  // An explicit record path asks for a replay regardless of the stored preference.
  app_->recordRequested = !recordReplayPath_.empty();
  // A verification run can pin the play mode regardless of the stored settings.
  app_->playOverrides.practice = practiceOverride_;
  app_->playOverrides.challenge = challengeOverride_;
  app_->playOverrides.mirrorX = mirrorOverride_;
  app_->playOverrides.autoplay = autoplayOverride_;
  updateAudioFromSettings();
  // Scanning the pack directory reads and parses every pack's metadata. Doing it here rather
  // than on demand keeps it off the first frame that shows a screen needing the library,
  // which is otherwise the single most expensive frame in the session.
  (void)app_->library.records();

  // A capture run can open a specific screen with representative state, so a single
  // screen can be inspected and compared without walking the interface by hand.
  if (!screenSetup_.empty()) {
    setupCaptureScreen(*app_, screenSetup_, aspectRatioOverride_);
  }

  SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
  SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, true);
  syncWindowMetrics();

  if (!initialPath_.empty()) {
    if (!replayPlaybackPath_.empty()) {
      startAdhocReplay(*app_, initialPath_, replayPlaybackPath_);
    } else {
      handleIncomingPath(*app_, initialPath_);
    }
  }
  if (!recordOutputPath_.empty()) runRecording(*app_);
  return true;
}

void Application::runRecording(AppContext& context) {
  // A command-line recording runs the whole encode before the interface exists, so it can be used
  // from a script. It uses the same session the button drives, with the defaults the page offers.
  if (!context.game.loaded || context.game.chart == nullptr || context.renderer == nullptr) {
    std::cerr << "record: no chart is loaded\n";
    return;
  }
  RecordSession::Settings settings;
  settings.outputDirectory = recordOutputPath_.parent_path();
  settings.fileName = recordOutputPath_.stem().string();
  if (settings.outputDirectory.empty()) settings.outputDirectory = std::filesystem::current_path();
  const bool h265 = recordOutputPath_.extension() == ".265";
  settings.codec = h265 ? VideoEncoder::Codec::H265 : VideoEncoder::Codec::H264;

  std::string error;
  if (!context.recording.start(context, settings, error)) {
    std::cerr << "record: " << error << '\n';
    return;
  }
  // Drive the session to completion here: there is no interface to share the frame budget with.
  while (context.recording.step(context)) {
  }
  if (context.recording.state() == RecordSession::State::Finished) {
    std::cout << "RECORD PASS path=" << context.recording.outputPath().string()
              << " frames=" << context.recording.renderedFrames() << '\n';
  } else {
    std::cout << "RECORD FAIL reason=" << context.recording.error() << '\n';
  }
}

int Application::run() {
  if (!initialize()) return 1;
  bool running = true;
  const auto frequency = SDL_GetPerformanceFrequency();
  runFrequency_ = frequency;
  auto previous = SDL_GetPerformanceCounter();
  runStartCounter_ = previous;
  double elapsed = 0.0;

  while (running) {
    const auto counter = SDL_GetPerformanceCounter();
    // The raw interval is measured before the fixed-step override, so pacing samples keep
    // their meaning in deterministic runs too.
    const double intervalMs = static_cast<double>(counter - previous) * 1000.0
        / static_cast<double>(frequency);
    float delta = static_cast<float>(static_cast<double>(counter - previous)
        / static_cast<double>(frequency));
    previous = counter;
    if (frameClockReset_) {
      delta = 0.0F;
      frameClockReset_ = false;
    }
    if (fixedDelta_) delta = 1.0F / 60.0F;
    delta = std::clamp(delta, 0.0F, 0.1F);
    elapsed += static_cast<double>(delta);
    if (delta > 0.0F) {
      smoothedFps_ = smoothedFps_ * 0.92F + (1.0F / delta) * 0.08F;
    }
    app_->deltaSec = static_cast<double>(delta);
    app_->nowSec = elapsed;
    app_->fps = smoothedFps_;

    processEvents(running);
    // Time spent in event dispatch is otherwise invisible: it sits between the frame start
    // and the swap, so a stall there is attributed to neither the scene nor the present.
    if (!frameStatsPath_.empty()) {
      const auto afterEvents = SDL_GetPerformanceCounter();
      lastEventMs_ = static_cast<double>(afterEvents - counter) * 1000.0
          / static_cast<double>(frequency);
      eventCounter_ = afterEvents;
    }
    if (!running) break;
    if (suspended_) {
      SDL_Delay(10);
      continue;
    }

    syncWindowMetrics();
    // A drop or picker answer can start an import that takes a moment. During the launch
    // transition the platform window surface is still being resized, so the import waits for
    // the first stable frames instead of extending the transition.
    if (!pendingDrops_.empty() && renderedFrames_ >= kDropStableFrames) {
      std::vector<std::string> pending;
      pending.swap(pendingDrops_);
      std::vector<std::filesystem::path> handled;
      for (const std::string& payload : pending) {
        // The platform reports a launch document twice in some configurations (once tagged by
        // the in-app picker layer, once by the base activity). Only the first answer for a
        // path starts an import.
        const auto separator = payload.find('|');
        const std::filesystem::path dropPath = separator == std::string::npos
            ? pathFromUtf8(payload.c_str())
            : pathFromUtf8(payload.substr(separator + 1).c_str());
        if (std::find(handled.begin(), handled.end(), dropPath) != handled.end()) continue;
        handled.push_back(dropPath);
        if (!handlePickerResult(*app_, payload)) {
          handleIncomingPath(*app_, dropPath);
        }
      }
    }
    pollGpuTimer();
    beginGpuTimer();

    const auto beforeUpdate = SDL_GetPerformanceCounter();
    if (app_->screen == Screen::Game) {
      gameUpdate(*app_, delta);
    }
    const auto afterUpdate = SDL_GetPerformanceCounter();
    // Split the frame so a stall can be attributed: the update phase, the interface phase and
    // the scene draw are separate costs and only their sum was measured before.
    const auto toMs = [frequency](std::uint64_t from, std::uint64_t to) {
      return static_cast<double>(to - from) * 1000.0 / static_cast<double>(frequency);
    };
    updateMs_ = toMs(beforeUpdate, afterUpdate);

    // The decision is made after the update: a gameplay frame can change screen (result
    // transition) or open a menu, and this flag must never suppress a frame that will draw
    // an interface.
    const bool imguiFrame = !(imguiSkipIdle_ && gameplayImGuiIdle());
    if (imguiFrame) {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
    }

    if (app_->screen == Screen::Game) {
      gameRender(*app_);
      if (imguiFrame) drawGameOverlay(*app_);
    } else {      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, app_->drawableWidth, app_->drawableHeight);
      if (backdrop_ != nullptr && backdrop_->ready()) {
        // The chart detail page supplies its own artwork behind the grid.
        unsigned int source = 0;
        bool hasSource = false;
        int sourceWidth = 0;
        int sourceHeight = 0;
        if (app_->screen == Screen::Detail && !app_->activeChartId.empty()) {
          if (const auto record = app_->library.find(app_->activeChartId)) {
            const std::uint64_t texture = coverTexture(*app_, *record, 1024);
            if (texture != 0) {
              source = static_cast<unsigned int>(texture);
              hasSource = true;
              sourceWidth = app_->textures.width(texture);
              sourceHeight = app_->textures.height(texture);
            }
          }
        }
        // A capture run may pin the animation clock so the moving band is visible
        // on a frame-count-bounded hidden run.
        const double backdropTime = captureTimeSec_ >= 0.0 ? captureTimeSec_ : app_->nowSec;
        backdrop_->render(app_->drawableWidth, app_->drawableHeight, backdropTime, source,
            hasSource, sourceWidth, sourceHeight);
      } else {
        glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
      }
      drawCurrentScreen();
    }
    if (imguiFrame) drawToast(*app_);
    // A recording advances outside the gameplay loop: it owns its own chart clock and renders
    // offscreen, so it must not also run through the interactive path. The overlay is drawn on top
    // of whatever screen is up, and it carries the cancel action.
    if (imguiFrame) {
      (void)updateRecording(*app_);
      drawRecordingProgress(*app_);
    }

    // The draw phase is measured on its own so a stall there is distinguishable from one in
    // the update phase.
    drawMs_ = toMs(afterUpdate, SDL_GetPerformanceCounter());

    if (app_->quitRequested) running = false;

    ++renderedFrames_;
    const int screenIndex = static_cast<int>(app_->screen);
    if (screenIndex >= 0 && screenIndex < 7) ++screenFrameCounts_[screenIndex];
    if (batchMode_) {
      updateBatchMode();
      if (renderedFrames_ >= frameLimit_) running = false;
    }

    // A focused text field needs the platform text input running, otherwise the on-screen
    // keyboard never appears and no character events are produced. Without an immediate-mode
    // frame there cannot be a focused field.
    const bool wantsText = imguiFrame && ImGui::GetIO().WantTextInput;
    if (wantsText != textInputActive_) {
      if (wantsText) {
        SDL_StartTextInput(window_);
      } else {
        SDL_StopTextInput(window_);
      }
      textInputActive_ = wantsText;
    }

    if (imguiFrame) {
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    endGpuTimer();
    // The capture happens after the interface pass, so the saved frame shows the
    // composed result (GPU backdrop or its fallback) exactly as presented. Gameplay is
    // included so a run can be inspected without depending on the presentation path.
    captureFramebufferIfRequested();
    // The frame's own work ends here; the swap waits for presentation, so the two times
    // together separate a slow frame from a stalled compositor. The swap itself is timed
    // separately: on a present-bound device the wait happens there, not in the frame.
    const auto preSwapCounter = SDL_GetPerformanceCounter();
    const double cpuMs = static_cast<double>(preSwapCounter - counter) * 1000.0
        / static_cast<double>(frequency);
    SDL_GL_SwapWindow(window_);
    const auto postSwapCounter = SDL_GetPerformanceCounter();
    const double swapMs = static_cast<double>(postSwapCounter - preSwapCounter) * 1000.0
        / static_cast<double>(frequency);
    if (!frameStatsPath_.empty() && frameSamples_.size() < 2000000) {
      FrameSample sample;
      sample.intervalMs = intervalMs;
      sample.cpuMs = cpuMs;
      sample.screen = static_cast<int>(app_->screen);
      sample.nowSec = app_->nowSec;
      sample.swapMs = swapMs;
      sample.eventMs = lastEventMs_;
      sample.updateMs = updateMs_;
      sample.drawMs = drawMs_;
      sample.wallSec = static_cast<double>(postSwapCounter - runStartCounter_)
          / static_cast<double>(runFrequency_);
      frameSamples_.push_back(sample);
    }
  }

  writeFrameStats();
  commitSettings(*app_);
  if (!recordReplayPath_.empty() && app_ != nullptr) {
    // A saved replay records what the run scored, not the values it started with.
    ReplayData recorded = app_->game.recorded;
    applyResultToReplayMeta(recorded, app_->lastResult);
    const bool saved = writeReplayFile(recordReplayPath_, recorded);
    std::cout << "REPLAY-RECORDED " << (saved ? "PASS" : "FAIL")
              << " path=" << recordReplayPath_.string()
              << " entries=" << (recorded.judgements.size() + recorded.entries.size())
              << " judgements=" << recorded.judgements.size()
              << " notes=" << recorded.meta.totalNotes
              << " score=" << recorded.meta.score
              << " accuracy=" << recorded.meta.accuracy << '\n';
    if (!saved) batchOk_ = false;
  }
  if (batchMode_) {
    printBatchSummary();
    return batchOk_ ? 0 : 1;
  }
  return 0;
}

void Application::updateBatchMode() {
  forwardedPointerEvents_ = app_->forwardedPointerEvents;
  auto& game = app_->game;
  if (app_->screen != Screen::Game) return;
  const int gameFrames = screenFrameCounts_[static_cast<int>(Screen::Game)];
  const auto& judgement = game.session.judgement();
  app_->totalNotesJudgedForBatch = judgement.counts()[0] + judgement.counts()[1]
      + judgement.counts()[2] + judgement.counts()[3];

  // Pause state machine verification: escape opens the menu, escape again starts
  // the resume countdown, and the countdown releases the game automatically.
  if (batchMode_ && expectPause_) {
    const auto pushEscape = [&]() {
      SDL_Event down{};
      down.type = SDL_EVENT_KEY_DOWN;
      down.key.windowID = SDL_GetWindowID(window_);
      down.key.down = true;
      down.key.repeat = false;
      down.key.key = SDLK_ESCAPE;
      down.key.scancode = SDL_SCANCODE_ESCAPE;
      SDL_PushEvent(&down);
      SDL_Event up = down;
      up.type = SDL_EVENT_KEY_UP;
      up.key.down = false;
      SDL_PushEvent(&up);
    };
    // The pause cycle runs as soon as the opening transition has finished rather than at a
    // fixed frame number. Charts differ in length, and a short fixture reaches its exit
    // animation before a late frame, which would leave the pause untested.
    if (pauseCycleStartFrame_ < 0 && game.loaded && !game.inIntro && !game.inOutro
        && !game.finished) {
      pauseCycleStartFrame_ = gameFrames;
    }
    if (pauseCycleStartFrame_ >= 0) {
      const int step = gameFrames - pauseCycleStartFrame_;
      if (step == 2) pushEscape();
      if (step == 12 && game.pause == GameRuntime::Pause::Menu) pauseMenuSeen_ = true;
      if (step == 20) pushEscape();
      if (step == 30 && (game.pause == GameRuntime::Pause::Countdown
                            || game.pause == GameRuntime::Pause::Fadeout)) {
        pauseCountdownSeen_ = true;
      }
      if (step > 60 && game.pause == GameRuntime::Pause::None) pauseResumedSeen_ = true;
    }
  }

  // Replay playback drives the touch pipeline from the recorded events, so the
  // harness must not inject synthetic input on top of it.
  if (game.replayPlayback) return;

  if (!game.loaded || game.chart == nullptr || game.inIntro) return;
  if (game.pause != GameRuntime::Pause::None) return;
  if (game.chart->allNotesSorted.empty()) return;
  if (batchNoteIndex_ >= game.chart->allNotesSorted.size() || batchNoteIndex_ >= 4) return;

  const auto pushButton = [&](Uint32 type, float x, float y, bool pressed) {
    SDL_Event event{};
    event.type = type;
    event.button.windowID = SDL_GetWindowID(window_);
    event.button.which = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = pressed;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
  };
  const auto pushMotion = [&](float x, float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_GetWindowID(window_);
    event.motion.which = 0;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = x;
    event.motion.y = y;
    SDL_PushEvent(&event);
  };

  // The play field centre is the judge line centre for the synthetic test chart.
  const float centerX = static_cast<float>(app_->windowWidth) * 0.5F;
  const float centerY = static_cast<float>(app_->windowHeight) * 0.5F + 6.0F;
  // A practice run may change the window before the notes are pressed. The value is
  // clamped by the window rules, so it can only narrow the perfect window.
  if (game.practice) {
    // The perfect window is narrowed first: the good window is clamped to stay above it.
    if (practicePerfectMs_ > 0.0) {
      game.practiceWindows.setPerfectMs(static_cast<float>(practicePerfectMs_));
    }
    if (practiceGoodMs_ > 0.0) game.practiceWindows.setGoodMs(static_cast<float>(practiceGoodMs_));
  }
  while (batchNoteIndex_ < game.chart->allNotesSorted.size()
      && game.chart->allNotesSorted[batchNoteIndex_]->judgeResult >= 0) {
    ++batchNoteIndex_;
  }
  if (batchNoteIndex_ >= game.chart->allNotesSorted.size() || batchNoteIndex_ >= 4) return;
  // A positive mistime presses late by that many milliseconds.
  const double hitTime = game.chart->allNotesSorted[batchNoteIndex_]->sect
      + batchMistimeMs_ / 1000.0;
  if (batchInputStage_ == 0 && game.chartTimeSec >= hitTime - 0.05) {
    pushButton(SDL_EVENT_MOUSE_BUTTON_DOWN, centerX, centerY, true);
    batchInputStage_ = 1;
  } else if (batchInputStage_ == 1) {
    pushMotion(centerX + 4.0F, centerY);
    batchInputStage_ = 2;
  } else if (batchInputStage_ == 2) {
    pushButton(SDL_EVENT_MOUSE_BUTTON_UP, centerX, centerY, false);
    batchInputStage_ = 0;
    ++batchNoteIndex_;
  }
}

void Application::printBatchSummary() {
  static const char* kScreenNames[7] = {"Home", "Library", "Detail", "Game", "Result",
      "Calibration", "Settings"};
  const int gameFrames = screenFrameCounts_[static_cast<int>(Screen::Game)];
  const int resultFrames = screenFrameCounts_[static_cast<int>(Screen::Result)];
  const bool homeOnly = initialPath_.empty();
  const bool replayRun = !replayPlaybackPath_.empty();
  bool chartRunPass = gameFrames >= 30 && app_->forwardedPointerEvents >= 3
      && app_->totalNotesJudgedForBatch >= 1;
  if (expectPause_) {
    chartRunPass = chartRunPass && pauseMenuSeen_ && pauseCountdownSeen_ && pauseResumedSeen_;
  }
  if (expectResult_) {
    chartRunPass = chartRunPass && resultFrames > 0 && app_->lastResult.totalNotes > 0;
  }
  if (replayRun) {
    // The recorded metadata is the reference. A judgement-entry replay takes its settlement
    // result FROM that metadata, so the comparison has to read the live judgement result:
    // re-applying the entries must reproduce every recorded judgement.
    const auto& recorded = app_->game.playback.meta;
    const auto played = app_->game.session.judgement().result();
    const bool scoreMatch = played.score == recorded.score;
    const bool accuracyMatch = std::abs(played.accuracy - recorded.accuracy) <= 1.0e-6;
    const bool countsMatch = played.perfect == recorded.perfect && played.good == recorded.good
        && played.bad == recorded.bad && played.miss == recorded.miss
        && played.totalNotes == recorded.totalNotes;
    replayMatch_ = resultFrames > 0 && scoreMatch && accuracyMatch && countsMatch;
    chartRunPass = replayMatch_;
    std::cout << "replay: recorded score=" << recorded.score
              << " accuracy=" << recorded.accuracy << " P/G/B/M=" << recorded.perfect << '/'
              << recorded.good << '/' << recorded.bad << '/' << recorded.miss
              << " | playback score=" << played.score << " accuracy=" << played.accuracy
              << " P/G/B/M=" << played.perfect << '/' << played.good << '/' << played.bad << '/'
              << played.miss << " | match=" << (replayMatch_ ? 1 : 0) << '\n';
  }
  batchOk_ = batchMode_ && renderedFrames_ >= frameLimit_ && (homeOnly ? true : chartRunPass);
  if (!capturePath_.empty() && (!captureDone_ || captureFailed_)) batchOk_ = false;
  std::cout << "BATCH-RUN " << (batchOk_ ? "PASS" : "FAIL") << " frames=" << renderedFrames_
            << " requested=" << frameLimit_ << " fps=" << smoothedFps_;
  if (audio_ != nullptr) std::cout << " audioHz=" << audio_->outputSampleRate();
  std::cout << " pointerEvents=" << app_->forwardedPointerEvents << '\n';
  std::cout << "backdrop: module=" << (backdrop_ != nullptr && backdrop_->ready() ? 1 : 0)
            << " captured=" << (captureDone_ && !captureFailed_ ? 1 : 0) << '\n';
  std::cout << "screens:";
  for (int index = 0; index < 7; ++index) {
    if (screenFrameCounts_[index] > 0) {
      std::cout << ' ' << kScreenNames[index] << '=' << screenFrameCounts_[index];
    }
  }
  std::cout << '\n';
  if (expectPause_) {
    std::cout << "pause: menu=" << (pauseMenuSeen_ ? 1 : 0)
              << " countdown=" << (pauseCountdownSeen_ ? 1 : 0)
              << " resumed=" << (pauseResumedSeen_ ? 1 : 0) << '\n';
  }
  if (!initialPath_.empty()) {
    std::cout << "chart: " << app_->play.songName << " / " << app_->play.difficulty << '\n';
    // The level effect a chart resolves to decides the HUD suppression and the scene extras,
    // so it is reported for verification runs: a chart whose effect fails to select looks
    // ordinary in a screenshot apart from the missing effect.
    if (app_->game.chart != nullptr) {
      const auto& effects = app_->game.chart->levelEffects;
      std::cout << "level-effect: "
                << (effects != nullptr ? effects->effectName() : std::string("none"))
                << " hideHud=" << (effects != nullptr && effects->hideHud() ? 1 : 0)
                << " lowPassOnBlock=" << (effects != nullptr && effects->lowPassOnBlock() ? 1 : 0)
                << " passes=" << app_->game.chart->prprEffects.size() << '\n';
    }
    std::cout << "practice: enabled=" << (app_->game.practice ? 1 : 0)
              << " perfectMs=" << app_->game.practiceWindows.perfectMs()
              << " active=" << (app_->game.practiceWindows.active(app_->game.practice, app_->game.challenge) ? 1 : 0) << '\n';
    // The mode a run actually used, so a verification run can assert it was applied.
    std::cout << "mode: practice=" << (app_->play.practice ? 1 : 0)
              << " challenge=" << (app_->play.challenge ? 1 : 0)
              << " mirror=" << (app_->play.mirrorX ? 1 : 0)
              << " autoplay=" << (app_->play.autoplay ? 1 : 0) << '\n';
    if (app_->game.loaded) {
      const auto& judgement = app_->game.session.judgement();
      std::cout << "game: chartTime=" << app_->game.chartTimeSec
                << " combo=" << judgement.combo() << " score=" << judgement.score()
                << " judged=" << app_->totalNotesJudgedForBatch << '\n';
    }
    if (resultFrames > 0) {
      const auto& result = app_->lastResult;
      std::cout << "result: score=" << result.score << " accuracy=" << result.accuracy
                << " maxCombo=" << result.maxCombo << " perfect=" << result.perfect
                << " good=" << result.good << " bad=" << result.bad
                << " miss=" << result.miss << " total=" << result.totalNotes << '\n';
    }
  }
}

void Application::writeFrameStats() const {
  if (frameStatsPath_.empty()) return;
  if (frameSamples_.empty()) {
    std::cout << "FRAME-STATS empty\n";
    return;
  }
  std::vector<double> intervals;
  std::vector<double> cpu;
  std::vector<double> swap;
  std::vector<double> gpu;
  intervals.reserve(frameSamples_.size());
  cpu.reserve(frameSamples_.size());
  swap.reserve(frameSamples_.size());
  gpu.reserve(frameSamples_.size());
  for (const FrameSample& sample : frameSamples_) {
    intervals.push_back(sample.intervalMs);
    cpu.push_back(sample.cpuMs);
    swap.push_back(sample.swapMs);
    gpu.push_back(sample.gpuMs);
  }
  const auto percentile = [](std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    if (values.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    return values[index];
  };
  const auto mean = [](const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (const double value : values) sum += value;
    return sum / static_cast<double>(values.size());
  };
  std::size_t over16 = 0;
  std::size_t over33 = 0;
  std::size_t over100 = 0;
  double worst = 0.0;
  for (const double value : intervals) {
    if (value > 16.7) ++over16;
    if (value > 33.3) ++over33;
    if (value > 100.0) ++over100;
    worst = std::max(worst, value);
  }
  if (!frameStatsPath_.parent_path().empty()) {
    std::error_code error;
    std::filesystem::create_directories(frameStatsPath_.parent_path(), error);
  }
  std::ofstream csv(frameStatsPath_);
  if (csv) {
    csv << "index,interval_ms,cpu_ms,screen,now_sec,gpu_ms,swap_ms,wall_sec,event_ms,update_ms,draw_ms\n";
    for (std::size_t index = 0; index < frameSamples_.size(); ++index) {
      const FrameSample& sample = frameSamples_[index];
      csv << index << ',' << sample.intervalMs << ',' << sample.cpuMs << ','
          << sample.screen << ',' << sample.nowSec << ',' << sample.gpuMs << ','
          << sample.swapMs << ',' << sample.wallSec << ',' << sample.eventMs << ','
          << sample.updateMs << ',' << sample.drawMs << '\n';
    }
  }
  std::cout << "FRAME-STATS frames=" << frameSamples_.size()
            << " interval mean=" << mean(intervals)
            << " p50=" << percentile(intervals, 0.50)
            << " p95=" << percentile(intervals, 0.95)
            << " p99=" << percentile(intervals, 0.99)
            << " max=" << worst
            << " over16=" << over16 << " over33=" << over33 << " over100=" << over100
            << " cpu mean=" << mean(cpu)
            << " cpu p99=" << percentile(cpu, 0.99)
            << " path=" << frameStatsPath_.string() << '\n';
  double worstSwap = 0.0;
  for (const double value : swap) worstSwap = std::max(worstSwap, value);
  double worstGpu = 0.0;
  bool anyGpu = false;
  for (const double value : gpu) {
    worstGpu = std::max(worstGpu, value);
    if (value > 0.0) anyGpu = true;
  }
  std::cout << "FRAME-STATS-PRESENT frames=" << frameSamples_.size()
            << " swap mean=" << mean(swap) << " p95=" << percentile(swap, 0.95)
            << " p99=" << percentile(swap, 0.99) << " max=" << worstSwap
            << " gpu " << (anyGpu ? "" : "unavailable ") << "mean=" << mean(gpu)
            << " p95=" << percentile(gpu, 0.95) << " p99=" << percentile(gpu, 0.99)
            << " max=" << worstGpu
            << " swap_interval=" << appliedSwapInterval_ << '\n';
}

bool Application::gameplayImGuiIdle() const {
  if (!app_ || app_->screen != Screen::Game) return false;
  const GameRuntime& game = app_->game;
  if (!game.loaded || game.finished) return false;
  if (game.pause != GameRuntime::Pause::None) return false;
  if (game.debugOverlay || app_->settings.showDebugInfo) return false;
  if (!app_->toastText.empty() && app_->nowSec <= app_->toastDeadlineSec) return false;
  return true;
}

void Application::initGpuTimer() {
  gpuTimerSupported_ = false;
  gpuQueries_[0] = 0;
  gpuQueries_[1] = 0;
  gpuActiveSlot_ = -1;
  gpuSlotFrame_[0] = -1;
  gpuSlotFrame_[1] = -1;
  if (frameStatsPath_.empty()) return;
#ifdef RECHIGROS_GLES
  auto& api = gpuTimerApi();
  api.genQueries = reinterpret_cast<decltype(api.genQueries)>(
      SDL_GL_GetProcAddress("glGenQueriesEXT"));
  api.deleteQueries = reinterpret_cast<decltype(api.deleteQueries)>(
      SDL_GL_GetProcAddress("glDeleteQueriesEXT"));
  api.beginQuery = reinterpret_cast<decltype(api.beginQuery)>(
      SDL_GL_GetProcAddress("glBeginQueryEXT"));
  api.endQuery = reinterpret_cast<decltype(api.endQuery)>(
      SDL_GL_GetProcAddress("glEndQueryEXT"));
  api.getQueryObjectuiv = reinterpret_cast<decltype(api.getQueryObjectuiv)>(
      SDL_GL_GetProcAddress("glGetQueryObjectuivEXT"));
  api.getQueryObjectui64v = reinterpret_cast<decltype(api.getQueryObjectui64v)>(
      SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT"));
  api.available = api.genQueries != nullptr && api.deleteQueries != nullptr
      && api.beginQuery != nullptr && api.endQuery != nullptr
      && api.getQueryObjectuiv != nullptr;
  if (!api.available) return;
  const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  if (extensions == nullptr || std::strstr(extensions, "GL_EXT_disjoint_timer_query") == nullptr) {
    api.available = false;
    return;
  }
  api.genQueries(2, gpuQueries_);
#else
  if (!render::gl::contextAvailable()) return;
  glGenQueries(2, gpuQueries_);
#endif
  gpuTimerSupported_ = gpuQueries_[0] != 0 && gpuQueries_[1] != 0;
  if (gpuTimerSupported_) std::cout << "present: gpu timer queries enabled\n";
}

void Application::shutdownGpuTimer() noexcept {
  if (!gpuTimerSupported_) return;
#ifdef RECHIGROS_GLES
  auto& api = gpuTimerApi();
  if (api.deleteQueries != nullptr && (gpuQueries_[0] != 0 || gpuQueries_[1] != 0)) {
    api.deleteQueries(2, gpuQueries_);
  }
#else
  if (gpuQueries_[0] != 0 || gpuQueries_[1] != 0) glDeleteQueries(2, gpuQueries_);
#endif
  gpuQueries_[0] = 0;
  gpuQueries_[1] = 0;
  gpuTimerSupported_ = false;
  gpuActiveSlot_ = -1;
  gpuSlotFrame_[0] = -1;
  gpuSlotFrame_[1] = -1;
}

void Application::beginGpuTimer() {
  if (!gpuTimerSupported_ || gpuQueries_[0] == 0) return;
  // Alternate the two queries. If the chosen slot still holds a pending result the GPU is
  // more than one frame behind, so this frame is left untimed instead of stalling or
  // reusing an active query.
  const int slot = gpuNextSlot_;
  gpuNextSlot_ = (gpuNextSlot_ + 1) % 2;
  if (gpuSlotFrame_[slot] >= 0) {
    gpuActiveSlot_ = -1;
    return;
  }
  gpuActiveSlot_ = slot;
  gpuSlotFrame_[slot] = static_cast<int>(frameSamples_.size());
#ifdef RECHIGROS_GLES
  gpuTimerApi().beginQuery(GL_TIME_ELAPSED_EXT, gpuQueries_[slot]);
#else
  glBeginQuery(GL_TIME_ELAPSED, gpuQueries_[slot]);
#endif
}

void Application::endGpuTimer() {
  if (!gpuTimerSupported_ || gpuActiveSlot_ < 0 || gpuQueries_[gpuActiveSlot_] == 0) return;
#ifdef RECHIGROS_GLES
  gpuTimerApi().endQuery(GL_TIME_ELAPSED_EXT);
#else
  glEndQuery(GL_TIME_ELAPSED);
#endif
  gpuActiveSlot_ = -1;
}

void Application::pollGpuTimer() {
  if (!gpuTimerSupported_) return;
  for (int slot = 0; slot < 2; ++slot) {
    const int sample = gpuSlotFrame_[slot];
    if (sample < 0 || gpuQueries_[slot] == 0) continue;
    unsigned int available = 0;
#ifdef RECHIGROS_GLES
    gpuTimerApi().getQueryObjectuiv(gpuQueries_[slot], GL_QUERY_RESULT_AVAILABLE, &available);
#else
    glGetQueryObjectuiv(gpuQueries_[slot], GL_QUERY_RESULT_AVAILABLE, &available);
#endif
    if (available == 0) continue;
    unsigned long long nanoseconds = 0;
#ifdef RECHIGROS_GLES
    auto& api = gpuTimerApi();
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint != 0) {
      gpuSlotFrame_[slot] = -1;
      continue;
    }
    if (api.getQueryObjectui64v != nullptr) {
      unsigned long long value = 0;
      api.getQueryObjectui64v(gpuQueries_[slot], GL_QUERY_RESULT, &value);
      nanoseconds = value;
    } else {
      unsigned int value = 0;
      api.getQueryObjectuiv(gpuQueries_[slot], GL_QUERY_RESULT, &value);
      nanoseconds = value;
    }
#else
    unsigned long long value = 0;
    glGetQueryObjectui64v(gpuQueries_[slot], GL_QUERY_RESULT, &value);
    nanoseconds = value;
#endif
    if (sample < static_cast<int>(frameSamples_.size())) {
      frameSamples_[static_cast<std::size_t>(sample)].gpuMs =
          static_cast<double>(nanoseconds) / 1.0e6;
    }
    gpuSlotFrame_[slot] = -1;
  }
}

void Application::captureFramebufferIfRequested() {
  // A capture of gameplay waits for the opening to finish, so the frame shows the play
  // field rather than the transition into it. An explicit frame can be requested instead.
  const int minimumFrames = captureAtFrame_ > 0
      ? captureAtFrame_
      : (app_ != nullptr && app_->screen == Screen::Game ? 140 : 20);
  if (capturePath_.empty() || captureDone_ || app_ == nullptr || renderedFrames_ < minimumFrames) {
    return;
  }
  const int width = app_->drawableWidth;
  const int height = app_->drawableHeight;
  if (width <= 0 || height <= 0) return;
  captureDone_ = true;

  const auto rowBytes = static_cast<std::size_t>(width) * 4;
  std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height));
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // Colour statistics prove the shaders produced content, not a flat clear.
  std::unordered_set<std::uint32_t> colours;
  colours.reserve(4096);
  std::uint64_t sumRed = 0;
  std::uint64_t sumGreen = 0;
  std::uint64_t sumBlue = 0;
  std::uint64_t samples = 0;
  const auto stride = static_cast<std::size_t>(std::max(1, (width * height) / 4096));
  const auto pixelCount = pixels.size() / 4;
  for (std::size_t index = 0; index < pixelCount; index += stride) {
    const std::uint8_t* pixel = pixels.data() + index * 4;
    const std::uint32_t quantised = (static_cast<std::uint32_t>(pixel[0]) >> 2) << 12
        | (static_cast<std::uint32_t>(pixel[1]) >> 2) << 6
        | (static_cast<std::uint32_t>(pixel[2]) >> 2);
    colours.insert(quantised);
    sumRed += pixel[0];
    sumGreen += pixel[1];
    sumBlue += pixel[2];
    ++samples;
  }

  // Readback rows are bottom-up; flip them for the PNG writer.
  std::vector<std::uint8_t> flipped(pixels.size());
  for (int row = 0; row < height; ++row) {
    std::copy_n(pixels.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes, rowBytes,
        flipped.data() + static_cast<std::size_t>(row) * rowBytes);
  }
  // A device capture path points into storage the caller owns; the directory is created
  // here because the package build has no shell to prepare it.
  if (!capturePath_.parent_path().empty()) {
    std::error_code error;
    std::filesystem::create_directories(capturePath_.parent_path(), error);
  }
  const bool written = stbi_write_png(capturePath_.string().c_str(), width, height, 4,
      flipped.data(), static_cast<int>(rowBytes)) != 0;
  captureFailed_ = !written;
  const unsigned meanRed = samples > 0 ? static_cast<unsigned>(sumRed / samples) : 0;
  const unsigned meanGreen = samples > 0 ? static_cast<unsigned>(sumGreen / samples) : 0;
  const unsigned meanBlue = samples > 0 ? static_cast<unsigned>(sumBlue / samples) : 0;
  std::cout << "CAPTURE " << (written ? "PASS" : "FAIL") << " path=" << capturePath_.string()
            << " size=" << width << 'x' << height << " uniqueColors=" << colours.size()
            << " meanRGB=" << meanRed << ',' << meanGreen << ',' << meanBlue << '\n';
}

void Application::drawCurrentScreen() {
  switch (app_->screen) {
    case Screen::Home: drawHomeScreen(*app_); break;
    case Screen::Library: drawLibraryScreen(*app_); break;
    case Screen::Detail: drawDetailScreen(*app_); break;
    case Screen::Settings: drawSettingsScreen(*app_); break;
    case Screen::Render: drawRenderScreen(*app_); break;
    case Screen::Calibration: drawCalibrationScreen(*app_); break;
    case Screen::Result: drawResultScreen(*app_); break;
    case Screen::Game: break;
  }
}

void Application::processEvents(bool& running) {
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    const bool imguiConsumed = ImGui_ImplSDL3_ProcessEvent(&event);
    (void)imguiConsumed;
    handleEvent(event, running);
  }
}

void Application::handleEvent(const SDL_Event& event, bool& running) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      running = false;
      app_->quitRequested = true;
      return;
    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr) {
        // The drop is handled after the window has settled: an import can run for a moment
        // and must not compete with the launch/resize transition on the platform thread.
        pendingDrops_.emplace_back(event.drop.data);
        SDL_free(const_cast<char*>(event.drop.data));
      }
      return;
    case SDL_EVENT_KEY_DOWN:
      if (!event.key.repeat) handleKeyDown(event);
      return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID
          && app_->screen == Screen::Game && !ImGui::GetIO().WantCaptureMouse) {
        gamePointerDown(*app_, -1, static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
      }
      return;
    case SDL_EVENT_MOUSE_MOTION:
      if (app_->screen == Screen::Game && event.motion.which != SDL_TOUCH_MOUSEID
          && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
        gamePointerMove(*app_, -1, static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y));
      }
      return;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID
          && app_->screen == Screen::Game) {
        gamePointerUp(*app_, -1);
      }
      return;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(window_, &width, &height);
      const float x = event.tfinger.x * static_cast<float>(width);
      const float y = event.tfinger.y * static_cast<float>(height);
      if (app_->screen == Screen::Game) {
        if (event.type == SDL_EVENT_FINGER_DOWN) {
          gamePointerDown(*app_, event.tfinger.fingerID, x, y);
        } else if (event.type == SDL_EVENT_FINGER_MOTION) {
          gamePointerMove(*app_, event.tfinger.fingerID, x, y);
        } else {
          gamePointerUp(*app_, event.tfinger.fingerID);
        }
      }
      return;
    }
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      syncWindowMetrics();
      return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      // Automated runs keep the loop running even though the hidden window
      // never holds keyboard focus.
      if (!batchMode_ && app_->screen == Screen::Game) gameFocusLost(*app_);
      return;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      if (!suspended_) {
        suspended_ = true;
        if (app_->screen == Screen::Game) gameFocusLost(*app_);
        audio_->suspend();
      }
      return;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      if (suspended_) {
        audio_->resume();
        suspended_ = false;
        frameClockReset_ = true;
      }
      return;
    case SDL_EVENT_TERMINATING:
      if (app_->screen == Screen::Game) gameFocusLost(*app_);
      audio_->suspend();
      return;
    default: break;
  }
}

void Application::handleKeyDown(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;
  if (key == SDLK_F11) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    SDL_SetWindowFullscreen(window_, (flags & SDL_WINDOW_FULLSCREEN) == 0);
    return;
  }
  if (key == SDLK_F3 && app_->screen == Screen::Game) {
    app_->game.debugOverlay = !app_->game.debugOverlay;
    return;
  }
  if (key == SDLK_ESCAPE) {
    switch (app_->screen) {
      case Screen::Game: gameTogglePause(*app_); break;
      case Screen::Settings: {
        const Screen target = app_->settingsReturn;
        commitSettings(*app_);
        navigateTo(*app_, target == Screen::Game ? Screen::Home : target);
        break;
      }
      case Screen::Calibration: break; // Handled by the screen itself.
      case Screen::Library: navigateTo(*app_, Screen::Home); break;
      case Screen::Detail: navigateTo(*app_, Screen::Library); break;
      case Screen::Result: navigateTo(*app_, Screen::Library); break;
      default: break;
    }
    return;
  }
  if (key == SDLK_F5 && app_->screen == Screen::Game) {
    gameRestart(*app_);
  }
}

void Application::syncWindowMetrics() {
  if (window_ == nullptr || app_ == nullptr) return;
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window_, &width, &height);
  app_->windowWidth = std::max(1, width);
  app_->windowHeight = std::max(1, height);
  int drawableWidth = 0;
  int drawableHeight = 0;
  SDL_GetWindowSizeInPixels(window_, &drawableWidth, &drawableHeight);
  app_->drawableWidth = std::max(1, drawableWidth);
  app_->drawableHeight = std::max(1, drawableHeight);
  app_->displayDpi = displayDpi_;
  if (drawableWidth != lastDrawableWidth_ || drawableHeight != lastDrawableHeight_) {
    lastDrawableWidth_ = drawableWidth;
    lastDrawableHeight_ = drawableHeight;
    if (renderer_ != nullptr) {
      renderer_->resize(app_->drawableWidth, app_->drawableHeight);
    }
  }
}

void Application::updateAudioFromSettings() {
  if (audio_ == nullptr || app_ == nullptr) return;
  audio_->setVolume(app_->settings.musicVolume);
  audio_->setSoundEffectVolume(app_->settings.soundEffectVolume);
  audio_->setMusicLooping(false);
  audio_->setLowPassEnabled(false);
}

void Application::shutdown() noexcept {
  ui::fontCache().shutdown();
  if (context_ != nullptr) shutdownGpuTimer();
  if (app_ != nullptr) {
    app_->quitRequested = true;
    commitSettings(*app_);
    app_->textures.reset();
    app_->audio = nullptr;
    app_->renderer = nullptr;
    app_.reset();
  }
  if (renderer_ != nullptr) {
    if (context_ != nullptr) renderer_->shutdown();
    renderer_.reset();
  }
  ui::setBackdropSuppressed(false);
  if (backdrop_ != nullptr) {
    if (context_ != nullptr) backdrop_->release();
    backdrop_.reset();
  }
  if (audio_ != nullptr) {
    audio_->shutdown();
    audio_.reset();
  }
  if (imguiReady_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    imguiReady_ = false;
  }
  if (context_ != nullptr) {
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

} // namespace rechigros::app
