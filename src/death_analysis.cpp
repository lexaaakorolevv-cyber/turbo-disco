#include "death_analysis.hpp"
#include <cmath>
namespace rf {
DeathDiagnosis diagnoseDeath(FormState const&s,PlayerHistory const& history,std::optional<Obstacle> const& hazard,Tick death){
 DeathDiagnosis result;result.actionTick=death;
 for(unsigned i=history.size();i>1;--i){auto const&a=history.at(i-2);auto const&b=history.at(i-1);if(a.state.held!=b.state.held){result.actionTick=b.tick;break;}}
 if(!hazard)return result;
 double sign=s.inverted?-1:1,relative=((hazard->bottom+hazard->top)/2-s.y)*sign;
 if(s.form==Form::Cube||s.form==Form::Robot){
  bool ceiling=hazard->left<=s.x&&hazard->right>=s.x&&relative>s.halfHeight*.5;
  if(ceiling){result.kind=s.form==Form::Robot?FailureKind::NeedRelease:FailureKind::EarlyPress;result.duration=s.form==Form::Robot;result.timingDirection=result.duration?-1:1;}
  else {result.kind=s.grounded||s.vy*sign>=0?FailureKind::LatePress:FailureKind::EarlyPress;result.timingDirection=result.kind==FailureKind::LatePress?-1:1;}
 }else if(s.form==Form::Ufo||s.form==Form::Swing){
  result.kind=relative<0?FailureKind::LatePress:FailureKind::EarlyPress;result.timingDirection=relative<0?-1:1;
 }else if(s.form==Form::Ship||s.form==Form::Wave){
  bool down=relative<0;result.kind=down?FailureKind::NeedHold:FailureKind::NeedRelease;
  result.duration=!down||!s.held;result.timingDirection=down?(s.held?-1:1):-1;
 }else {result.kind=FailureKind::WrongRoute;}
 // For duration repairs select the release edge; timing repairs select takeoff.
 for(unsigned i=history.size();i>1;--i){auto const&a=history.at(i-2);auto const&b=history.at(i-1);
  if(a.state.held!=b.state.held&&(result.duration?!b.state.held:b.state.held)){result.actionTick=b.tick;break;}}
 return result;
}
}
