#include "Application.hpp"

#include "rechigros/app/AppContext.hpp"
#include "rechigros/app/ScreenCommon.hpp"
#include "rechigros/app/Screens.hpp"
#include "rechigros/app/UiStyle.hpp"
#include "rechigros/audio/AudioEngine.hpp"
#include "rechigros/platform/FileSystem.hpp"
#include "rechigros/render/GameRenderer.hpp"
#include "rechigros/render/gl/Gl.hpp"

#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include <stb_image.h>

namespace rechigros::app {
namespace {

std::filesystem::path pathFromUtf8(const char* value) {
  if (value == nullptr || *value == '\0') return {};
  std::u8string utf8;
  while (*value) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(*value++)));
  return std::filesystem::path(utf8);
}

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
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
    return false;
  }
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

  Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(__ANDROID__) || defined(__IPHONEOS__)
  windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
  // Automated runs exercise the real GL context and event loop without putting a
  // window on the user's desktop.
  if (batchMode_) windowFlags |= SDL_WINDOW_HIDDEN;
#endif
  window_ = SDL_CreateWindow("ReChigros", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
      windowFlags);
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
      if (auto* icon = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, width * 4,
              SDL_PIXELFORMAT_RGBA32)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_FreeSurface(icon);
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
  SDL_GL_SetSwapInterval(1);

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
    float diagonal = 0.0F;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window_), &diagonal, nullptr, nullptr) == 0
        && std::isfinite(diagonal) && diagonal > 0.0F) {
      displayDpi_ = diagonal;
    }
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
  const float fontDensity = interfaceScale;
  {
    const auto fontPath = platform::assetRoot() / "res" / "font.ttf";
    ImFont* font = nullptr;
    if (platform::fileExists(fontPath)) {
      ImFontConfig config;
      // Rasterise denser without changing the reported metrics, so the layout keeps its
      // design sizes while the glyph texture carries more detail.
      config.RasterizerDensity = fontDensity;
      config.OversampleH = 1;
      config.OversampleV = 1;
      config.PixelSnapH = false;
      font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 20.0F, &config,
          io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
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
  if (!ImGui_ImplSDL2_InitForOpenGL(window_, context_)) {
    std::cerr << "ImGui SDL2 backend initialization failed\n";
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

  app_ = std::make_unique<AppContext>(platform::userDataRoot() / "app");
  app_->audio = audio_.get();
  app_->renderer = renderer_.get();
  app_->displayDpi = displayDpi_;
  app_->settingsDirty = false;
  // A verification run can pin the play mode regardless of the stored settings.
  app_->playOverrides.practice = practiceOverride_;
  app_->playOverrides.challenge = challengeOverride_;
  app_->playOverrides.mirrorX = mirrorOverride_;
  app_->playOverrides.autoplay = autoplayOverride_;
  updateAudioFromSettings();

  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  SDL_EventState(SDL_DROPTEXT, SDL_ENABLE);
  syncWindowMetrics();

  if (!initialPath_.empty()) {
    if (!replayPlaybackPath_.empty()) {
      startAdhocReplay(*app_, initialPath_, replayPlaybackPath_);
    } else {
      handleIncomingPath(*app_, initialPath_);
    }
  }
  return true;
}

int Application::run() {
  if (!initialize()) return 1;
  bool running = true;
  const auto frequency = SDL_GetPerformanceFrequency();
  auto previous = SDL_GetPerformanceCounter();
  double elapsed = 0.0;

  while (running) {
    const auto counter = SDL_GetPerformanceCounter();
    float delta = static_cast<float>(static_cast<double>(counter - previous)
        / static_cast<double>(frequency));
    previous = counter;
    if (frameClockReset_) {
      delta = 0.0F;
      frameClockReset_ = false;
    }
    delta = std::clamp(delta, 0.0F, 0.1F);
    elapsed += static_cast<double>(delta);
    if (delta > 0.0F) {
      smoothedFps_ = smoothedFps_ * 0.92F + (1.0F / delta) * 0.08F;
    }
    app_->deltaSec = static_cast<double>(delta);
    app_->nowSec = elapsed;
    app_->fps = smoothedFps_;

    processEvents(running);
    if (!running) break;
    if (suspended_) {
      SDL_Delay(10);
      continue;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    syncWindowMetrics();

    if (app_->screen == Screen::Game) {
      gameUpdate(*app_, delta);
    }

    if (app_->screen == Screen::Game) {
      gameRender(*app_);
      drawGameOverlay(*app_);
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, app_->drawableWidth, app_->drawableHeight);
      glClearColor(0.05F, 0.06F, 0.08F, 1.0F);
      glClear(GL_COLOR_BUFFER_BIT);
      drawCurrentScreen();
    }
    drawToast(*app_);

    if (app_->quitRequested) running = false;

    ++renderedFrames_;
    const int screenIndex = static_cast<int>(app_->screen);
    if (screenIndex >= 0 && screenIndex < 7) ++screenFrameCounts_[screenIndex];
    if (batchMode_) {
      updateBatchMode();
      if (renderedFrames_ >= frameLimit_) running = false;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
  }

  commitSettings(*app_);
  if (!recordReplayPath_.empty() && app_ != nullptr) {
    // A saved replay records what the run scored, not the values it started with.
    ReplayData recorded = app_->game.recorded;
    applyResultToReplayMeta(recorded, app_->lastResult);
    const bool saved = writeReplayFile(recordReplayPath_, recorded);
    std::cout << "REPLAY-RECORDED " << (saved ? "PASS" : "FAIL")
              << " path=" << recordReplayPath_.string()
              << " entries=" << recorded.entries.size()
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
      down.type = SDL_KEYDOWN;
      down.key.windowID = SDL_GetWindowID(window_);
      down.key.state = SDL_PRESSED;
      down.key.repeat = 0;
      down.key.keysym.sym = SDLK_ESCAPE;
      down.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
      SDL_PushEvent(&down);
      SDL_Event up = down;
      up.type = SDL_KEYUP;
      up.key.state = SDL_RELEASED;
      SDL_PushEvent(&up);
    };
    if (gameFrames == 200) pushEscape();
    if (gameFrames == 215 && game.pause == GameRuntime::Pause::Menu) pauseMenuSeen_ = true;
    if (gameFrames == 225) pushEscape();
    if (gameFrames == 240 && game.pause == GameRuntime::Pause::Countdown) {
      pauseCountdownSeen_ = true;
    }
    if (gameFrames > 300 && game.pause == GameRuntime::Pause::None) pauseResumedSeen_ = true;
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
    event.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.button.clicks = 1;
    event.button.x = static_cast<Sint32>(x);
    event.button.y = static_cast<Sint32>(y);
    SDL_PushEvent(&event);
  };
  const auto pushMotion = [&](float x, float y) {
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.windowID = SDL_GetWindowID(window_);
    event.motion.which = 0;
    event.motion.state = SDL_BUTTON_LMASK;
    event.motion.x = static_cast<Sint32>(x);
    event.motion.y = static_cast<Sint32>(y);
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
    pushButton(SDL_MOUSEBUTTONDOWN, centerX, centerY, true);
    batchInputStage_ = 1;
  } else if (batchInputStage_ == 1) {
    pushMotion(centerX + 4.0F, centerY);
    batchInputStage_ = 2;
  } else if (batchInputStage_ == 2) {
    pushButton(SDL_MOUSEBUTTONUP, centerX, centerY, false);
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
    // The recorded metadata is the reference: driving the touch pipeline from the
    // replay must reproduce the same score, accuracy and judgement counts.
    const auto& recorded = app_->game.playback.meta;
    const auto& played = app_->lastResult;
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
  std::cout << "BATCH-RUN " << (batchOk_ ? "PASS" : "FAIL") << " frames=" << renderedFrames_
            << " requested=" << frameLimit_ << " fps=" << smoothedFps_;
  if (audio_ != nullptr) std::cout << " audioHz=" << audio_->outputSampleRate();
  std::cout << " pointerEvents=" << app_->forwardedPointerEvents << '\n';
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

void Application::drawCurrentScreen() {
  switch (app_->screen) {
    case Screen::Home: drawHomeScreen(*app_); break;
    case Screen::Library: drawLibraryScreen(*app_); break;
    case Screen::Detail: drawDetailScreen(*app_); break;
    case Screen::Settings: drawSettingsScreen(*app_); break;
    case Screen::Calibration: drawCalibrationScreen(*app_); break;
    case Screen::Result: drawResultScreen(*app_); break;
    case Screen::Game: break;
  }
}

void Application::processEvents(bool& running) {
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    const bool imguiConsumed = ImGui_ImplSDL2_ProcessEvent(&event);
    (void)imguiConsumed;
    handleEvent(event, running);
  }
}

void Application::handleEvent(const SDL_Event& event, bool& running) {
  switch (event.type) {
    case SDL_QUIT:
      running = false;
      app_->quitRequested = true;
      return;
    case SDL_DROPFILE:
      if (event.drop.file != nullptr) {
        const auto path = pathFromUtf8(event.drop.file);
        handleIncomingPath(*app_, path);
        SDL_free(event.drop.file);
      }
      return;
    case SDL_KEYDOWN:
      if (event.key.repeat == 0) handleKeyDown(event);
      return;
    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID
          && app_->screen == Screen::Game && !ImGui::GetIO().WantCaptureMouse) {
        gamePointerDown(*app_, -1, static_cast<float>(event.button.x),
            static_cast<float>(event.button.y));
      }
      return;
    case SDL_MOUSEMOTION:
      if (app_->screen == Screen::Game && event.motion.which != SDL_TOUCH_MOUSEID
          && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
        gamePointerMove(*app_, -1, static_cast<float>(event.motion.x),
            static_cast<float>(event.motion.y));
      }
      return;
    case SDL_MOUSEBUTTONUP:
      if (event.button.button == SDL_BUTTON_LEFT && event.button.which != SDL_TOUCH_MOUSEID
          && app_->screen == Screen::Game) {
        gamePointerUp(*app_, -1);
      }
      return;
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP: {
      int width = 0;
      int height = 0;
      SDL_GetWindowSize(window_, &width, &height);
      const float x = event.tfinger.x * static_cast<float>(width);
      const float y = event.tfinger.y * static_cast<float>(height);
      if (app_->screen == Screen::Game) {
        if (event.type == SDL_FINGERDOWN) {
          gamePointerDown(*app_, event.tfinger.fingerId, x, y);
        } else if (event.type == SDL_FINGERMOTION) {
          gamePointerMove(*app_, event.tfinger.fingerId, x, y);
        } else {
          gamePointerUp(*app_, event.tfinger.fingerId);
        }
      }
      return;
    }
    case SDL_WINDOWEVENT:
      switch (event.window.event) {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
          syncWindowMetrics();
          break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
          // Automated runs keep the loop running even though the hidden window
          // never holds keyboard focus.
          if (!batchMode_ && app_->screen == Screen::Game) gameFocusLost(*app_);
          break;
        default: break;
      }
      return;
    case SDL_APP_WILLENTERBACKGROUND:
      if (!suspended_) {
        suspended_ = true;
        if (app_->screen == Screen::Game) gameFocusLost(*app_);
        audio_->suspend();
      }
      return;
    case SDL_APP_DIDENTERFOREGROUND:
      if (suspended_) {
        audio_->resume();
        suspended_ = false;
        frameClockReset_ = true;
      }
      return;
    case SDL_APP_TERMINATING:
      if (app_->screen == Screen::Game) gameFocusLost(*app_);
      audio_->suspend();
      return;
    default: break;
  }
}

void Application::handleKeyDown(const SDL_Event& event) {
  const SDL_Keycode key = event.key.keysym.sym;
  if (key == SDLK_F11) {
    const Uint32 flags = SDL_GetWindowFlags(window_);
    SDL_SetWindowFullscreen(window_,
        (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
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
  SDL_GL_GetDrawableSize(window_, &drawableWidth, &drawableHeight);
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
  if (audio_ != nullptr) {
    audio_->shutdown();
    audio_.reset();
  }
  if (imguiReady_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    imguiReady_ = false;
  }
  if (context_ != nullptr) {
    SDL_GL_DeleteContext(context_);
    context_ = nullptr;
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

} // namespace rechigros::app
