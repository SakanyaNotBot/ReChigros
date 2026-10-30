#include "rechigros/chart/OfficialChartParser.hpp"

#include "rechigros/game/EventUtils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rechigros::chart {
namespace {
using nlohmann::json;
template<class T> T value(const json& object,std::initializer_list<const char*> keys,T fallback){
  for(const auto* key:keys){const auto it=object.find(key);if(it==object.end()||it->is_null())continue;try{return it->get<T>();}catch(const json::exception&){} }
  return fallback;
}
const json* array(const json& object,std::initializer_list<const char*> keys){for(const auto* key:keys){const auto it=object.find(key);if(it!=object.end()&&it->is_array())return &*it;}return nullptr;}
game::LineEvent lineEvent(const json& source,double fallback){
  game::LineEvent event;event.startTime=value<double>(source,{"startTime","StartTime"},0);event.endTime=value<double>(source,{"endTime","EndTime"},event.startTime);event.start=value<double>(source,{"start","Start"},fallback);event.end=value<double>(source,{"end","End"},event.start);if(event.endTime<event.startTime)event.endTime=event.startTime;return event;
}
void ensureCoverage(std::vector<game::LineEvent>& events,double fallback){
  std::stable_sort(events.begin(),events.end(),[](const auto&a,const auto&b){return a.startTime<b.startTime;});
  if(events.empty()){game::LineEvent event;event.endTime=1e9;event.start=fallback;event.end=fallback;events.push_back(event);return;}
  if(events.front().startTime>0){game::LineEvent event;event.endTime=events.front().startTime;event.start=events.front().start;event.end=event.start;events.insert(events.begin(),event);}
}
std::vector<game::LineEvent> parseLineEvents(const json& object,std::initializer_list<const char*> keys,double fallback){
  std::vector<game::LineEvent> result;const auto* source=array(object,keys);if(!source)return result;result.reserve(source->size());for(const auto& item:*source)if(item.is_object())result.push_back(lineEvent(item,fallback));return result;
}
std::vector<game::LineEvent> parseMoveEvents(const json& object,bool x,int format){
  std::vector<game::LineEvent> result;const auto* source=array(object,{"judgeLineMoveEvents"});if(!source)return result;result.reserve(source->size());
  for(const auto& item:*source){if(!item.is_object())continue;game::LineEvent event;event.startTime=value<double>(item,{"startTime","StartTime"},0);event.endTime=value<double>(item,{"endTime","EndTime"},event.startTime);double start{},end{};
    if(format==1){const auto packedStart=static_cast<long long>(std::llround(value<double>(item,{"start","Start"},0)));const auto packedEnd=static_cast<long long>(std::llround(value<double>(item,{"end","End"},packedStart)));start=x?static_cast<double>(packedStart-packedStart%1000)/1000:static_cast<double>(packedStart%1000);end=x?static_cast<double>(packedEnd-packedEnd%1000)/1000:static_cast<double>(packedEnd%1000);start/=x?880.0:520.0;end/=x?880.0:520.0;
    }else{start=x?value<double>(item,{"start","Start"},0.5):value<double>(item,{"start2","Start2"},0.5);end=x?value<double>(item,{"end","End"},start):value<double>(item,{"end2","End2"},start);}
    event.start=start-0.5;event.end=end-0.5;if(event.endTime<event.startTime)event.endTime=event.startTime;result.push_back(event);
  }return result;
}
game::Note parseNote(const json& item,bool above){
  game::Note note;note.type=value<int>(item,{"type","Type"},1);note.isAbove=above;note.time=value<double>(item,{"time","Time"},0);note.holdTime=value<double>(item,{"holdTime","HoldTime"},0);note.positionX=value<double>(item,{"positionX","PositionX"},0);note.speed=value<double>(item,{"speed","Speed"},1);note.isFake=value<bool>(item,{"isFake","IsFake","fake","Fake"},false);note.alpha=value<float>(item,{"alpha","Alpha"},1);note.size=value<float>(item,{"size","Size"},1);note.xScale=value<float>(item,{"xScale","XScale"},1);note.yOffset=value<float>(item,{"yOffset","YOffset","offsetY"},0);note.visibleTime=value<float>(item,{"visibleTime","VisibleTime"},-1);note.judgeArea=value<float>(item,{"judgeArea","JudgeArea"},1);return note;
}
std::vector<game::Note> parseNotes(const json& object,bool above){
  std::vector<game::Note> result;const auto* source=array(object,{above?"notesAbove":"notesBelow"});if(!source)return result;result.reserve(source->size());
  for(const auto& item:*source)if(item.is_object())result.push_back(parseNote(item,above));
  return result;
}
std::unique_ptr<game::JudgeLine> parseLine(const json& object,int format,int index){
  auto line=std::make_unique<game::JudgeLine>();line->index=index;line->bpm=value<double>(object,{"bpm","BPM"},120);if(line->bpm<=0)line->bpm=120;line->invertRotation=false;line->isCover=true;line->notesAbove=parseNotes(object,true);line->notesBelow=parseNotes(object,false);
  game::JudgeLine::EventLayer layer;layer.judgeLineDisappearEvents=parseLineEvents(object,{"judgeLineDisappearEvents","disappearEvents"},0);layer.judgeLineMoveXEvents=parseMoveEvents(object,true,format);layer.judgeLineMoveYEvents=parseMoveEvents(object,false,format);layer.judgeLineRotateEvents=parseLineEvents(object,{"judgeLineRotateEvents","rotateEvents"},0);for(auto& event:layer.judgeLineRotateEvents){event.start=-event.start;event.end=-event.end;}ensureCoverage(layer.judgeLineDisappearEvents,0);ensureCoverage(layer.judgeLineMoveXEvents,0);ensureCoverage(layer.judgeLineMoveYEvents,0);ensureCoverage(layer.judgeLineRotateEvents,0);line->eventLayers.push_back(std::move(layer));
  if(const auto* speeds=array(object,{"speedEvents","SpeedEvents","speed"})){for(const auto& item:*speeds){if(!item.is_object())continue;game::SpeedEvent event;event.startTime=value<double>(item,{"startTime","StartTime"},0);event.endTime=value<double>(item,{"endTime","EndTime"},event.startTime+1e9);if(event.endTime<event.startTime)event.endTime=event.startTime;event.value=value<double>(item,{"value","Value","speed","Speed"},1);event.endValue=event.value;line->speedEvents.push_back(std::move(event));}}
  if(line->speedEvents.empty()){game::SpeedEvent event;event.endTime=1e9;event.value=event.endValue=1;line->speedEvents.push_back(std::move(event));}
  return line;
}
// Reads a nested {x,y} pair, falling back per component.
void readPoint(const json& source, std::initializer_list<const char*> keys, float& x, float& y,
    float fallbackX, float fallbackY) {
  const json* node = nullptr;
  for (const auto* key : keys) {
    const auto it = source.find(key);
    if (it != source.end() && it->is_object()) { node = &*it; break; }
  }
  if (node == nullptr) { x = fallbackX; y = fallbackY; return; }
  x = value<float>(*node, {"x", "X"}, fallbackX);
  y = value<float>(*node, {"y", "Y"}, fallbackY);
}

// Parses one official block area (red zone): a percentage-space rectangle plus optional
// transform tracks.
game::OfficialBlock parseBlockEntry(const json& entry) {
  game::OfficialBlock block;
  readPoint(entry, {"topRightPercentage", "TopRightPercentage"}, block.topRightX,
      block.topRightY, 1.0F, 1.0F);
    readPoint(entry, {"bottomLeftPercentage", "BottomLeftPercentage"}, block.bottomLeftX,
        block.bottomLeftY, 0.0F, 0.0F);
    block.appearTime = value<float>(entry, {"appearTime", "AppearTime"}, 0.0F);
    block.enableTime = value<float>(entry, {"enableTime", "EnableTime"}, 0.0F);
    block.disableTime = value<float>(entry, {"disableTime", "DisableTime"}, 3.402823466e+38F);
    block.disappearTime = value<float>(entry, {"disappearTime", "DisappearTime"}, 3.402823466e+38F);
    block.isSubtract = value<bool>(entry, {"isSubtract", "IsSubtract"}, false);

    if (const auto* rotates = array(entry, {"rotateEvents", "RotateEvents"})) {
      for (const auto& item : *rotates) {
        if (!item.is_object()) continue;
        game::OfficialBlock::RotateEvent event;
        readPoint(item, {"anchor", "Anchor"}, event.anchorX, event.anchorY, 0.5F, 0.5F);
        event.time = value<float>(item, {"time", "Time"}, 0.0F);
        event.easeType = value<int>(item, {"easeType", "EaseType"}, 0);
        event.rotation = value<float>(item, {"rotation", "Rotation"}, 0.0F);
        block.rotateEvents.push_back(event);
      }
      std::stable_sort(block.rotateEvents.begin(), block.rotateEvents.end(),
          [](const auto& first, const auto& second) { return first.time < second.time; });
    }
    if (const auto* moves = array(entry, {"moveEvents", "MoveEvents"})) {
      for (const auto& item : *moves) {
        if (!item.is_object()) continue;
        game::OfficialBlock::MoveEvent event;
        readPoint(item, {"endPosition", "EndPosition"}, event.posX, event.posY, 0.5F, 0.5F);
        event.time = value<float>(item, {"time", "Time"}, 0.0F);
        event.easeTypeX = value<int>(item, {"easeTypeX", "EaseTypeX"}, 0);
        event.easeTypeY = value<int>(item, {"easeTypeY", "EaseTypeY"}, 0);
        block.moveEvents.push_back(event);
      }
      std::stable_sort(block.moveEvents.begin(), block.moveEvents.end(),
          [](const auto& first, const auto& second) { return first.time < second.time; });
    }
    if (const auto* scales = array(entry, {"scaleEvents", "ScaleEvents"})) {
      for (const auto& item : *scales) {
        if (!item.is_object()) continue;
        game::OfficialBlock::ScaleEvent event;
        readPoint(item, {"anchor", "Anchor"}, event.anchorX, event.anchorY, 0.5F, 0.5F);
        event.time = value<float>(item, {"time", "Time"}, 0.0F);
        event.easeTypeX = value<int>(item, {"easeTypeX", "EaseTypeX"}, 0);
        event.easeTypeY = value<int>(item, {"easeTypeY", "EaseTypeY"}, 0);
        readPoint(item, {"scale", "Scale"}, event.scaleX, event.scaleY, 1.0F, 1.0F);
        block.scaleEvents.push_back(event);
      }
      std::stable_sort(block.scaleEvents.begin(), block.scaleEvents.end(),
          [](const auto& first, const auto& second) { return first.time < second.time; });
    }
  return block;
}

std::vector<game::OfficialBlock> parseBlockAreas(const json& root) {
  std::vector<game::OfficialBlock> blocks;
  const auto* areas = array(root, {"blockAreaList", "BlockAreaList"});
  if (areas == nullptr) return blocks;
  blocks.reserve(areas->size());
  for (const auto& entry : *areas) {
    if (!entry.is_object()) continue;
    blocks.push_back(parseBlockEntry(entry));
  }
  return blocks;
}

// --- raw-text slicing --------------------------------------------------------
// The parallel path splits the two large arrays out of the document text before parsing.
// The helpers below only need to understand JSON's lexical shape: strings (with escapes) and
// the bracket nesting.

bool skipJsonString(std::string_view text, std::size_t& index) {
  if (index >= text.size() || text[index] != '"') return false;
  ++index;
  while (index < text.size()) {
    const char character = text[index];
    if (character == '\\') {
      index += 2;
      continue;
    }
    if (character == '"') return true;
    ++index;
  }
  return false;
}

// Advances past one value; leaves `index` just after its last character.
bool skipJsonValue(std::string_view text, std::size_t& index) {
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }
  if (index >= text.size()) return false;
  if (text[index] == '"') {
    if (!skipJsonString(text, index)) return false;
    ++index;
    return true;
  }
  if (text[index] == '{' || text[index] == '[') {
    int depth = 0;
    while (index < text.size()) {
      const char character = text[index];
      if (character == '"') {
        if (!skipJsonString(text, index)) return false;
        ++index;
        continue;
      }
      if (character == '{' || character == '[') {
        ++depth;
      } else if (character == '}' || character == ']') {
        --depth;
        if (depth == 0) {
          ++index;
          return true;
        }
      }
      ++index;
    }
    return false;
  }
  // Number, true, false or null: runs up to the next structural character.
  while (index < text.size()) {
    const char character = text[index];
    if (character == ',' || character == ']' || character == '}' ||
        std::isspace(static_cast<unsigned char>(character)) != 0) {
      break;
    }
    ++index;
  }
  return true;
}

// Finds the value of one of `keys` at the root object level and returns its byte range.
bool findTopLevelValue(std::string_view text, std::initializer_list<std::string_view> keys,
    std::size_t& valueBegin, std::size_t& valueEnd) {
  std::size_t index = 0;
  int depth = 0;
  while (index < text.size()) {
    const char character = text[index];
    if (character == '"') {
      const std::size_t keyStart = index;
      if (!skipJsonString(text, index)) return false;
      const std::size_t keyEnd = index;
      ++index;
      if (depth == 1) {
        std::size_t cursor = index;
        while (cursor < text.size()
            && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
          ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ':') {
          ++cursor;
          const std::string_view key = text.substr(keyStart + 1, keyEnd - keyStart - 1);
          for (const std::string_view wanted : keys) {
            if (key != wanted) continue;
            std::size_t begin = cursor;
            while (begin < text.size()
                && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
              ++begin;
            }
            std::size_t end = begin;
            if (!skipJsonValue(text, end)) return false;
            valueBegin = begin;
            valueEnd = end;
            return true;
          }
        }
      }
      continue;
    }
    if (character == '{' || character == '[') {
      ++depth;
    } else if (character == '}' || character == ']') {
      if (depth == 0) return false;
      --depth;
    }
    ++index;
  }
  return false;
}

// Splits the elements of an array whose text is text[begin, end) into byte ranges.
bool splitJsonArray(std::string_view text, std::size_t begin, std::size_t end,
    std::vector<std::pair<std::size_t, std::size_t>>& elements) {
  if (begin >= end || text[begin] != '[' || text[end - 1] != ']') return false;
  const std::size_t closing = end - 1;
  std::size_t index = begin + 1;
  while (index < closing) {
    while (index < closing && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    if (index >= closing) break;
    const std::size_t elementBegin = index;
    if (!skipJsonValue(text, index)) return false;
    std::size_t elementEnd = index;
    while (elementEnd > elementBegin
        && std::isspace(static_cast<unsigned char>(text[elementEnd - 1])) != 0) {
      --elementEnd;
    }
    elements.emplace_back(elementBegin, elementEnd);
    while (index < closing && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
      ++index;
    }
    if (index < closing && text[index] == ',') {
      ++index;
      continue;
    }
    if (index < closing) return false;
  }
  return true;
}

std::string buildSkeleton(std::string_view text,
    std::vector<std::pair<std::size_t, std::size_t>> replacements) {
  std::sort(replacements.begin(), replacements.end(),
      [](const auto& first, const auto& second) { return first.first < second.first; });
  std::string skeleton;
  skeleton.reserve(text.size() / 32 + 1024);
  std::size_t cursor = 0;
  for (const auto& [begin, end] : replacements) {
    skeleton.append(text.substr(cursor, begin - cursor));
    skeleton += "[]";
    cursor = end;
  }
  skeleton.append(text.substr(cursor));
  return skeleton;
}

// Parses element slices on a worker pool, preserving index order in `results`.
template <typename Result, typename Convert>
void parseElementChunks(std::string_view text,
    const std::vector<std::pair<std::size_t, std::size_t>>& elements, unsigned workerCount,
    std::vector<Result>& results, std::vector<unsigned char>& state, Convert convert,
    std::atomic<bool>& failed) {
  if (elements.empty()) return;
  const std::size_t wantedChunks =
      std::max<std::size_t>(1, static_cast<std::size_t>(workerCount) * 4);
  const std::size_t chunkSize = (elements.size() + wantedChunks - 1) / wantedChunks;
  const std::size_t chunkCount = (elements.size() + chunkSize - 1) / chunkSize;
  std::atomic<std::size_t> nextChunk{0};
  const auto work = [&]() {
    for (;;) {
      const std::size_t chunk = nextChunk.fetch_add(1);
      if (chunk >= chunkCount) return;
      const std::size_t first = chunk * chunkSize;
      const std::size_t last = std::min(elements.size(), first + chunkSize);
      for (std::size_t index = first; index < last; ++index) {
        if (failed.load(std::memory_order_relaxed)) return;
        const auto [begin, end] = elements[index];
        const auto parsed = json::parse(text.substr(begin, end - begin), nullptr, false);
        if (parsed.is_discarded()) {
          failed.store(true, std::memory_order_relaxed);
          return;
        }
        if (!parsed.is_object()) {
          state[index] = 0;
          continue;
        }
        results[index] = convert(parsed, static_cast<int>(index));
        state[index] = 1;
      }
    }
  };
  const unsigned threads = static_cast<unsigned>(std::min<std::size_t>(
      std::max(2U, workerCount), std::max<std::size_t>(1, elements.size())));
  std::vector<std::thread> workers;
  workers.reserve(threads);
  for (unsigned worker = 0; worker < threads; ++worker) workers.emplace_back(work);
  for (auto& thread : workers) thread.join();
}

struct StreamingLine {
  std::pair<std::size_t, std::size_t> aboveRange{};
  std::pair<std::size_t, std::size_t> belowRange{};
};

void parseLinesStreaming(std::string_view text,
    const std::vector<std::pair<std::size_t, std::size_t>>& elements, int format,
    unsigned workerCount, std::vector<std::unique_ptr<game::JudgeLine>>& lines,
    std::vector<unsigned char>& state, std::atomic<bool>& failed) {
  std::vector<StreamingLine> ranges(elements.size());
  std::atomic<std::size_t> nextLine{0};
  const auto metadataWork = [&]() {
    for (;;) {
      const std::size_t index = nextLine.fetch_add(1);
      if (index >= elements.size()) return;
      if (failed.load(std::memory_order_relaxed)) return;
      const auto [lineBegin, lineEnd] = elements[index];
      const std::string_view lineText = text.substr(lineBegin, lineEnd - lineBegin);
      if (lineText.empty() || lineText.front() != '{') continue;
      std::size_t aboveBegin = 0, aboveEnd = 0, belowBegin = 0, belowEnd = 0;
      const bool hasAbove = findTopLevelValue(lineText, {"notesAbove"}, aboveBegin, aboveEnd)
          && aboveEnd > aboveBegin && lineText[aboveBegin] == '[';
      const bool hasBelow = findTopLevelValue(lineText, {"notesBelow"}, belowBegin, belowEnd)
          && belowEnd > belowBegin && lineText[belowBegin] == '[';
      std::vector<std::pair<std::size_t, std::size_t>> replacements;
      if (hasAbove) replacements.emplace_back(aboveBegin, aboveEnd);
      if (hasBelow) replacements.emplace_back(belowBegin, belowEnd);
      const auto root = json::parse(buildSkeleton(lineText, std::move(replacements)), nullptr, false);
      if (root.is_discarded() || !root.is_object()) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
      lines[index] = parseLine(root, format, static_cast<int>(index));
      if (hasAbove) ranges[index].aboveRange = {lineBegin + aboveBegin, lineBegin + aboveEnd};
      if (hasBelow) ranges[index].belowRange = {lineBegin + belowBegin, lineBegin + belowEnd};
      state[index] = 1;
    }
  };
  const unsigned metadataThreads = std::max(2U, std::min<unsigned>(workerCount,
      static_cast<unsigned>(std::max<std::size_t>(1, elements.size()))));
  std::vector<std::thread> metadataWorkers;
  metadataWorkers.reserve(metadataThreads);
  for (unsigned worker = 0; worker < metadataThreads; ++worker) {
    metadataWorkers.emplace_back(metadataWork);
  }
  for (auto& worker : metadataWorkers) worker.join();
  if (failed.load(std::memory_order_relaxed)) return;

  struct NoteSlice { game::Note* output{}; std::size_t begin{}, end{}; bool above{}; };
  std::vector<NoteSlice> notes;
  for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
    if (state[lineIndex] == 0) continue;
    const auto append = [&](const std::pair<std::size_t, std::size_t> range, bool above) {
      if (range.first == range.second) return;
      std::vector<std::pair<std::size_t, std::size_t>> elements;
      if (!splitJsonArray(text, range.first, range.second, elements)) {
        failed.store(true, std::memory_order_relaxed);
        return;
      }
      std::size_t count = 0;
      for (const auto& [begin, end] : elements) {
        if (begin < end && text[begin] == '{') ++count;
      }
      auto& destination = above ? lines[lineIndex]->notesAbove : lines[lineIndex]->notesBelow;
      destination.resize(count);
      std::size_t output = 0;
      for (const auto& [begin, end] : elements) {
        if (begin >= end || text[begin] != '{') continue;
        notes.push_back({&destination[output++], begin, end, above});
      }
    };
    append(ranges[lineIndex].aboveRange, true);
    append(ranges[lineIndex].belowRange, false);
  }
  if (failed.load(std::memory_order_relaxed) || notes.empty()) return;

  const std::size_t wantedChunks = static_cast<std::size_t>(workerCount) * 4;
  const std::size_t chunkSize = (notes.size() + wantedChunks - 1) / wantedChunks;
  const std::size_t chunkCount = (notes.size() + chunkSize - 1) / chunkSize;
  std::atomic<std::size_t> nextChunk{0};
  const auto noteWork = [&]() {
    for (;;) {
      const std::size_t chunk = nextChunk.fetch_add(1);
      if (chunk >= chunkCount) return;
      const std::size_t first = chunk * chunkSize;
      const std::size_t last = std::min(notes.size(), first + chunkSize);
      for (std::size_t index = first; index < last; ++index) {
        if (failed.load(std::memory_order_relaxed)) return;
        const auto& slice = notes[index];
        const auto root = json::parse(text.substr(slice.begin, slice.end - slice.begin), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
          failed.store(true, std::memory_order_relaxed);
          return;
        }
        *slice.output = parseNote(root, slice.above);
      }
    }
  };
  std::vector<std::thread> noteWorkers;
  const unsigned noteThreads = static_cast<unsigned>(std::min<std::size_t>(
      std::max(2U, workerCount), notes.size()));
  noteWorkers.reserve(noteThreads);
  for (unsigned worker = 0; worker < noteThreads; ++worker) noteWorkers.emplace_back(noteWork);
  for (auto& worker : noteWorkers) worker.join();
}

game::Chart parseRoot(const json& root){
  if(!root.is_object())throw std::invalid_argument("Official chart root must be an object");game::Chart chart;chart.formatVersion=value<int>(root,{"formatVersion","FormatVersion"},3);if(chart.formatVersion!=1&&chart.formatVersion!=3)chart.formatVersion=3;chart.offset=value<double>(root,{"offset","Offset"},0);chart.blockAreaList=parseBlockAreas(root);const auto* lines=array(root,{"judgeLineList"});if(!lines){chart.prepareRuntime();return chart;}chart.judgeLineList.reserve(lines->size());int sourceIndex=0;for(const auto& item:*lines){if(item.is_object()){auto line=parseLine(item,chart.formatVersion,sourceIndex);const auto ui=line->attachUiElementId;chart.judgeLineList.push_back(std::move(line));// An attached UI element records the authored line index, not the index after any
      // earlier non-object entries were skipped.
      if(ui>=1&&ui<=7)chart.attachUiLineIndex[static_cast<std::size_t>(ui)]=sourceIndex;}++sourceIndex;}chart.prepareRuntime();return chart;
}
}

game::Chart OfficialChartParser::parse(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot open chart: "+path.string());json root;stream>>root;return parseRoot(root);}
game::Chart OfficialChartParser::parse(std::string_view text){if(text.size()>=3&&static_cast<unsigned char>(text[0])==0xEF&&static_cast<unsigned char>(text[1])==0xBB&&static_cast<unsigned char>(text[2])==0xBF)text.remove_prefix(3);return parseRoot(json::parse(text));}

game::Chart OfficialChartParser::parseParallel(std::string_view text, unsigned workerCount) {
  std::string_view body = text;
  if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF
      && static_cast<unsigned char>(body[1]) == 0xBB
      && static_cast<unsigned char>(body[2]) == 0xBF) {
    body.remove_prefix(3);
  }
  const unsigned hardware = std::max(1U, std::thread::hardware_concurrency());
  const unsigned workers = workerCount == 0 ? std::min(8U, hardware) : workerCount;
  if (workers <= 1 || body.size() < (1U << 20)) return parse(text);

  std::size_t lineBegin = 0;
  std::size_t lineEnd = 0;
  std::size_t blockBegin = 0;
  std::size_t blockEnd = 0;
  const bool hasLines = findTopLevelValue(body, {"judgeLineList"}, lineBegin, lineEnd)
      && lineEnd > lineBegin && body[lineBegin] == '[';
  if (!hasLines) return parse(text);
  const bool hasBlocks =
      findTopLevelValue(body, {"blockAreaList", "BlockAreaList"}, blockBegin, blockEnd)
      && blockEnd > blockBegin && body[blockBegin] == '[';

  std::vector<std::pair<std::size_t, std::size_t>> lineElements;
  if (!splitJsonArray(body, lineBegin, lineEnd, lineElements) || lineElements.empty()) {
    return parse(text);
  }
  std::vector<std::pair<std::size_t, std::size_t>> blockElements;
  if (hasBlocks && !splitJsonArray(body, blockBegin, blockEnd, blockElements)) {
    return parse(text);
  }

  std::vector<std::pair<std::size_t, std::size_t>> replacements{{lineBegin, lineEnd}};
  if (hasBlocks) replacements.emplace_back(blockBegin, blockEnd);
  const std::string skeleton = buildSkeleton(body, replacements);
  const auto root = json::parse(skeleton, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return parse(text);

  game::Chart chart;
  chart.formatVersion = value<int>(root, {"formatVersion", "FormatVersion"}, 3);
  if (chart.formatVersion != 1 && chart.formatVersion != 3) chart.formatVersion = 3;
  chart.offset = value<double>(root, {"offset", "Offset"}, 0.0);

  std::atomic<bool> failed{false};
  std::vector<std::unique_ptr<game::JudgeLine>> lines(lineElements.size());
  std::vector<unsigned char> lineState(lineElements.size(), 0);
  parseLinesStreaming(body, lineElements, chart.formatVersion, workers, lines, lineState, failed);
  std::vector<game::OfficialBlock> blocks(blockElements.size());
  std::vector<unsigned char> blockState(blockElements.size(), 0);
  if (!blockElements.empty()) {
    parseElementChunks(body, blockElements, workers, blocks, blockState,
        [](const json& entry, int) { return parseBlockEntry(entry); }, failed);
  }
  if (failed.load(std::memory_order_relaxed)) return parse(text);

  chart.blockAreaList.reserve(blockElements.size());
  for (std::size_t index = 0; index < blocks.size(); ++index) {
    if (blockState[index] == 0) continue;
    chart.blockAreaList.push_back(std::move(blocks[index]));
  }
  chart.judgeLineList.reserve(lineElements.size());
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (lineState[index] == 0 || lines[index] == nullptr) continue;
    const auto ui = lines[index]->attachUiElementId;
    chart.judgeLineList.push_back(std::move(lines[index]));
    if (ui >= 1 && ui <= 7) {
      chart.attachUiLineIndex[static_cast<std::size_t>(ui)] =
          static_cast<int>(chart.judgeLineList.size() - 1);
    }
  }
  chart.prepareRuntime();
  return chart;
}
} // namespace rechigros::chart
