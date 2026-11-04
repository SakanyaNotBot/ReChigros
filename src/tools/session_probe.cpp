#include "rechigros/game/Chart.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/GameplaySession.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace rechigros::game;

Chart makeChart() {
  Chart chart;
  chart.formatVersion = 3;
  for (int lineIndex = 0; lineIndex < 2; ++lineIndex) {
    auto line = std::make_unique<JudgeLine>();
    line->bpm = 120.0;
    SpeedEvent speed;
    speed.endTime = 1.0e9;
    speed.value = speed.endValue = 1.0;
    line->speedEvents.push_back(speed);
    const auto add = [&](int type, double seconds, double x, double duration = 0.0) {
      Note note;
      note.type = type;
      note.time = seconds / (PGR_BEAT / 120.0);
      note.holdTime = duration / (PGR_BEAT / 120.0);
      note.positionX = x;
      line->notesAbove.push_back(note);
    };
    add(NOTE_TAP, 0.50, 0.0);
    add(NOTE_TAP, 0.50, 0.0);
    add(NOTE_FLICK, 0.75, 0.0);
    add(NOTE_HOLD, 1.00, 0.0, 0.80);
    add(NOTE_DRAG, 1.25, 0.0);
    add(NOTE_TAP, 1.50, 0.0);
    add(NOTE_HOLD, 2.00, 0.0, 0.40);
    add(NOTE_TAP, 2.20, 0.0);
    add(NOTE_TAP, 2.20, 0.20);
    chart.judgeLineList.push_back(std::move(line));
  }
  chart.prepareRuntime();
  return chart;
}

struct Input {
  double time{};
  int kind{};
  std::int64_t id{};
  float x{};
  float y{};
};

std::vector<Input> makeInputs(const Chart& chart) {
  std::vector<Input> inputs;
  for (std::size_t index = 0; index < chart.allNotesSorted.size(); ++index) {
    const Note& note = *chart.allNotesSorted[index];
    const double offset = index % 5 == 0 ? 0.02 : (index % 5 == 1 ? -0.02 : 0.0);
    const double down = note.sect + offset;
    const std::int64_t id = static_cast<std::int64_t>(index + 1);
    const float x = 640.0F + static_cast<float>(note.positionX * PGR_WIDTH * 1280.0);
    inputs.push_back({down, 0, id, x, 360.0F});
    if (note.type == NOTE_FLICK) inputs.push_back({down + 1.0 / 60.0, 1, id, x + 80.0F, 360.0F});
    const double up = note.isHold ? note.holdEndTime + 0.02 : down + 0.05;
    inputs.push_back({up, 2, id, x, 360.0F});
  }
  std::stable_sort(inputs.begin(), inputs.end(), [](const Input& first, const Input& second) {
    return first.time != second.time ? first.time < second.time : first.kind < second.kind;
  });
  return inputs;
}

std::uint64_t noteHash(const Chart& chart) {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  for (const auto* note : chart.allNotesSorted) {
    mix(static_cast<std::uint64_t>(note->judgeResult + 2));
    mix(note->isJudged ? 1 : 0);
    mix(note->clicked ? 1 : 0);
    mix(note->preJudge ? 1 : 0);
    mix(note->dragPrimed ? 1 : 0);
    mix(note->holdActive ? 1 : 0);
    mix(note->holdPerfect ? 1 : 0);
    mix(note->holdPreJudge ? 1 : 0);
    mix(note->holdBroken ? 1 : 0);
    mix(static_cast<std::uint64_t>(note->safeFrame + 4));
  }
  return hash;
}

void printFrame(const char* tag, int frame, double time, GameplaySession& session, const Chart& chart) {
  const auto& counts = session.judgement().counts();
  std::cout << tag << ' ' << frame << ' ' << std::fixed << std::setprecision(6) << time
            << " P" << counts[0] << " G" << counts[1] << " B" << counts[2] << " M" << counts[3]
            << " C" << session.judgement().combo() << " S" << session.judgement().score()
            << " H" << noteHash(chart);
  for (const auto& sound : session.takeDetailedSoundEvents()) {
    std::cout << " sound:" << sound.type << ':' << sound.hitSound;
  }
  for (const auto& burst : session.takeHoldHeadBursts()) {
    std::cout << " head:" << burst.noteIndex << ':' << burst.timeSec << ':' << burst.perfect;
  }
  for (const auto& burst : session.takeHoldBodyBursts()) {
    std::cout << " body:" << burst.noteIndex << ':' << burst.timeSec << ':' << burst.perfect;
  }
  std::cout << '\n';
}

std::vector<ReplayJudgement> runManual(const char* tag, Chart& chart, bool record) {
  GameplaySession session;
  session.setViewport({1280.0F, 720.0F, 16.0F / 9.0F, 1.0F, 380.0F});
  session.setReplayRecording(record);
  session.setChart(&chart);
  auto inputs = makeInputs(chart);
  std::size_t next = 0;
  for (int frame = 0; frame <= 190; ++frame) {
    const double time = frame / 60.0;
    while (next < inputs.size() && inputs[next].time <= time) {
      const auto& input = inputs[next++];
      if (input.kind == 0) session.touchDown(input.id, input.x, input.y);
      else if (input.kind == 1) session.touchMove(input.id, input.x, input.y);
      else session.touchUp(input.id);
    }
    session.update(time, 1.0F / 60.0F);
    printFrame(tag, frame, time, session, chart);
  }
  return session.recordedReplay();
}

void runAutoplay(Chart& chart) {
  GameplaySession session;
  session.setViewport({1280.0F, 720.0F, 16.0F / 9.0F, 1.0F, 380.0F});
  session.setChart(&chart);
  session.setAutoplay(true);
  for (int frame = 0; frame <= 190; ++frame) {
    const double time = frame / 60.0;
    session.update(time, 1.0F / 60.0F);
    printFrame("auto", frame, time, session, chart);
  }
}

void runReplay(Chart& chart, const std::vector<ReplayJudgement>& entries) {
  GameplaySession session;
  session.setViewport({1280.0F, 720.0F, 16.0F / 9.0F, 1.0F, 380.0F});
  session.setChart(&chart);
  session.setReplayPlayback(entries);
  for (int frame = 0; frame <= 190; ++frame) {
    const double time = frame / 60.0;
    session.update(time, 1.0F / 60.0F);
    printFrame("replay", frame, time, session, chart);
  }
}

void runSeeks(Chart& chart) {
  for (const double target : {2.5, 0.5, 2.0, 0.75}) {
    GameplaySession session;
    session.setViewport({1280.0F, 720.0F, 16.0F / 9.0F, 1.0F, 380.0F});
    session.setChart(&chart);
    session.setAutoplay(true);
    for (auto* note : chart.allNotesSorted) {
      if (note->sect < target) {
        note->judgeResult = static_cast<int>(JudgementEngine::Judgement::Miss);
        note->isJudged = true;
        note->clicked = true;
      }
    }
    for (int frame = 0; frame <= 60; ++frame) {
      const double time = target + frame / 60.0;
      session.update(time, 1.0F / 60.0F);
      printFrame("seek", frame, time, session, chart);
    }
  }
}
} // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "all";
  if (mode == "manual" || mode == "all") {
    Chart chart = makeChart();
    const auto entries = runManual("manual", chart, true);
    std::cout << "recorded " << entries.size() << '\n';
    Chart replay = makeChart();
    runReplay(replay, entries);
  }
  if (mode == "autoplay" || mode == "all") {
    Chart chart = makeChart();
    runAutoplay(chart);
  }
  if (mode == "seek" || mode == "all") {
    Chart chart = makeChart();
    runSeeks(chart);
  }
  return 0;
}
