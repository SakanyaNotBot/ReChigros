#include "rechigros/chart/OfficialChartParser.hpp"

#include "rechigros/game/EventUtils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

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
std::vector<game::Note> parseNotes(const json& object,bool above){
  std::vector<game::Note> result;const auto* source=array(object,{above?"notesAbove":"notesBelow"});if(!source)return result;result.reserve(source->size());
  for(const auto& item:*source){if(!item.is_object())continue;game::Note note;note.type=value<int>(item,{"type","Type"},1);note.isAbove=above;note.time=value<double>(item,{"time","Time"},0);note.holdTime=value<double>(item,{"holdTime","HoldTime"},0);note.positionX=value<double>(item,{"positionX","PositionX"},0);note.speed=value<double>(item,{"speed","Speed"},1);note.isFake=value<bool>(item,{"isFake","IsFake","fake","Fake"},false);note.alpha=value<float>(item,{"alpha","Alpha"},1);note.size=value<float>(item,{"size","Size"},1);note.xScale=value<float>(item,{"xScale","XScale"},1);note.yOffset=value<float>(item,{"yOffset","YOffset","offsetY"},0);note.visibleTime=value<float>(item,{"visibleTime","VisibleTime"},-1);note.judgeArea=value<float>(item,{"judgeArea","JudgeArea"},1);result.push_back(note);}
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

// Parses the official block areas (red zones). Each one is a percentage-space rectangle
// plus optional transform tracks.
std::vector<game::OfficialBlock> parseBlockAreas(const json& root) {
  std::vector<game::OfficialBlock> blocks;
  const auto* areas = array(root, {"blockAreaList", "BlockAreaList"});
  if (areas == nullptr) return blocks;
  blocks.reserve(areas->size());
  for (const auto& entry : *areas) {
    if (!entry.is_object()) continue;
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
    blocks.push_back(std::move(block));
  }
  return blocks;
}

game::Chart parseRoot(const json& root){
  if(!root.is_object())throw std::invalid_argument("Official chart root must be an object");game::Chart chart;chart.formatVersion=value<int>(root,{"formatVersion","FormatVersion"},3);if(chart.formatVersion!=1&&chart.formatVersion!=3)chart.formatVersion=3;chart.offset=value<double>(root,{"offset","Offset"},0);chart.blockAreaList=parseBlockAreas(root);const auto* lines=array(root,{"judgeLineList"});if(!lines){chart.prepareRuntime();return chart;}chart.judgeLineList.reserve(lines->size());int sourceIndex=0;for(const auto& item:*lines){if(item.is_object()){auto line=parseLine(item,chart.formatVersion,sourceIndex);const auto ui=line->attachUiElementId;chart.judgeLineList.push_back(std::move(line));if(ui>=1&&ui<=7)chart.attachUiLineIndex[static_cast<std::size_t>(ui)]=static_cast<int>(chart.judgeLineList.size()-1);}++sourceIndex;}chart.prepareRuntime();return chart;
}
}

game::Chart OfficialChartParser::parse(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot open chart: "+path.string());json root;stream>>root;return parseRoot(root);}
game::Chart OfficialChartParser::parse(std::string_view text){if(text.size()>=3&&static_cast<unsigned char>(text[0])==0xEF&&static_cast<unsigned char>(text[1])==0xBB&&static_cast<unsigned char>(text[2])==0xBF)text.remove_prefix(3);return parseRoot(json::parse(text));}
} // namespace rechigros::chart
