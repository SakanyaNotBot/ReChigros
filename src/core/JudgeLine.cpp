#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/Chart.hpp"
#include "rechigros/game/Easing.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

namespace rechigros::game {
namespace {
template<class T> void sortEvents(std::vector<T>& events){ std::stable_sort(events.begin(),events.end(),[](const T&a,const T&b){return a.startTime<b.startTime;}); }
double progress(double time,double begin,double end){ return end<=begin?1.0:std::clamp((time-begin)/(end-begin),0.0,1.0); }
double eased(const EasingSpec& event,double p){
  if(event.bezier) return event.bezierLut.empty()?Easing::cubicBezierYForX(p,event.bezierPoints[0],event.bezierPoints[1],event.bezierPoints[2],event.bezierPoints[3]):Easing::lookupBezierLut(event.bezierLut,p);
  return Easing::clampedTween(Easing::rpeEasingToTweenId(event.type),p,event.left,event.right);
}
template<class T> const T* activeEvent(double time,const std::vector<T>& list,EventCursor& cursor){
  if(list.empty()) return nullptr;
  if(cursor.index>=list.size() || time<list[cursor.index].startTime) cursor.index=0;
  while(cursor.index+1<list.size() && time>=list[cursor.index+1].startTime) ++cursor.index;
  return &list[cursor.index];
}
double lineValue(double time,const std::vector<LineEvent>& list,EventCursor& cursor,double fallback){
  const auto* event=activeEvent(time,list,cursor); if(!event)return fallback;
  if(time<event->startTime)return fallback; if(time>=event->endTime)return event->end;
  const auto p=eased(*event,progress(time,event->startTime,event->endTime)); return event->start+(event->end-event->start)*p;
}
double moveValue(double time,const std::vector<MoveEvent>& list,EventCursor& cursor,double fallback,bool second){
  const auto* event=activeEvent(time,list,cursor); if(!event)return fallback;
  if(time<event->startTime)return fallback; const auto a=second?event->start2:event->start,b=second?event->end2:event->end;
  if(time>=event->endTime)return b; return a+(b-a)*eased(*event,progress(time,event->startTime,event->endTime));
}
const ColorEvent* colorValue(double time,const std::vector<ColorEvent>& list,EventCursor& cursor,std::array<float,3>& value){
  const auto* event=activeEvent(time,list,cursor);if(!event||time<event->startTime)return nullptr;
  const auto p=time>=event->endTime?1.0:eased(*event,progress(time,event->startTime,event->endTime));
  const float startChannel[3]={event->startR,event->startG,event->startB};
  const float endChannel[3]={event->endR,event->endG,event->endB};
  for(std::size_t i=0;i<value.size();++i)value[i]=startChannel[i]+(endChannel[i]-startChannel[i])*static_cast<float>(p);
  return event;
}
std::string textValue(double time,const std::vector<TextEvent>& list,EventCursor& cursor,const std::string& fallback){
  const auto* event=activeEvent(time,list,cursor);if(!event||time<event->startTime)return fallback;
  if(time>=event->endTime&&!event->endText.empty())return event->endText;
  return event->text;
}
float gifValue(double time,const std::vector<GifEvent>& list,EventCursor& cursor){
  const auto* event=activeEvent(time,list,cursor);if(!event||time<event->startTime)return std::numeric_limits<float>::quiet_NaN();
  if(time>=event->endTime)return static_cast<float>(event->end);
  const auto p=eased(*event,progress(time,event->startTime,event->endTime));return static_cast<float>(event->start+(event->end-event->start)*p);
}
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
  if(second==lastStateTimeSec_)return lastState_; const auto beat=sec2beat(second); State state; double cx=0,cy=0;
  if(!eventLayers.empty()){
    for(auto& layer:eventLayers){state.rotateDeg+=static_cast<float>(lineValue(beat,layer.judgeLineRotateEvents,layer.curRotate,0));cx+=lineValue(beat,layer.judgeLineMoveXEvents,layer.curMoveX,moveValue(beat,layer.judgeLineMoveEvents,layer.curMove1,0,false));cy+=lineValue(beat,layer.judgeLineMoveYEvents,layer.curMoveY,moveValue(beat,layer.judgeLineMoveEvents,layer.curMove2,0,true));state.alpha+=static_cast<float>(lineValue(beat,layer.judgeLineDisappearEvents,layer.curDisappear,0));}
    state.yNorm=static_cast<float>(0.5-cy);state.xNorm=static_cast<float>(0.5+cx);
  }else{
    state.rotateDeg=static_cast<float>(lineValue(beat,judgeLineRotateEvents,curRotate_,0));state.xNorm=static_cast<float>(moveValue(beat,judgeLineMoveEvents,curMove1_,0.5,false));state.yNorm=static_cast<float>(moveValue(beat,judgeLineMoveEvents,curMove2_,0.5,true));state.alpha=static_cast<float>(lineValue(beat,judgeLineDisappearEvents,curDisappear_,0));cx=state.xNorm-0.5;cy=0.5-state.yNorm;
  }
  if(invertRotation)state.rotateDeg=-state.rotateDeg;
  state.scaleX=static_cast<float>(lineValue(beat,judgeLineScaleXEvents,curScaleX_,moveValue(beat,judgeLineScaleEvents,curScale1_,1,false)));
  state.scaleY=static_cast<float>(lineValue(beat,judgeLineScaleYEvents,curScaleY_,moveValue(beat,judgeLineScaleEvents,curScale2_,1,true)));
  if(!judgeLineInclineEvents.empty())state.inclineSinr=static_cast<float>(std::sin(lineValue(beat,judgeLineInclineEvents,curIncline_,0)));
  state.colorR=PERFECT_COLOR[0];state.colorG=PERFECT_COLOR[1];state.colorB=PERFECT_COLOR[2];state.anchorX=anchorX;state.anchorY=anchorY;state.text=text;state.floorPosition=EventUtils::getFloorPosition(beat,speedEvents,speedEventLayers);
  if(!texture.empty()||!text.empty()||!judgeLineTextEvents.empty()||attachUiElementId!=0)state.colorR=state.colorG=state.colorB=1;
  std::array<float,3> eventColor{};if(colorValue(beat,judgeLineColorEvents,curColor_,eventColor)){state.colorR=eventColor[0];state.colorG=eventColor[1];state.colorB=eventColor[2];state.hasColorEvent=true;}
  state.text=textValue(beat,judgeLineTextEvents,curText_,state.text);state.gifPosition=gifValue(beat,gifEvents,curGif_);
  if(fatherLine){const auto parent=fatherLine->fillState(second,aspect);const auto radians=parent.rotateDeg*3.14159265358979323846/180.0;const auto safeAspect=aspect>1e-6F?aspect:16.0F/9.0F;const auto nx=cx*std::cos(radians)+(cy*std::sin(radians))/safeAspect+(parent.xNorm-0.5);const auto ny=cy*std::cos(radians)-(cx*std::sin(radians))*safeAspect+(0.5-parent.yNorm);state.xNorm=static_cast<float>(nx+0.5);state.yNorm=static_cast<float>(0.5-ny);if(rotateWithFather)state.rotateDeg+=parent.rotateDeg;}
  if(state.alpha<0 && static_cast<int>(std::floor(-state.alpha))==1)state.hideLine=state.hideNotes=true;
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
  std::vector<Note*> allNotes;
  std::unordered_map<long long,int> simultaneous;
  const bool official=formatVersion>0;
  for(std::size_t i=0;i<judgeLineList.size();++i){auto& line=*judgeLineList[i];line.index=static_cast<int>(i);line.bpmTimeline=bpmTimeline;line.resolveFather(judgeLineList);}
  for(auto& lineOwner:judgeLineList){
    auto& line=*lineOwner;line.normalize();line.mergeNotes();
    for(auto& note:line.notes){
      note.master=&line;note.isHold=note.type==NOTE_HOLD;note.clicked=false;
      note.floorPosition=EventUtils::getFloorPosition(note.time,line.speedEvents,line.speedEventLayers);
      note.sect=line.beat2sec(note.time);note.secht=line.beat2sec(note.holdTime);note.holdEndTime=note.sect+note.secht;note.useOfficialSpeed=official;
      if(note.isHold){
        if(official){note.holdEndFloorPosition=note.floorPosition+note.holdTime*note.speed*PGR_HEIGHT;note.holdLength=note.holdTime*note.speed*PGR_HEIGHT*(PGR_BEAT/line.bpm);}
        else{const auto endFloor=EventUtils::getFloorPosition(note.time+note.holdTime,line.speedEvents,line.speedEventLayers);note.holdEndFloorPosition=endFloor;note.holdLength=(endFloor-note.floorPosition)*note.speed*PGR_HEIGHT*(PGR_BEAT/line.bpm);}
      }else{note.holdEndFloorPosition=note.floorPosition;note.holdLength=0;}
      ++simultaneous[static_cast<long long>(std::llround(note.sect*1'000'000.0))];allNotes.push_back(&note);
      if(!note.isFake)allNotesSorted.push_back(&note);chartEndTimeSec=std::max(chartEndTimeSec,note.isHold?note.holdEndTime:note.sect);
    }
  }
  for(auto* note:allNotes)note->morebets=simultaneous[static_cast<long long>(std::llround(note->sect*1'000'000.0))]>1?1:0;
  std::stable_sort(allNotesSorted.begin(),allNotesSorted.end(),[](const Note*a,const Note*b){return a->sect<b->sect;});for(std::size_t i=0;i<allNotesSorted.size();++i)allNotesSorted[i]->sortedIndex=static_cast<int>(i);
  std::mt19937 random(std::random_device{}());clickEffectsSorted.reserve(allNotesSorted.size());for(auto* note:allNotesSorted)clickEffectsSorted.emplace_back(note,note->sect,random);std::stable_sort(clickEffectsSorted.begin(),clickEffectsSorted.end(),[](const auto&a,const auto&b){return a.timeSec<b.timeSec;});
  std::vector<Note*> flicks,tapHolds;for(auto* note:allNotes){note->nearNotes.clear();if(note->isFake)continue;if(note->type==NOTE_FLICK)flicks.push_back(note);else if(note->type==NOTE_TAP||note->type==NOTE_HOLD)tapHolds.push_back(note);}
  const auto connectNear=[](std::vector<Note*>& notes){std::stable_sort(notes.begin(),notes.end(),[](const Note*a,const Note*b){return a->sect<b->sect;});for(std::size_t i=0;i<notes.size();++i)for(std::size_t j=i+1;j<notes.size()&&notes[j]->sect-notes[i]->sect<=0.01;++j)notes[i]->nearNotes.push_back(notes[j]);};connectNear(flicks);connectNear(tapHolds);
}
void Chart::resetRuntime()noexcept{for(auto& line:judgeLineList){line->resetCursors();for(auto& note:line->notes){note.clicked=false;note.judgeResult=-1;note.judgeDiffSec=0;note.judgeTimeSec=std::numeric_limits<double>::quiet_NaN();note.preJudge=false;note.dragPrimed=false;note.holdActive=false;note.holdPerfect=false;note.holdPreJudge=false;note.holdFxActive=false;note.holdUpTimeSec=std::numeric_limits<double>::infinity();note.holdDiffSec=0;note.holdFxAtSec=std::numeric_limits<double>::quiet_NaN();note.holdStatus=0;note.holdBroken=false;note.safeFrame=0;note.isJudged=false;note.badTimeMs=0;note.scored=false;note.statOffset=0;note.offsetIndicatorShown=false;note.judgementDetailOffsetRecorded=false;note.frameCount=0;}}}
} // namespace rechigros::game
