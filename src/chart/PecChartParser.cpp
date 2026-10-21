#include "rechigros/chart/PecChartParser.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rechigros::chart {
namespace {
using namespace game;
constexpr double BEAT_MULT = 32.0;
constexpr double VISIBLE_STEP_TICK = 4.0;

double number(std::string_view text, double fallback) noexcept {
  try {
    std::size_t consumed{};
    const auto value = std::stod(std::string(text), &consumed);
    return consumed == text.size() ? value : fallback;
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> tokens(const std::string& line) {
  std::istringstream input(line);
  std::vector<std::string> result;
  for (std::string token; input >> token;) result.push_back(std::move(token));
  return result;
}

std::string trim(std::string text) {
  const auto begin = text.find_first_not_of(" \t\n\r");
  if (begin == std::string::npos) return {};
  const auto end = text.find_last_not_of(" \t\n\r");
  return text.substr(begin, end - begin + 1);
}

struct BpmPoint { double beat{}, bpm{120.0}; };
struct SpeedPoint { double beat{}, speed{1.0}; };
struct LineBuilder {
  std::vector<SpeedPoint> speedPoints;
  std::vector<MoveEvent> moveEvents;
  std::vector<LineEvent> rotateEvents;
  std::vector<LineEvent> alphaEvents;
  std::vector<Note> notesAbove;
  std::vector<Note> notesBelow;
};

class BpmConverter final {
public:
  explicit BpmConverter(std::vector<BpmPoint> points) : points_(std::move(points)) {
    std::stable_sort(points_.begin(), points_.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
    startSeconds_.resize(points_.size());
    for (std::size_t i = 1; i < points_.size(); ++i) {
      const auto delta = std::max(0.0, points_[i].beat - points_[i - 1].beat);
      startSeconds_[i] = startSeconds_[i - 1] + delta * (PGR_BEAT / points_[i - 1].bpm);
    }
  }

  [[nodiscard]] double baseBpm() const noexcept { return points_.empty() ? 120.0 : points_.front().bpm; }
  [[nodiscard]] double bpmAtBeat(double beat) const noexcept { return points_[segment(beat)].bpm; }
  [[nodiscard]] double beatToSec(double beat) const noexcept {
    const auto index = segment(beat);
    return startSeconds_[index] + std::max(0.0, beat - points_[index].beat) * (PGR_BEAT / points_[index].bpm);
  }
  [[nodiscard]] double beatToNormBeat(double beat, double lineBpm) const noexcept {
    const auto bpm = lineBpm > 0.0 ? lineBpm : baseBpm();
    return beatToSec(beat) / (PGR_BEAT / bpm);
  }

private:
  [[nodiscard]] std::size_t segment(double beat) const noexcept {
    const auto upper = std::upper_bound(points_.begin(), points_.end(), beat,
        [](double value, const BpmPoint& point) { return value < point.beat; });
    return upper == points_.begin() ? 0 : static_cast<std::size_t>(upper - points_.begin() - 1);
  }
  std::vector<BpmPoint> points_;
  std::vector<double> startSeconds_;
};

int clampEase(int easing) noexcept { return std::clamp(easing, 0, 28); }

Note parseNote(const std::vector<std::string>& fields) {
  const auto hold = fields[0] == "n2";
  const auto minimum = hold ? 7U : 6U;
  // A malformed command is skipped, not fatal: the reference returns null and continues.
  if (fields.size() < minimum) throw std::invalid_argument("skip");
  Note note;
  note.type = fields[0] == "n1" ? NOTE_TAP : fields[0] == "n2" ? NOTE_HOLD
      : fields[0] == "n3" ? NOTE_FLICK : NOTE_DRAG;
  note.time = number(fields[2], 0.0) * BEAT_MULT;
  if (hold) note.holdTime = std::max(0.0, number(fields[3], 0.0) * BEAT_MULT - note.time);
  const auto xIndex = hold ? 4U : 3U;
  const auto aboveIndex = hold ? 5U : 4U;
  const auto fakeIndex = hold ? 6U : 5U;
  note.positionX = number(fields[xIndex], 0.0) * (9.0 / 1024.0);
  note.isAbove = number(fields[aboveIndex], 1.0) == 1.0;
  note.isFake = number(fields[fakeIndex], 0.0) == 1.0;
  note.speed = 1.0;
  note.xScale = 1.0F;
  note.size = 1.0F;
  return note;
}

template<class Event> void normalizeTimes(std::vector<Event>& events) {
  std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (!std::isfinite(events[i].endTime)) events[i].endTime = i + 1 < events.size() ? events[i + 1].startTime : 1e9;
    events[i].endTime = std::max(events[i].startTime, events[i].endTime);
  }
}

void normalizeMove(std::vector<MoveEvent>& events) {
  normalizeTimes(events);
  double x = 0.5, y = 0.5;
  for (auto& event : events) {
    if (!std::isfinite(event.start)) event.start = x;
    if (!std::isfinite(event.start2)) event.start2 = y;
    x = event.end;
    y = event.end2;
  }
}

void normalizeLine(std::vector<LineEvent>& events, double fallback) {
  normalizeTimes(events);
  for (auto& event : events) {
    if (!std::isfinite(event.start)) event.start = fallback;
    fallback = event.end;
  }
}

// The visibleTime encoding is sampled on a fixed 4-tick grid across the alpha event, not
// per note: at each sample the interpolated alpha decides the visible-beat value, and every
// note sitting exactly on that sample receives it. Sampling matters because an alpha ramp
// can cross the -100 boundary partway through the event.
bool assignVisibleTime(const LineEvent& event, std::vector<Note*>& notes, const BpmConverter& bpm) {
  if (notes.empty()) return false;
  const auto start = event.startTime;
  const auto end = event.endTime;
  if (!std::isfinite(start) || !std::isfinite(end) || end <= start) return false;

  bool any = false;
  const auto alphaStart = event.start;
  const auto alphaEnd = event.end;
  const auto duration = end - start;
  auto time = start;
  std::size_t noteIndex = 0;

  while (time <= end + 1e-9) {
    auto progress = (time - start) / duration;
    progress = std::clamp(progress, 0.0, 1.0);
    const auto currentAlpha = alphaStart + (alphaEnd - alphaStart) * progress;

    if (currentAlpha < -100.0 && currentAlpha >= -1000.0) {
      auto visibleBeat = ((currentAlpha + 100.0) * -1.0) / 10.0;
      if (visibleBeat < 0.0) visibleBeat = 0.0;

      while (noteIndex < notes.size() && notes[noteIndex]->time < time - 1e-6) ++noteIndex;
      auto index = noteIndex;
      while (index < notes.size()) {
        auto* note = notes[index];
        if (std::abs(note->time - time) < 1e-6) {
          auto beatBpm = bpm.bpmAtBeat(note->time);
          if (!std::isfinite(beatBpm) || beatBpm <= 0.0) beatBpm = bpm.baseBpm();
          note->visibleTime = static_cast<float>(visibleBeat * (60.0 / beatBpm));
          any = true;
          ++index;
          continue;
        }
        if (note->time > time + 1e-6) break;
        ++index;
      }
    }
    time += VISIBLE_STEP_TICK;
  }
  return any;
}

void applyAlphaSpecials(std::vector<LineEvent>& events, LineBuilder& builder, const BpmConverter& bpm) {
  std::vector<Note*> notes;
  notes.reserve(builder.notesAbove.size() + builder.notesBelow.size());
  for (auto& note : builder.notesAbove) notes.push_back(&note);
  for (auto& note : builder.notesBelow) notes.push_back(&note);
  std::stable_sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) { return a->time < b->time; });
  for (auto& event : events) {
    auto start = event.start == -1.0 ? -255.0 : event.start == -2.0 ? -510.0 : event.start;
    auto end = event.end == -1.0 ? -255.0 : event.end == -2.0 ? -510.0 : event.end;
    if (start < -100.0 && start >= -1000.0) start = assignVisibleTime(event, notes, bpm) ? 0.0 : -255.0;
    if (end < -100.0 && end >= -1000.0) end = assignVisibleTime(event, notes, bpm) ? 0.0 : -255.0;
    event.start = start / 255.0;
    event.end = end / 255.0;
  }
}

std::vector<SpeedEvent> buildSpeed(std::vector<SpeedPoint> points, const BpmConverter& bpm, double lineBpm) {
  if (points.empty()) {
    SpeedEvent event;
    event.endTime = 1e9;
    event.value = event.endValue = 1.0;
    return {std::move(event)};
  }
  std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
  if (points.front().beat > 0.0) points.insert(points.begin(), {0.0, points.front().speed});
  std::vector<SpeedEvent> result;
  result.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    SpeedEvent event;
    event.startTime = bpm.beatToNormBeat(points[i].beat, lineBpm);
    event.endTime = i + 1 < points.size() ? bpm.beatToNormBeat(points[i + 1].beat, lineBpm) : 1e9;
    event.value = event.endValue = points[i].speed;
    result.push_back(std::move(event));
  }
  return result;
}

std::vector<MoveEvent> convert(std::vector<MoveEvent> events, const BpmConverter& bpm, double lineBpm) {
  if (events.empty()) return {{.startTime = 0.0, .endTime = 1e9, .start = 0.5, .end = 0.5, .start2 = 0.5, .end2 = 0.5}};
  for (auto& event : events) { event.startTime = bpm.beatToNormBeat(event.startTime, lineBpm); event.endTime = bpm.beatToNormBeat(event.endTime, lineBpm); }
  return events;
}

std::vector<LineEvent> convert(std::vector<LineEvent> events, const BpmConverter& bpm, double lineBpm, double fallback) {
  if (events.empty()) return {{.startTime = -1e9, .endTime = 1e9, .start = fallback, .end = fallback}};
  for (auto& event : events) { event.startTime = bpm.beatToNormBeat(event.startTime, lineBpm); event.endTime = bpm.beatToNormBeat(event.endTime, lineBpm); }
  return events;
}

void convertNotes(std::vector<Note>& notes, const BpmConverter& bpm, double lineBpm) {
  for (auto& note : notes) {
    const auto rawTime = note.time;
    note.time = bpm.beatToNormBeat(rawTime, lineBpm);
    if (note.holdTime > 0.0) {
      const auto duration = std::max(0.0, bpm.beatToSec(rawTime + note.holdTime) - bpm.beatToSec(rawTime));
      note.holdTime = duration / (PGR_BEAT / lineBpm);
    }
    if (!std::isfinite(note.speed)) note.speed = 1.0;
    if (!std::isfinite(note.xScale) || note.xScale == 0.0F) note.xScale = 1.0F;
    if (!std::isfinite(note.size) || note.size == 0.0F) note.size = 1.0F;
  }
  std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
}
} // namespace

game::Chart PecChartParser::parse(std::string_view content) {
  std::vector<std::string> lines;
  std::istringstream input{std::string(content)};
  for (std::string line; std::getline(input, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (const auto comment = line.find("//"); comment != std::string::npos) line.erase(comment);
    line = trim(std::move(line));
    if (!line.empty()) lines.push_back(std::move(line));
  }
  if (lines.empty()) throw std::invalid_argument("Empty PEC chart");
  const auto offsetMs = number(lines.front(), std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(offsetMs)) throw std::invalid_argument("PEC offset is not numeric");

  Chart chart;
  chart.offset = std::round(((offsetMs - 150.0) / 1000.0) * 10000.0) / 10000.0;
  std::vector<BpmPoint> bpmPoints;
  std::map<int, LineBuilder> builders;
  Note* lastNote{};

  for (std::size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
    const auto& source = lines[lineIndex];
    if (source[0] == '#') { if (lastNote) { lastNote->speed = number(trim(source.substr(1)), 1.0); if (!std::isfinite(lastNote->speed)) lastNote->speed = 1.0; } continue; }
    if (source[0] == '&') { if (lastNote) { lastNote->xScale = static_cast<float>(number(trim(source.substr(1)), 1.0)); if (!std::isfinite(lastNote->xScale) || lastNote->xScale == 0.0F) lastNote->xScale = 1.0F; } continue; }
    const auto fields = tokens(source);
    if (fields.empty()) continue;
    const auto& command = fields[0];
    if (command == "bp" && fields.size() >= 3) {
      auto bpm = number(fields[2], 120.0); if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
      bpmPoints.push_back({number(fields[1], 0.0) * BEAT_MULT, bpm});
      continue;
    }
    if ((command == "n1" || command == "n2" || command == "n3" || command == "n4") && fields.size() >= 2) {
      Note note;
      try {
        note = parseNote(fields);
      } catch (const std::exception&) {
        // Malformed note line: skip it and keep parsing, as the reference does.
        continue;
      }
      auto& builder = builders[static_cast<int>(number(fields[1], 0.0))];
      auto& destination = note.isAbove ? builder.notesAbove : builder.notesBelow;
      destination.push_back(std::move(note)); lastNote = &destination.back(); continue;
    }
    if (fields.size() < 2) continue;
    auto& builder = builders[static_cast<int>(number(fields[1], 0.0))];
    if (command == "cv" && fields.size() >= 4) {
      // Missing or non-finite speed defaults to 1.0 BEFORE the /7 normalisation, so an
      // absent value becomes 1/7 rather than 1.
      auto raw = number(fields[3], 1.0);
      if (!std::isfinite(raw)) raw = 1.0;
      builder.speedPoints.push_back({number(fields[2], 0.0) * BEAT_MULT, raw / 7.0});
    } else if (command == "cm" && fields.size() >= 6) {
      MoveEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = number(fields[3], 0.0) * BEAT_MULT;
      event.start = event.start2 = std::numeric_limits<double>::quiet_NaN(); event.end = number(fields[4], 1024.0) / 2048.0; event.end2 = number(fields[5], 700.0) / 1400.0; event.type = clampEase(static_cast<int>(fields.size() >= 7 ? number(fields[6], 1.0) : 1.0) - 1); builder.moveEvents.push_back(event);
    } else if (command == "cp" && fields.size() >= 5) {
      MoveEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = std::numeric_limits<double>::quiet_NaN(); event.start = event.end = number(fields[3], 1024.0) / 2048.0; event.start2 = event.end2 = number(fields[4], 700.0) / 1400.0; builder.moveEvents.push_back(event);
    } else if (command == "cr" && fields.size() >= 5) {
      LineEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = number(fields[3], 0.0) * BEAT_MULT; event.start = std::numeric_limits<double>::quiet_NaN(); event.end = number(fields[4], 0.0); event.type = clampEase(static_cast<int>(fields.size() >= 6 ? number(fields[5], 1.0) : 1.0) - 1); builder.rotateEvents.push_back(event);
    } else if (command == "cd" && fields.size() >= 4) {
      LineEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = std::numeric_limits<double>::quiet_NaN(); event.start = event.end = number(fields[3], 0.0); builder.rotateEvents.push_back(event);
    } else if (command == "cf" && fields.size() >= 5) {
      LineEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = number(fields[3], 0.0) * BEAT_MULT; event.start = std::numeric_limits<double>::quiet_NaN(); event.end = number(fields[4], 255.0); builder.alphaEvents.push_back(event);
    } else if (command == "ca" && fields.size() >= 4) {
      LineEvent event; event.startTime = number(fields[2], 0.0) * BEAT_MULT; event.endTime = std::numeric_limits<double>::quiet_NaN(); event.start = event.end = number(fields[3], 255.0); builder.alphaEvents.push_back(event);
    }
  }

  if (bpmPoints.empty()) bpmPoints.push_back({0.0, 120.0});
  std::stable_sort(bpmPoints.begin(), bpmPoints.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
  if (bpmPoints.front().beat > 0.0) bpmPoints.insert(bpmPoints.begin(), {0.0, bpmPoints.front().bpm});
  const BpmConverter bpm(bpmPoints);
  const auto lineBpm = bpm.baseBpm();

  for (auto& [index, builder] : builders) {
    (void)index;
    normalizeMove(builder.moveEvents); normalizeLine(builder.rotateEvents, 0.0); normalizeLine(builder.alphaEvents, 0.0);
    applyAlphaSpecials(builder.alphaEvents, builder, bpm);
    auto line = std::make_unique<JudgeLine>(); line->bpm = lineBpm; line->invertRotation = false;
    line->speedEvents = buildSpeed(std::move(builder.speedPoints), bpm, lineBpm);
    line->judgeLineMoveEvents = convert(std::move(builder.moveEvents), bpm, lineBpm);
    line->judgeLineRotateEvents = convert(std::move(builder.rotateEvents), bpm, lineBpm, 0.0);
    line->judgeLineDisappearEvents = convert(std::move(builder.alphaEvents), bpm, lineBpm, 0.0);
    convertNotes(builder.notesAbove, bpm, lineBpm); convertNotes(builder.notesBelow, bpm, lineBpm);
    line->notesAbove = std::move(builder.notesAbove); line->notesBelow = std::move(builder.notesBelow);
    chart.judgeLineList.push_back(std::move(line));
  }
  chart.prepareRuntime();
  return chart;
}
} // namespace rechigros::chart
