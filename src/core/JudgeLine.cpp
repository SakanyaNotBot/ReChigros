#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/Easing.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <random>
#include <thread>
#include <unordered_map>

namespace rechigros::game {
namespace {
constexpr double PI = 3.14159265358979323846;
template<class T> void sortEvents(std::vector<T>& events){ std::stable_sort(events.begin(),events.end(),[](const T&a,const T&b){return a.startTime<b.startTime;}); }
}

double JudgeLine::sec2beat(double second)const noexcept{return second/(PGR_BEAT/std::max(1e-9,bpm));}
double JudgeLine::globalSec2beat(double second)const noexcept{return bpmTimeline?bpmTimeline->secToBeat(second):sec2beat(second);}
double JudgeLine::beat2sec(double beat)const noexcept{return beat*(PGR_BEAT/std::max(1e-9,bpm));}
double JudgeLine::globalBeat2sec(double beat)const noexcept{return bpmTimeline?bpmTimeline->beatToSec(beat):beat2sec(beat);}
void JudgeLine::resolveFather(std::vector<std::unique_ptr<JudgeLine>>& lines)noexcept{fatherLine=father>=0&&static_cast<std::size_t>(father)<lines.size()?lines[static_cast<std::size_t>(father)].get():nullptr;}
void JudgeLine::mergeNotes(){
  notes.clear(); notes.reserve(notesAbove.size()+notesBelow.size());
  for(auto& note:notesAbove){note.isAbove=true;note.master=this;notes.push_back(note);}
  for(auto& note:notesBelow){note.isAbove=false;note.master=this;notes.push_back(note);}
  std::stable_sort(notes.begin(),notes.end(),[](const Note&a,const Note&b){return a.time<b.time;});
}
void JudgeLine::normalize(){
  for(auto& layer:eventLayers){sortEvents(layer.judgeLineRotateEvents);sortEvents(layer.judgeLineMoveXEvents);sortEvents(layer.judgeLineMoveYEvents);sortEvents(layer.judgeLineMoveEvents);sortEvents(layer.judgeLineDisappearEvents);}
  sortEvents(speedEvents); for(auto& layer:speedEventLayers)sortEvents(layer); sortEvents(judgeLineRotateEvents);sortEvents(judgeLineMoveEvents);sortEvents(judgeLineDisappearEvents);
  sortEvents(judgeLineScaleEvents);sortEvents(judgeLineScaleXEvents);sortEvents(judgeLineScaleYEvents);sortEvents(judgeLineInclineEvents);sortEvents(judgeLineColorEvents);sortEvents(judgeLineTextEvents);sortEvents(gifEvents);
  auto sortControls=[](auto& list){std::stable_sort(list.begin(),list.end(),[](const auto&a,const auto&b){return a.y<b.y;});};
  sortControls(noteAlphaControls);sortControls(noteScaleControls);sortControls(noteXControls);sortControls(noteYControls);sortControls(noteSkewControls);sortControls(noteJudgeAreaControls);
  EventUtils::initSpeedEvents(speedEvents);for(auto& layer:speedEventLayers)EventUtils::initSpeedEvents(layer);
}
void JudgeLine::initSpeedEventsPhi()noexcept{
  if(speedEvents.empty())return; const auto safeBpm=std::max(1e-9,bpm); float height=static_cast<float>(speedEvents.front().startTime/safeBpm*PGR_BEAT);
  for(auto& event:speedEvents){event.floorPosition=height;const auto delta=static_cast<float>((event.endTime-event.startTime)/safeBpm*PGR_BEAT);height+=delta*static_cast<float>(event.value);}
}
void JudgeLine::resetCursors()noexcept{
  curRotate_.reset();curMove1_.reset();curMove2_.reset();curDisappear_.reset();curScaleX_.reset();curScaleY_.reset();curScale1_.reset();curScale2_.reset();curIncline_.reset();curColor_.reset();curText_.reset();curGif_.reset();
  for(auto& l:eventLayers){l.curRotate.reset();l.curMoveX.reset();l.curMoveY.reset();l.curMove1.reset();l.curMove2.reset();l.curDisappear.reset();} lastStateTimeSec_=std::numeric_limits<double>::quiet_NaN();
}
JudgeLine::State JudgeLine::fillState(double second,float aspect){
  if(second==lastStateTimeSec_)return lastState_;
  const auto beat=sec2beat(second);
  State state;
  state.floorPosition=EventUtils::getFloorPosition(beat,speedEvents,speedEventLayers);

  float rotate=0.0F;
  float alpha=0.0F;
  // cx/cy are the judge-line centre in a stage-local, y-up space: 0 is the centre of the
  // play field, +1 the top edge. Every branch below produces that space.
  double cx=0.0,cy=0.0;

  if(!eventLayers.empty()){
    float rotateSum=0.0F;
    float alphaSum=0.0F;
    for(auto& layer:eventLayers){
      rotateSum+=static_cast<float>(EventUtils::getEventVal(beat,layer.judgeLineRotateEvents,layer.curRotate));
      // Layers carry move on either a split X/Y pair of 1D tracks or a packed 2D track. Both
      // report a centre offset directly here, so neither is shifted; a layer with any split
      // data resolves both axes from it, and an axis with no events contributes zero.
      const bool splitMove=!layer.judgeLineMoveXEvents.empty()||!layer.judgeLineMoveYEvents.empty();
      float lx=0.0F;
      float ly=0.0F;
      if(splitMove){
        lx=static_cast<float>(EventUtils::getEventVal(beat,layer.judgeLineMoveXEvents,layer.curMoveX));
        ly=static_cast<float>(EventUtils::getEventVal(beat,layer.judgeLineMoveYEvents,layer.curMoveY));
      }else if(!layer.judgeLineMoveEvents.empty()){
        lx=static_cast<float>(EventUtils::getMoveEventVal1(beat,layer.judgeLineMoveEvents,layer.curMove1));
        ly=static_cast<float>(EventUtils::getMoveEventVal2(beat,layer.judgeLineMoveEvents,layer.curMove2));
      }
      if(!std::isfinite(lx))lx=0.0F;
      if(!std::isfinite(ly))ly=0.0F;
      cx+=lx;
      cy+=ly;
      const float layerAlpha=static_cast<float>(EventUtils::getEventVal(beat,layer.judgeLineDisappearEvents,layer.curDisappear));
      alphaSum+=std::isfinite(layerAlpha)?layerAlpha:0.0F;
    }
    rotate=invertRotation?-rotateSum:rotateSum;
    alpha=alphaSum;
  }else{
    rotate=static_cast<float>(EventUtils::getEventVal(beat,judgeLineRotateEvents,curRotate_));
    if(invertRotation)rotate=-rotate;
    // The packed move stream carries normalised 0..1 positions; the centre offset is what
    // the parent transform below consumes.
    float x=static_cast<float>(EventUtils::getMoveEventVal1(beat,judgeLineMoveEvents,curMove1_));
    float y=static_cast<float>(EventUtils::getMoveEventVal2(beat,judgeLineMoveEvents,curMove2_));
    alpha=static_cast<float>(EventUtils::getEventVal(beat,judgeLineDisappearEvents,curDisappear_));
    if(!std::isfinite(x))x=0.5F;
    if(!std::isfinite(y))y=0.5F;
    cx=x-0.5;
    cy=y-0.5;
  }

  if(!std::isfinite(rotate))rotate=0.0F;
  if(!std::isfinite(alpha))alpha=0.0F;

  float sx=1.0F;
  float sy=1.0F;
  if(!judgeLineScaleXEvents.empty()||!judgeLineScaleYEvents.empty()){
    // A list that is present but empty resolves to 0, which is what the reference's own
    // evaluator returns for an absent stream; it is not a fallback to 1.
    sx=static_cast<float>(EventUtils::getEventVal(beat,judgeLineScaleXEvents,curScaleX_));
    sy=static_cast<float>(EventUtils::getEventVal(beat,judgeLineScaleYEvents,curScaleY_));
  }else if(!judgeLineScaleEvents.empty()){
    sx=static_cast<float>(EventUtils::getMoveEventVal1(beat,judgeLineScaleEvents,curScale1_));
    sy=static_cast<float>(EventUtils::getMoveEventVal2(beat,judgeLineScaleEvents,curScale2_));
  }
  if(!std::isfinite(sx))sx=1.0F;
  if(!std::isfinite(sy))sy=1.0F;

  float inclineSinr=std::numeric_limits<float>::quiet_NaN();
  if(!judgeLineInclineEvents.empty()){
    const double angle=EventUtils::getEventVal(beat,judgeLineInclineEvents,curIncline_);
    if(std::isfinite(angle))inclineSinr=static_cast<float>(std::sin(angle));
  }

  float r=PERFECT_COLOR[0];
  float g=PERFECT_COLOR[1];
  float b=PERFECT_COLOR[2];
  bool hasColorEvent=false;
  // A line that draws artwork, storyboard text or an attached UI element is tinted white so
  // the asset keeps its own colours.
  if(!texture.empty()||!text.empty()||!judgeLineTextEvents.empty()||attachUiElementId!=0)r=g=b=1.0F;
  if(!judgeLineColorEvents.empty()){
    std::array<float,3> eventColor{};
    if(EventUtils::getColorVal(beat,judgeLineColorEvents,curColor_,eventColor)){
      r=eventColor[0];g=eventColor[1];b=eventColor[2];hasColorEvent=true;
    }
  }

  std::string resolvedText=text;
  if(!judgeLineTextEvents.empty()&&!EventUtils::getTextVal(beat,judgeLineTextEvents,curText_,resolvedText)){
    resolvedText.clear();
  }
  float resolvedGifPosition=std::numeric_limits<float>::quiet_NaN();
  if(!gifEvents.empty())resolvedGifPosition=EventUtils::getGifVal(beat,gifEvents,curGif_);

  if(fatherLine!=nullptr){
    const auto parent=fatherLine->fillState(second,aspect);
    const double fx=static_cast<double>(parent.xNorm)-0.5;
    const double fy=0.5-static_cast<double>(parent.yNorm);
    const double radians=static_cast<double>(parent.rotateDeg)*PI/180.0;
    const double cosine=std::cos(radians);
    const double sine=std::sin(radians);
    double safeAspect=static_cast<double>(aspect);
    if(!std::isfinite(safeAspect)||safeAspect<=1e-6)safeAspect=16.0/9.0;
    // The child offset is expressed in stage units, so it is de-normalised by the stage
    // aspect before it is rotated in the parent's frame and re-normalised afterwards.
    const double nx=cx*cosine+(cy*sine)/safeAspect+fx;
    const double ny=cy*cosine-(cx*sine)*safeAspect+fy;
    cx=nx;
    cy=ny;
    if(rotateWithFather)rotate+=parent.rotateDeg;
  }

  state.rotateDeg=rotate;
  state.xNorm=static_cast<float>(cx+0.5);
  state.yNorm=static_cast<float>(0.5-cy);
  state.alpha=alpha;
  state.scaleX=sx;
  state.scaleY=sy;
  state.inclineSinr=inclineSinr;
  state.colorR=r;
  state.colorG=g;
  state.colorB=b;
  state.anchorX=anchorX;
  state.anchorY=anchorY;
  state.text=std::move(resolvedText);
  state.hasColorEvent=hasColorEvent;
  state.gifPosition=resolvedGifPosition;
  // A negative alpha in [-1,-2) is the authored "hide" encoding; other negatives only carry
  // their integer part as metadata for chart extensions.
  if(state.alpha<0.0F&&static_cast<int>(std::floor(-state.alpha))==1)state.hideLine=state.hideNotes=true;
  lastState_=state;lastStateTimeSec_=second;return state;
}
float JudgeLine::calcNoteControl(float height,int type,float fallback)const noexcept{
  const std::vector<NoteControlPoint>* list=nullptr; switch(type){case 0:list=&noteAlphaControls;break;case 1:list=&noteScaleControls;break;case 2:list=&noteXControls;break;case 3:list=&noteYControls;break;case 4:list=&noteSkewControls;break;case 5:list=&noteJudgeAreaControls;break;default:return fallback;}
  if(list->empty())return fallback;if(height<=list->front().y)return list->front().value;if(height>=list->back().y)return list->back().value;
  const auto it=std::lower_bound(list->begin(),list->end(),height,[](const auto& point,float value){return point.y<value;});const auto& b=*it;const auto& a=*(it-1);if(b.y-a.y<=1e-6F)return b.value;const auto p=(height-a.y)/(b.y-a.y);return a.value+(b.value-a.value)*static_cast<float>(Easing::apply(std::clamp(a.easing,0,28),p));
}

void Chart::updateUiTransform(int id,float x,float y,float r,float sx,float sy,float alpha){uiTransforms[id]={x,y,r,sx,sy,alpha};}
void Chart::prepareRuntime(){
  allNotesSorted.clear();clickEffectsSorted.clear();chartEndTimeSec=0;
  // The block runtime is part of the authored chart and may have been replaced since the
  // last preparation, so the lazily built window index restarts here.
  blockWindowOrder_.clear();blockWindowActive_.clear();blockWindowCursor_=0;
  blockWindowTimeSec_=-1.0e300;blockWindowReady_=false;
  const bool official=formatVersion>0;
  for(std::size_t i=0;i<judgeLineList.size();++i){auto& line=*judgeLineList[i];line.index=static_cast<int>(i);line.bpmTimeline=bpmTimeline;line.resolveFather(judgeLineList);}
  // Per-line runtime data, assembled on a worker thread and merged afterwards in chart order
  // so the final note order does not depend on scheduling.
  struct LineRuntimeData{
    std::vector<Note*> allNotes;
    std::vector<Note*> visibleNotes;
    std::unordered_map<long long,int> simultaneous;
    double maxEndSec{};
  };
  const auto prepareLine=[](JudgeLine& line,bool official,LineRuntimeData& out){
    line.normalize();line.mergeNotes();
    // Note times are sorted, so the floor-position lookup walks the speed events forward with
    // one cursor instead of binary-searching on every note. This is the incremental form the
    // reference uses for its runtime preparation.
    int speedIndex=0,holdEndIndex=0;std::vector<int> speedLayerIndices,holdEndLayerIndices;
    line.notesByType.assign(8,{});
    line.maxHoldDurationSec=0.0;
    for(auto& note:line.notes){
      note.master=&line;note.isHold=note.type==NOTE_HOLD;note.clicked=false;
      if(note.type>=0&&note.type<8)line.notesByType[static_cast<std::size_t>(note.type)].push_back(&note);
      note.floorPosition=EventUtils::floorPositionMonotonic(note.time,line.speedEvents,line.speedEventLayers,speedIndex,speedLayerIndices);
      note.sect=line.beat2sec(note.time);note.secht=line.beat2sec(note.holdTime);note.holdEndTime=note.sect+note.secht;note.useOfficialSpeed=official;
      if(note.isHold){
        if(official){note.holdEndFloorPosition=note.floorPosition+note.holdTime*note.speed*PGR_HEIGHT;note.holdLength=note.holdTime*note.speed*PGR_HEIGHT*(PGR_BEAT/line.bpm);}
        else{const auto endFloor=EventUtils::floorPositionMonotonic(note.time+note.holdTime,line.speedEvents,line.speedEventLayers,holdEndIndex,holdEndLayerIndices);note.holdEndFloorPosition=endFloor;note.holdLength=(endFloor-note.floorPosition)*note.speed*PGR_HEIGHT*(PGR_BEAT/line.bpm);}
        line.maxHoldDurationSec=std::max(line.maxHoldDurationSec,note.secht);
      }else{note.holdEndFloorPosition=note.floorPosition;note.holdLength=0;}
      ++out.simultaneous[static_cast<long long>(std::llround(note.sect*1'000'000.0))];out.allNotes.push_back(&note);
      if(!note.isFake)out.visibleNotes.push_back(&note);
      out.maxEndSec=std::max(out.maxEndSec,note.isHold?note.holdEndTime:note.sect);
    }
  };
  std::size_t noteCount=0;
  for(const auto& line:judgeLineList)if(line)noteCount+=line->notesAbove.size()+line->notesBelow.size();
  std::vector<LineRuntimeData> results(judgeLineList.size());
  const unsigned cores=std::max(1U,std::thread::hardware_concurrency());
  const bool parallel=cores>=2&&judgeLineList.size()>=2&&noteCount>=800;
  if(parallel){
    std::atomic<std::size_t> nextLine{0};
    const unsigned workers=std::min<unsigned>(cores,static_cast<unsigned>(judgeLineList.size()));
    std::vector<std::thread> threads;threads.reserve(workers);
    for(unsigned worker=0;worker<workers;++worker){
      threads.emplace_back([&](){
        for(;;){
          const auto index=nextLine.fetch_add(1);
          if(index>=judgeLineList.size())break;
          auto& line=*judgeLineList[index];
          prepareLine(line,official,results[index]);
        }
      });
    }
    for(auto& thread:threads)thread.join();
  }else{
    for(std::size_t index=0;index<judgeLineList.size();++index)prepareLine(*judgeLineList[index],official,results[index]);
  }
  std::vector<Note*> allNotes;
  std::unordered_map<long long,int> simultaneous;
  for(auto& result:results){
    allNotes.insert(allNotes.end(),result.allNotes.begin(),result.allNotes.end());
    allNotesSorted.insert(allNotesSorted.end(),result.visibleNotes.begin(),result.visibleNotes.end());
    chartEndTimeSec=std::max(chartEndTimeSec,result.maxEndSec);
    for(const auto& entry:result.simultaneous)simultaneous[entry.first]+=entry.second;
  }
  for(auto* note:allNotes)note->morebets=simultaneous[static_cast<long long>(std::llround(note->sect*1'000'000.0))]>1?1:0;
  std::stable_sort(allNotesSorted.begin(),allNotesSorted.end(),[](const Note*a,const Note*b){return a->sect<b->sect;});for(std::size_t i=0;i<allNotesSorted.size();++i)allNotesSorted[i]->sortedIndex=static_cast<int>(i);
  std::mt19937 random(std::random_device{}());clickEffectsSorted.reserve(allNotesSorted.size());for(auto* note:allNotesSorted)clickEffectsSorted.emplace_back(note,note->sect,random);std::stable_sort(clickEffectsSorted.begin(),clickEffectsSorted.end(),[](const auto&a,const auto&b){return a.timeSec<b.timeSec;});
  std::vector<Note*> flicks,tapHolds;for(auto* note:allNotes){note->nearNotes.clear();if(note->isFake)continue;if(note->type==NOTE_FLICK)flicks.push_back(note);else if(note->type==NOTE_TAP||note->type==NOTE_HOLD)tapHolds.push_back(note);}
  const auto connectNear=[](std::vector<Note*>& notes){std::stable_sort(notes.begin(),notes.end(),[](const Note*a,const Note*b){return a->sect<b->sect;});for(std::size_t i=0;i<notes.size();++i)for(std::size_t j=i+1;j<notes.size()&&notes[j]->sect-notes[i]->sect<=0.01;++j)notes[i]->nearNotes.push_back(notes[j]);};connectNear(flicks);connectNear(tapHolds);
}
void Chart::resetRuntime()noexcept{for(auto& line:judgeLineList){line->resetCursors();for(auto& note:line->notes){note.clicked=false;note.judgeResult=-1;note.judgeDiffSec=0;note.judgeTimeSec=std::numeric_limits<double>::quiet_NaN();note.preJudge=false;note.dragPrimed=false;note.holdActive=false;note.holdPerfect=false;note.holdPreJudge=false;note.holdFxActive=false;note.holdUpTimeSec=std::numeric_limits<double>::infinity();note.holdDiffSec=0;note.holdFxTimerSec=0.0;note.holdStatus=0;note.holdBroken=false;note.safeFrame=0;note.isJudged=false;note.badTimeMs=0;note.scored=false;note.statOffset=0;note.offsetIndicatorShown=false;note.judgementDetailOffsetRecorded=false;note.frameCount=0;}}}

bool Chart::hasLiveBlock(double time) const noexcept {
  if(blockAreaList.empty())return false;
  if(!blockWindowReady_||time<blockWindowTimeSec_){
    // First query, or a seek backwards: rebuild the appear-time order and the active set.
    blockWindowOrder_.resize(blockAreaList.size());
    for(std::size_t index=0;index<blockWindowOrder_.size();++index)blockWindowOrder_[index]=index;
    std::stable_sort(blockWindowOrder_.begin(),blockWindowOrder_.end(),[this](std::size_t first,std::size_t second){return blockAreaList[first].appearTime<blockAreaList[second].appearTime;});
    blockWindowActive_.clear();blockWindowCursor_=0;blockWindowReady_=true;
  }else if(time==blockWindowTimeSec_){
    return !blockWindowActive_.empty();
  }
  const float chartTime=static_cast<float>(time);
  while(blockWindowCursor_<blockWindowOrder_.size()
      &&blockAreaList[blockWindowOrder_[blockWindowCursor_]].appearTime<=chartTime){
    blockWindowActive_.push_back(blockWindowOrder_[blockWindowCursor_]);
    ++blockWindowCursor_;
  }
  std::erase_if(blockWindowActive_,[this,chartTime](std::size_t index){
    const auto& zone=blockAreaList[index];
    return chartTime<zone.appearTime||chartTime>=zone.disappearTime;
  });
  blockWindowTimeSec_=time;
  return !blockWindowActive_.empty();
}
} // namespace rechigros::game
