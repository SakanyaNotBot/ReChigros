// Headless frame-time profile for the gameplay renderer.
//
// Drives a real chart with fixed-step session updates and a full GameRenderer frame.
// A hidden window, warm-up phase and bounded frame count make runs comparable when the
// same chart, settings and machine load are used. Each rendered frame is serialized with
// glFinish and (on desktop GL) an immediate timer-query read; this is not presentation
// latency or the throughput of the application's pipelined render loop.
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
#include "rechigros/render/fx/BlockOverlay.hpp"
#include "rechigros/render/fx/HitEffect.hpp"
#include "rechigros/render/scene/NoteBatch.hpp"

#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef RECHIGROS_GLES
# include <glad/glad.h>
#else
# include <GLES3/gl3.h>
#endif

#ifdef _WIN32
# include <windows.h>
# include <malloc.h>
#endif

// The tap profile replaces the process-wide allocation operators so per-frame and per-tap
// heap traffic can be counted. The replacement lives in the executable and therefore also
// covers the linked static libraries.
namespace {
std::atomic<unsigned long long> gAllocationCount{0};
std::atomic<unsigned long long> gAllocationBytes{0};
}

void* operator new(std::size_t size) {
  gAllocationCount.fetch_add(1, std::memory_order_relaxed);
  gAllocationBytes.fetch_add(size, std::memory_order_relaxed);
  if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  return ::operator new(size);
}
void operator delete(void* pointer) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
  gAllocationCount.fetch_add(1, std::memory_order_relaxed);
  gAllocationBytes.fetch_add(size, std::memory_order_relaxed);
#ifdef _WIN32
  if (void* pointer = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment))) {
    return pointer;
  }
#else
  void* pointer = nullptr;
  if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) == 0) {
    return pointer;
  }
#endif
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#ifdef _WIN32
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Process CPU time excludes time spent waiting for the GPU; wall time reports the complete
// serialized frame cost. Other processes, scheduling and thermal state still affect results.
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
  std::filesystem::path skinDirectory;
  std::filesystem::path backgroundImage;
  std::filesystem::path csvPath;
  bool antialias{false};
  float backgroundDim{0.0F};
  float keyScale{1.0F};
  double fpsBudget{60.0};
  int frames{3000};
  int warmupFrames{180};
  int width{1920};
  int height{1080};
  double startTimeSec{0.0};
  double frameDeltaSec{1.0 / 60.0};
  bool runAblations{false};
  bool runMicro{false};
  bool profileLoad{true};
  // When set, prints a per-bin census of the chart (live zones, notes) instead of a
  // steady-state run, so device frame times can be correlated with chart content.
  double scanBinSec{0.0};
  // Tap profile: drive synthetic touches at chart note times in manual mode and report the
  // per-frame cost around taps plus heap traffic.
  bool tapProfile{false};
  double tapDurationSec{30.0};
  bool tapRender{true};
  bool tapHud{true};
  std::filesystem::path tapCsvPath;
};

std::atomic<unsigned long long> allocationCount() noexcept {
  return gAllocationCount.load(std::memory_order_relaxed);
}

std::atomic<unsigned long long> allocationBytes() noexcept {
  return gAllocationBytes.load(std::memory_order_relaxed);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index] != nullptr ? argv[index] : "";
    const auto next = [&]() -> std::string {
      if (index + 1 >= argc || argv[index + 1] == nullptr
          || std::string(argv[index + 1]).starts_with("--")) {
        throw std::invalid_argument("Missing value for " + flag);
      }
      const std::string value = argv[++index];
      if (value.empty()) throw std::invalid_argument("Empty value for " + flag);
      return value;
    };
    const auto number = [&](double minimum, double maximum) {
      const std::string value = next();
      std::size_t consumed = 0;
      double result = 0.0;
      try {
        result = std::stod(value, &consumed);
      } catch (const std::exception&) {
        throw std::invalid_argument("Invalid number for " + flag + ": " + value);
      }
      if (consumed != value.size() || !std::isfinite(result)
          || result < minimum || result > maximum) {
        throw std::invalid_argument("Out-of-range value for " + flag + ": " + value);
      }
      return result;
    };
    const auto integer = [&](int minimum) {
      const double value = number(minimum, std::numeric_limits<int>::max());
      if (std::floor(value) != value) {
        throw std::invalid_argument("Expected an integer for " + flag);
      }
      return static_cast<int>(value);
    };
    if (flag == "--chart") {
      options.chartPath = next();
    } else if (flag == "--assets") {
      options.assetRoot = next();
    } else if (flag == "--skin") {
      options.skinDirectory = next();
    } else if (flag == "--background") {
      options.backgroundImage = next();
    } else if (flag == "--antialias") {
      options.antialias = true;
    } else if (flag == "--no-antialias") {
      options.antialias = false;
    } else if (flag == "--dim") {
      options.backgroundDim = static_cast<float>(number(0.0, 1.0));
    } else if (flag == "--key-scale") {
      options.keyScale = static_cast<float>(number(0.7, 1.5));
    } else if (flag == "--fps-budget") {
      options.fpsBudget = number(1.0, 1000.0);
    } else if (flag == "--csv") {
      options.csvPath = next();
    } else if (flag == "--frames") {
      options.frames = integer(1);
    } else if (flag == "--warmup") {
      options.warmupFrames = integer(0);
    } else if (flag == "--width") {
      options.width = integer(64);
    } else if (flag == "--height") {
      options.height = integer(64);
    } else if (flag == "--start") {
      options.startTimeSec = number(-std::numeric_limits<double>::max(),
          std::numeric_limits<double>::max());
    } else if (flag == "--frame-delta") {
      options.frameDeltaSec = number(0.001, std::numeric_limits<float>::max());
    } else if (flag == "--ablations") {
      options.runAblations = true;
    } else if (flag == "--micro") {
      options.runMicro = true;
    } else if (flag == "--no-load-profile") {
      options.profileLoad = false;
    } else if (flag == "--scan-bins") {
      options.scanBinSec = number(0.1, std::numeric_limits<double>::max());
    } else if (flag == "--tap-profile") {
      options.tapProfile = true;
    } else if (flag == "--tap-duration") {
      options.tapDurationSec = number(1.0, std::numeric_limits<double>::max());
    } else if (flag == "--no-render") {
      options.tapRender = false;
    } else if (flag == "--no-hud") {
      options.tapHud = false;
    } else if (flag == "--tap-csv") {
      options.tapCsvPath = next();
    } else if (flag != "--frame-profile") {
      throw std::invalid_argument("Unknown option: " + flag);
    }
  }
  if (options.frames > std::numeric_limits<int>::max() - options.warmupFrames
      || options.tapDurationSec / options.frameDeltaSec + 0.5
          >= static_cast<double>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Requested frame count exceeds the supported range");
  }
  return options;
}

std::ofstream openCsv(const std::filesystem::path& path) {
  std::ofstream csv;
  if (!path.empty()) {
    csv.open(path, std::ios::binary);
    if (!csv) throw std::runtime_error("Could not open CSV: " + path.string());
    csv.exceptions(std::ios::failbit | std::ios::badbit);
    csv << std::fixed << std::setprecision(9);
  }
  return csv;
}

rechigros::game::GameplaySession::Viewport viewportFor(const Options& options) {
  return {static_cast<float>(options.width), static_cast<float>(options.height),
      16.0F / 9.0F, 1.0F, 380.0F, options.keyScale};
}

void initializeRenderer(rechigros::render::GameRenderer& renderer, const Options& options,
    rechigros::game::Chart& chart) {
  const auto start = Clock::now();
  if (!renderer.initialize(options.assetRoot)) {
    throw std::runtime_error("GameRenderer initialization failed");
  }
  renderer.resize(options.width, options.height);
  renderer.prepareAntialias(options.antialias);
  renderer.setSkinDirectory(options.skinDirectory);
  renderer.setBackgroundImage(options.backgroundImage);
  renderer.resetJudgementEffects(&chart);
  if (options.profileLoad) {
    std::cout << "load: rendererInitializeAndConfigure=" << millisecondsSince(start) << "ms\n";
  }
}

void configureInput(rechigros::render::RenderFrameInput& input, const Options& options,
    bool autoplay) {
  input.antialias = options.antialias;
  input.backgroundDim = options.backgroundDim;
  input.keyScale = options.keyScale;
  input.scrollSpeed = 1.0F;
  input.dpi = 380.0F;
  input.practice.autoPlay = autoplay;
}

struct FrameSample {
  double updateMs{};
  double renderMs{};
  double totalMs{};
  // Process CPU and GPU time for the serialized frame; neither includes presentation.
  double cpuMs{};
  double gpuMs{};
  std::size_t judgementEvents{};
  std::size_t holdHeadBursts{};
  std::size_t holdBodyBursts{};
};

struct Stats {
  double meanMs{};
  double p50Ms{};
  double p95Ms{};
  double p99Ms{};
  double maxMs{};
  // Keep the literal legacy thresholds, and report the exact 60/30 fps budgets separately.
  int over16ms{};
  int over33ms{};
  int over60fps{};
  int over30fps{};
  int overBudget{};
  double budgetMs{};
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

Stats summarize(const std::vector<FrameSample>& samples, double budgetMs) {
  Stats stats;
  stats.budgetMs = budgetMs;
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
    if (sample.totalMs > 1000.0 / 60.0) ++stats.over60fps;
    if (sample.totalMs > 1000.0 / 30.0) ++stats.over30fps;
    if (sample.totalMs > budgetMs) ++stats.overBudget;
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
            << " over60fps=" << stats.over60fps
            << " over30fps=" << stats.over30fps
            << " budgetMs=" << stats.budgetMs
            << " overBudget=" << stats.overBudget
            << " over100=" << stats.over100ms << '\n';
}

std::unique_ptr<rechigros::game::Chart> loadChart(const Options& options) {
  const auto loadStart = Clock::now();
  auto chart = rechigros::chart::ChartLoader::loadFromFile(options.chartPath, options.assetRoot);
  const double loadMs = millisecondsSince(loadStart);
  if (chart == nullptr) return nullptr;
  std::cout << std::fixed << std::setprecision(3)
            << "load: chartLoad=" << loadMs
            << "ms (includes parse, initial runtime preparation, digest and level-effect match)\n";
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
    // prepareRuntime re-rolls random click particles. Probe a separately loaded chart so the
    // measured scene retains the first load's runtime data, just like the application.
    auto prepChart = rechigros::chart::ChartLoader::loadFromFile(
        options.chartPath, options.assetRoot);
    if (!prepChart) throw std::runtime_error("Could not load the preparation probe chart");
    const auto prepStart = Clock::now();
    prepChart->prepareRuntime();
    std::cout << "load: prepareRuntime=" << millisecondsSince(prepStart)
              << "ms (separate chart, main chart keeps first-load particles)\n";
    rechigros::game::GameplaySession probe;
    probe.setViewport(viewportFor(options));
    const auto sessionStart = Clock::now();
    probe.setChart(prepChart.get());
    std::cout << "load: sessionSetChart=" << millisecondsSince(sessionStart) << "ms\n";
  }
  return chart;
}

// One measured run: fresh renderer on a shared chart, fixed-step session updates.
std::vector<FrameSample> measure(const Options& options, rechigros::game::Chart& chart,
    int frames, int warmupFrames, bool hudVisible, const std::filesystem::path& csvPath) {
  auto csv = openCsv(csvPath);
  if (csv.is_open()) {
    csv << "frame,chartTime,updateMs,renderMs,totalMs,cpuMs,gpuMs,judgementEvents,"
           "holdHeadBursts,holdBodyBursts\n";
  }
  rechigros::render::GameRenderer renderer;
  initializeRenderer(renderer, options, chart);
  rechigros::game::GameplaySession gameplay;
  gameplay.setViewport(viewportFor(options));
  gameplay.setChart(&chart);
  gameplay.setAutoplay(true);
  // Drive the session to the requested start time before measuring so the first measured
  // frame is steady state. The session is advanced in one step: this is a rendering
  // profile, so the note state before the start time is not interesting.
  gameplay.update(options.startTimeSec, static_cast<float>(options.frameDeltaSec));

  // Discard the seek's catch-up events; the first profiled update starts a new frame.
  gameplay.takeSoundEvents();
  gameplay.takeJudgementEvents();
  gameplay.takeHoldHeadBursts();
  gameplay.takeHoldBodyBursts();

  rechigros::render::RenderFrameInput input;
  configureInput(input, options, gameplay.autoplay());
  input.hud.visible = hudVisible;
  input.hud.comboLabel = "AUTOPLAY";
  input.songName = "profile";
  input.levelName = "profile";

  std::vector<FrameSample> samples;
  samples.reserve(static_cast<std::size_t>(frames));
  double chartTime = options.startTimeSec;
  const int totalFrames = warmupFrames + frames;
#ifndef RECHIGROS_GLES
  GLuint gpuQuery = 0;
  glGenQueries(1, &gpuQuery);
#endif
  // Exclude queued setup work from the first frame's GPU wait.
  glFinish();
  for (int frame = 0; frame < totalFrames; ++frame) {
    const auto frameStart = Clock::now();
    const double frameStartCpu = processCpuMilliseconds();
    const auto updateStart = Clock::now();
    gameplay.update(chartTime, static_cast<float>(options.frameDeltaSec));
    gameplay.takeSoundEvents();
    input.judgementEvents = gameplay.takeJudgementEvents();
    input.judgementEventsAuthoritative = true;
    input.holdHeadBursts = gameplay.takeHoldHeadBursts();
    input.holdBodyBursts = gameplay.takeHoldBodyBursts();
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
    input.hud.visualTimeSec = input.visualTimeSec;
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
    double gpuMs = 0.0;
#ifndef RECHIGROS_GLES
    if (gpuQuery != 0) {
      GLuint64 elapsed = 0;
      glGetQueryObjectui64v(gpuQuery, GL_QUERY_RESULT, &elapsed);
      gpuMs = static_cast<double>(elapsed) / 1.0e6;
    }
#endif

    // End after the synchronous query read: total includes HUD/input assembly and all GPU
    // waits, but excludes sample storage and CSV output.
    const double cpuMs = processCpuMilliseconds() - frameStartCpu;
    const double totalMs = millisecondsSince(frameStart);
    if (frame >= warmupFrames) {
      FrameSample sample;
      sample.updateMs = updateMs;
      sample.renderMs = renderMs;
      sample.totalMs = totalMs;
      sample.cpuMs = cpuMs;
      sample.gpuMs = gpuMs;
      sample.judgementEvents = input.judgementEvents.size();
      sample.holdHeadBursts = input.holdHeadBursts.size();
      sample.holdBodyBursts = input.holdBodyBursts.size();
      samples.push_back(sample);
      if (csv.is_open()) {
        csv << (frame - warmupFrames) << ',' << chartTime << ','
            << sample.updateMs << ',' << sample.renderMs << ',' << sample.totalMs << ','
            << sample.cpuMs << ',' << sample.gpuMs << ',' << sample.judgementEvents << ','
            << sample.holdHeadBursts << ',' << sample.holdBodyBursts << '\n';
      }
    }
    chartTime += options.frameDeltaSec;
  }
#ifndef RECHIGROS_GLES
  if (gpuQuery != 0) glDeleteQueries(1, &gpuQuery);
#endif
  renderer.shutdown();
  if (csv.is_open()) csv.close();
  return samples;
}

// Drives synthetic touches at chart note times in manual mode and compares the frames that
// contain a tap against a no-tap control run over the same chart range. Also accounts heap
// traffic per frame and per tap, which is where input stutter usually comes from.
void runTapProfile(const Options& options, rechigros::game::Chart& chart) {
  auto csv = openCsv(options.tapCsvPath);
  using rechigros::game::GameplaySession;
  struct StageGeometry {
    float left{};
    float top{};
    float width{};
    float height{};
    float aspect{16.0F / 9.0F};
  };
  const auto stageOf = [&options]() {
    StageGeometry stage;
    stage.aspect = 16.0F / 9.0F;
    stage.width = static_cast<float>(options.width);
    stage.height = stage.width / stage.aspect;
    if (stage.height > static_cast<float>(options.height)) {
      stage.height = static_cast<float>(options.height);
      stage.width = stage.height * stage.aspect;
    }
    stage.left = (static_cast<float>(options.width) - stage.width) * 0.5F;
    stage.top = (static_cast<float>(options.height) - stage.height) * 0.5F;
    return stage;
  };
  const auto viewport = [&options]() { return viewportFor(options); };
  const auto stage = stageOf();
  struct ScheduledTap {
    double timeSec{};
    std::int64_t id{};
    float x{};
    float y{};
  };
  std::vector<ScheduledTap> taps;
  taps.reserve(chart.allNotesSorted.size());
  std::int64_t nextTapId = -1000;
  for (const auto* note : chart.allNotesSorted) {
    if (note == nullptr) continue;
    if (note->type != rechigros::game::NOTE_TAP && note->type != rechigros::game::NOTE_HOLD
        && note->type != rechigros::game::NOTE_DRAG
        && note->type != rechigros::game::NOTE_FLICK) {
      continue;
    }
    if (note->sect < options.startTimeSec) continue;
    if (note->sect > options.startTimeSec + options.tapDurationSec) continue;
    auto* line = note->master;
    if (line == nullptr) continue;
    const auto state = line->fillState(note->sect, stage.aspect);
    const float lineX = stage.left + state.xNorm * stage.width;
    const float lineY = stage.top + state.yNorm * stage.height;
    const float radians = state.rotateDeg * 3.14159265358979323846F / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    // The judge position of a note is the line position plus its along-line offset.
    const float offsetPx =
        static_cast<float>(note->positionX * rechigros::game::PGR_WIDTH * stage.width);
    ScheduledTap tap;
    tap.timeSec = note->sect;
    tap.id = nextTapId--;
    tap.x = lineX + offsetPx * cosine;
    tap.y = lineY + offsetPx * sine;
    taps.push_back(tap);
  }

  struct FrameRow {
    double chartTimeSec{};
    int taps{};
    double tapMs{};
    double updateMs{};
    double renderMs{};
    double totalMs{};
    unsigned long long allocations{};
    unsigned long long allocationBytes{};
    unsigned long long tapAllocations{};
    unsigned long long updateAllocations{};
    unsigned long long renderAllocations{};
    int judgedPerfect{};
    int judgedGood{};
    int judgedBad{};
    int judgedMiss{};
    std::size_t judgementEvents{};
    std::size_t holdHeadBursts{};
    std::size_t holdBodyBursts{};
    bool control{};
  };
  const auto runSegment = [&](bool control) {
    // Give each segment its own renderer so the control cannot inherit hit/HUD/blur state.
    rechigros::render::GameRenderer renderer;
    if (options.tapRender) initializeRenderer(renderer, options, chart);
    GameplaySession session;
    session.setViewport(viewport());
    session.setChart(&chart);
    session.setAutoplay(false);
    rechigros::render::RenderFrameInput input;
    configureInput(input, options, session.autoplay());
    input.hud.visible = options.tapHud;
    input.hud.totalDurationSec = std::max(1.0, chart.chartEndTimeSec);
    std::vector<FrameRow> rows;
    const int frameCount =
        static_cast<int>(options.tapDurationSec / options.frameDeltaSec + 0.5);
    rows.reserve(static_cast<std::size_t>(frameCount));
    double time = options.startTimeSec;
    std::size_t nextTap = 0;
    while (nextTap < taps.size() && taps[nextTap].timeSec < time) ++nextTap;
    std::vector<std::pair<double, std::int64_t>> releases;
    if (options.tapRender) glFinish();
    for (int frame = 0; frame < frameCount; ++frame) {
      FrameRow row;
      row.control = control;
      row.chartTimeSec = time;
      const auto frameStart = Clock::now();
      const auto countsBefore = session.judgement().counts();
      const auto allocBefore = allocationCount();
      const auto bytesBefore = allocationBytes();
      if (!control) {
        const auto tapStart = Clock::now();
        while (nextTap < taps.size() && taps[nextTap].timeSec <= time) {
          const auto& tap = taps[nextTap];
          session.touchDown(tap.id, tap.x, tap.y);
          releases.emplace_back(time + 0.05, tap.id);
          ++row.taps;
          ++nextTap;
        }
        std::size_t releaseIndex = 0;
        while (releaseIndex < releases.size()) {
          if (releases[releaseIndex].first <= time) {
            session.touchUp(releases[releaseIndex].second);
            releases.erase(releases.begin() + static_cast<std::ptrdiff_t>(releaseIndex));
          } else {
            ++releaseIndex;
          }
        }
        row.tapMs = millisecondsSince(tapStart);
        row.tapAllocations = allocationCount() - allocBefore;
      }
      const auto updateStart = Clock::now();
      const auto updateAllocBefore = allocationCount();
      session.update(time, static_cast<float>(options.frameDeltaSec));
      session.takeSoundEvents();
      input.judgementEvents = session.takeJudgementEvents();
      input.judgementEventsAuthoritative = true;
      input.holdHeadBursts = session.takeHoldHeadBursts();
      input.holdBodyBursts = session.takeHoldBodyBursts();
      row.judgementEvents = input.judgementEvents.size();
      row.holdHeadBursts = input.holdHeadBursts.size();
      row.holdBodyBursts = input.holdBodyBursts.size();
      row.updateMs = millisecondsSince(updateStart);
      row.updateAllocations = allocationCount() - updateAllocBefore;
      const auto countsAfter = session.judgement().counts();
      row.judgedPerfect = countsAfter[0] - countsBefore[0];
      row.judgedGood = countsAfter[1] - countsBefore[1];
      row.judgedBad = countsAfter[2] - countsBefore[2];
      row.judgedMiss = countsAfter[3] - countsBefore[3];

      double renderMs = 0.0;
      if (options.tapRender) {
        const auto& judgement = session.judgement();
        input.chartTimeSec = time;
        input.playheadSec = time;
        input.visualTimeSec = static_cast<double>(frame) * options.frameDeltaSec;
        input.hud.combo = judgement.combo();
        input.hud.score = judgement.score();
        input.hud.accuracy = judgement.accuracy();
        input.hud.maxCombo = judgement.maxCombo();
        input.hud.perfect = judgement.counts()[0];
        input.hud.good = judgement.counts()[1];
        input.hud.bad = judgement.counts()[2];
        input.hud.miss = judgement.counts()[3];
        input.hud.playheadSec = time;
        input.hud.visualTimeSec = input.visualTimeSec;
        input.blockedTouchPositions = session.blockedTouchPositions();
        input.blockedTouchPulseSec = session.blockedTouchPulseSec();
        const auto renderAllocBefore = allocationCount();
        const auto renderStart = Clock::now();
        renderer.render(input, &chart, &judgement);
        glFinish();
        renderMs = millisecondsSince(renderStart);
        row.renderAllocations = allocationCount() - renderAllocBefore;
      }
      row.renderMs = renderMs;
      row.totalMs = millisecondsSince(frameStart);
      row.allocations = allocationCount() - allocBefore;
      row.allocationBytes = allocationBytes() - bytesBefore;
      rows.push_back(row);
      time += options.frameDeltaSec;
    }
    return rows;
  };

  std::cout << std::fixed << std::setprecision(3)
            << "tap-profile: taps=" << taps.size() << " duration=" << options.tapDurationSec
            << "s start=" << options.startTimeSec << "s render=" << (options.tapRender ? 1 : 0)
            << " autoplay=0 warmup=0 mode="
            << (options.tapRender ? "serialized-glFinish" : "update-only")
            << " present=0 (cold segments; taps are synthetic presses, not sustained holds/flicks)\n";
  const auto tapRows = runSegment(false);
  const auto controlRows = runSegment(true);

  const auto stats = [](std::vector<double> values) {
    Stats result;
    if (values.empty()) return result;
    std::sort(values.begin(), values.end());
    const auto mean = std::accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
    const auto at = [&values](double fraction) {
      return values[static_cast<std::size_t>(std::clamp(fraction, 0.0, 1.0)
          * static_cast<double>(values.size() - 1))];
    };
    result.meanMs = mean;
    result.p50Ms = at(0.50);
    result.p95Ms = at(0.95);
    result.p99Ms = at(0.99);
    result.maxMs = values.back();
    return result;
  };
  const auto column = [](const std::vector<FrameRow>& rows, const auto& pick) {
    std::vector<double> values;
    values.reserve(rows.size());
    for (const auto& row : rows) values.push_back(static_cast<double>(pick(row)));
    return values;
  };
  std::vector<double> tapOnly;
  tapOnly.reserve(taps.size());
  unsigned long long tapAllocTotal = 0;
  std::size_t executedTaps = 0;
  for (const auto& row : tapRows) {
    tapAllocTotal += row.tapAllocations;
    if (row.taps > 0) {
      executedTaps += static_cast<std::size_t>(row.taps);
      const double costPerTap = row.tapMs / static_cast<double>(row.taps);
      for (int repeat = 0; repeat < row.taps; ++repeat) tapOnly.push_back(costPerTap);
    }
  }
  const auto tapFrames = [](const std::vector<FrameRow>& rows, bool withTaps) {
    std::vector<FrameRow> filtered;
    for (const auto& row : rows) {
      if ((row.taps > 0) == withTaps) filtered.push_back(row);
    }
    return filtered;
  };
  const auto tapHit = tapFrames(tapRows, true);
  const auto tapMiss = tapFrames(tapRows, false);
  const auto printBlock = [&](const char* label, const std::vector<FrameRow>& rows,
                            const std::vector<FrameRow>& control) {
    const auto update = stats(column(rows, [](const FrameRow& row) { return row.updateMs; }));
    const auto render = stats(column(rows, [](const FrameRow& row) { return row.renderMs; }));
    const auto total = stats(column(rows, [](const FrameRow& row) { return row.totalMs; }));
    const auto allocations = stats(column(rows, [](const FrameRow& row) {
      return static_cast<double>(row.allocations);
    }));
    const auto controlAllocations =
        stats(column(control, [](const FrameRow& row) {
          return static_cast<double>(row.allocations);
        }));
    const auto updateAllocations =
        stats(column(rows, [](const FrameRow& row) {
          return static_cast<double>(row.updateAllocations);
        }));
    const auto renderAllocations =
        stats(column(rows, [](const FrameRow& row) {
          return static_cast<double>(row.renderAllocations);
        }));
    int perfect = 0;
    int good = 0;
    int bad = 0;
    int miss = 0;
    int overBudget = 0;
    const double budgetMs = 1000.0 / options.fpsBudget;
    for (const auto& row : rows) {
      perfect += row.judgedPerfect;
      good += row.judgedGood;
      bad += row.judgedBad;
      miss += row.judgedMiss;
      if (row.totalMs > budgetMs) ++overBudget;
    }
    std::cout << label << " frames=" << rows.size()
              << " update mean=" << update.meanMs << " p95=" << update.p95Ms
              << " p99=" << update.p99Ms
              << " render mean=" << render.meanMs << " p95=" << render.p95Ms
              << " p99=" << render.p99Ms
              << " total mean=" << total.meanMs << " p99=" << total.p99Ms
              << " max=" << total.maxMs
              << " budgetMs=" << budgetMs << " overBudget=" << overBudget
              << " alloc/frame=" << allocations.meanMs
              << " p99=" << allocations.p99Ms
              << " (updateAlloc=" << updateAllocations.meanMs
              << " renderAlloc=" << renderAllocations.meanMs << ')'
              << " controlAlloc=" << controlAllocations.meanMs
              << " judged P/G/B/M=" << perfect << '/' << good << '/' << bad << '/' << miss
              << '\n';
  };
  printBlock("tap-frames  ", tapHit, controlRows);
  printBlock("no-tap frames", tapMiss, controlRows);
  printBlock("control     ", controlRows, controlRows);
  const auto tapCost = stats(tapOnly);
  std::cout << "tap-ops: scheduled=" << taps.size() << " taps=" << executedTaps
            << " cost=amortized-per-tap mean=" << tapCost.meanMs
            << " p50=" << tapCost.p50Ms << " p95=" << tapCost.p95Ms
            << " p99=" << tapCost.p99Ms << " max=" << tapCost.maxMs
            << " alloc/tap=" << (executedTaps == 0 ? 0.0
                                              : static_cast<double>(tapAllocTotal)
                                                    / static_cast<double>(executedTaps))
            << '\n';
  if (csv.is_open()) {
    csv << "kind,frame,chartTime,taps,tapMs,updateMs,renderMs,totalMs,allocations,"
           "allocationBytes,tapAllocations,updateAllocations,renderAllocations,"
           "perfect,good,bad,miss,judgementEvents,holdHeadBursts,holdBodyBursts\n";
    const auto writeRows = [&csv](const char* kind, const std::vector<FrameRow>& rows) {
      for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        csv << kind << ',' << index << ',' << row.chartTimeSec << ',' << row.taps << ','
            << row.tapMs << ',' << row.updateMs << ',' << row.renderMs << ',' << row.totalMs
            << ',' << row.allocations << ',' << row.allocationBytes << ','
            << row.tapAllocations << ',' << row.updateAllocations << ','
            << row.renderAllocations << ',' << row.judgedPerfect << ',' << row.judgedGood
            << ',' << row.judgedBad << ',' << row.judgedMiss << ',' << row.judgementEvents
            << ',' << row.holdHeadBursts << ',' << row.holdBodyBursts << '\n';
      }
    };
    writeRows("tap", tapRows);
    writeRows("control", controlRows);
    csv.close();
  }
}

// Times the one-off and per-frame CPU structures directly, so a hotspot shows up without
// having to instrument the renderer.
void runMicroBenchmarks(const Options& options, rechigros::game::Chart& chart) {
  std::cout << std::fixed << std::setprecision(4);
  double checksum = 0.0;
  // A volatile observable sink prevents scalar scans and returned states being optimized
  // away. Each invocation publishes one aggregate, keeping volatile traffic outside loops.
  volatile double observable = 0.0;
  const auto timeIt = [&checksum, &observable](int repeats, const auto& work) {
    const auto publish = [&]() {
      std::atomic_signal_fence(std::memory_order_seq_cst);
      const double value = static_cast<double>(work());
      if (!std::isfinite(value)) throw std::runtime_error("Non-finite microbenchmark result");
      observable = value;
      checksum += value;
      std::atomic_signal_fence(std::memory_order_seq_cst);
    };
    publish();
    const auto start = Clock::now();
    for (int repeat = 0; repeat < repeats; ++repeat) publish();
    return millisecondsSince(start) / static_cast<double>(repeats);
  };
  {
    // The chart load's fixed cost is dominated by the JSON document build; report it on its
    // own so a chart-load improvement can be attributed exactly.
    std::ifstream input(options.chartPath, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open chart for JSON microbenchmark");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();
    std::cout << "micro jsonParse="
              << timeIt(3, [&content]() { return nlohmann::json::parse(content).size(); })
              << "ms (" << (static_cast<double>(content.size()) / (1024.0 * 1024.0)) << " MiB)\n";
  }
  std::cout << "micro prepareRuntime="
            << timeIt(5, [&chart]() {
                 chart.prepareRuntime();
                 return chart.allNotesSorted.size() + chart.clickEffectsSorted.size();
               }) << "ms\n";
  std::cout << "micro resetRuntime="
            << timeIt(5, [&chart]() {
                 chart.resetRuntime();
                 return chart.allNotesSorted.size();
               }) << "ms\n";
  std::cout << "micro lineStates="
            << timeIt(20, [&chart, &options]() {
                 double sum = 0.0;
                 for (const auto& line : chart.judgeLineList) {
                   if (line == nullptr) continue;
                   line->resetCursors();
                   const auto state = line->fillState(options.startTimeSec, 16.0F / 9.0F);
                   sum += state.xNorm + state.yNorm + state.rotateDeg + state.alpha
                       + state.floorPosition + static_cast<double>(state.text.size());
                 }
                 return sum;
               })
            << "ms (cursor reset per sample)\n";
  std::cout << "micro blockAdvance="
            << timeIt(20, [&chart, &options]() {
                 const double now = options.startTimeSec;
                 double sum = 0.0;
                 for (auto& zone : chart.blockAreaList) {
                   zone.update(static_cast<float>(now), 0.0F, 0.0F,
                       static_cast<float>(options.width), static_cast<float>(options.height),
                       1.0F / 60.0F);
                   sum += zone.centerX + zone.centerY + zone.halfW + zone.halfH
                       + zone.rotationDeg + zone.showAlpha();
                 }
                 return sum;
               })
            << "ms (" << chart.blockAreaList.size() << " zones)\n";
  {
    // One frame's worth of note passes (hold + every head type, both highlight variants),
    // built against synthetic line frames so the batch builders can be timed in isolation.
    std::vector<rechigros::render::scene::LineFrame> frames(chart.judgeLineList.size());
    for (auto& frame : frames) {
      frame.valid = true;
      frame.renderable = true;
      frame.noteValid = true;
      frame.visibleMin = -2000.0F;
      frame.visibleMax = 2000.0F;
      frame.visiblePad = 200.0F;
      frame.bpm = 120.0;
      frame.floorPosition = 0.0;
      frame.x = static_cast<float>(options.width) * 0.5F;
      frame.y = static_cast<float>(options.height) * 0.5F;
    }
    std::vector<int> order;
    order.reserve(chart.judgeLineList.size());
    for (std::size_t index = 0; index < chart.judgeLineList.size(); ++index) {
      order.push_back(static_cast<int>(index));
    }
    rechigros::render::scene::NotePassParams params;
    params.chartTimeSec = options.startTimeSec;
    params.stageWidth = static_cast<float>(options.width);
    params.stageHeight = static_cast<float>(options.height);
    params.noteWidthBase = 60.0F;
    params.cullBounds = {-1000.0F, -1000.0F, 4000.0F, 3000.0F};
    const auto uv = rechigros::render::scene::buildHoldUv(256, 64, 64);
    rechigros::render::batch::SpriteBatch batch;
    const auto runNotePasses = [&]() {
      std::size_t sprites = 0;
      for (int highlight = 0; highlight < 2; ++highlight) {
        batch.begin();
        rechigros::render::scene::buildHoldBatch(batch, frames, order, chart,
            highlight == 1, 128.0F, 256.0F, uv, false, false, false, 0.0F, 0.0F, params);
        sprites += static_cast<std::size_t>(batch.size());
        for (const int type : {1, 2, 4}) {
          batch.begin();
          rechigros::render::scene::buildHeadBatch(batch, frames, order, chart, type,
              highlight == 1, 128.0F, 256.0F, params);
          sprites += static_cast<std::size_t>(batch.size());
        }
      }
      return sprites;
    };
    std::cout << "micro noteBatches=" << timeIt(20, runNotePasses) << "ms (8 passes, "
              << chart.allNotesSorted.size() << " notes)\n";
  }
  std::cout << "micro noteScan="
            << timeIt(20, [&chart]() {
                 std::size_t visible = 0;
                 for (const auto* note : chart.allNotesSorted) {
                   if (note != nullptr && note->judgeResult < 0) ++visible;
                 }
                 return visible;
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
                     if (note.type == rechigros::game::NOTE_HOLD) {
                       sum += rechigros::game::EventUtils::getFloorPosition(
                           note.time + note.holdTime, line->speedEvents, line->speedEventLayers);
                     }
                   }
                 }
                 return sum;
               })
            << "ms (note heads and hold ends; per-note binary search)\n";
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
                 return sum;
               })
            << "ms (incremental cursor)\n";
  std::cout << "micro clickEffectScan="
            << timeIt(20, [&chart, &options]() {
                 std::size_t active = 0;
                 for (const auto& item : chart.clickEffectsSorted) {
                   if (item.timeSec < options.startTimeSec) ++active;
                 }
                 return active;
               })
            << "ms\n";
  {
    // Whole-list zone advancing (the previous per-frame behaviour) against the windowed
    // advancing the overlay now performs, on the same list and the same time range.
    std::vector<rechigros::game::OfficialBlock> whole = chart.blockAreaList;
    std::vector<rechigros::game::OfficialBlock> windowed = chart.blockAreaList;
    rechigros::render::fx::BlockOverlay overlay;
    const float stageWidth = static_cast<float>(options.width);
    const float stageHeight = static_cast<float>(options.height);
    constexpr int kFrames = 600;
    constexpr float kDelta = 1.0F / 60.0F;
    const double begin = options.startTimeSec;
    const auto wholeStart = Clock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
      const double time = begin + static_cast<double>(frame) * static_cast<double>(kDelta);
      for (auto& block : whole) {
        if (time < static_cast<double>(block.appearTime)
            || time >= static_cast<double>(block.disappearTime)) {
          block.resetRuntime();
          continue;
        }
        block.update(static_cast<float>(time), 0.0F, 0.0F, stageWidth, stageHeight, kDelta);
      }
    }
    const double wholeMs = millisecondsSince(wholeStart) / static_cast<double>(kFrames);
    const auto windowStart = Clock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
      const float time = static_cast<float>(begin)
          + static_cast<float>(frame) * kDelta;
      overlay.updateAll(windowed, time, kDelta, 0.0F, 0.0F, stageWidth, stageHeight);
    }
    const double windowedMs = millisecondsSince(windowStart) / static_cast<double>(kFrames);
    bool same = whole.size() == windowed.size();
    if (same) {
      for (std::size_t index = 0; index < whole.size(); ++index) {
        if (whole[index].visible != windowed[index].visible
            || whole[index].active != windowed[index].active) {
          same = false;
          break;
        }
      }
    }
    std::cout << "micro blockAdvanceWholeList=" << wholeMs
              << "ms blockAdvanceWindowed=" << windowedMs
              << "ms match=" << (same ? 1 : 0) << " (" << chart.blockAreaList.size()
              << " zones)\n";
  }
  {
    // Session update and hit-effect bookkeeping over 600 real frames of the chart, which is
    // the per-frame gameplay cost that runs on the device beside the draw passes.
    rechigros::game::GameplaySession session;
    session.setViewport(viewportFor(options));
    session.setChart(&chart);
    session.setAutoplay(true);
    rechigros::render::fx::HitEffectField effects;
    constexpr int kFrames = 600;
    constexpr float kDelta = 1.0F / 60.0F;
    double time = options.startTimeSec;
    session.update(time, kDelta);
    effects.update(chart, time, time, 0.0F, 0.0F, static_cast<float>(options.width),
        static_cast<float>(options.height), 60.0F);
    const auto sessionStart = Clock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
      time += static_cast<double>(kDelta);
      session.update(time, kDelta);
      session.takeSoundEvents();
      checksum += static_cast<double>(session.takeJudgementEvents().size()
          + session.takeHoldHeadBursts().size() + session.takeHoldBodyBursts().size());
    }
    const double sessionMs = millisecondsSince(sessionStart) / static_cast<double>(kFrames);
    const auto effectStart = Clock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
      const double effectTime = options.startTimeSec + static_cast<double>(frame) * kDelta;
      effects.update(chart, effectTime, effectTime, 0.0F, 0.0F,
          static_cast<float>(options.width), static_cast<float>(options.height), 60.0F);
    }
    const double effectMs = millisecondsSince(effectStart) / static_cast<double>(kFrames);
    std::cout << "micro sessionUpdate=" << sessionMs << "ms hitEffectsUpdate=" << effectMs
              << "ms (session blocks=" << session.blocks().size()
              << " total=" << millisecondsSince(sessionStart) << "ms)\n";
  }
  {
    // Draw cost of the red-zone overlay for a burst of simultaneously visible zones, which
    // is where the device CPU correlates with the number of live zones.
    constexpr int kZones = 20;
    std::vector<rechigros::game::OfficialBlock> zones(static_cast<std::size_t>(kZones));
    for (int index = 0; index < kZones; ++index) {
      auto& zone = zones[static_cast<std::size_t>(index)];
      zone.bottomLeftX = 0.05F + 0.01F * static_cast<float>(index % 6);
      zone.bottomLeftY = 0.10F;
      zone.topRightX = 0.35F + 0.02F * static_cast<float>(index % 5);
      zone.topRightY = 0.90F;
      zone.appearTime = 0.0F;
      zone.enableTime = 0.0F;
      zone.disableTime = 1.0e9F;
      zone.disappearTime = 1.0e9F;
    }
    rechigros::render::fx::BlockOverlay overlay;
    rechigros::render::batch::SpriteBatch batch;
    const float stageWidth = static_cast<float>(options.width);
    const float stageHeight = static_cast<float>(options.height);
    overlay.updateAll(zones, static_cast<float>(options.startTimeSec), 1.0F / 60.0F, 0.0F,
        0.0F, stageWidth, stageHeight);
    const auto drawPass = [&]() {
      batch.begin();
      for (const auto& zone : zones) {
        overlay.draw(zone, static_cast<float>(options.startTimeSec), batch);
      }
      return batch.size();
    };
    std::cout << "micro zoneDraw20=" << timeIt(50, drawPass) << "ms ("
              << kZones << " active zones)\n";
  }
  {
    // Equivalence of the cached block-window query against a full scan across the whole
    // chart, which is the check the renderer patch relies on.
    std::size_t mismatches = 0;
    std::size_t samples = 0;
    constexpr double kStep = 1.0 / 60.0;
    for (double time = 0.0; time <= chart.chartEndTimeSec; time += kStep) {
      const float chartTime = static_cast<float>(time);
      bool expected = false;
      for (const auto& zone : chart.blockAreaList) {
        if (chartTime >= zone.appearTime && chartTime < zone.disappearTime) {
          expected = true;
          break;
        }
      }
      if (chart.hasLiveBlock(time) != expected) ++mismatches;
      ++samples;
    }
    std::cout << "micro blockWindowQuery mismatches=" << mismatches << " over " << samples
              << " samples (" << chart.blockAreaList.size() << " zones)\n";
  }
  std::cout << "micro eventSamplePerLine="
            << timeIt(20, [&chart, &options]() {
                 double sum = 0.0;
                 for (const auto& line : chart.judgeLineList) {
                   if (line == nullptr) continue;
                   sum += rechigros::game::EventUtils::getEventVal(
                       options.startTimeSec, line->judgeLineRotateEvents);
                 }
                 return sum;
               })
            << "ms\n";
  std::cout << "micro checksum=" << checksum << " last=" << observable << '\n';
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "frame-profile: " << error.what() << '\n';
    return 2;
  }
  if (options.chartPath.empty()) {
    std::cerr << "--frame-profile requires --chart <path>\n";
    return 2;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return 3;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_Window* window = SDL_CreateWindow("frame-profile", options.width, options.height,
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
    SDL_GL_DestroyContext(context);
    context = nullptr;
  }
#endif
  if (context == nullptr) {
    std::cerr << "Could not create GL context: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 4;
  }

  std::cout << std::fixed << std::setprecision(6)
            << "settings: antialias=" << (options.antialias ? 1 : 0)
            << " skin=" << std::quoted(options.skinDirectory.string())
            << " background=" << std::quoted(options.backgroundImage.string())
            << " dim=" << options.backgroundDim << " keyScale=" << options.keyScale
            << " fpsBudget=" << options.fpsBudget << " budgetMs=" << 1000.0 / options.fpsBudget
            << " frameDeltaSec=" << options.frameDeltaSec << '\n';
#ifndef RECHIGROS_GLES
  std::cout << "measurement: mode=serialized-glFinish-immediate-query present=0"
               " total=wall(update+input+HUD+render+GPU-wait+query-read)"
               " excludes=CSV/storage (not presentation latency or pipelined throughput)\n";
#else
  std::cout << "measurement: mode=serialized-glFinish present=0 gpuTimer=unavailable"
               " total=wall(update+input+HUD+render+GPU-wait) excludes=CSV/storage\n";
#endif

  int exitCode = 0;
  try {
    auto chart = loadChart(options);
    if (chart == nullptr) {
      std::cerr << "Could not load the chart\n";
      exitCode = 5;
    }

    if (exitCode == 0 && options.runMicro) {
      auto microChart = rechigros::chart::ChartLoader::loadFromFile(
          options.chartPath, options.assetRoot);
      if (!microChart) throw std::runtime_error("Could not load the microbenchmark chart");
      std::cout << "micro: separate chart; main measurement keeps first-load runtime data\n";
      runMicroBenchmarks(options, *microChart);
    }

    bool scanOnly = false;
    if (exitCode == 0 && options.scanBinSec > 0.0) {
      scanOnly = true;
      // Census of the chart per time bin: how many zones are inside their window and how many
      // notes are authored, so external frame-time CSVs can be correlated with content.
      std::cout << std::fixed << std::setprecision(3) << "scan: bin,startSec,endSec,liveZones,notes\n";
      const double step = options.scanBinSec;
      int bin = 0;
      for (double start = 0.0; start < chart->chartEndTimeSec + step; start += step, ++bin) {
        const double end = start + step;
        const double middle = start + step * 0.5;
        std::size_t liveZones = 0;
        for (const auto& zone : chart->blockAreaList) {
          if (middle >= static_cast<double>(zone.appearTime)
              && middle < static_cast<double>(zone.disappearTime)) {
            ++liveZones;
          }
        }
        std::size_t notes = 0;
        for (const auto* note : chart->allNotesSorted) {
          if (note != nullptr && note->sect >= start && note->sect < end) ++notes;
        }
        std::cout << "scan: " << bin << ',' << start << ',' << end << ',' << liveZones << ','
                  << notes << '\n';
      }
    }

    if (exitCode == 0 && options.tapProfile) {
      runTapProfile(options, *chart);
      scanOnly = true;
    }

    if (exitCode == 0 && !scanOnly) {
      std::cout << "profile: viewport=" << options.width << 'x' << options.height
                << " frames=" << options.frames << " warmup=" << options.warmupFrames
                << " start=" << options.startTimeSec << "s autoplay=1"
                << " firstMeasuredChartTime="
                << options.startTimeSec + options.warmupFrames * options.frameDeltaSec << "s\n";
      const auto samples = measure(options, *chart, options.frames, options.warmupFrames, true,
          options.csvPath);
      if (!options.csvPath.empty()) std::cout << "csv: " << options.csvPath.string() << '\n';
      printStats("steady-state", summarize(samples, 1000.0 / options.fpsBudget),
          static_cast<int>(samples.size()));
    }

    if (exitCode == 0 && options.runAblations) {
      // Reload for every variant, including the repeated full run. Never accumulate removals
      // or reuse session/renderer state from an earlier variant.
      const int frames = std::max(120, options.frames / 8);
      const int warmup = std::max(30, options.warmupFrames / 4);
      const auto variant = [&options, frames, warmup](const char* label, bool hudVisible,
                               const std::function<void(rechigros::game::Chart&)>& mutate) {
        auto variantChart = rechigros::chart::ChartLoader::loadFromFile(
            options.chartPath, options.assetRoot);
        if (!variantChart) throw std::runtime_error("Could not load ablation chart");
        mutate(*variantChart);
        const auto samples = measure(options, *variantChart, frames, warmup, hudVisible, {});
        printStats(label, summarize(samples, 1000.0 / options.fpsBudget),
            static_cast<int>(samples.size()));
      };
      std::cout << "ablation: independent chart loads; first-load particle rolls may vary\n";
      variant("ablation full", true, [](auto&) {});
      variant("ablation no-hud", false, [](auto&) {});
      variant("ablation no-blocks", true, [](auto& value) { value.blockAreaList.clear(); });
      variant("ablation no-effects", true, [](auto& value) {
        value.prprEffects.clear();
        value.levelEffects.reset();
      });
      variant("ablation no-notes", true, [](auto& value) {
        // Fake notes still draw before their hit time. Clear runtime indices as well as note
        // storage, preserving the chart duration and every unrelated scene contributor.
        value.allNotesSorted.clear();
        value.clickEffectsSorted.clear();
        for (const auto& line : value.judgeLineList) {
          if (line == nullptr) continue;
          line->notesByType.clear();
          line->noteVariantMasks.fill(0);
          line->notes.clear();
          line->notesAbove.clear();
          line->notesBelow.clear();
          line->maxHoldDurationSec = 0.0;
        }
      });
      variant("ablation full (repeat)", true, [](auto&) {});
    }
  } catch (const std::exception& error) {
    std::cerr << "frame-profile: " << error.what() << '\n';
    exitCode = 6;
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
