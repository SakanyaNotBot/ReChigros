// Headless frame-time profile for the gameplay renderer.
//
// Drives a real chart the way the game does — fixed-step session update, then a full frame
// through GameRenderer — and reports the distribution of update and render times. The point
// is repeatability: a hidden window with no presentation, a fixed timestep, a warm-up phase
// and a bounded number of frames, so before/after measurements are comparable.
//
// Three kinds of measurement are supported:
//  * steady-state runs over N frames with per-frame CSV and a histogram,
//  * one-off phase timings (chart parse, runtime preparation, renderer initialisation),
//  * ablation runs that remove one scene contributor at a time (blocks, effects, notes, HUD)
//    so its cost can be attributed without instrumenting the renderer.

#include "rechigros/chart/ChartLoader.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/render/GameRenderer.hpp"

#include <nlohmann/json.hpp>

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#ifndef RECHIGROS_GLES
# include <glad/glad.h>
#else
# include <GLES3/gl3.h>
#endif

#ifdef _WIN32
# include <windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Process CPU time, so measurements stay comparable while other work is running on the
// machine. Wall time is still reported separately for the end-to-end frame cost.
double processCpuMilliseconds() {
#ifdef _WIN32
  FILETIME creation{}, exit{}, kernel{}, user{};
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user) == 0) {
    return 0.0;
  }
  const auto toMs = [](const FILETIME& time) {
    const ULARGE_INTEGER value{{time.dwLowDateTime, time.dwHighDateTime}};
    return static_cast<double>(value.QuadPart) / 10000.0;
  };
  return toMs(kernel) + toMs(user);
#else
  timespec time{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) != 0) return 0.0;
  return static_cast<double>(time.tv_sec) * 1000.0
      + static_cast<double>(time.tv_nsec) / 1.0e6;
#endif
}

struct Options {
  std::filesystem::path chartPath;
  std::filesystem::path assetRoot{"assets"};
  std::filesystem::path csvPath;
  int frames{3000};
  int warmupFrames{180};
  int width{1920};
  int height{1080};
  double startTimeSec{0.0};
  double frameDeltaSec{1.0 / 60.0};
  bool runAblations{false};
  bool runMicro{false};
  bool profileLoad{true};
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
    } else if (flag == "--assets") {
      options.assetRoot = next();
    } else if (flag == "--csv") {
      options.csvPath = next();
    } else if (flag == "--frames") {
      options.frames = std::max(1, std::atoi(next().c_str()));
    } else if (flag == "--warmup") {
      options.warmupFrames = std::max(0, std::atoi(next().c_str()));
    } else if (flag == "--width") {
      options.width = std::max(64, std::atoi(next().c_str()));
    } else if (flag == "--height") {
      options.height = std::max(64, std::atoi(next().c_str()));
    } else if (flag == "--start") {
      options.startTimeSec = std::atof(next().c_str());
    } else if (flag == "--frame-delta") {
      options.frameDeltaSec = std::max(0.001, std::atof(next().c_str()));
    } else if (flag == "--ablations") {
      options.runAblations = true;
    } else if (flag == "--micro") {
      options.runMicro = true;
    } else if (flag == "--no-load-profile") {
      options.profileLoad = false;
    }
  }
  return options;
}

struct FrameSample {
  double updateMs{};
  double renderMs{};
  double totalMs{};
  // Process CPU and GPU time for the whole frame, which stay comparable while the machine
  // is busy with other work.
  double cpuMs{};
  double gpuMs{};
};

struct Stats {
  double meanMs{};
  double p50Ms{};
  double p95Ms{};
  double p99Ms{};
  double maxMs{};
  // Frames above the common frame budgets: 60 fps, 30 fps and the >100 ms hitch class.
  int over16ms{};
  int over33ms{};
  int over100ms{};
  double meanUpdateMs{};
  double meanRenderMs{};
  double meanCpuMs{};
  double p99CpuMs{};
  double meanGpuMs{};
  double p99GpuMs{};
};

double percentileOf(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
      std::clamp(fraction * static_cast<double>(values.size() - 1), 0.0,
          static_cast<double>(values.size() - 1)));
  return values[index];
}

Stats summarize(const std::vector<FrameSample>& samples) {
  Stats stats;
  if (samples.empty()) return stats;
  std::vector<double> totals, cpus, gpus;
  totals.reserve(samples.size());
  cpus.reserve(samples.size());
  gpus.reserve(samples.size());
  double updateSum = 0.0;
  double renderSum = 0.0;
  double cpuSum = 0.0;
  double gpuSum = 0.0;
  for (const auto& sample : samples) {
    totals.push_back(sample.totalMs);
    cpus.push_back(sample.cpuMs);
    gpus.push_back(sample.gpuMs);
    updateSum += sample.updateMs;
    renderSum += sample.renderMs;
    cpuSum += sample.cpuMs;
    gpuSum += sample.gpuMs;
    if (sample.totalMs > 16.0) ++stats.over16ms;
    if (sample.totalMs > 33.0) ++stats.over33ms;
    if (sample.totalMs > 100.0) ++stats.over100ms;
    if (sample.totalMs > stats.maxMs) stats.maxMs = sample.totalMs;
  }
  std::sort(totals.begin(), totals.end());
  const auto percentile = [&totals](double fraction) {
    if (totals.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        std::clamp(fraction * static_cast<double>(totals.size() - 1), 0.0,
            static_cast<double>(totals.size() - 1)));
    return totals[index];
  };
  stats.meanMs = std::accumulate(totals.begin(), totals.end(), 0.0)
      / static_cast<double>(totals.size());
  stats.meanUpdateMs = updateSum / static_cast<double>(samples.size());
  stats.meanRenderMs = renderSum / static_cast<double>(samples.size());
  stats.p50Ms = percentile(0.50);
  stats.p95Ms = percentile(0.95);
  stats.p99Ms = percentile(0.99);
  stats.meanCpuMs = cpuSum / static_cast<double>(samples.size());
  stats.p99CpuMs = percentileOf(cpus, 0.99);
  stats.meanGpuMs = gpuSum / static_cast<double>(samples.size());
  stats.p99GpuMs = percentileOf(gpus, 0.99);
  return stats;
}

void printStats(const std::string& label, const Stats& stats, int frames) {
  std::cout << std::fixed << std::setprecision(3) << label
            << " frames=" << frames
            << " mean=" << stats.meanMs << "ms"
            << " p50=" << stats.p50Ms << "ms"
            << " p95=" << stats.p95Ms << "ms"
            << " p99=" << stats.p99Ms << "ms"
            << " max=" << stats.maxMs << "ms"
            << " update=" << stats.meanUpdateMs << "ms"
            << " render=" << stats.meanRenderMs << "ms"
            << " cpu=" << stats.meanCpuMs << "ms"
            << " cpuP99=" << stats.p99CpuMs << "ms"
            << " gpu=" << stats.meanGpuMs << "ms"
            << " over16=" << stats.over16ms
            << " over33=" << stats.over33ms
            << " over100=" << stats.over100ms << '\n';
}

std::unique_ptr<rechigros::game::Chart> loadChart(const Options& options) {
  const auto loadStart = Clock::now();
  auto chart = rechigros::chart::ChartLoader::loadFromFile(options.chartPath, options.assetRoot);
  const double loadMs = millisecondsSince(loadStart);
  if (chart == nullptr) return nullptr;
  std::cout << std::fixed << std::setprecision(3)
            << "load: parse=" << loadMs << "ms (includes digest and level-effect match)\n";
  std::size_t liveZones = 0;
  for (const auto& zone : chart->blockAreaList) {
    if (options.startTimeSec >= zone.appearTime && options.startTimeSec < zone.disappearTime) {
      ++liveZones;
    }
  }
  std::cout << "chart: lines=" << chart->judgeLineList.size()
            << " notes=" << chart->allNotesSorted.size()
            << " blocks=" << chart->blockAreaList.size()
            << " (live at start=" << liveZones << ')'
            << " effects=" << chart->prprEffects.size()
            << " duration=" << chart->chartEndTimeSec << "s\n";
  if (options.profileLoad) {
    const auto prepStart = Clock::now();
    chart->prepareRuntime();
    std::cout << "load: prepareRuntime=" << millisecondsSince(prepStart) << "ms\n";
    rechigros::game::GameplaySession probe;
    probe.setViewport({static_cast<float>(options.width), static_cast<float>(options.height),
        16.0F / 9.0F, 1.0F, 380.0F});
    const auto sessionStart = Clock::now();
    probe.setChart(chart.get());
    std::cout << "load: sessionSetChart=" << millisecondsSince(sessionStart) << "ms\n";
  }
  return chart;
}

// One measured run: fresh renderer on a shared chart, fixed-step session updates.
std::vector<FrameSample> measure(const Options& options, rechigros::game::Chart& chart,
    int frames, int warmupFrames, bool hudVisible, const std::filesystem::path& csvPath) {
  rechigros::render::GameRenderer renderer;
  if (!renderer.initialize(options.assetRoot)) return {};
  renderer.resize(options.width, options.height);
  rechigros::game::GameplaySession gameplay;
  gameplay.setViewport({static_cast<float>(options.width), static_cast<float>(options.height),
      16.0F / 9.0F, 1.0F, 380.0F});
  gameplay.setChart(&chart);
  gameplay.setAutoplay(true);
  // Drive the session to the requested start time before measuring so the first measured
  // frame is steady state. The session is advanced in one step: this is a rendering
  // profile, so the note state before the start time is not interesting.
  gameplay.update(options.startTimeSec, static_cast<float>(options.frameDeltaSec));

  rechigros::render::RenderFrameInput input;
  input.hud.visible = hudVisible;
  input.hud.comboLabel = "AUTOPLAY";
  input.backgroundDim = 0.0F;
  input.scrollSpeed = 1.0F;
  input.dpi = 380.0F;
  input.songName = "profile";
  input.levelName = "profile";

  std::vector<FrameSample> samples;
  samples.reserve(static_cast<std::size_t>(frames));
  std::ofstream csv;
  if (!csvPath.empty()) {
    csv.open(csvPath, std::ios::binary);
    csv << "frame,chartTime,updateMs,renderMs,totalMs,cpuMs,gpuMs\n";
  }
  double chartTime = options.startTimeSec;
  const int totalFrames = warmupFrames + frames;
#ifndef RECHIGROS_GLES
  GLuint gpuQuery = 0;
  glGenQueries(1, &gpuQuery);
#endif
  for (int frame = 0; frame < totalFrames; ++frame) {
    const double frameStartCpu = processCpuMilliseconds();
    const auto updateStart = Clock::now();
    gameplay.update(chartTime, static_cast<float>(options.frameDeltaSec));
    gameplay.takeSoundEvents();
    const double updateMs = millisecondsSince(updateStart);

    const auto& judgement = gameplay.judgement();
    input.chartTimeSec = chartTime;
    input.playheadSec = chartTime;
    input.visualTimeSec = static_cast<double>(frame) * options.frameDeltaSec;
    input.hud.combo = judgement.combo();
    input.hud.score = judgement.score();
    input.hud.accuracy = judgement.accuracy();
    input.hud.maxCombo = judgement.maxCombo();
    input.hud.perfect = judgement.counts()[0];
    input.hud.good = judgement.counts()[1];
    input.hud.bad = judgement.counts()[2];
    input.hud.miss = judgement.counts()[3];
    input.hud.playheadSec = chartTime;
    input.hud.totalDurationSec = std::max(1.0, chart.chartEndTimeSec);

    const auto renderStart = Clock::now();
#ifndef RECHIGROS_GLES
    if (gpuQuery != 0) glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
#endif
    renderer.render(input, &chart, &judgement);
#ifndef RECHIGROS_GLES
    if (gpuQuery != 0) glEndQuery(GL_TIME_ELAPSED);
#endif
    glFinish();
    const double renderMs = millisecondsSince(renderStart);
    const double cpuMs = processCpuMilliseconds() - frameStartCpu;
    double gpuMs = 0.0;
#ifndef RECHIGROS_GLES
    if (gpuQuery != 0) {
      GLuint64 elapsed = 0;
      glGetQueryObjectui64v(gpuQuery, GL_QUERY_RESULT, &elapsed);
      gpuMs = static_cast<double>(elapsed) / 1.0e6;
    }
#endif

    chartTime += options.frameDeltaSec;
    if (frame < warmupFrames) continue;
    FrameSample sample;
    sample.updateMs = updateMs;
    sample.renderMs = renderMs;
    sample.totalMs = updateMs + renderMs;
    sample.cpuMs = cpuMs;
    sample.gpuMs = gpuMs;
    samples.push_back(sample);
    if (csv.is_open()) {
      csv << (frame - warmupFrames) << ',' << std::setprecision(6) << chartTime << ','
          << sample.updateMs << ',' << sample.renderMs << ',' << sample.totalMs << ','
          << sample.cpuMs << ',' << sample.gpuMs << '\n';
    }
  }
#ifndef RECHIGROS_GLES
  if (gpuQuery != 0) glDeleteQueries(1, &gpuQuery);
#endif
  renderer.shutdown();
  return samples;
}

// Times the one-off and per-frame CPU structures directly, so a hotspot shows up without
// having to instrument the renderer.
void runMicroBenchmarks(const Options& options, rechigros::game::Chart& chart) {
  std::cout << std::fixed << std::setprecision(4);
  const auto timeIt = [](int repeats, const std::function<void()>& work) {
    work();
    const auto start = Clock::now();
    for (int repeat = 0; repeat < repeats; ++repeat) work();
    return millisecondsSince(start) / static_cast<double>(repeats);
  };
  {
    // The chart load's fixed cost is dominated by the JSON document build; report it on its
    // own so a chart-load improvement can be attributed exactly.
    std::ifstream input(options.chartPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();
    std::cout << "micro jsonParse="
              << timeIt(3, [&content]() { const auto parsed = nlohmann::json::parse(content); (void)parsed; })
              << "ms (" << (static_cast<double>(content.size()) / (1024.0 * 1024.0)) << " MiB)\n";
  }
  std::cout << "micro prepareRuntime="
            << timeIt(5, [&chart]() { chart.prepareRuntime(); }) << "ms\n";
  std::cout << "micro resetRuntime="
            << timeIt(5, [&chart]() { chart.resetRuntime(); }) << "ms\n";
  std::cout << "micro lineStates="
            << timeIt(20, [&chart]() {
                 for (const auto& line : chart.judgeLineList) {
                   if (line != nullptr) line->fillState(30.0, 16.0F / 9.0F);
                 }
               })
            << "ms\n";
  std::cout << "micro blockAdvance="
            << timeIt(20, [&chart, &options]() {
                 const double now = options.startTimeSec;
                 for (auto& zone : chart.blockAreaList) {
                   zone.update(static_cast<float>(now), 0.0F, 0.0F,
                       static_cast<float>(options.width), static_cast<float>(options.height),
                       1.0F / 60.0F);
                 }
               })
            << "ms (" << chart.blockAreaList.size() << " zones)\n";
  std::cout << "micro noteScan="
            << timeIt(20, [&chart]() {
                 std::size_t visible = 0;
                 for (const auto* note : chart.allNotesSorted) {
                   if (note != nullptr && note->judgeResult < 0) ++visible;
                 }
               })
            << "ms (" << chart.allNotesSorted.size() << " notes)\n";
  std::cout << "micro floorBinary="
            << timeIt(10, [&chart]() {
                 double sum = 0.0;
                 for (const auto& line : chart.judgeLineList) {
                   if (line == nullptr) continue;
                   for (const auto& note : line->notes) {
                     sum += rechigros::game::EventUtils::getFloorPosition(note.time,
                         line->speedEvents, line->speedEventLayers);
                   }
                 }
                 if (sum < -1.0e300) std::cout << "unreachable";
               })
            << "ms (per-note binary search)\n";
  std::cout << "micro floorMonotonic="
            << timeIt(10, [&chart]() {
                 double sum = 0.0;
                 for (const auto& line : chart.judgeLineList) {
                   if (line == nullptr) continue;
                   int index = 0;
                   int holdIndex = 0;
                   std::vector<int> layerIndices;
                   std::vector<int> holdLayerIndices;
                   for (const auto& note : line->notes) {
                     sum += rechigros::game::EventUtils::floorPositionMonotonic(note.time,
                         line->speedEvents, line->speedEventLayers, index, layerIndices);
                     if (note.type == rechigros::game::NOTE_HOLD) {
                       sum += rechigros::game::EventUtils::floorPositionMonotonic(
                           note.time + note.holdTime, line->speedEvents,
                           line->speedEventLayers, holdIndex, holdLayerIndices);
                     }
                   }
                 }
                 if (sum < -1.0e300) std::cout << "unreachable";
               })
            << "ms (incremental cursor)\n";
  std::cout << "micro clickEffectScan="
            << timeIt(20, [&chart]() {
                 std::size_t active = 0;
                 for (const auto& item : chart.clickEffectsSorted) {
                   if (item.timeSec < 30.0) ++active;
                 }
               })
            << "ms\n";
  std::cout << "micro eventSamplePerLine="
            << timeIt(20, [&chart]() {
                 for (const auto& line : chart.judgeLineList) {
                   if (line == nullptr) continue;
                   for (const auto& event : line->judgeLineRotateEvents) {
                     if (event.startTime > 1e9) break;
                   }
                 }
               })
            << "ms\n";
}

} // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  if (options.chartPath.empty()) {
    std::cerr << "--frame-profile requires --chart <path>\n";
    return 2;
  }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 3;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_Window* window = SDL_CreateWindow("frame-profile", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, options.width, options.height,
      SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
  if (window == nullptr) {
    std::cerr << "No hidden GL window available: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 4;
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
#ifndef RECHIGROS_GLES
  if (context != nullptr && gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)) == 0) {
    std::cerr << "Failed to load OpenGL entry points\n";
    SDL_GL_DeleteContext(context);
    context = nullptr;
  }
#endif
  if (context == nullptr) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }

  int exitCode = 0;
  auto chart = loadChart(options);
  if (chart == nullptr) {
    std::cerr << "Could not load the chart\n";
    exitCode = 5;
  }

  if (exitCode == 0 && options.runMicro) {
    runMicroBenchmarks(options, *chart);
  }

  if (exitCode == 0) {
    std::cout << "profile: viewport=" << options.width << 'x' << options.height
              << " frames=" << options.frames << " warmup=" << options.warmupFrames
              << " start=" << options.startTimeSec << "s\n";
    const auto samples = measure(options, *chart, options.frames, options.warmupFrames, true,
        options.csvPath);
    if (!options.csvPath.empty()) std::cout << "csv: " << options.csvPath.string() << '\n';
    printStats("steady-state", summarize(samples), static_cast<int>(samples.size()));
  }

  if (exitCode == 0 && options.runAblations) {
    // Each variant runs on the same loaded chart with a fresh renderer. The full run is
    // measured twice, before the destructive variants, so measurement drift is visible.
    const int frames = std::max(120, options.frames / 8);
    const int warmup = std::max(30, options.warmupFrames / 4);
    const auto variant = [&options, &chart, frames, warmup](const char* label, bool hudVisible,
                                                        const std::function<void()>& mutate) {
      mutate();
      const auto samples = measure(options, *chart, frames, warmup, hudVisible, {});
      printStats(label, summarize(samples), static_cast<int>(samples.size()));
    };
    variant("ablation full", true, []() {});
    variant("ablation no-hud", false, []() {});
    variant("ablation full (repeat)", true, []() {});
    variant("ablation no-blocks", true, [&chart]() { chart->blockAreaList.clear(); });
    variant("ablation no-effects", true, [&chart]() {
      chart->prprEffects.clear();
      chart->levelEffects.reset();
    });
    variant("ablation no-notes", true, [&chart]() {
      for (const auto& line : chart->judgeLineList) {
        if (line == nullptr) continue;
        for (auto& note : line->notesAbove) note.isFake = true;
        for (auto& note : line->notesBelow) note.isFake = true;
      }
      chart->prepareRuntime();
    });
  }

  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
