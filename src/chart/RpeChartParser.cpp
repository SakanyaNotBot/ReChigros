#include "rechigros/chart/RpeChartParser.hpp"

#include "rechigros/game/Easing.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rechigros::chart {
namespace {
using nlohmann::json;
using namespace rechigros::game;

constexpr double EVENT_END = 1e9;

const json* field(const json& object, std::initializer_list<const char*> keys) {
  if (!object.is_object()) return nullptr;
  for (const auto* key : keys) {
    const auto found = object.find(key);
    if (found != object.end() && !found->is_null()) return &*found;
  }
  return nullptr;
}

const json* array(const json& object, std::initializer_list<const char*> keys) {
  const auto* result = field(object, keys);
  return result && result->is_array() ? result : nullptr;
}

const json* object(const json& source, std::initializer_list<const char*> keys) {
  const auto* result = field(source, keys);
  return result && result->is_object() ? result : nullptr;
}

double number(const json& source, std::initializer_list<const char*> keys, double fallback) {
  const auto* value = field(source, keys);
  if (!value || value->is_structured()) return fallback;
  try {
    if (value->is_number()) return value->get<double>();
    if (value->is_boolean()) return value->get<bool>() ? 1.0 : 0.0;
    if (value->is_string()) {
      const std::string text = value->get<std::string>();
      std::size_t consumed = 0;
      const double parsed = std::stod(text, &consumed);
      return consumed > 0 ? parsed : fallback;
    }
  } catch (const std::exception&) {
  }
  return fallback;
}

// Numeric accessor semantics: a fractional value truncates toward zero, it does not round.
int integer(const json& source, std::initializer_list<const char*> keys, int fallback) {
  const auto* value = field(source, keys);
  if (!value || value->is_structured()) return fallback;
  if (value->is_number()) {
    const double result = value->get<double>();
    if (!std::isfinite(result)) return fallback;
    return static_cast<int>(result);
  }
  if (value->is_boolean()) return value->get<bool>() ? 1 : 0;
  if (value->is_string()) {
    try {
      const std::string text = value->get<std::string>();
      std::size_t consumed = 0;
      const int parsed = std::stoi(text, &consumed);
      return consumed > 0 ? parsed : fallback;
    } catch (const std::exception&) {
      return fallback;
    }
  }
  return fallback;
}

bool boolean(const json& source, std::initializer_list<const char*> keys, bool fallback) {
  const auto* value = field(source, keys);
  if (!value) return fallback;
  if (value->is_boolean()) return value->get<bool>();
  // A numeric flag converts through the integer view, so a fraction below one is false.
  if (value->is_number()) return static_cast<int>(value->get<double>()) != 0;
  if (value->is_string()) {
    const auto text = value->get<std::string>();
    if (text == "true" || text == "True" || text == "TRUE") return true;
    if (text == "false" || text == "False" || text == "FALSE") return false;
    try { return std::stoi(text) != 0; } catch (const std::exception&) {}
  }
  return fallback;
}

std::string stringValue(const json& source, std::initializer_list<const char*> keys,
                        std::string fallback = {}) {
  const auto* value = field(source, keys);
  return value && value->is_string() ? value->get<std::string>() : std::move(fallback);
}

// Reads an RPE beat value: a number, or an array whose first two entries are a whole beat
// and a numerator over the third, then quantises it to thousandths before scaling to ticks.
double parseBeat(const json* value) {
  if (!value || value->is_null()) return 0.0;
  double beat{};
  try {
    if (value->is_number()) beat = value->get<double>();
    else if (value->is_string()) {
      std::size_t consumed = 0;
      const std::string text = value->get<std::string>();
      beat = std::stod(text, &consumed);
      if (consumed == 0) beat = 0.0;
    } else if (value->is_array()) {
      const auto component = [&value](std::size_t index) {
        return index < value->size() && (*value)[index].is_number()
            ? (*value)[index].get<double>() : 0.0;
      };
      if (value->size() >= 3) {
        const auto denominator = component(2);
        beat = denominator == 0.0 ? component(0) : component(0) + component(1) / denominator;
      } else if (value->size() == 2) {
        beat = component(0) + component(1);
      } else if (value->size() == 1) {
        beat = component(0);
      }
    }
  } catch (const json::exception&) {
    beat = 0.0;
  }
  if (!std::isfinite(beat)) beat = 0.0;
  // Round half up rather than to nearest-even so the tie-break matches the source format.
  return std::floor(beat * 1000.0 + 0.5) / 1000.0 * 32.0;
}

const json* eventTime(const json& event, bool start) {
  if (const auto* result = start
          ? field(event, {"startTime", "StartTime", "start_time"})
          : field(event, {"endTime", "EndTime", "end_time"})) return result;
  const auto* fallback = field(event, {start ? "start" : "end"});
  return fallback && fallback->is_array() ? fallback : nullptr;
}

struct BpmPoint { double beat{}, bpm{120.0}; };

class BpmConverter final {
public:
  explicit BpmConverter(std::vector<BpmPoint> points) : points_(std::move(points)) {
    std::stable_sort(points_.begin(), points_.end(),
        [](const auto& a, const auto& b) { return a.beat < b.beat; });
    if (points_.empty()) points_.push_back({0.0, 120.0});
    std::vector<BpmPoint> unique;
    for (const auto& point : points_) {
      if (!unique.empty() && std::abs(unique.back().beat - point.beat) < 1e-9) unique.back() = point;
      else unique.push_back(point);
    }
    points_ = std::move(unique);
    seconds_.resize(points_.size(), 0.0);
    for (std::size_t i = 1; i < points_.size(); ++i) {
      seconds_[i] = seconds_[i - 1] + std::max(0.0, points_[i].beat - points_[i - 1].beat)
          * (PGR_BEAT / points_[i - 1].bpm);
    }
    std::vector<double> beats, bpms;
    beats.reserve(points_.size()); bpms.reserve(points_.size());
    for (const auto& point : points_) { beats.push_back(point.beat); bpms.push_back(point.bpm); }
    timeline_ = std::make_shared<BpmTimeline>(std::move(beats), std::move(bpms), seconds_);
  }

  [[nodiscard]] double baseBpm() const noexcept { return points_.front().bpm; }
  [[nodiscard]] double beatToSec(double beat) const noexcept { return timeline_->beatToSec(beat); }
  [[nodiscard]] double normBeat(double beat, double timeBaseBpm) const noexcept {
    const auto bpm = timeBaseBpm > 0.0 ? timeBaseBpm : baseBpm();
    return beatToSec(beat) / (PGR_BEAT / bpm);
  }
  [[nodiscard]] const std::shared_ptr<BpmTimeline>& timeline() const noexcept { return timeline_; }

private:
  std::vector<BpmPoint> points_;
  std::vector<double> seconds_;
  std::shared_ptr<BpmTimeline> timeline_;
};

// Line, move, colour and text tracks store the zero-based tween index their evaluator
// expects. Speed tracks evaluate against the authored one-based index instead, so their
// caller asks for the raw value; collapsing it here would turn a linear ramp into the
// constant-speed sentinel.
void parseEasing(EasingSpec& target, const json& source, bool oneBasedType = false) {
  const auto easingType = std::max(0, integer(source, {"easingType", "easing", "ease"}, 1));
  target.type = oneBasedType ? easingType : std::max(0, easingType - 1);
  target.left = static_cast<float>(number(source, {"easingLeft", "left", "clipStart"}, 0.0));
  target.right = static_cast<float>(number(source, {"easingRight", "right", "clipEnd"}, 1.0));
  target.bezier = boolean(source, {"bezier", "useBezier"}, false);
  target.bezierPoints = {0.0F, 0.0F, 1.0F, 1.0F};
  if (const auto* points = array(source, {"bezierPoints", "BezierPoints", "points"}); points && points->size() >= 4) {
    for (std::size_t i = 0; i < 4; ++i) {
      if ((*points)[i].is_number()) target.bezierPoints[i] = (*points)[i].get<float>();
    }
  }
  if (target.bezier && target.bezierPoints.size() >= 4) {
    target.bezierLut = Easing::buildBezierLut(target.bezierPoints[0], target.bezierPoints[1],
        target.bezierPoints[2], target.bezierPoints[3]);
  }
}

template<class Event>
void ensureCoverage(std::vector<Event>& events, double fallback, bool extrapolate = true) {
  std::stable_sort(events.begin(), events.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  if (events.empty()) {
    Event event; event.endTime = EVENT_END; event.start = fallback; event.end = fallback;
    events.push_back(std::move(event));
  } else if (events.front().startTime > 0.0) {
    Event event; event.endTime = events.front().startTime;
    event.start = extrapolate ? events.front().start : fallback; event.end = event.start;
    events.insert(events.begin(), std::move(event));
  }
}

void ensureMoveCoverage(std::vector<MoveEvent>& events, double x = 0.0, double y = 0.0) {
  std::stable_sort(events.begin(), events.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  if (events.empty()) {
    MoveEvent event; event.endTime = EVENT_END; event.start = event.end = x;
    event.start2 = event.end2 = y; events.push_back(std::move(event));
  } else if (events.front().startTime > 0.0) {
    MoveEvent event; event.endTime = events.front().startTime;
    event.start = event.end = events.front().start;
    event.start2 = event.end2 = events.front().start2;
    events.insert(events.begin(), std::move(event));
  }
}

std::vector<LineEvent> parseLineEvents(const json& source, const BpmConverter& bpm,
    double timeBaseBpm, std::initializer_list<const char*> keys, bool alpha = false) {
  std::vector<LineEvent> result;
  const auto* events = array(source, keys);
  if (!events) return result;
  result.reserve(events->size());
  for (const auto& item : *events) {
    if (!item.is_object()) continue;
    LineEvent event;
    event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBaseBpm);
    event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBaseBpm);
    if (event.endTime < event.startTime) event.endTime = event.startTime;
    event.start = number(item, {"start", "startValue", "value", "v0"}, 0.0);
    event.end = number(item, {"end", "endValue", "v1"}, event.start);
    if (alpha) {
      auto normalize = [](double value) {
        if (value == -1.0) value = -255.0;
        else if (value == -2.0) value = -510.0;
        if (value > 1.5 || value < -2.5) value /= 255.0;
        return value;
      };
      event.start = normalize(event.start); event.end = normalize(event.end);
    }
    parseEasing(event, item);
    result.push_back(std::move(event));
  }
  return result;
}

std::vector<LineEvent> parseAlphaEvents(const json& source, const BpmConverter& bpm, double timeBaseBpm) {
  auto events = parseLineEvents(source, bpm, timeBaseBpm, {"alphaEvents", "opacityEvents"}, true);
  if (!events.empty()) return events;
  events = parseLineEvents(source, bpm, timeBaseBpm,
      {"judgeLineDisappearEvents", "disappearEvents"}, true);
  for (auto& event : events) { event.start = 1.0 - event.start; event.end = 1.0 - event.end; }
  return events;
}

std::array<double, 2> vec2(const json* value, std::array<double, 2> fallback) {
  if (!value || !value->is_array() || value->size() < 2) return fallback;
  try { return {(*value)[0].get<double>(), (*value)[1].get<double>()}; }
  catch (const json::exception&) { return fallback; }
}

std::vector<MoveEvent> parseMoveEvents(const json& source, const BpmConverter& bpm, double timeBaseBpm) {
  std::vector<MoveEvent> result;
  const auto* events = array(source, {"moveEvents", "judgeLineMoveEvents"});
  if (!events) return result;
  result.reserve(events->size());
  for (const auto& item : *events) {
    if (!item.is_object()) continue;
    MoveEvent event;
    event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBaseBpm);
    event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBaseBpm);
    if (event.endTime < event.startTime) event.endTime = event.startTime;
    // The vector form is taken only when both ends are arrays; otherwise every component is
    // read as a scalar, with the stage centre as the implicit start.
    const auto* startArray = field(item, {"start", "startValue"});
    const auto* endArray = field(item, {"end", "endValue"});
    double startX = 0.5;
    double startY = 0.5;
    double endX = 0.5;
    double endY = 0.5;
    if (startArray != nullptr && endArray != nullptr && startArray->is_array()
        && endArray->is_array() && startArray->size() >= 2 && endArray->size() >= 2) {
      const auto start = vec2(startArray, {0.5, 0.5});
      const auto end = vec2(endArray, start);
      startX = start[0]; startY = start[1]; endX = end[0]; endY = end[1];
    } else {
      startX = number(item, {"start", "startX", "x0"}, 0.5);
      endX = number(item, {"end", "endX", "x1"}, startX);
      startY = number(item, {"start2", "startY", "y0"}, 0.5);
      endY = number(item, {"end2", "endY", "y1"}, startY);
    }
    event.start = startX / RPE_WIDTH; event.end = endX / RPE_WIDTH;
    event.start2 = startY / RPE_HEIGHT; event.end2 = endY / RPE_HEIGHT;
    parseEasing(event, item); result.push_back(std::move(event));
  }
  std::stable_sort(result.begin(), result.end(),
      [](const MoveEvent& a, const MoveEvent& b) { return a.startTime < b.startTime; });
  return result;
}

RpeTrajectoryOptions trajectoryOptions(const json& source) {
  RpeTrajectoryOptions options;
  options.mode = stringValue(source, {"mode"}, options.mode);
  options.xExpression = stringValue(source, {"xExpression"}, options.xExpression);
  options.yExpression = stringValue(source, {"yExpression"}, options.yExpression);
  options.radiusExpression = stringValue(source, {"radiusExpression"}, options.radiusExpression);
  options.rotationExpression = stringValue(source, {"rotationExpression"});
  options.tangentRotation = boolean(source, {"tangentRotation"}, false);
  options.parameterStart = stringValue(source, {"parameterStart"}, options.parameterStart);
  options.parameterEnd = stringValue(source, {"parameterEnd"}, options.parameterEnd);
  options.angleStart = stringValue(source, {"angleStart"}, options.angleStart);
  options.angleEnd = stringValue(source, {"angleEnd"}, options.angleEnd);
  options.trimStart = number(source, {"trimStart"}, 0.0);
  options.trimEnd = number(source, {"trimEnd"}, 1.0);
  options.rotation = number(source, {"rotation"}, 0.0);
  options.scaleX = number(source, {"scaleX"}, 1.0);
  options.scaleY = number(source, {"scaleY"}, 1.0);
  options.alignStart = boolean(source, {"alignStart"}, false);
  options.startX = number(source, {"startX"}, 0.0);
  options.startY = number(source, {"startY"}, 0.0);
  options.easingX = integer(source, {"easingX"}, 1);
  options.easingY = integer(source, {"easingY"}, 1);
  options.seed = number(source, {"seed"}, 1.0);
  options.randomness = number(source, {"randomness"}, 0.0);
  options.shape = stringValue(source, {"shape"});
  if (!options.shape.empty() && options.shape != "star" && options.shape != "random-polygon") {
    throw std::invalid_argument("unsupported trajectory shape");
  }
  if (const auto* parameters = object(source, {"parameters"})) {
    for (const auto& [key, value] : parameters->items()) {
      if (value.is_number()) options.parameters[key] = value.get<double>();
    }
  }
  return options;
}

void attachTrajectories(const json& layerSource, JudgeLine::EventLayer& layer) {
  const auto* source = array(layerSource, {"moveXEvents", "judgeLineMoveXEvents"});
  if (source == nullptr) return;
  std::size_t index = 0;
  for (const auto& item : *source) {
    if (!item.is_object()) continue;
    if (index >= layer.judgeLineMoveXEvents.size()) break;
    auto& x = layer.judgeLineMoveXEvents[index++];
    const auto* trajectory = object(item, {"trajectory"});
    if (trajectory == nullptr) continue;
    if (integer(*trajectory, {"version"}, 1) != 1) {
      throw std::invalid_argument("unsupported trajectory version");
    }
    const auto* rawOptions = object(*trajectory, {"options"});
    if (rawOptions == nullptr) continue;
    auto compiled = std::make_shared<RpeTrajectory>(trajectoryOptions(*rawOptions));
    x.trajectory = compiled;
    x.trajectoryScale = 1.0 / RPE_WIDTH;
    layer.judgeLineMoveXEvents[index - 1] = x;
    auto y = x;
    y.trajectoryAxis = 1;
    y.trajectoryScale = 1.0 / RPE_HEIGHT;
    layer.judgeLineMoveYEvents.push_back(std::move(y));
    if (compiled->hasRotation()) {
      auto rotation = x;
      rotation.trajectoryAxis = 2;
      rotation.trajectoryScale = 1.0;
      layer.judgeLineRotateEvents.push_back(std::move(rotation));
    }
  }
}

double readyFlash(const json& source) {
  if (const auto* value = field(source, {"readyFlash"})) {
    if (value->is_boolean()) return value->get<bool>() ? 0.5 : 0.0;
    if (value->is_number()) return std::round(std::max(0.0, value->get<double>()) * 1000.0) / 1000.0;
  }
  return 0.5;
}

RpeNoiseTrack parseNoiseTrack(const json& source, const BpmConverter& bpm,
    std::initializer_list<const char*> keys) {
  RpeNoiseTrack track;
  const auto* events = array(source, keys);
  if (events == nullptr) return track;
  for (const auto& item : *events) {
    if (!item.is_object()) continue;
    RpeNoiseEvent event;
    event.startTime = bpm.beatToSec(parseBeat(eventTime(item, true)));
    event.endTime = bpm.beatToSec(parseBeat(eventTime(item, false)));
    event.start = number(item, {"start", "startValue", "v0"}, 0.0);
    event.end = number(item, {"end", "endValue", "v1"}, event.start);
    parseEasing(event, item);
    if (const auto* anchor = object(item, {"anchor"})) {
      event.hasAnchor = true;
      event.anchorX = number(*anchor, {"x"}, 0.0);
      event.anchorY = number(*anchor, {"y"}, 0.0);
    }
    if (const auto* transformStart = field(item, {"transformStart"})) {
      event.hasTransformStart = transformStart->is_number();
      event.transformStart = number(item, {"transformStart"}, 0.0);
    }
    if (const auto* transformEnd = field(item, {"transformEnd"})) {
      event.hasTransformEnd = transformEnd->is_number();
      event.transformEnd = number(item, {"transformEnd"}, 0.0);
    }
    event.transformStep = boolean(item, {"transformStep"}, false);
    track.events.push_back(std::move(event));
  }
  std::stable_sort(track.events.begin(), track.events.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  return track;
}

RpeNoiseLayer parseNoiseLayer(const json& source) {
  RpeNoiseLayer layer;
  layer.id = stringValue(source, {"id"}, "default");
  layer.name = stringValue(source, {"name"}, layer.id);
  layer.skin = stringValue(source, {"skin"});
  layer.channel = stringValue(source, {"channel"}, "normal");
  if (const auto* parameters = object(source, {"parameters"})) {
    for (const auto& [name, value] : parameters->items()) {
      if (value.is_number()) layer.parameters[name] = {value.get<float>()};
      else if (value.is_array()) {
        std::vector<float> values;
        for (const auto& component : value) {
          if (!component.is_number()) { values.clear(); break; }
          values.push_back(component.get<float>());
        }
        if (values.size() == 2 || values.size() == 4) layer.parameters[name] = std::move(values);
      }
    }
  }
  return layer;
}

constexpr std::array<const char*, 50> kShaderNames{
    "chromatic", "circle_blur", "fisheye", "glitch", "grayscale", "noise", "pixel",
    "radial_blur", "shockwave", "vignette", "chromatic_2", "circle_blur_2", "fisheye_2",
    "glitch_2", "grayscale_2", "noise_2", "pixel_2", "radial_blur_2", "shockwave_2",
    "vignette_2", "liquid", "flowing", "image_noise", "snow", "glow_effect", "flip",
    "night_vision", "scanline", "rain", "rainbow", "flare", "underwater", "fog",
    "kaleidoscope", "emboss", "sobel", "oil_painting", "distortion", "hologram", "burn",
    "camera", "lightning", "old_tv", "bokeh", "neon", "heat_distortion", "rays",
    "color_shift", "wave", "two_tone"};

int shaderIndexByName(std::string name) {
  if (name.size() > 1 && (name.front() == '/' || name.front() == '\\')) name.erase(name.begin());
  const auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name.erase(0, slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos) name.resize(dot);
  if (name.size() > 3 && name.compare(name.size() - 3, 3, "_pr") == 0) name.resize(name.size() - 3);
  std::string normalized;
  for (std::size_t index = 0; index < name.size(); ++index) {
    const unsigned char c = static_cast<unsigned char>(name[index]);
    if (std::isupper(c) && index > 0) normalized.push_back('_');
    normalized.push_back(static_cast<char>(std::tolower(c)));
  }
  for (std::size_t index = 0; index < normalized.size(); ++index) {
    if (normalized[index] == '-') normalized[index] = '_';
  }
  for (std::size_t index = 0; index < kShaderNames.size(); ++index) {
    if (normalized == kShaderNames[index]) return static_cast<int>(index);
  }
  return -1;
}

double effectBeatSeconds(const json* value, const BpmConverter& bpm, double factor) {
  if (value == nullptr || value->is_null()) return 0.0;
  return bpm.beatToSec(parseBeat(value)) * factor;
}

void parseEffectVariables(const json& vars, const BpmConverter& bpm, double factor,
    PrprEffect& effect) {
  for (const auto& [name, value] : vars.items()) {
    PrprVariable variable;
    if (value.is_number()) {
      variable.values = {value.get<float>()};
    } else if (value.is_array() && !value.empty() && value.front().is_number()) {
      variable.values.clear();
      for (const auto& component : value) {
        if (component.is_number()) variable.values.push_back(component.get<float>());
      }
      if (variable.values.empty()) variable.values.push_back(0.0F);
    } else if (value.is_array()) {
      for (const auto& item : value) {
        if (!item.is_object()) continue;
        PrprVariableEvent event;
        event.startTime = effectBeatSeconds(field(item, {"startTime"}), bpm, factor);
        event.endTime = effectBeatSeconds(field(item, {"endTime"}), bpm, factor);
        parseEasing(event, item);
        const auto* start = field(item, {"start", "startValue"});
        const auto* end = field(item, {"end", "endValue"});
        if (start != nullptr && start->is_array()) {
          event.count = static_cast<int>(std::min<std::size_t>(4, start->size()));
          for (int index = 0; index < event.count; ++index) {
            event.from[static_cast<std::size_t>(index)] = (*start)[static_cast<std::size_t>(index)].is_number()
                ? (*start)[static_cast<std::size_t>(index)].get<float>() : 0.0F;
            event.to[static_cast<std::size_t>(index)] = end != nullptr && end->is_array()
                && static_cast<std::size_t>(index) < end->size()
                && (*end)[static_cast<std::size_t>(index)].is_number()
                ? (*end)[static_cast<std::size_t>(index)].get<float>()
                : event.from[static_cast<std::size_t>(index)];
          }
        } else {
          event.from[0] = start != nullptr && start->is_number() ? start->get<float>() : 0.0F;
          event.to[0] = end != nullptr && end->is_number() ? end->get<float>() : event.from[0];
        }
        variable.vectorEvents.push_back(std::move(event));
      }
      std::stable_sort(variable.vectorEvents.begin(), variable.vectorEvents.end(),
          [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
      if (!variable.vectorEvents.empty()) {
        variable.values.assign(static_cast<std::size_t>(variable.vectorEvents.front().count), 0.0F);
        for (int index = 0; index < variable.vectorEvents.front().count; ++index) {
          variable.values[static_cast<std::size_t>(index)] =
              variable.vectorEvents.front().from[static_cast<std::size_t>(index)];
        }
      }
    }
    effect.variables[name] = std::move(variable);
  }
}

std::optional<PrprEffect> parseEffect(const json& source, const BpmConverter& bpm,
    int defaultLine, double lineFactor) {
  if (!source.is_object()) return std::nullopt;
  PrprEffect effect;
  effect.lineIndex = integer(source, {"line"}, defaultLine);
  effect.order = integer(source, {"order"}, 0);
  effect.global = boolean(source, {"global", "isGlobal"}, false);
  const auto* shader = field(source, {"shader", "shaderType", "name"});
  if (shader == nullptr) return std::nullopt;
  std::string name;
  if (shader->is_number()) {
    const int index = shader->get<int>();
    if (index < 0 || index >= static_cast<int>(kShaderNames.size())) return std::nullopt;
    effect.sourceName = index >= 20 ? std::string(kShaderNames[static_cast<std::size_t>(index)]) + "_pr.glsl"
                                   : kShaderNames[static_cast<std::size_t>(index)];
    effect.shader = kShaderNames[static_cast<std::size_t>(index)];
  } else if (shader->is_string()) {
    effect.sourceName = shader->get<std::string>();
    const int index = shaderIndexByName(effect.sourceName);
    if (index < 0) {
      effect.shader = effect.sourceName;
    } else {
      effect.shader = kShaderNames[static_cast<std::size_t>(index)];
    }
  } else return std::nullopt;
  effect.startTime = effectBeatSeconds(field(source, {"startTime", "start", "time1"}), bpm, lineFactor);
  effect.endTime = effectBeatSeconds(field(source, {"endTime", "end", "time2"}), bpm, lineFactor);
  if (effect.endTime <= effect.startTime) effect.endTime = std::numeric_limits<double>::infinity();
  if (const auto* vars = object(source, {"vars"})) parseEffectVariables(*vars, bpm, lineFactor, effect);
  return effect;
}

void appendShaderEffects(const json& root, Chart& chart, const BpmConverter& bpm) {
  const auto append = [&](const json& source, int defaultLine, double factor) {
    if (auto effect = parseEffect(source, bpm, defaultLine, factor)) {
      chart.prprEffects.push_back(std::move(*effect));
    }
  };
  for (const char* key : {"effects", "shaderEvents"}) {
    if (const auto* effects = array(root, {key})) {
      for (const auto& item : *effects) {
        const int line = integer(item, {"line"}, 0);
        const double factor = line >= 0 && static_cast<std::size_t>(line) < chart.judgeLineList.size()
            ? chart.judgeLineList[static_cast<std::size_t>(line)]->bpmfactor : 1.0;
        append(item, line, factor);
      }
    }
  }
  if (const auto* meta = object(root, {"META", "meta"})) {
    if (const auto* effects = array(*meta, {"effects"})) {
      for (const auto& item : *effects) append(item, 0, 1.0);
    }
  }
  for (std::size_t index = 0; index < chart.judgeLineList.size(); ++index) {
    const auto* lines = array(root, {"judgeLineList", "JudgeLineList", "judgeLines", "judgeLinesList", "lines"});
    if (lines == nullptr || index >= lines->size() || !(*lines)[index].is_object()) continue;
    const auto* extended = object((*lines)[index], {"extended", "Extended"});
    if (extended == nullptr) continue;
    if (const auto* paints = array(*extended, {"paintEvents"})) {
      for (const auto& item : *paints) {
        append(item, static_cast<int>(index), chart.judgeLineList[index]->bpmfactor);
      }
    }
  }
  std::stable_sort(chart.prprEffects.begin(), chart.prprEffects.end(),
      [](const PrprEffect& a, const PrprEffect& b) {
        return a.order != b.order ? a.order < b.order : a.startTime < b.startTime;
      });
}

std::vector<RpeNoiseArea> parseNoiseAreas(const json& root, const BpmConverter& bpm) {
  std::vector<RpeNoiseArea> result;
  const auto* list = array(root, {"blockAreaList", "BlockAreaList", "noiseAreas"});
  if (list == nullptr) return result;
  for (const auto& item : *list) {
    if (!item.is_object()) continue;
    const auto* bottomLeft = object(item, {"bottomLeft"});
    const auto* topRight = object(item, {"topRight"});
    if (bottomLeft == nullptr || topRight == nullptr) continue;
    RpeNoiseArea area;
    area.name = stringValue(item, {"Name", "name"});
    area.layerId = stringValue(item, {"layerId"}, "default");
    const double x0 = number(*bottomLeft, {"x"}, 0.0);
    const double y0 = number(*bottomLeft, {"y"}, 0.0);
    const double x1 = number(*topRight, {"x"}, 0.0);
    const double y1 = number(*topRight, {"y"}, 0.0);
    area.left = std::min(x0, x1);
    area.right = std::max(x0, x1);
    area.bottom = std::min(y0, y1);
    area.top = std::max(y0, y1);
    area.appear = bpm.beatToSec(parseBeat(field(item, {"appearTime"})));
    area.disappear = bpm.beatToSec(parseBeat(field(item, {"disappearTime"})));
    area.invert = boolean(item, {"isInvert"}, false);
    if (const auto* intervals = array(item, {"activeIntervals"})) {
      for (const auto& intervalSource : *intervals) {
        if (!intervalSource.is_object()) continue;
        const double originalStart = bpm.beatToSec(parseBeat(eventTime(intervalSource, true)));
        const double originalEnd = bpm.beatToSec(parseBeat(eventTime(intervalSource, false)));
        const double start = std::max(area.appear, originalStart);
        const double end = std::min(area.disappear, originalEnd);
        if (end > start || (originalStart == originalEnd && start == end)) {
          area.intervals.push_back({start, end, readyFlash(intervalSource)});
        }
      }
      std::stable_sort(area.intervals.begin(), area.intervals.end(),
          [](const auto& a, const auto& b) { return a.start < b.start; });
      std::vector<RpeNoiseInterval> merged;
      for (const auto& interval : area.intervals) {
        if (!merged.empty() && merged.back().start == interval.start) {
          merged.back().end = std::max(merged.back().end, interval.end);
          merged.back().readyFlash = std::max(merged.back().readyFlash, interval.readyFlash);
        } else merged.push_back(interval);
      }
      area.intervals = std::move(merged);
    }
    area.moveX = parseNoiseTrack(item, bpm, {"moveXEvents"});
    area.moveY = parseNoiseTrack(item, bpm, {"moveYEvents"});
    area.rotate = parseNoiseTrack(item, bpm, {"rotateEvents"});
    area.scaleX = parseNoiseTrack(item, bpm, {"scaleXEvents"});
    area.scaleY = parseNoiseTrack(item, bpm, {"scaleYEvents"});
    result.push_back(std::move(area));
  }
  return result;
}

void ensureSpeedCoverage(std::vector<SpeedEvent>& events) {
  std::stable_sort(events.begin(), events.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  if (events.empty()) {
    SpeedEvent event; event.endTime = EVENT_END; event.value = event.endValue = 1.0;
    events.push_back(std::move(event)); return;
  }
  std::vector<SpeedEvent> covered;
  if (events.front().startTime > 0.0) {
    SpeedEvent event; event.endTime = events.front().startTime;
    event.value = event.endValue = events.front().value; covered.push_back(std::move(event));
  }
  for (auto event : events) {
    if (!std::isfinite(event.endTime) || event.endTime < event.startTime) event.endTime = event.startTime;
    if (!std::isfinite(event.endValue)) event.endValue = event.value;
    if (!covered.empty() && covered.back().endTime < event.startTime) {
      SpeedEvent gap; gap.startTime = covered.back().endTime; gap.endTime = event.startTime;
      gap.value = gap.endValue = std::isfinite(covered.back().endValue)
          ? covered.back().endValue : covered.back().value;
      covered.push_back(std::move(gap));
    }
    covered.push_back(std::move(event));
  }
  if (covered.back().endTime < EVENT_END) {
    SpeedEvent tail; tail.startTime = covered.back().endTime; tail.endTime = EVENT_END;
    tail.value = tail.endValue = std::isfinite(covered.back().endValue)
        ? covered.back().endValue : covered.back().value;
    covered.push_back(std::move(tail));
  }
  events = std::move(covered);
}

// ---- Speed-layer merging -------------------------------------------------------------
//
// RPE speed layers each integrate independently, but some paths need one combined event
// list. The merge walks the union of every layer's boundary times, evaluates each layer's
// instantaneous speed at the boundaries, and re-expresses the sum as piecewise segments.
// Everything below reproduces that walk, including its two distinct boundary samplers.

double speedValueAtStart(const std::vector<SpeedEvent>& events, std::size_t& index, double time) {
  if (events.empty()) return 0.0;
  const auto size = events.size();
  if (index >= size) index = size - 1;
  while (index > 0) {
    const auto start = events[index].startTime;
    if (std::isfinite(start) && time < start) --index;
    else break;
  }
  while (index + 1 < size) {
    const auto start = events[index + 1].startTime;
    if (!std::isfinite(start) || time >= start) ++index;
    else break;
  }
  const auto& event = events[index];
  const auto start = event.startTime;
  const auto endValue = std::isfinite(event.endValue) ? event.endValue : event.value;
  if (std::isfinite(start) && time < start) return event.value;
  auto end = event.endTime;
  if (!std::isfinite(end)) end = start;
  if (time > end) return endValue;
  const auto duration = end - start;
  if (duration <= 1e-9) return endValue;
  const auto progress = std::clamp((time - start) / duration, 0.0, 1.0);
  if (event.type == 0) return event.value;
  if (event.type <= 1) return event.value + (endValue - event.value) * progress;
  // Higher easing types defer to the shared evaluator for the exact curve.
  return EventUtils::speedAt(events, time);
}

double speedValueBefore(const std::vector<SpeedEvent>& events, std::size_t& index, double time) {
  if (events.empty()) return 0.0;
  const auto size = events.size();
  if (index >= size) index = size - 1;
  while (index > 0) {
    const auto start = events[index].startTime;
    if (std::isfinite(start) && time <= start) --index;
    else break;
  }
  while (index + 1 < size) {
    const auto start = events[index + 1].startTime;
    if (!std::isfinite(start) || time > start) ++index;
    else break;
  }
  const auto& event = events[index];
  const auto start = event.startTime;
  const auto endValue = std::isfinite(event.endValue) ? event.endValue : event.value;
  if (std::isfinite(start) && time < start) return event.value;
  auto end = event.endTime;
  if (!std::isfinite(end)) end = start;
  if (time >= end) return endValue;
  const auto duration = end - start;
  if (duration <= 1e-9) return endValue;
  const auto progress = std::clamp((time - start) / duration, 0.0, 1.0);
  if (event.type == 0) return event.value;
  if (event.type <= 1) return event.value + (endValue - event.value) * progress;
  // Sample just left of the boundary so the segment that ends here is used.
  return EventUtils::speedAt(events, std::nextafter(time, -std::numeric_limits<double>::infinity()));
}

double speedAtLayersStart(const std::vector<std::vector<SpeedEvent>>& layers, double time) {
  double sum = 0.0;
  for (const auto& layer : layers) sum += EventUtils::speedAt(layer, time);
  return sum;
}

double speedAtLayersEnd(const std::vector<std::vector<SpeedEvent>>& layers, double time) {
  if (!std::isfinite(time)) return speedAtLayersStart(layers, time);
  auto left = std::nextafter(time, -std::numeric_limits<double>::infinity());
  if (!std::isfinite(left)) left = time;
  return speedAtLayersStart(layers, left);
}

bool hasSpeedBezier(const SpeedEvent& event) {
  if (!event.bezier) return false;
  return event.bezierLut.size() >= 2 || event.bezierPoints.size() >= 4;
}

double evalSpeedBezier(const SpeedEvent& event, double progress) {
  const auto p = Easing::clamp01(progress);
  if (event.bezierLut.size() >= 2) return Easing::lookupBezierLut(event.bezierLut, p);
  if (event.bezierPoints.size() >= 4) {
    return Easing::cubicBezierYForX(p, event.bezierPoints[0], event.bezierPoints[1],
        event.bezierPoints[2], event.bezierPoints[3]);
  }
  return p;
}

double speedEventProgress(const SpeedEvent& event, double time) {
  auto start = event.startTime;
  auto end = event.endTime;
  if (!std::isfinite(end)) end = start;
  const auto duration = end - start;
  if (!(duration > 1e-9)) return 1.0;
  return Easing::clamp01((time - start) / duration);
}

std::array<float, 2> subSpeedEasingWindow(const SpeedEvent& event, double q0, double q1) {
  auto left = std::isfinite(event.left) ? static_cast<double>(event.left) : 0.0;
  auto right = std::isfinite(event.right) ? static_cast<double>(event.right) : 1.0;
  left = Easing::clamp01(left);
  right = Easing::clamp01(right);
  if (left >= right) { left = 0.0; right = 1.0; }
  const auto subLeft = left + (right - left) * Easing::clamp01(q0);
  auto subRight = left + (right - left) * Easing::clamp01(q1);
  if (subRight < subLeft) subRight = subLeft;
  return {static_cast<float>(subLeft), static_cast<float>(subRight)};
}

std::vector<float> buildSpeedSubBezierLut(const SpeedEvent& event, double q0, double q1) {
  q0 = Easing::clamp01(q0);
  q1 = Easing::clamp01(q1);
  if (q1 <= q0 + 1e-9) return {};
  const auto y0 = evalSpeedBezier(event, q0);
  const auto y1 = evalSpeedBezier(event, q1);
  const auto denominator = y1 - y0;
  if (!std::isfinite(denominator) || std::abs(denominator) < 1e-9) return {};
  const auto count = Easing::BEZIER_LUT_SIZE;
  std::vector<float> lut(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    const auto p = static_cast<double>(i) / (count - 1);
    const auto q = q0 + (q1 - q0) * p;
    lut[static_cast<std::size_t>(i)] = static_cast<float>((evalSpeedBezier(event, q) - y0) / denominator);
  }
  return lut;
}

void addMergedSpeedEvent(std::vector<SpeedEvent>& out, double startTime, double endTime,
    double value, double endValue, int easingType, float easingLeft, float easingRight,
    bool bezier, const std::vector<float>& bezierPoints, const std::vector<float>& bezierLut) {
  if (!(endTime > startTime)) return;
  // A constant segment abutting an identical constant segment is folded into its neighbour.
  if (!out.empty()) {
    auto& last = out.back();
    if (std::abs(last.endTime - startTime) < 1e-6
        && std::abs(last.value - last.endValue) < 1e-9
        && std::abs(value - endValue) < 1e-9
        && std::abs(last.endValue - value) < 1e-9) {
      last.endTime = endTime;
      last.endValue = endValue;
      return;
    }
  }
  SpeedEvent event;
  event.startTime = startTime;
  event.endTime = endTime;
  event.value = value;
  event.endValue = endValue;
  event.type = std::abs(endValue - value) < 1e-9 ? 0 : easingType;
  event.left = easingLeft;
  event.right = easingRight;
  event.bezier = bezier;
  event.bezierPoints = bezierPoints;
  event.bezierLut = bezierLut;
  out.push_back(std::move(event));
}

void addMergedSpeedEventFromVarying(std::vector<SpeedEvent>& out,
    const std::vector<std::vector<SpeedEvent>>& layers, double t0, double t1,
    const SpeedEvent* varying) {
  const auto value = speedAtLayersStart(layers, t0);
  const auto endValue = speedAtLayersEnd(layers, t1);
  if (!varying || varying->type <= 1) {
    addMergedSpeedEvent(out, t0, t1, value, endValue,
        varying ? varying->type : 1,
        varying ? varying->left : 0.0F,
        varying ? varying->right : 1.0F,
        varying && varying->bezier,
        varying ? varying->bezierPoints : std::vector<float>{},
        varying ? varying->bezierLut : std::vector<float>{});
    return;
  }
  auto q0 = speedEventProgress(*varying, t0);
  auto q1 = speedEventProgress(*varying, t1);
  if (q1 < q0) q1 = q0;
  if (hasSpeedBezier(*varying)) {
    auto subLut = buildSpeedSubBezierLut(*varying, q0, q1);
    if (!subLut.empty()) {
      addMergedSpeedEvent(out, t0, t1, value, endValue, varying->type, 0.0F, 1.0F, true,
          std::vector<float>{}, std::move(subLut));
      return;
    }
  }
  const auto window = subSpeedEasingWindow(*varying, q0, q1);
  addMergedSpeedEvent(out, t0, t1, value, endValue, varying->type, window[0], window[1], false,
      std::vector<float>{}, std::vector<float>{});
}

void addSampledMergedSpeedEvents(std::vector<SpeedEvent>& out,
    const std::vector<std::vector<SpeedEvent>>& layers, double t0, double t1) {
  const auto duration = t1 - t0;
  if (!(duration > 0.0)) return;
  // Subdividing at 0.125s keeps the combined curve within a few percent of the layers.
  const auto segments = std::max(1, static_cast<int>(std::ceil(duration / 0.125)));
  for (int i = 0; i < segments; ++i) {
    const auto a = t0 + duration * i / segments;
    const auto b = t0 + duration * (i + 1) / segments;
    addMergedSpeedEvent(out, a, b, speedAtLayersStart(layers, a), speedAtLayersEnd(layers, b),
        1, 0.0F, 1.0F, false, std::vector<float>{}, std::vector<float>{});
  }
}

std::vector<SpeedEvent> mergeSpeedLayers(const std::vector<std::vector<SpeedEvent>>& layers) {
  if (layers.empty()) return {};
  if (layers.size() == 1) return layers.front();

  std::vector<double> times{0.0};
  double maxEnd = 0.0;
  for (const auto& layer : layers) {
    for (const auto& event : layer) {
      times.push_back(event.startTime);
      times.push_back(event.endTime);
      if (std::isfinite(event.endTime) && event.endTime > maxEnd) maxEnd = event.endTime;
    }
  }
  if (maxEnd < 1e8) maxEnd = 1e9;
  times.push_back(maxEnd);

  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end()), times.end());
  if (times.size() < 2) return layers.front();

  std::vector<SpeedEvent> out;
  std::vector<std::size_t> cursors(layers.size(), 0);
  for (std::size_t i = 0; i + 1 < times.size(); ++i) {
    const auto t0 = times[i];
    const auto t1 = times[i + 1];
    if (!(t1 > t0)) continue;
    double value = 0.0;
    double endValue = 0.0;
    for (std::size_t l = 0; l < layers.size(); ++l) {
      value += speedValueAtStart(layers[l], cursors[l], t0);
      endValue += speedValueBefore(layers[l], cursors[l], t1);
    }
    addMergedSpeedEvent(out, t0, t1, value, endValue,
        std::abs(value - endValue) < 1e-9 ? 0 : 1, 0.0F, 1.0F, false,
        std::vector<float>{}, std::vector<float>{});
  }
  std::stable_sort(out.begin(), out.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  return out;
}


void addSpeedEvent(std::vector<SpeedEvent>& out, double startTime, double endTime,
    double value, double endValue, int easingType, float easingLeft, float easingRight,
    bool bezier, const std::vector<float>& bezierPoints) {
  SpeedEvent event;
  event.startTime = startTime;
  event.endTime = endTime;
  event.value = value;
  event.endValue = endValue;
  event.type = std::abs(endValue - value) < 1e-9 ? 0 : easingType;
  event.left = easingLeft;
  event.right = easingRight;
  event.bezier = bezier;
  event.bezierPoints = bezierPoints;
  out.push_back(std::move(event));
}
// Legacy speed-height mode: some charts express speed as a height animation instead of a
// rate, so each layer is re-sampled at every boundary (plus every sign change) to become an
// equivalent piecewise-linear rate curve.
std::vector<SpeedEvent> buildLegacySpeedHeightLayer(const std::vector<SpeedEvent>& speedAnimation,
    double maxTime) {
  if (speedAnimation.empty()) return speedAnimation;
  if (!std::isfinite(maxTime) || maxTime <= 0.0) maxTime = 1e9;

  auto events = speedAnimation;
  std::stable_sort(events.begin(), events.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });

  std::vector<double> points{0.0, maxTime};
  for (const auto& event : events) {
    if (std::isfinite(event.startTime)) points.push_back(event.startTime);
    if (std::isfinite(event.endTime)) points.push_back(event.endTime);
  }
  std::sort(points.begin(), points.end());
  points.erase(std::unique(points.begin(), points.end()), points.end());

  // Insert the zero crossing of every monotone segment so a sign flip lands on a boundary.
  std::vector<double> refined = points;
  std::size_t cursor = 0;
  for (std::size_t i = 0; i + 1 < points.size(); ++i) {
    const auto t0 = points[i];
    const auto t1 = points[i + 1];
    if (!(t1 > t0)) continue;
    const auto v0 = speedValueAtStart(events, cursor, t0);
    const auto v1 = speedValueBefore(events, cursor, t1);
    if (v0 * v1 < 0.0) {
      const auto crossing = v0 / (v0 - v1);
      if (std::isfinite(crossing) && crossing > 0.0 && crossing < 1.0) {
        refined.push_back(t0 + (t1 - t0) * crossing);
      }
    }
  }
  std::sort(refined.begin(), refined.end());
  refined.erase(std::unique(refined.begin(), refined.end()), refined.end());

  std::vector<SpeedEvent> out;
  std::size_t buildCursor = 0;
  for (std::size_t i = 0; i + 1 < refined.size(); ++i) {
    const auto t0 = refined[i];
    const auto t1 = refined[i + 1];
    if (!(t1 > t0)) continue;
    const auto v0 = speedValueAtStart(events, buildCursor, t0);
    const auto v1 = speedValueBefore(events, buildCursor, t1);
    addSpeedEvent(out, t0, t1, v0, v1, std::abs(v0 - v1) < 1e-6 ? 0 : 1, 0.0F, 1.0F, false,
        std::vector<float>{});
  }
  return out.empty() ? speedAnimation : out;
}

void applyLegacySpeedHeightMode(std::vector<std::vector<SpeedEvent>>& speedLayers, double maxTime) {
  for (auto& layer : speedLayers) {
    if (layer.empty()) continue;
    layer = buildLegacySpeedHeightLayer(layer, maxTime);
  }
}
std::vector<SpeedEvent> parseSpeedEvents(const json& source, const BpmConverter& bpm, double timeBaseBpm) {
  std::vector<SpeedEvent> result;
  const auto* events = array(source, {"speedEvents", "judgeLineSpeedEvents", "speed"});
  if (!events) return result;
  result.reserve(events->size());
  for (const auto& item : *events) {
    if (!item.is_object()) continue;
    SpeedEvent event;
    event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBaseBpm);
    const auto* end = eventTime(item, false);
    event.endTime = end ? bpm.normBeat(parseBeat(end), timeBaseBpm)
                        : std::numeric_limits<double>::quiet_NaN();
    event.value = number(item, {"start", "startValue", "value", "speed", "v0", "s"}, 1.0) / 4.5;
    event.endValue = number(item, {"end", "endValue", "endSpeed", "v1"}, event.value * 4.5) / 4.5;
    parseEasing(event, item, true); result.push_back(std::move(event));
  }
  std::stable_sort(result.begin(), result.end(),
      [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (!std::isfinite(result[i].endTime)) result[i].endTime = i + 1 < result.size()
        ? result[i + 1].startTime : EVENT_END;
  }
  ensureSpeedCoverage(result);
  return result;
}

std::vector<JudgeLine::NoteControlPoint> parseControls(const json& line,
    std::initializer_list<const char*> keys, std::initializer_list<const char*> valueKeys,
    float fallback) {
  struct Raw { float x{}, value{}; int easing{}; };
  std::vector<Raw> raw;
  const auto* controls = array(line, keys);
  if (!controls) return {};
  for (const auto& item : *controls) {
    if (!item.is_object()) continue;
    double controlValue = fallback;
    if (const auto* found = field(item, valueKeys); found && found->is_number()) controlValue = found->get<double>();
    else controlValue = number(item, {"value", "v"}, fallback);
    raw.push_back({static_cast<float>(number(item, {"x", "X"}, 0.0)),
        static_cast<float>(controlValue), std::clamp(integer(item, {"easing", "ease", "easingType"}, 1) - 1, 0, 28)});
  }
  // The "no controls" sentinel is a two-entry ramp from x=0 to x>=10000 holding the default
  // throughout; it is recognised in authored order, before the list is ordered by x.
  if (raw.size() == 2 && std::abs(raw[0].x) < 1e-6F && raw[1].x >= 10000.0F
      && std::abs(raw[0].value - fallback) < 1e-6F && std::abs(raw[1].value - fallback) < 1e-6F) {
    return {};
  }
  std::stable_sort(raw.begin(), raw.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
  std::vector<JudgeLine::NoteControlPoint> result;
  result.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    result.push_back({raw[i].x, raw[i].value, i + 1 < raw.size() ? raw[i + 1].easing : 0});
  }
  return result;
}

std::array<float, 3> rgb(const json* value, bool& valid) {
  valid = false; std::array<float, 3> result{1.0F, 1.0F, 1.0F};
  if (!value || value->is_null()) return result;
  std::array<double, 3> components{0.0, 0.0, 0.0};
  bool parsed = false;
  try {
    if (value->is_array() && value->size() >= 3) {
      for (std::size_t i = 0; i < 3; ++i) components[i] = (*value)[i].get<double>();
      parsed = true;
    } else if (value->is_object()) {
      // Colours may be written as {r,g,b} objects; each channel accepts the same aliases.
      const json* channels[3] = {field(*value, {"r", "R", "red", "Red"}),
          field(*value, {"g", "G", "green", "Green"}),
          field(*value, {"b", "B", "blue", "Blue"})};
      if (channels[0] != nullptr && channels[1] != nullptr && channels[2] != nullptr) {
        for (std::size_t i = 0; i < 3; ++i) components[i] = channels[i]->get<double>();
        parsed = true;
      }
    }
  } catch (const json::exception&) {
    return result;
  }
  if (!parsed) return result;
  // The scale decision is taken once for the whole colour: a single component above 1 means
  // all three are byte-valued, never a per-channel mix.
  const bool byteValued =
      components[0] > 1.0 || components[1] > 1.0 || components[2] > 1.0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double scaled = byteValued ? components[i] / 255.0 : components[i];
    result[i] = std::clamp(static_cast<float>(scaled), 0.0F, 1.0F);
  }
  valid = true;
  return result;
}

// Zero-based type values shift into the one-based range; other values outside it are
// discarded rather than silently turned into a tap.
std::optional<Note> parseNote(const json& source, const BpmConverter& bpm, double timeBaseBpm) {
  Note note;
  auto type = integer(source, {"type", "noteType", "note"}, 0);
  // A zero-based spelling is only reinterpreted when the value is not already a valid
  // one-based type; a two keeps its hold meaning rather than shifting into a flick.
  if (type < 1 || type > 4) {
    if (type >= 0 && type <= 3) ++type;
  }
  if (type < 1 || type > 4) return std::nullopt;
  switch (type) {
    case 1: note.type = NOTE_TAP; break;
    case 2: note.type = NOTE_HOLD; break;
    case 3: note.type = NOTE_FLICK; break;
    case 4: note.type = NOTE_DRAG; break;
    default: return std::nullopt;
  }
  const auto globalBeat = parseBeat(field(source, {"time", "startTime", "t"}));
  note.rpeGlobalBeat = globalBeat; note.time = bpm.normBeat(globalBeat, timeBaseBpm);
  if (note.type == NOTE_HOLD) {
    auto endBeat = globalBeat;
    if (const auto* duration = field(source, {"holdTime"})) endBeat += parseBeat(duration);
    else if (const auto* end = field(source, {"endTime"})) endBeat = parseBeat(end);
    else endBeat += number(source, {"hold", "duration"}, 0.0) * 32.0;
    note.holdTime = (bpm.beatToSec(endBeat) - bpm.beatToSec(globalBeat))
        / (PGR_BEAT / timeBaseBpm);
  }
  note.speed = number(source, {"speed", "noteSpeed"}, 1.0);
  if (!std::isfinite(note.speed)) note.speed = 1.0;
  note.positionX = number(source, {"positionX", "x", "posX"}, 0.0) / (670.0 * (9.0 / 80.0));
  note.isFake = boolean(source, {"isFake", "fake"}, false);
  // `size` and `xScale` feed the same field; non-finite or non-positive falls back to 1.
  const auto widthScale = number(source, {"size", "width", "xScale"}, 1.0);
  note.size = 1.0F;
  note.xScale = static_cast<float>(std::isfinite(widthScale) && widthScale > 0.0 ? widthScale : 1.0);
  note.xScale = std::isfinite(note.xScale) && note.xScale > 0.0F ? note.xScale : 1.0F;
  auto alpha = number(source, {"alpha", "opacity"}, 1.0);
  if (!std::isfinite(alpha)) alpha = 1.0;
  // Byte-scaled alphas (0..255) are detected by magnitude and normalised before clamping.
  if (alpha > 1.5 || alpha < -2.5) alpha /= 255.0;
  if (alpha < 0.0) alpha = 0.0;
  if (alpha > 1.0) alpha = 1.0;
  note.alpha = static_cast<float>(alpha);
  note.visibleTime = static_cast<float>(number(source, {"visibleTime"}, -1.0));
  if (!std::isfinite(note.visibleTime) || note.visibleTime >= 999999.0F) note.visibleTime = -1.0F;
  const auto offsetY = number(source, {"yOffset", "offsetY"}, 0.0);
  note.yOffset = static_cast<float>((std::isfinite(offsetY) ? offsetY : 0.0) / RPE_HEIGHT);
  note.hitSound = stringValue(source, {"hitsound", "hitSound"});
  note.judgeArea = static_cast<float>(number(source,
      {"judgeArea", "judge_area", "JudgeArea", "judgementArea", "judgmentArea", "hitArea", "hitRange", "judgeRange"}, 1.0));
  if (!std::isfinite(note.judgeArea) || note.judgeArea <= 0.0F) note.judgeArea = 1.0F;
  bool valid{}; const auto tint = rgb(field(source, {"tint", "color"}), valid);
  if (valid) { note.hasTint = true; note.tintR = tint[0]; note.tintG = tint[1]; note.tintB = tint[2]; }
  bool hitValid{}; auto hit = rgb(field(source, {"hitEffectTint", "hitEffectsTint", "HEcolor"}), hitValid);
  if (!hitValid) hit = rgb(field(source, {"tintHitEffects"}), hitValid);
  if (hitValid) {
    note.hasHitEffectTint = true;
    note.hitEffectR = hit[0]; note.hitEffectG = hit[1]; note.hitEffectB = hit[2];
  } else if (note.hasTint && boolean(source, {"tintHitEffects", "hitEffectTint", "hitEffectsTint"}, false)) {
    // A bare tint switch on a tinted note reuses the note tint for its hit effect.
    note.hasHitEffectTint = true;
    note.hitEffectR = note.tintR; note.hitEffectG = note.tintG; note.hitEffectB = note.tintB;
  }
  return note;
}

bool noteAbove(const json& source, bool fallback) {
  if (field(source, {"isAbove"})) return boolean(source, {"isAbove"}, fallback);
  const auto* above = field(source, {"above"});
  if (above && above->is_number()) {
    const auto value = static_cast<int>(std::llround(above->get<double>()));
    if (value == 1) return true; if (value == 2) return false; return value != 0;
  }
  return boolean(source, {"above", "aboveLine"}, fallback);
}

void parseNotes(const json& source, const BpmConverter& bpm, double timeBaseBpm, JudgeLine& line) {
  // Presence of either explicit array selects the split form, even when it is empty: the
  // combined `notes` form below is only consulted when neither key exists at all.
  const auto* above = array(source, {"notesAbove", "NotesAbove"});
  const auto* below = array(source, {"notesBelow", "NotesBelow"});
  if (above != nullptr || below != nullptr) {
    if (above) for (const auto& item : *above) {
      if (!item.is_object()) continue;
      if (auto note = parseNote(item, bpm, timeBaseBpm)) line.notesAbove.push_back(std::move(*note));
    }
    if (below) for (const auto& item : *below) {
      if (!item.is_object()) continue;
      if (auto note = parseNote(item, bpm, timeBaseBpm)) line.notesBelow.push_back(std::move(*note));
    }
    return;
  }
  if (const auto* notes = array(source, {"notes", "noteList", "Notes"})) {
    for (const auto& item : *notes) {
      if (!item.is_object()) continue;
      auto note = parseNote(item, bpm, timeBaseBpm);
      if (!note) continue;
      if (noteAbove(item, true)) line.notesAbove.push_back(std::move(*note));
      else line.notesBelow.push_back(std::move(*note));
    }
  }
}

int attachUiId(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (value == "pause") return 1; if (value == "combonumber" || value == "combo_number") return 2;
  if (value == "combo") return 3; if (value == "score") return 4; if (value == "bar") return 5;
  if (value == "name") return 6; if (value == "level") return 7;
  try { return std::stoi(value); } catch (const std::exception&) { return 0; }
}

std::unique_ptr<JudgeLine> parseJudgeLine(const json& source, const BpmConverter& bpm,
                                          const std::filesystem::path& baseDirectory) {
  auto line = std::make_unique<JudgeLine>();
  line->bpmfactor = number(source, {"bpmfactor", "bpmFactor", "BPMFactor"}, 1.0);
  if (!std::isfinite(line->bpmfactor) || line->bpmfactor <= 0.0) line->bpmfactor = 1.0;
  line->bpm = bpm.baseBpm() / line->bpmfactor; line->bpmTimeline = bpm.timeline(); line->invertRotation = false;
  // Authored event and note times stay on the chart's time base. `line.bpm` only maps that
  // base onto chart seconds at runtime, so dividing it by the factor is what stretches the
  // whole line -- animations, notes and holds alike -- by that factor instead of cancelling
  // it out of both conversions.
  const double timeBase = bpm.baseBpm();
  line->zOrder = integer(source, {"zOrder", "z", "order"}, 0);
  line->isCover = boolean(source, {"isCover", "cover", "isCoverLine", "coverLine"}, false);
  line->father = integer(source, {"father", "parent", "fatherLine", "parentLine"}, -1);
  line->rotateWithFather = boolean(source, {"rotateWithFather", "rotWithFather", "rotateWithParent"}, false);
  const auto anchor = vec2(field(source, {"anchor", "Anchor"}), {0.5, 0.5});
  line->anchorX = static_cast<float>(anchor[0]); line->anchorY = static_cast<float>(anchor[1]);
  const auto attach = stringValue(source, {"attachUI", "attachUi", "attach_ui"});
  line->attachUiElementId = attach.empty() ? integer(source, {"attachUI", "attachUi", "attach_ui"}, 0) : attachUiId(attach);
  line->noteAlphaControls = parseControls(source, {"alphaControl"}, {"alpha"}, 1.0F);
  line->noteScaleControls = parseControls(source, {"sizeControl"}, {"size"}, 1.0F);
  line->noteXControls = parseControls(source, {"posControl"}, {"pos"}, 1.0F);
  line->noteYControls = parseControls(source, {"yControl"}, {"y"}, 1.0F);
  line->noteSkewControls = parseControls(source, {"skewControl"}, {"skew"}, 0.0F);
  line->noteJudgeAreaControls = parseControls(source,
      {"judgeAreaControl", "judge_area_control", "judgementAreaControl", "judgmentAreaControl", "judgeRangeControl", "hitAreaControl", "areaControl"},
      {"judgeArea", "judge_area", "JudgeArea", "judgementArea", "judgmentArea", "hitArea", "hitRange", "judgeRange", "area"}, 1.0F);
  const auto texture = stringValue(source, {"Texture", "texture"});
  if (!texture.empty() && texture != "line.png") line->texture = (baseDirectory / std::filesystem::path(texture)).string();

  const auto parseLayer = [&](const json& layerSource) {
    JudgeLine::EventLayer layer;
    layer.judgeLineRotateEvents = parseLineEvents(layerSource, bpm, timeBase,
        {"rotateEvents", "judgeLineRotateEvents", "rotationEvents", "rotEvents"});
    layer.judgeLineDisappearEvents = parseAlphaEvents(layerSource, bpm, timeBase);
    layer.judgeLineMoveXEvents = parseLineEvents(layerSource, bpm, timeBase,
        {"moveXEvents", "judgeLineMoveXEvents", "xEvents", "moveX"});
    layer.judgeLineMoveYEvents = parseLineEvents(layerSource, bpm, timeBase,
        {"moveYEvents", "judgeLineMoveYEvents", "yEvents", "moveY"});
    for (auto& event : layer.judgeLineMoveXEvents) { event.start /= RPE_WIDTH; event.end /= RPE_WIDTH; }
    for (auto& event : layer.judgeLineMoveYEvents) { event.start /= RPE_HEIGHT; event.end /= RPE_HEIGHT; }
    if (layer.judgeLineMoveXEvents.empty() && layer.judgeLineMoveYEvents.empty())
      layer.judgeLineMoveEvents = parseMoveEvents(layerSource, bpm, timeBase);
    attachTrajectories(layerSource, layer);
    ensureCoverage(layer.judgeLineRotateEvents, 0.0);
    ensureCoverage(layer.judgeLineDisappearEvents, 0.0, false);
    ensureCoverage(layer.judgeLineMoveXEvents, 0.0);
    ensureCoverage(layer.judgeLineMoveYEvents, 0.0);
    if (!layer.judgeLineMoveEvents.empty()) ensureMoveCoverage(layer.judgeLineMoveEvents);
    line->eventLayers.push_back(std::move(layer));
    auto speeds = parseSpeedEvents(layerSource, bpm, timeBase);
    if (!speeds.empty()) line->speedEventLayers.push_back(std::move(speeds));
  };
  if (const auto* layers = array(source, {"eventLayers", "EventLayers", "layers"}); layers && !layers->empty()) {
    for (const auto& layer : *layers) if (layer.is_object()) parseLayer(layer);
  }
  if (line->eventLayers.empty()) parseLayer(source);

  // The per-layer lists stay on the line so the runtime can integrate them exactly, but a
  // single combined curve is also built: consumers that only read one speed list then see
  // the same motion the layers produce together.
  line->speedEvents = mergeSpeedLayers(line->speedEventLayers);
  if (line->speedEvents.empty()) {
    SpeedEvent defaultSpeed;
    defaultSpeed.endTime = EVENT_END;
    defaultSpeed.value = defaultSpeed.endValue = 1.0;
    line->speedEvents.push_back(defaultSpeed);
  }

  if (const auto* extended = object(source, {"extended", "Extended"})) {
    line->judgeLineScaleXEvents = parseLineEvents(*extended, bpm, timeBase, {"scaleXEvents", "sxEvents"});
    line->judgeLineScaleYEvents = parseLineEvents(*extended, bpm, timeBase, {"scaleYEvents", "syEvents"});
    if (!line->judgeLineScaleXEvents.empty() || !line->judgeLineScaleYEvents.empty()) {
      ensureCoverage(line->judgeLineScaleXEvents, 1.0);
      ensureCoverage(line->judgeLineScaleYEvents, 1.0);
    } else if (const auto* scales = array(*extended, {"scaleEvents"})) {
      // Combined 2D scale track; only used when the per-axis lists are absent.
      for (const auto& item : *scales) if (item.is_object()) {
        MoveEvent event;
        event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBase);
        event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBase);
        const auto start = vec2(field(item, {"start", "startValue"}), {1.0, 1.0});
        const auto end = vec2(field(item, {"end", "endValue"}), start);
        event.start = start[0];
        event.start2 = start[1];
        event.end = end[0];
        event.end2 = end[1];
        parseEasing(event, item);
        line->judgeLineScaleEvents.push_back(std::move(event));
      }
      if (!line->judgeLineScaleEvents.empty()) ensureMoveCoverage(line->judgeLineScaleEvents, 1.0, 1.0);
    }
    line->judgeLineInclineEvents = parseLineEvents(*extended, bpm, timeBase,
        {"inclineEvents", "tiltEvents", "skewEvents"});
    for (auto& event : line->judgeLineInclineEvents) {
      event.start *= 3.14159265358979323846 / 180.0; event.end *= 3.14159265358979323846 / 180.0;
    }
    if (!line->judgeLineInclineEvents.empty()) ensureCoverage(line->judgeLineInclineEvents, 0.0);

    if (const auto* colors = array(*extended, {"colorEvents", "colourEvents", "tintEvents"})) {
      for (const auto& item : *colors) if (item.is_object()) {
        bool valid{}; auto startColor = rgb(field(item, {"start", "startValue", "rgb", "color"}), valid);
        if (!valid) continue; bool endValid{}; auto endColor = rgb(field(item, {"end", "endValue"}), endValid);
        ColorEvent event; event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBase);
        event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBase);
        const auto resolvedEnd = endValid ? endColor : startColor;
        event.startR = startColor[0]; event.startG = startColor[1]; event.startB = startColor[2];
        event.endR = resolvedEnd[0]; event.endG = resolvedEnd[1]; event.endB = resolvedEnd[2];
        parseEasing(event, item);
        line->judgeLineColorEvents.push_back(std::move(event));
      }
    }
    if (const auto* texts = array(*extended, {"textEvents", "labelEvents"})) {
      for (const auto& item : *texts) if (item.is_object()) {
        const auto start = stringValue(item, {"start", "startValue", "startText", "text", "value", "label"});
        if (start.empty()) continue; TextEvent event; event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBase);
        event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBase);
        event.text = start; event.endText = stringValue(item, {"end", "endValue", "endText"}, start);
        parseEasing(event, item); line->judgeLineTextEvents.push_back(std::move(event));
      }
    }
    if (const auto* gifs = array(*extended, {"gifEvents"})) {
      for (const auto& item : *gifs) if (item.is_object()) {
        GifEvent event; event.startTime = bpm.normBeat(parseBeat(eventTime(item, true)), timeBase);
        event.endTime = bpm.normBeat(parseBeat(eventTime(item, false)), timeBase);
        event.start = number(item, {"start", "startValue"}, 0.0);
        event.end = number(item, {"end", "endValue"}, 1.0); parseEasing(event, item);
        line->gifEvents.push_back(std::move(event));
      }
    }
  }
  const auto skew = number(source, {"skew", "Skew"}, 0.0) * 3.14159265358979323846 / 180.0;
  if (std::abs(skew) > 1e-9) {
    if (line->judgeLineInclineEvents.empty()) {
      LineEvent event; event.endTime = EVENT_END; event.start = event.end = skew;
      line->judgeLineInclineEvents.push_back(std::move(event));
    } else for (auto& event : line->judgeLineInclineEvents) { event.start += skew; event.end += skew; }
  }
  line->textureIsGif = line->texture.size() >= 4
      && line->texture.substr(line->texture.size() - 4) == ".gif";
  parseNotes(source, bpm, timeBase, *line);
  return line;
}

Chart parseRoot(const json& root, const std::filesystem::path& baseDirectory) {
  if (!root.is_object()) throw std::invalid_argument("RPE chart root must be an object");
  std::vector<BpmPoint> points;
  if (const auto* list = array(root, {"BPMList", "bpmList", "BpmList"})) {
    for (const auto& item : *list) if (item.is_object()) {
      const auto value = number(item, {"bpm", "BPM"}, 0.0);
      if (std::isfinite(value) && value > 0.0) points.push_back({parseBeat(field(item, {"startTime", "time", "beat"})), value});
    }
  }
  if (points.empty()) points.push_back({0.0, 120.0});
  std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
  if (points.front().beat > 0.0) points.insert(points.begin(), {0.0, points.front().bpm});
  BpmConverter bpm(std::move(points));
  Chart chart; chart.bpmTimeline = bpm.timeline();
  const auto* meta = object(root, {"META", "meta", "Meta"});
  chart.offset = number(meta ? *meta : root, {"offset", "Offset", "chartOffset"}, 0.0) / 1000.0;
  chart.rpeVersion = integer(meta ? *meta : root, {"RPEVersion", "rpeVersion", "version"}, 160);
  const auto* lines = array(root, {"judgeLineList", "JudgeLineList", "judgeLines", "judgeLinesList", "lines"});
  if (lines) {
    chart.judgeLineList.reserve(lines->size());
    for (const auto& item : *lines) if (item.is_object()) {
      auto line = parseJudgeLine(item, bpm, baseDirectory);
      if (field(item, {"rotateWithFather", "rotWithFather", "rotateWithParent"}) == nullptr) {
        line->rotateWithFather = chart.rpeVersion >= 163;
      }
      for (auto& note : line->notesAbove) {
        if (!note.hitSound.empty()) note.hitSound = (baseDirectory / note.hitSound).generic_string();
      }
      for (auto& note : line->notesBelow) {
        if (!note.hitSound.empty()) note.hitSound = (baseDirectory / note.hitSound).generic_string();
      }
      const auto attach = line->attachUiElementId;
      chart.judgeLineList.push_back(std::move(line));
      if (attach >= 1 && attach <= 7) chart.attachUiLineIndex[static_cast<std::size_t>(attach)] =
          static_cast<int>(chart.judgeLineList.size() - 1);
    }
  }
  chart.rpeNoiseAreas = parseNoiseAreas(root, bpm);
  if (const auto* layers = array(root, {"noiseAreaLayers"})) {
    for (const auto& item : *layers) {
      if (item.is_object()) chart.rpeNoiseLayers.push_back(parseNoiseLayer(item));
    }
  }
  if (chart.rpeNoiseLayers.empty()) {
    RpeNoiseLayer layer;
    layer.id = "default";
    layer.name = "default";
    chart.rpeNoiseLayers.push_back(std::move(layer));
  }
  if (const auto* options = object(root, {"noiseAreaOptions"})) {
    chart.rpeNoiseIgnoreTripleInversion = boolean(*options, {"ignoreTripleInversion"}, false);
  }
  if (const auto* global = field(root, {"globalEvents"})) {
    if (global->is_object()) chart.rpeGlobalEventsJson = global->dump();
  }
  appendShaderEffects(root, chart, bpm);
  chart.prepareRuntime();
  return chart;
}
} // namespace

game::Chart RpeChartParser::parse(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open RPE chart: " + path.string());
  json root; input >> root; return parseRoot(root, path.parent_path());
}

game::Chart RpeChartParser::parse(std::string_view text,
                                 const std::filesystem::path& baseDirectory) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
      && static_cast<unsigned char>(text[1]) == 0xBB
      && static_cast<unsigned char>(text[2]) == 0xBF) text.remove_prefix(3);
  return parseRoot(json::parse(text), baseDirectory);
}
} // namespace rechigros::chart
