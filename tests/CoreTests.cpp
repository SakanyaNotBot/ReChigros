#include "rechigros/game/BpmTimeline.hpp"
#include "rechigros/game/Easing.hpp"
#include "rechigros/game/GameConstants.hpp"
#include "rechigros/game/EventUtils.hpp"
#include "rechigros/game/HitOffsetIndicator.hpp"
#include "rechigros/game/JudgeLine.hpp"
#include "rechigros/game/JudgementEngine.hpp"
#include "rechigros/game/PracticeJudgeWindows.hpp"
#include "rechigros/game/GameplaySession.hpp"
#include "rechigros/game/NoteGeometry.hpp"
#include "rechigros/chart/PecChartParser.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char* message){if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
bool close(double a,double b,double epsilon=1e-5){return std::abs(a-b)<=epsilon;}
}

int main(){
  using namespace rechigros::game;
  const auto singleNoteChart=[](int type,double holdSeconds=0.0){Chart chart;chart.formatVersion=3;auto line=std::make_unique<JudgeLine>();line->bpm=120;SpeedEvent speed;speed.endTime=1e9;speed.value=speed.endValue=1;line->speedEvents.push_back(speed);Note note;note.type=type;note.time=64;note.holdTime=holdSeconds/(PGR_BEAT/120.0);note.positionX=0;line->notesAbove.push_back(note);chart.judgeLineList.push_back(std::move(line));chart.prepareRuntime();return chart;};
  require(close(HitOffsetIndicator::angleForDiff(-0.220,0.220),-60.0),"early offset angle");
  require(close(HitOffsetIndicator::angleForDiff(0.040,0.140),60.0*0.040/0.140),"challenge offset angle");
  require(HitOffsetIndicator::judgementForOffset(0.080,0.080,0.180,0.220)==HitOffsetIndicator::Judgement::Perfect,"perfect boundary");
  require(!HitOffsetIndicator::shouldShowCommitMark(NOTE_HOLD,false,HitOffsetIndicator::Judgement::Perfect),"hold tail mark suppression");
  require(HitOffsetIndicator::formatOffsetMs(-0.008)=="-008ms","offset formatting");

  BpmTimeline timeline({0,32},{120,240},{0,0.5});
  require(close(timeline.beatToSec(16),0.25),"beat to second");
  require(close(timeline.beatToSec(64),0.75),"BPM segment conversion");
  require(close(timeline.secToBeat(0.75),64),"second to beat");
  require(close(Easing::applyTween(2,0.25),0.25),"linear easing");
  require(close(Easing::cubicBezierYForX(0.5,0,0,1,1),0.5),"linear bezier");

  JudgeLine line;line.bpm=120;line.notesAbove.push_back(Note{.type=NOTE_TAP,.time=32});line.notesBelow.push_back(Note{.type=NOTE_FLICK,.time=16});line.mergeNotes();
  require(line.notes.size()==2&&line.notes.front().time==16&&!line.notes.front().isAbove,"note merge ordering");
  line.judgeLineMoveEvents.push_back(MoveEvent{.startTime=0,.endTime=32,.start=0.5,.end=1,.start2=0.5,.end2=0});
  line.judgeLineDisappearEvents.push_back(LineEvent{.startTime=0,.endTime=32,.start=0,.end=1});line.normalize();
  const auto state=line.fillState(0.25,16.0F/9.0F);require(close(state.xNorm,0.75),"line X interpolation");require(close(state.yNorm,0.25),"line Y interpolation");require(close(state.alpha,0.5),"line alpha interpolation");
  JudgeLine hidden;hidden.invertRotation=false;LineEvent alpha;alpha.startTime=0;alpha.endTime=100;alpha.start=-1;alpha.end=-1;hidden.judgeLineDisappearEvents.push_back(alpha);hidden.normalize();const auto hiddenState=hidden.fillState(0,16.0F/9.0F);require(hiddenState.hideLine&&hiddenState.hideNotes,"negative alpha extension");
  std::vector<SpeedEvent> speed(1);speed[0].startTime=0;speed[0].endTime=10;speed[0].value=1;speed[0].endValue=1;EventUtils::initSpeedEvents(speed);require(close(EventUtils::getFloorPosition(4,speed),4),"constant speed integral");
  const auto pec=rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1 128 1 0\n# 2\n& 1.5\nn2 0 2 3 0 0 0\ncv 0 0 7\ncp 0 0 1024 700\nca 0 0 255\n");
  require(close(pec.offset,-0.15),"PEC offset");require(pec.judgeLineList.size()==1,"PEC line count");require(pec.allNotesSorted.size()==2,"PEC note count");require(pec.allNotesSorted[0]->type==NOTE_TAP&&close(pec.allNotesSorted[0]->speed,2),"PEC note modifier");require(close(pec.allNotesSorted[0]->xScale,1.5),"PEC X scale modifier");require(close(pec.judgeLineList[0]->speedEvents[0].value,1),"PEC speed normalization");
  const auto pecVisible=rechigros::chart::PecChartParser::parse("0\nbp 0 120\nn1 0 1.125 0 1 0\ncf 0 1 1.125 -120\n");require(pecVisible.allNotesSorted.size()==1&&close(pecVisible.allNotesSorted[0]->visibleTime,1),"PEC visibleTime encoding");
  const auto pecHidden=rechigros::chart::PecChartParser::parse("0\nbp 0 120\ncf 0 0 1 -1\n");const auto pecHiddenState=pecHidden.judgeLineList[0]->fillState(0.5,16.0F/9.0F);require(close(pecHiddenState.alpha,-1)&&pecHiddenState.hideLine,"PEC negative alpha encoding");
  Chart runtime;runtime.formatVersion=3;auto runtimeLine1=std::make_unique<JudgeLine>();runtimeLine1->bpm=120;SpeedEvent runtimeSpeed;runtimeSpeed.endTime=1e9;runtimeSpeed.value=runtimeSpeed.endValue=1;runtimeLine1->speedEvents.push_back(runtimeSpeed);Note runtimeHold;runtimeHold.type=NOTE_HOLD;runtimeHold.time=32;runtimeHold.holdTime=32;runtimeHold.speed=2;runtimeLine1->notesAbove.push_back(runtimeHold);Note runtimeTap;runtimeTap.type=NOTE_TAP;runtimeTap.time=64;runtimeLine1->notesAbove.push_back(runtimeTap);Note runtimeFake=runtimeTap;runtimeFake.isFake=true;runtimeLine1->notesBelow.push_back(runtimeFake);auto runtimeLine2=std::make_unique<JudgeLine>();runtimeLine2->bpm=120;runtimeLine2->speedEvents.push_back(runtimeSpeed);runtimeLine2->notesAbove.push_back(runtimeTap);runtime.judgeLineList.push_back(std::move(runtimeLine1));runtime.judgeLineList.push_back(std::move(runtimeLine2));runtime.prepareRuntime();
  require(runtime.allNotesSorted.size()==3&&runtime.clickEffectsSorted.size()==3,"fake note runtime exclusion");require(close(runtime.allNotesSorted[0]->sect,0.5)&&close(runtime.allNotesSorted[0]->holdEndTime,1),"runtime note seconds");require(close(runtime.allNotesSorted[0]->holdEndFloorPosition,70.4)&&close(runtime.allNotesSorted[0]->holdLength,0.6),"official hold geometry");require(runtime.allNotesSorted[1]->morebets==1&&runtime.allNotesSorted[2]->morebets==1,"multi press marker");require(runtime.allNotesSorted[1]->nearNotes.size()==1,"near note competition");require(close(runtime.chartEndTimeSec,1),"chart end time");runtime.allNotesSorted[0]->clicked=true;runtime.resetRuntime();require(!runtime.allNotesSorted[0]->clicked,"runtime reset");
  JudgementEngine judge(4);Note jp,jg,jb,jm;jp.type=jg.type=jb.type=jm.type=NOTE_TAP;require(judge.commit(jp,JudgementEngine::Judgement::Perfect,0.010,1),"perfect commit");require(judge.commit(jg,JudgementEngine::Judgement::Good,-0.010,2),"good commit");require(judge.commit(jb,JudgementEngine::Judgement::Bad,0.2,3),"bad commit");require(judge.commit(jm,JudgementEngine::Judgement::Miss,0.25,4),"miss commit");require(!judge.commit(jp,JudgementEngine::Judgement::Miss,0,5),"duplicate commit protection");const auto play=judge.result();require(play.score==421250&&close(play.accuracy,41.25),"prpr score formula");require(play.maxCombo==2&&play.perfect==1&&play.good==1&&play.bad==1&&play.miss==1,"judgement counts");require(play.goodLate==1&&close(play.stdDevMs,10),"timing statistics");require(!play.isFullCombo()&&!play.isAllPerfect(),"result flags");
  require(JudgementEngine::checkNoteCandidateReplaces(true,NOTE_TAP,0.050,0.050,NOTE_HOLD,0.045,0.045,0.7,0.2),"tap hold candidate distance");require(!JudgementEngine::checkNoteCandidateReplaces(true,NOTE_TAP,0.050,0.050,NOTE_DRAG,-0.001,0.001,0.5,0.1),"drag candidate protection");require(close(JudgementEngine::lateMissLimitSec(NOTE_FLICK,{0.080,0.180,0.220}),0.140),"flick miss window");require(close(JudgementEngine::judgePositionScalePx(900,900,1600),90),"judge position scale");
  JudgementEngine::FlickTracker flick(0,0);const auto flickThreshold=JudgementEngine::flickJudgeSpeed(380);flick.move(0.16F,0);flick.advanceFrame(1.0F/60.0F,flickThreshold);require(!flick.newFlick(),"first flick frame");flick.move(0.32F,0);flick.advanceFrame(1.0F/60.0F,flickThreshold);require(flick.newFlick(),"two frame flick");
  GameplaySession::Viewport viewport{1600,900,16.0F/9.0F,1,380};
  auto tapChart=singleNoteChart(NOTE_TAP);GameplaySession tapSession;tapSession.setViewport(viewport);tapSession.setChart(&tapChart);tapSession.touchDown(1,800,450);tapSession.update(0.95,1.0F/60.0F);require(tapChart.allNotesSorted[0]->judgeResult==0&&tapSession.judgement().score()==1000000,"tap session perfect");const auto tapSounds=tapSession.takeSoundEvents();require(tapSounds.size()==1&&tapSounds[0]==NOTE_TAP,"tap sound event");require(tapSession.takeSoundEvents().empty(),"tap sound event drains once");
  // Drag on a fresh touch-down takes the CheckNote claim first: phase 2 sets isJudged for
  // every type except Flick, so the drag phase reads preJudge = isJudged = true and never
  // reaches the priming branch. Expect preJudge true, dragPrimed false, then a Perfect once
  // delta < 0.005. (note.sect = 64 * PGR_BEAT/120 = 1.0s)
  auto dragChart=singleNoteChart(NOTE_DRAG);GameplaySession dragSession;dragSession.setViewport(viewport);dragSession.setChart(&dragChart);dragSession.touchDown(1,800,450);dragSession.update(0.95,1.0F/60.0F);require(dragChart.allNotesSorted[0]->preJudge,"drag claimed via isJudged");require(!dragChart.allNotesSorted[0]->dragPrimed,"dragClaimed touch-down does not prime");dragSession.update(1.0,1.0F/60.0F);require(dragChart.allNotesSorted[0]->judgeResult==0,"drag session perfect");const auto dragSounds=dragSession.takeSoundEvents();require(dragSounds.size()==1&&dragSounds[0]==NOTE_DRAG,"drag sound event");
  // A finger already down when the note enters the priming window is the path that sets
  // dragPrimed: touch-down at 0.5s has delta 0.5 > BAD 0.22 so CheckNote skips the note.
  auto primeChart=singleNoteChart(NOTE_DRAG);GameplaySession primeSession;primeSession.setViewport(viewport);primeSession.setChart(&primeChart);primeSession.touchDown(1,800,450);primeSession.update(0.5,1.0F/60.0F);require(primeChart.allNotesSorted[0]->judgeResult<0,"drag not yet claimed");primeSession.update(0.95,1.0F/60.0F);require(primeChart.allNotesSorted[0]->dragPrimed&&primeChart.allNotesSorted[0]->preJudge,"held finger primes drag");primeSession.update(1.0,1.0F/60.0F);require(primeChart.allNotesSorted[0]->judgeResult==0,"primed drag perfect");
  auto flickChart=singleNoteChart(NOTE_FLICK);GameplaySession flickSession;flickSession.setViewport(viewport);flickSession.setChart(&flickChart);flickSession.touchDown(1,800,450);flickSession.update(0.90,1.0F/60.0F);flickSession.touchMove(1,814.4F,450);flickSession.update(0.92,1.0F/60.0F);flickSession.touchMove(1,828.8F,450);flickSession.update(0.94,1.0F/60.0F);require(flickChart.allNotesSorted[0]->preJudge,"flick prejudge");flickSession.update(1.0,1.0F/60.0F);require(flickChart.allNotesSorted[0]->judgeResult==0,"flick session perfect");const auto flickSounds=flickSession.takeSoundEvents();require(flickSounds.size()==1&&flickSounds[0]==NOTE_FLICK,"flick sound event");
  auto holdChart=singleNoteChart(NOTE_HOLD,1.0);GameplaySession holdSession;holdSession.setViewport(viewport);holdSession.setChart(&holdChart);holdSession.touchDown(1,800,450);holdSession.update(0.95,1.0F/60.0F);require(holdChart.allNotesSorted[0]->holdActive&&holdChart.allNotesSorted[0]->judgeResult<0,"hold head activation");const auto holdHeadSounds=holdSession.takeSoundEvents();require(holdHeadSounds.size()==1&&holdHeadSounds[0]==NOTE_TAP,"hold head tap sound event");holdSession.update(1.79,1.0F/60.0F);require(holdChart.allNotesSorted[0]->judgeResult==0,"hold tail settlement");require(holdSession.takeSoundEvents().empty(),"hold tail has no duplicate sound");
  auto brokenChart=singleNoteChart(NOTE_HOLD,1.0);GameplaySession brokenSession;brokenSession.setViewport(viewport);brokenSession.setChart(&brokenChart);brokenSession.touchDown(1,800,450);brokenSession.update(0.95,1.0F/60.0F);brokenSession.touchUp(1);for(int i=0;i<4;++i)brokenSession.update(1.05+i/60.0,1.0F/60.0F);require(brokenChart.allNotesSorted[0]->judgeResult==3,"hold safe frame miss");
  auto missChart=singleNoteChart(NOTE_TAP);GameplaySession missSession;missSession.setViewport(viewport);missSession.setChart(&missChart);missSession.update(1.19,1.0F/60.0F);require(missChart.allNotesSorted[0]->judgeResult==3,"automatic tap miss");require(missSession.takeSoundEvents().empty(),"miss has no sound event");
  auto areaChart=singleNoteChart(NOTE_TAP);areaChart.allNotesSorted[0]->judgeArea=2;GameplaySession areaSession;areaSession.setViewport(viewport);areaSession.setChart(&areaChart);require(close(areaSession.touchPosition(*areaChart.allNotesSorted[0],890,450,1.0),0.5),"spatial judge area scale");

  // --- Easing: 33 tween functions and their analytic integrals ---
  // Values derived by evaluating the reference formulas directly at the stated x.
  require(close(Easing::applyTween(0,0.5),0.0),"tween 0 is zero");
  require(close(Easing::applyTween(1,0.5),1.0),"tween 1 is one");
  require(close(Easing::applyTween(3,0.5),1.0-std::cos(0.25*3.14159265358979323846)),"sineIn");
  require(close(Easing::applyTween(6,0.5),0.25),"quadIn");
  require(close(Easing::applyTween(7,0.5),1.0-0.25),"quadOut");
  require(close(Easing::applyTween(9,0.5),0.125),"cubicIn");
  require(close(Easing::applyTween(12,0.5),0.0625),"quartIn");
  require(close(Easing::applyTween(15,0.5),0.03125),"quintIn");
  require(close(Easing::applyTween(18,0.5),std::pow(2.0,-5.0)),"expoIn");
  require(close(Easing::applyTween(21,0.5),1.0-std::sqrt(0.75)),"circIn");
  // backIn at 0.5: ((1.70158+1)*0.5 - 1.70158) * 0.25
  require(close(Easing::applyTween(24,0.5),((1.70158+1.0)*0.5-1.70158)*0.25),"backIn");
  // bounceOut(0.5): xm=0.5 -> 0.5 < 2/2.75, d = 0.5-1.5/2.75, 7.5625*d*d+0.75, negated.
  {const double d=0.5-1.5/2.75;require(close(Easing::applyTween(31,0.5),1.0-(7.5625*d*d+0.75)),"bounceOut");}
  // inOut halves the argument then maps the two sides.
  require(close(Easing::applyTween(8,0.25),0.125),"quadInOut lower half");
  require(close(Easing::applyTween(8,0.75),1.0-0.125),"quadInOut upper half");
  // Integrals: intQuadIn = x^3/3, intCubicIn = x^4/4, linear = x^2/2.
  require(close(Easing::applyTweenInt(2,0.5),0.125),"linear integral");
  require(close(Easing::applyTweenInt(6,0.5),std::pow(0.5,3)/3.0),"quadIn integral at 0.5 = 0.5^3/3");
  require(close(Easing::applyTweenInt(9,0.5),std::pow(0.5,4)/4.0),"cubicIn integral");
  require(close(Easing::applyTweenInt(1,0.5),0.5),"tween 1 integral is x");
  // Out-variant integral identity: intOut(x) = x + intIn(1-x) - intIn(1).
  require(close(Easing::applyTweenInt(7,0.5),0.5+std::pow(0.5,3)/3.0-std::pow(1.0,3)/3.0),"quadOut integral");
  // RPE easing ids map through the 30-entry table {2,2,4,3,7,6,5,8,10,9,13,12,11,14,16,15,19,18,22,21,...}.
  // rpeEasingToTweenId uses idx = type + 1; rpeRawEasingToTweenId uses idx = max(1, type).
  require(Easing::rpeEasingToTweenId(0)==2,"rpe id 0 -> linear (table[1])");
  require(Easing::rpeEasingToTweenId(1)==4,"rpe id 1 -> sineOut (table[2])");
  require(Easing::rpeEasingToTweenId(2)==3,"rpe id 2 -> sineIn (table[3])");
  require(Easing::rpeEasingToTweenId(28)==29,"rpe id 28 -> elasticInOut (table[29])");
  require(Easing::rpeEasingToTweenId(29)==2,"out-of-range rpe id falls back to linear");
  require(Easing::rpeRawEasingToTweenId(0)==2,"raw rpe id 0 -> linear");
  require(Easing::rpeRawEasingToTweenId(1)==2,"raw rpe id 1 -> linear");
  require(Easing::rpeRawEasingToTweenId(2)==4,"raw rpe id 2 -> sineOut");
  require(Easing::apply(0,0.25)==0.25,"apply(rpe 0) is linear");
  // clampedTween remaps [left,right] onto [0,1]; linear is unaffected by the remap.
  require(close(Easing::clampedTween(2,0.5,0.25,0.75),0.5),"clamped linear midpoint");
  // quadIn over [0,1] restricted to [0,0.5]: y = (E(0.5p)-E(0))/(E(0.5)-E(0)) = p^2.
  require(close(Easing::clampedTween(6,0.5,0.0,0.5),0.25),"clamped quadIn half range");
  require(close(Easing::intClampedTween(2,0.5,0.0,1.0),0.125),"intClampedTween linear");
  // Bezier: the identity control points short-circuit to x.
  require(close(Easing::cubicBezierYForX(0.37,0,0,1,1),0.37),"bezier identity");
  // Symmetric ease-in-out cubic bezier is 0.5 at 0.5.
  require(close(Easing::cubicBezierYForX(0.5,0.42,0.0,0.58,1.0),0.5),"bezier symmetric midpoint");
  {const auto lut=Easing::buildBezierLut(0.42F,0.0F,0.58F,1.0F,128);require(lut.size()==128,"bezier lut size");
   require(close(Easing::lookupBezierLut(lut,0.5),0.5,2e-3),"bezier lut midpoint");}
  require(close(Easing::clamp01(-3.0),0.0)&&close(Easing::clamp01(7.0),1.0),"clamp01 range");

  // --- JudgementEngine windows and per-type rules ---
  {JudgementEngine normal(10,false);const auto w=normal.windows();
   require(close(w.perfect,0.080)&&close(w.good,0.180)&&close(w.bad,0.220),"normal windows");
   require(close(normal.judgeWindowSec(NOTE_TAP),0.220),"tap judge window is bad");
   require(close(normal.judgeWindowSec(NOTE_HOLD),0.180),"hold judge window is good");
   require(close(normal.judgeWindowSec(NOTE_DRAG),0.220),"drag judge window is bad");
   // calculate() bounds: strict < for perfect, strict < for good, <= for bad.
   require(normal.calculate(NOTE_TAP,0.0799)==JudgementEngine::Judgement::Perfect,"tap perfect");
   require(normal.calculate(NOTE_TAP,0.0800)==JudgementEngine::Judgement::Good,"tap good at window edge");
   require(normal.calculate(NOTE_TAP,0.1800)==JudgementEngine::Judgement::Bad,"tap bad at good edge");
   require(normal.calculate(NOTE_TAP,0.2200)==JudgementEngine::Judgement::Bad,"tap bad inclusive");
   require(normal.calculate(NOTE_TAP,0.2201)==JudgementEngine::Judgement::Miss,"tap miss past bad");
   // Drag and Flick have no GOOD or BAD branch at all.
   require(normal.calculate(NOTE_DRAG,0.0800)==JudgementEngine::Judgement::Miss,"drag miss past perfect");
   require(normal.calculate(NOTE_FLICK,0.0800)==JudgementEngine::Judgement::Miss,"flick miss past perfect");
   // Hold has no BAD branch either.
   require(normal.calculate(NOTE_HOLD,0.1800)==JudgementEngine::Judgement::Miss,"hold miss past good");
   // Late-miss limits: drag fixed 0.1, flick perfect*1.75, tap/hold good.
   require(close(JudgementEngine::lateMissLimitSec(NOTE_DRAG,w),0.100),"drag late miss");
   require(close(JudgementEngine::lateMissLimitSec(NOTE_FLICK,w),0.140),"flick late miss");
   require(close(JudgementEngine::lateMissLimitSec(NOTE_TAP,w),0.180),"tap late miss");
   require(close(JudgementEngine::lateMissLimitSec(NOTE_HOLD,w),0.180),"hold late miss");}
  // Challenge windows widen with challengeTimeSum: 0.040/0.090/0.140 + sum.
  {JudgementEngine challenge(10,true);const auto w=challenge.windows(0.010);
   require(close(w.perfect,0.050)&&close(w.good,0.100)&&close(w.bad,0.150),"challenge windows shift");}
  // Score: normal = acc*900000 + maxCombo*100000/n; challenge drops the combo term.
  {JudgementEngine normal(4,false);Note n0,n1,n2,n3;n0.type=n1.type=n2.type=n3.type=NOTE_TAP;
   normal.commit(n0,JudgementEngine::Judgement::Perfect,0.0,1);normal.commit(n1,JudgementEngine::Judgement::Perfect,0.0,2);
   normal.commit(n2,JudgementEngine::Judgement::Good,0.0,3);normal.commit(n3,JudgementEngine::Judgement::Miss,0.25,4);
   // acc = (2 + 0.65)/4 = 0.6625, maxCombo = 3 -> 0.6625*900000 + 3*100000/4 = 596250 + 75000
   require(close(normal.accuracy(),66.25,1e-9),"accuracy 66.25%");
   require(normal.score()==671250,"normal score 671250");}
  // The same tallies in challenge mode drop the combo term: 0.6625*1000000 = 662500.
  // Fresh Note objects are required because commit() refuses an already-judged note.
  {JudgementEngine challenge(4,true);Note c0,c1,c2,c3;c0.type=c1.type=c2.type=c3.type=NOTE_TAP;
   challenge.commit(c0,JudgementEngine::Judgement::Perfect,0.0,1);
   challenge.commit(c1,JudgementEngine::Judgement::Perfect,0.0,2);
   challenge.commit(c2,JudgementEngine::Judgement::Good,0.0,3);
   challenge.commit(c3,JudgementEngine::Judgement::Miss,0.25,4);
   require(close(challenge.accuracy(),66.25,1e-9),"challenge accuracy 66.25%");
   require(challenge.score()==662500,"challenge score 662500");}
  // Detail bins: normal range 180ms, width = 360/18 = 20ms; index = round((ms+180)/20).
  {JudgementEngine detail(2,false);Note n;n.type=NOTE_TAP;detail.commit(n,JudgementEngine::Judgement::Perfect,-0.010,1);
   // displayMs = +10 -> (10+180)/20 = 9.5 -> round-half-up 10.
   require(detail.detailBins()[10]==1,"detail bin index 10");
   Note m;m.type=NOTE_TAP;detail.commit(m,JudgementEngine::Judgement::Miss,0.25,2);
   require(detail.detailBins()[10]==1,"miss records no detail bin");}
  // Autoplay suppresses offset statistics but still counts the judgement.
  {JudgementEngine auto_engine(2,false);auto_engine.setAutoplay(true);Note n;n.type=NOTE_TAP;
   auto_engine.commit(n,JudgementEngine::Judgement::Perfect,-0.010,1);
   require(auto_engine.detailBins()[10]==0,"autoplay records no detail bin");
   require(auto_engine.counts()[0]==1&&auto_engine.combo()==1,"autoplay still counts perfect");}
  require(close(JudgementEngine::judgementDetailRangeMs(false),180)&&close(JudgementEngine::judgementDetailRangeMs(true),90),"detail ranges");
  require(JudgementEngine::judgementDetailBinWidthMs(180)==20&&JudgementEngine::judgementDetailBinWidthMs(90)==10,"detail bin widths");
  // Spatial judge window: full BAD inside 0.9, linear taper to 0 at 1.9.
  require(close(JudgementEngine::spatialJudgeWindow(0.080,0.220,0.5F),0.220),"spatial full window");
  require(close(JudgementEngine::spatialJudgeWindow(0.080,0.220,1.4F),0.220-0.5*0.080*0.5),"spatial taper midpoint");
  require(close(JudgementEngine::spatialJudgeWindow(0.080,0.220,1.9F),0.0),"spatial cutoff");
  require(close(JudgementEngine::flickJudgeSpeed(380),0.06),"flick speed at reference dpi");
  require(close(JudgementEngine::flickJudgeSpeed(760),0.12),"flick speed scales with dpi");

  // --- NoteGeometry: three distinct speed bases and the incline/integral helpers ---
  {Note n;n.speed=2.0;n.useOfficialSpeed=true;
   // Render and Judge both fall back to 1*scroll when useOfficialSpeed is set; Control only
   // collapses for holds.
   require(close(renderSpeed(n,1.0),1.0),"render speed official");
   require(close(controlSpeed(n,1.0),2.0),"control speed non-hold keeps note speed");
   require(close(judgeSpeed(n,1.0),2.0),"judge speed ignores official flag");
   n.isHold=true;
   require(close(controlSpeed(n,1.0),1.0),"control speed collapses for official hold");
   require(close(renderSpeed(n,1.0),1.0),"render speed unchanged by isHold");}
  // applyYControlScale: NaN control is a no-op.
  require(close(applyYControlScale(100.0F,2.0F),200.0F),"y control scale");
  require(close(applyYControlScale(100.0F,std::numeric_limits<float>::quiet_NaN()),100.0F),"y control NaN passthrough");
  // rpeControlHeightFromBasePx: h = yOffset*RPE_HEIGHT + base*aspect*RPE_HEIGHT/(stageH*speed).
  // base=100, yOffset=0.1, speed=1, aspect=16/9, stageH=900:
  // 90 + 100*(16/9)*900/(900*1) = 90 + 177.777... = 267.777...
  require(close(rpeControlHeightFromBasePx(100.0F,0.1F,1.0,16.0F/9.0F,900.0F),90.0+100.0*(16.0/9.0),1e-3),"control height");
  // Degenerate speed falls back to the yOffset term alone.
  require(close(rpeControlHeightFromBasePx(100.0F,0.1F,0.0,16.0F/9.0F,900.0F),90.0),"control height zero speed");
  // calcInclineValue: 1 - sin * (base*aspect + transY)*RPE_HEIGHT/(stageH*360).
  // base=0 -> param = transY*900/(900*360) = transY/360.
  require(close(calcInclineValue(std::sin(0.5),0.0F,36.0F,16.0F/9.0F,900.0F),1.0-std::sin(0.5)*0.1,1e-5),"incline value");
  require(close(calcInclineValue(0.0F,100.0F,50.0F,16.0F/9.0F,900.0F),1.0),"incline zero sin is identity");
  // combineInclineSinr: both NaN -> NaN; base only -> asin round-trip; skew adds degrees.
  require(std::isnan(combineInclineSinr(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::quiet_NaN())),"combine NaN");
  require(close(combineInclineSinr(0.5F,std::numeric_limits<float>::quiet_NaN()),0.5,1e-5),"combine base only");
  require(close(combineInclineSinr(std::numeric_limits<float>::quiet_NaN(),30.0F),std::sin(30.0*3.14159265358979323846/180.0),1e-5),"combine skew only");
  // yOffsetToTransYPx ignores speed entirely.
  require(close(yOffsetToTransYPx(0.25F,1.0,900.0F),225.0F)&&close(yOffsetToTransYPx(0.25F,7.0,900.0F),225.0F),"yOffset to pixels");
  require(close(sanitizeJudgeArea(0.0F),1.0F)&&close(sanitizeJudgeArea(-2.0F),1.0F)&&close(sanitizeJudgeArea(2.5F),2.5F),"judge area sanitize");

  // --- replay model: recording, playback and hold body feedback ---------------
  {
    const GameplaySession::Viewport replayViewport{1600,900,16.0F/9.0F,1,380};
    // A manual tap run records one judgement entry, timed at the actual hit rather than at
    // the frame that observed it.
    auto tapChart=singleNoteChart(NOTE_TAP);
    GameplaySession recorder;
    recorder.setViewport(replayViewport);
    recorder.setReplayRecording(true);
    recorder.setChart(&tapChart);
    recorder.touchDown(1,800,450);
    recorder.update(0.95,1.0F/60.0F);
    require(recorder.recordedReplay().size()==1,"a tap run records one judgement entry");
    if(!recorder.recordedReplay().empty()){
      const auto& entry=recorder.recordedReplay().front();
      require(entry.type==ReplayJudgement::Type::Tap,"the tap entry carries the tap type");
      require(entry.noteIndex==0,"the tap entry is keyed by note index");
      require(entry.judgement==0,"the tap entry records the judgement");
      require(close(entry.timeSec,0.95),"the tap entry is timed at the hit, not the frame");
    }
    const auto recordedTapResult=recorder.judgement().result();
    const auto tapEntries=recorder.recordedReplay();
    recorder.touchUp(1);
    recorder.setReplayRecording(false);
    require(recorder.recordedReplay().empty(),"turning recording off drops the collected entries");

    // Playback re-applies the recorded judgement without any touch input.
    tapChart.resetRuntime();
    GameplaySession player;
    player.setViewport(replayViewport);
    player.setReplayPlayback(tapEntries);
    player.setChart(&tapChart);
    require(player.replayPlayback(),"replay playback is active for a non-empty list");
    for(double t=0.95;t<=1.2;t+=1.0/60.0)player.update(t,1.0F/60.0F);
    const auto playedTapResult=player.judgement().result();
    require(playedTapResult.perfect==1,"replay playback commits the recorded tap judgement");
    require(playedTapResult.score==1000000,"replay playback reproduces the recorded score");
    require(tapChart.allNotesSorted[0]->judgeResult==0,"the replayed tap is judged perfect");
    require(close(tapChart.allNotesSorted[0]->judgeTimeSec,0.95),
        "the replayed judgement keeps the recorded effect time");

    // A hold records a press entry when the head activates and a release entry when it
    // settles; the head is not a plain judgement entry.
    auto holdChart=singleNoteChart(NOTE_HOLD,1.0);
    GameplaySession holdRecorder;
    holdRecorder.setViewport(replayViewport);
    holdRecorder.setReplayRecording(true);
    holdRecorder.setChart(&holdChart);
    holdRecorder.touchDown(1,800,450);
    holdRecorder.update(0.95,1.0F/60.0F);
    require(holdRecorder.recordedReplay().size()==1,"a hold head records a press entry");
    // Body bursts: interval is half a beat, 0.25 s at 120 bpm, counted after the note start.
    int burstCount=0;
    double firstBurstTime=-1.0;
    double t=0.95;
    while(t<1.30){
      t+=1.0/60.0;
      holdRecorder.update(t,1.0F/60.0F);
      for(const auto& burst:holdRecorder.takeHoldBodyBursts()){
        ++burstCount;
        if(firstBurstTime<0.0)firstBurstTime=burst.timeSec;
        require(burst.noteIndex==0,"a hold body burst addresses the hold note");
        require(burst.perfect,"a perfect hold body burst is marked perfect");
      }
    }
    require(burstCount==1,"exactly one body burst lands inside the first interval");
    require(firstBurstTime>1.0&&firstBurstTime<1.31,
        "the first body burst lands one interval after the hold start");
    while(t<2.0){
      t+=1.0/60.0;
      holdRecorder.update(t,1.0F/60.0F);
    }
    holdRecorder.takeHoldBodyBursts();
    require(holdChart.allNotesSorted[0]->judgeResult==0,"the manual hold settles perfect");
    require(holdRecorder.recordedReplay().size()==2,"a hold records a press and a release");
    if(holdRecorder.recordedReplay().size()==2){
      require(holdRecorder.recordedReplay()[0].type==ReplayJudgement::Type::HoldPress,
          "the first hold entry is the press");
      require(holdRecorder.recordedReplay()[1].type==ReplayJudgement::Type::HoldRelease,
          "the second hold entry is the release");
      require(close(holdRecorder.recordedReplay()[0].timeSec,0.95),
          "the press entry is timed at the head hit");
      require(holdRecorder.recordedReplay()[1].timeSec>1.7,
          "the release entry is timed at the settlement frame");
    }
    const auto recordedHoldResult=holdRecorder.judgement().result();
    const auto holdEntries=holdRecorder.recordedReplay();
    holdRecorder.setReplayRecording(false);
    holdRecorder.touchUp(1);

    // Playback of the hold reproduces the judgement and still shows body feedback.
    holdChart.resetRuntime();
    GameplaySession holdPlayer;
    holdPlayer.setViewport(replayViewport);
    holdPlayer.setReplayPlayback(holdEntries);
    holdPlayer.setChart(&holdChart);
    int replayBursts=0;
    for(double time=0.95;time<=2.0;time+=1.0/60.0){
      holdPlayer.update(time,1.0F/60.0F);
      replayBursts+=static_cast<int>(holdPlayer.takeHoldBodyBursts().size());
    }
    const auto playedHoldResult=holdPlayer.judgement().result();
    require(playedHoldResult.perfect==recordedHoldResult.perfect
            &&playedHoldResult.score==recordedHoldResult.score,
        "hold replay playback reproduces the recorded judgement");
    require(holdChart.allNotesSorted[0]->judgeResult==0,"the replayed hold is judged perfect");
    require(replayBursts>0,"a replayed hold still produces body feedback");
  }

  // --- runtime preparation: incremental floor cursor and parallel per-line pass -----
  {
    // A large chart takes the parallel per-line path; every derived value must still equal a
    // direct binary-search floor lookup, so the speed-up cannot change the timing maths.
    Chart runtimeChart;
    runtimeChart.formatVersion = 3;
    runtimeChart.bpmTimeline = std::make_shared<BpmTimeline>(
        std::vector<double>{0.0}, std::vector<double>{120.0}, std::vector<double>{0.0});
    constexpr int kLineCount = 4;
    constexpr int kNotesPerLine = 260;
    for (int lineIndex = 0; lineIndex < kLineCount; ++lineIndex) {
      auto line = std::make_unique<JudgeLine>();
      line->bpm = 120.0;
      SpeedEvent first;
      first.startTime = 0.0;
      first.endTime = 4.0;
      first.value = 1.0;
      first.endValue = 1.0;
      line->speedEvents.push_back(first);
      SpeedEvent second;
      second.startTime = 4.0;
      second.endTime = 1000.0;
      second.value = 2.5;
      second.endValue = 2.5;
      line->speedEvents.push_back(second);
      // A layered line exercises the per-layer cursor; the plain speed list stays empty so
      // the layered sum is the only contribution.
      if (lineIndex == 0) {
        std::vector<SpeedEvent> layer;
        SpeedEvent layerEvent;
        layerEvent.startTime = 0.0;
        layerEvent.endTime = 1000.0;
        layerEvent.value = 0.5;
        layerEvent.endValue = 0.5;
        layer.push_back(layerEvent);
        line->speedEventLayers.push_back(layer);
      }
      for (int noteIndex = 0; noteIndex < kNotesPerLine; ++noteIndex) {
        Note note;
        note.type = noteIndex % 9 == 0 ? NOTE_HOLD : NOTE_TAP;
        note.time = static_cast<double>(noteIndex) * 0.05;
        note.holdTime = note.type == NOTE_HOLD ? 0.3 : 0.0;
        note.positionX = 0.25;
        note.speed = 1.0;
        note.isAbove = true;
        line->notesAbove.push_back(note);
      }
      runtimeChart.judgeLineList.push_back(std::move(line));
    }
    runtimeChart.prepareRuntime();
    require(runtimeChart.allNotesSorted.size() == kLineCount * kNotesPerLine,
        "every note reaches the sorted list");
    for (const auto& lineOwner : runtimeChart.judgeLineList) {
      const auto& line = *lineOwner;
      for (const auto& note : line.notes) {
        const double expectedFloor = EventUtils::getFloorPosition(note.time,
            line.speedEvents, line.speedEventLayers);
        require(std::abs(note.floorPosition - expectedFloor) <= 1e-9,
            "the incremental floor cursor matches the binary-search lookup");
        const double expectedSect = line.beat2sec(note.time);
        require(std::abs(note.sect - expectedSect) <= 1e-12, "the note beat matches");
        if (note.isHold) {
          // Official charts derive the hold end from the speed multiplier; the plain format
          // walks the speed events, which is the path the cursor touches here.
          const double expectedEndFloor = runtimeChart.formatVersion > 0
              ? note.floorPosition + note.holdTime * note.speed * PGR_HEIGHT
              : EventUtils::getFloorPosition(note.time + note.holdTime, line.speedEvents,
                    line.speedEventLayers);
          require(std::abs(note.holdEndFloorPosition - expectedEndFloor) <= 1e-9,
              "the hold end floor matches the binary-search lookup");
        }
      }
    }
    // Running the preparation again must produce exactly the same values: the parallel
    // per-line pass merges results in chart order rather than completion order.
    std::vector<double> firstFloors;
    std::vector<double> firstSects;
    for (const auto* note : runtimeChart.allNotesSorted) firstFloors.push_back(note->floorPosition);
    for (const auto* note : runtimeChart.allNotesSorted) firstSects.push_back(note->sect);
    runtimeChart.prepareRuntime();
    require(runtimeChart.allNotesSorted.size() == firstFloors.size(),
        "repeated preparation keeps the note count");
    bool stable = true;
    for (std::size_t index = 0; index < firstFloors.size(); ++index) {
      if (runtimeChart.allNotesSorted[index]->floorPosition != firstFloors[index]
          || runtimeChart.allNotesSorted[index]->sect != firstSects[index]) {
        stable = false;
        break;
      }
    }
    require(stable, "repeated preparation produces identical note values");
  }

  std::cout<<"All ReChigros core tests passed\n";return 0;
}
