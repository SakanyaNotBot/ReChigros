#include "rechigros/game/HitOffsetIndicator.hpp"

#include "rechigros/game/GameConstants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rechigros::game {
HitOffsetIndicator::HitOffsetIndicator(std::size_t count) : marks_(std::max<std::size_t>(1, count)) {}
void HitOffsetIndicator::reset() noexcept {
  *this = HitOffsetIndicator(marks_.size());
  latestJudgement_ = Judgement::Perfect;
}
void HitOffsetIndicator::addHit(double diff, Judgement judgement, double now, double badWindow) {
  update(now);
  const auto angle = angleForDiff(diff,badWindow);
  marks_[nextMarkIndex_] = {true,angle,judgement,now,diff*1000.0};
  nextMarkIndex_=(nextMarkIndex_+1)%marks_.size();
  pointerMoveStartAngleDeg_=pointerAngleDeg_; pointerMoveTargetAngleDeg_=angle;
  pointerMoveStartSec_=now; pointerMoveDurationSec_=MOVE_DURATION_SEC; lastHitSec_=now;
  returningToCenter_=false; latestText_=formatOffsetMs(diff); latestTextStartSec_=now;
  latestJudgement_=judgement;
}
void HitOffsetIndicator::update(double now) noexcept {
  if(!returningToCenter_ && now-lastHitSec_>=RETURN_DELAY_SEC && std::abs(pointerMoveTargetAngleDeg_)>0.01F){
    applyPointerMove(now); pointerMoveStartAngleDeg_=pointerAngleDeg_; pointerMoveTargetAngleDeg_=0;
    pointerMoveStartSec_=now; pointerMoveDurationSec_=RETURN_DURATION_SEC; returningToCenter_=true;
  }
  applyPointerMove(now);
  for(auto& mark:marks_) if(mark.active && markAge(now,mark)>=1.0F) mark.active=false;
}
float HitOffsetIndicator::latestTextAlpha(double now) const noexcept {
  if(latestText_.empty()) return 0; const auto age=static_cast<float>((now-latestTextStartSec_)/MARK_LIFE_SEC);
  return age<0 || age>=1 ? 0 : fadeOutAlpha(age);
}
float HitOffsetIndicator::markAge(double now,const Mark& mark) noexcept { return static_cast<float>((now-mark.startSec)/MARK_LIFE_SEC); }
float HitOffsetIndicator::angleForDiff(double diff,double bad) noexcept { return std::clamp(static_cast<float>(diff/std::max(0.001,std::abs(bad))),-1.0F,1.0F)*MAX_ANGLE_DEG; }
HitOffsetIndicator::Judgement HitOffsetIndicator::judgementForOffset(double d,double perfect,double good,double bad) noexcept {
  if(d<=perfect)return Judgement::Perfect; if(d<=good)return Judgement::Good; if(d<=bad)return Judgement::Bad; return Judgement::Miss;
}
bool HitOffsetIndicator::shouldShowCommitMark(int type,bool,Judgement judgement) noexcept { return judgement!=Judgement::Miss && type!=NOTE_HOLD; }
float HitOffsetIndicator::fadeOutAlpha(float t) noexcept { const auto inv=1.0F-std::clamp(t,0.0F,1.0F); return inv*inv; }
std::string HitOffsetIndicator::formatOffsetMs(double diff) {
  const auto ms=std::clamp(static_cast<int>(std::lround(diff*1000.0)),-999,999); char buffer[16]{};
  std::snprintf(buffer,sizeof(buffer),"%+04dms",ms); return buffer;
}
void HitOffsetIndicator::applyPointerMove(double now) noexcept {
  const auto duration=std::max(0.001F,pointerMoveDurationSec_); const auto raw=static_cast<float>((now-pointerMoveStartSec_)/duration);
  if(raw>=1){pointerAngleDeg_=pointerMoveTargetAngleDeg_;return;} if(raw<=0){pointerAngleDeg_=pointerMoveStartAngleDeg_;return;}
  const auto inv=1.0F-raw; const auto eased=1.0F-inv*inv*inv;
  pointerAngleDeg_=pointerMoveStartAngleDeg_+(pointerMoveTargetAngleDeg_-pointerMoveStartAngleDeg_)*eased;
}
} // namespace rechigros::game
