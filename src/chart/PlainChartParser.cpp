#include "rechigros/chart/PlainChartParser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rechigros::chart {
namespace {

using nlohmann::json;

// The binding below follows a reflective field binding: a key that is present overwrites
// the model's default, a missing or mistyped key leaves the default in place, and unknown
// keys are ignored. Numbers written as strings are accepted, and a boolean accepts a
// number or the strings "true"/"1", which is how the reference's lenient reader binds.
const json* member(const json& object, const char* key) {
  if (!object.is_object()) return nullptr;
  const auto iterator = object.find(key);
  if (iterator == object.end() || iterator->is_null()) return nullptr;
  return &*iterator;
}

void bindNumber(const json& object, const char* key, double& target) {
  const json* node = member(object, key);
  if (node == nullptr) return;
  if (node->is_number()) {
    target = node->get<double>();
    return;
  }
  if (node->is_boolean()) {
    target = node->get<bool>() ? 1.0 : 0.0;
    return;
  }
  if (node->is_string()) {
    try {
      std::size_t consumed = 0;
      const std::string text = node->get<std::string>();
      const double parsed = std::stod(text, &consumed);
      if (consumed > 0) target = parsed;
    } catch (...) {
    }
  }
}

void bindNumber(const json& object, const char* key, float& target) {
  double value = static_cast<double>(target);
  bindNumber(object, key, value);
  target = static_cast<float>(value);
}

void bindInteger(const json& object, const char* key, int& target) {
  const json* node = member(object, key);
  if (node == nullptr) return;
  if (node->is_number()) {
    target = static_cast<int>(node->get<double>());
    return;
  }
  if (node->is_boolean()) {
    target = node->get<bool>() ? 1 : 0;
    return;
  }
  if (node->is_string()) {
    try {
      target = std::stoi(node->get<std::string>());
    } catch (...) {
    }
  }
}

void bindBoolean(const json& object, const char* key, bool& target) {
  const json* node = member(object, key);
  if (node == nullptr) return;
  if (node->is_boolean()) {
    target = node->get<bool>();
    return;
  }
  if (node->is_number()) {
    target = node->get<double>() != 0.0;
    return;
  }
  if (node->is_string()) {
    std::string text = node->get<std::string>();
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (text == "true" || text == "1") target = true;
    else if (text == "false" || text == "0") target = false;
  }
}

void bindText(const json& object, const char* key, std::string& target) {
  const json* node = member(object, key);
  if (node == nullptr) return;
  if (node->is_string()) target = node->get<std::string>();
}

void bindEasing(const json& item, game::EasingSpec& spec) {
  bindInteger(item, "easingType", spec.type);
  bindNumber(item, "easingLeft", spec.left);
  bindNumber(item, "easingRight", spec.right);
  bindBoolean(item, "bezier", spec.bezier);
  if (const json* points = member(item, "bezierPoints");
      points != nullptr && points->is_array()) {
    spec.bezierPoints.clear();
    for (const auto& component : *points) {
      if (component.is_number()) spec.bezierPoints.push_back(component.get<float>());
    }
  }
}

void bindLineEvent(const json& item, game::LineEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindNumber(item, "start", event.start);
  bindNumber(item, "end", event.end);
  bindEasing(item, event);
}

void bindMoveEvent(const json& item, game::MoveEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindNumber(item, "start", event.start);
  bindNumber(item, "end", event.end);
  bindNumber(item, "start2", event.start2);
  bindNumber(item, "end2", event.end2);
  bindEasing(item, event);
}

void bindSpeedEvent(const json& item, game::SpeedEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindNumber(item, "value", event.value);
  // The model's default (NaN) means "constant at value", so only bind a present endValue.
  const json* endValue = member(item, "endValue");
  if (endValue != nullptr) {
    bindNumber(item, "endValue", event.endValue);
  }
  bindEasing(item, event);
}

void bindGifEvent(const json& item, game::GifEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindNumber(item, "start", event.start);
  bindNumber(item, "end", event.end);
  bindEasing(item, event);
}

void bindTextEvent(const json& item, game::TextEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindText(item, "text", event.text);
  event.hasEndText = member(item, "endText") != nullptr;
  bindText(item, "endText", event.endText);
  bindEasing(item, event);
}

void bindColorEvent(const json& item, game::ColorEvent& event) {
  bindNumber(item, "startTime", event.startTime);
  bindNumber(item, "endTime", event.endTime);
  bindNumber(item, "startR", event.startR);
  bindNumber(item, "startG", event.startG);
  bindNumber(item, "startB", event.startB);
  bindNumber(item, "endR", event.endR);
  bindNumber(item, "endG", event.endG);
  bindNumber(item, "endB", event.endB);
  bindEasing(item, event);
}

template <typename Event>
void bindEventList(const json& object, const char* key, std::vector<Event>& list,
    void (*bindOne)(const json&, Event&)) {
  const json* array = member(object, key);
  if (array == nullptr || !array->is_array()) return;
  list.reserve(array->size());
  for (const auto& item : *array) {
    if (!item.is_object()) continue;
    Event event{};
    bindOne(item, event);
    list.push_back(std::move(event));
  }
}

void bindNote(const json& item, game::Note& note) {
  bindInteger(item, "type", note.type);
  bindNumber(item, "time", note.time);
  bindNumber(item, "holdTime", note.holdTime);
  bindNumber(item, "speed", note.speed);
  bindNumber(item, "positionX", note.positionX);
  bindBoolean(item, "isFake", note.isFake);
  bindNumber(item, "size", note.size);
  bindNumber(item, "xScale", note.xScale);
  bindNumber(item, "alpha", note.alpha);
  bindNumber(item, "judgeArea", note.judgeArea);
  bindNumber(item, "visibleTime", note.visibleTime);
  bindNumber(item, "yOffset", note.yOffset);
  bindBoolean(item, "hasTint", note.hasTint);
  bindNumber(item, "tintR", note.tintR);
  bindNumber(item, "tintG", note.tintG);
  bindNumber(item, "tintB", note.tintB);
  bindBoolean(item, "hasHitEffectTint", note.hasHitEffectTint);
  bindNumber(item, "hitEffectR", note.hitEffectR);
  bindNumber(item, "hitEffectG", note.hitEffectG);
  bindNumber(item, "hitEffectB", note.hitEffectB);
  bindBoolean(item, "useOfficialSpeed", note.useOfficialSpeed);
}

void bindNotes(const json& object, const char* key, std::vector<game::Note>& notes) {
  const json* array = member(object, key);
  if (array == nullptr || !array->is_array()) return;
  notes.reserve(array->size());
  for (const auto& item : *array) {
    if (!item.is_object()) continue;
    game::Note note;
    bindNote(item, note);
    notes.push_back(note);
  }
}

std::unique_ptr<game::JudgeLine> bindLine(const json& item) {
  auto line = std::make_unique<game::JudgeLine>();
  // Start from the model defaults, then overwrite what the document declares. In
  // particular bpm keeps the plain model's zero default when absent, and invertRotation
  // keeps its plain-chart default of true; the official parser's overrides are not applied.
  line->bpm = 0.0;
  bindNumber(item, "bpm", line->bpm);
  bindNumber(item, "bpmfactor", line->bpmfactor);
  bindInteger(item, "zOrder", line->zOrder);
  bindInteger(item, "attachUiElementId", line->attachUiElementId);
  bindBoolean(item, "isCover", line->isCover);
  bindInteger(item, "father", line->father);
  bindBoolean(item, "rotateWithFather", line->rotateWithFather);
  bindBoolean(item, "invertRotation", line->invertRotation);
  bindText(item, "texture", line->texture);
  bindText(item, "text", line->text);
  bindBoolean(item, "rpeTextureScaleInPixels", line->rpeTextureScaleInPixels);
  bindBoolean(item, "useOfficialScale", line->useOfficialScale);
  bindBoolean(item, "textureIsGif", line->textureIsGif);
  bindNumber(item, "anchorX", line->anchorX);
  bindNumber(item, "anchorY", line->anchorY);

  if (const json* layers = member(item, "eventLayers");
      layers != nullptr && layers->is_array()) {
    line->eventLayers.reserve(layers->size());
    for (const auto& layerNode : *layers) {
      if (!layerNode.is_object()) continue;
      game::JudgeLine::EventLayer layer;
      bindEventList(layerNode, "judgeLineRotateEvents", layer.judgeLineRotateEvents, bindLineEvent);
      bindEventList(layerNode, "judgeLineMoveXEvents", layer.judgeLineMoveXEvents, bindLineEvent);
      bindEventList(layerNode, "judgeLineMoveYEvents", layer.judgeLineMoveYEvents, bindLineEvent);
      bindEventList(layerNode, "judgeLineMoveEvents", layer.judgeLineMoveEvents, bindMoveEvent);
      bindEventList(layerNode, "judgeLineDisappearEvents", layer.judgeLineDisappearEvents,
          bindLineEvent);
      line->eventLayers.push_back(std::move(layer));
    }
  }

  bindEventList(item, "speedEvents", line->speedEvents, bindSpeedEvent);
  bindEventList(item, "judgeLineRotateEvents", line->judgeLineRotateEvents, bindLineEvent);
  bindEventList(item, "judgeLineMoveEvents", line->judgeLineMoveEvents, bindMoveEvent);
  bindEventList(item, "judgeLineDisappearEvents", line->judgeLineDisappearEvents, bindLineEvent);
  bindEventList(item, "judgeLineScaleEvents", line->judgeLineScaleEvents, bindMoveEvent);
  bindEventList(item, "judgeLineScaleXEvents", line->judgeLineScaleXEvents, bindLineEvent);
  bindEventList(item, "judgeLineScaleYEvents", line->judgeLineScaleYEvents, bindLineEvent);
  bindEventList(item, "judgeLineInclineEvents", line->judgeLineInclineEvents, bindLineEvent);
  bindEventList(item, "judgeLineColorEvents", line->judgeLineColorEvents, bindColorEvent);
  bindEventList(item, "judgeLineTextEvents", line->judgeLineTextEvents, bindTextEvent);
  bindEventList(item, "gifEvents", line->gifEvents, bindGifEvent);
  bindNotes(item, "notesAbove", line->notesAbove);
  bindNotes(item, "notesBelow", line->notesBelow);
  return line;
}

game::Chart parseRoot(const json& root) {
  if (!root.is_object()) throw std::invalid_argument("plain chart root must be an object");
  game::Chart chart;
  // formatVersion keeps its zero default: that is what selects the non-official hold
  // geometry. A document may still declare it, in which case the declaration wins.
  bindInteger(root, "formatVersion", chart.formatVersion);
  bindNumber(root, "offset", chart.offset);
  bindBoolean(root, "useAttachUiFix", chart.useAttachUiFix);
  if (const json* indices = member(root, "attachUiLineIndex");
      indices != nullptr && indices->is_array()) {
    for (std::size_t index = 0; index < chart.attachUiLineIndex.size() && index < indices->size();
        ++index) {
      const auto& node = (*indices)[index];
      if (node.is_number()) {
        chart.attachUiLineIndex[index] = static_cast<int>(node.get<double>());
      }
    }
  }
  if (const json* lines = member(root, "judgeLineList"); lines != nullptr && lines->is_array()) {
    chart.judgeLineList.reserve(lines->size());
    for (const auto& item : *lines) {
      if (!item.is_object()) continue;
      auto line = bindLine(item);
      const int attach = line->attachUiElementId;
      chart.judgeLineList.push_back(std::move(line));
      if (attach >= 1 && attach <= 7) {
        chart.attachUiLineIndex[static_cast<std::size_t>(attach)] =
            static_cast<int>(chart.judgeLineList.size()) - 1;
      }
    }
  }
  chart.prepareRuntime();
  return chart;
}

} // namespace

game::Chart PlainChartParser::parse(std::string_view text) {
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text.remove_prefix(3);
  }
  return parseRoot(json::parse(text));
}

game::Chart PlainChartParser::parse(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot open chart: " + path.string());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string text = buffer.str();
  return parse(std::string_view(text));
}

} // namespace rechigros::chart
