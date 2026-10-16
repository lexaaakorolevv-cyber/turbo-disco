#include "planner.hpp"
#include "input.hpp"
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <random>
using namespace rf;
namespace {
void check(bool b,char const*s){if(!b)throw std::runtime_error(s);}
// Independent integration and coefficients. No call to advanceForm here.
void oracle(FormState&s,bool down,double dt){
 double sign=s.inverted?-1:1;bool press=down&&!s.held;
 if(s.form==Form::Cube&&s.grounded&&down){s.vy=sign*11.05*(s.scale<.9?.8:1);s.grounded=false;}
 if(s.form==Form::Ball&&s.grounded&&press){s.vy=sign*6.7*(s.scale<.9?.8:1);s.inverted=!s.inverted;s.grounded=false;}
 sign=s.inverted?-1:1;double oldVy=s.vy;
 if(s.form==Form::Ship){s.vy+=sign*(down?.38:-.45)*dt;s.vy=std::clamp(s.vy,-6.3,7.9);}
 else if(!s.grounded)s.vy-=sign*(s.form==Form::Ball?.57:.94)*dt;
 s.x+=s.vx*dt;s.y+=(oldVy+s.vy)*dt/2;s.held=down;
 if(s.form!=Form::Ship){s.grounded=false;if(!s.inverted&&s.y-s.halfHeight<=s.floor){s.y=s.floor+s.halfHeight;s.vy=0;s.grounded=true;}if(s.inverted&&s.y+s.halfHeight>=s.ceiling){s.y=s.ceiling-s.halfHeight;s.vy=0;s.grounded=true;}}
 check(s.y-s.halfHeight>=s.floor-.1&&s.y+s.halfHeight<=s.ceiling+.1,"Oracle left bounds");
}
void safe(FormState const&s,std::span<Obstacle const> objects){for(auto const&o:objects)if(s.x+s.halfWidth>o.left&&s.x-s.halfWidth<o.right&&s.y+s.halfHeight>o.bottom+.05&&s.y-s.halfHeight<o.top-.05){std::cerr<<"form="<<static_cast<int>(s.form)<<" x="<<s.x<<" y="<<s.y<<'\n';throw std::runtime_error("Independent route collided");}}
void cubeRoad(bool inverted,double direction){
 FormState s;s.form=Form::Cube;s.floor=91;s.ceiling=451;s.y=inverted?437:105;s.inverted=inverted;s.vx=direction*5.2;s.gravity=.958199;
 std::vector<Obstacle> objects{{160,184,91,118},{370,394,91,119},{620,646,91,116}};
 if(inverted)for(auto&o:objects){double bottom=542-o.top;o.top=542-o.bottom;o.bottom=bottom;}
 if(direction<0)for(auto&o:objects){double left=-o.right;o.right=-o.left;o.left=left;}
 InputPacer pacer;pacer.reset();InputCommitment commitment;int presses=0;bool last=false;
 for(int tick=0;tick<580;tick++){
  s.pressRemaining=pacer.pressRemainingFrames(tick)/4.;bool requested=false;if(!commitment.sample(tick/4.,s,0,requested)){auto decision=chooseFormInput(s,objects,{},nullptr,0,false);commitment.begin(decision.plan,tick/4.,s,0);requested=decision.down;}bool down=pacer.apply(requested,tick);presses+=down&&!last;last=down;
  for(int i=0;i<2;i++){oracle(s,down,.125);safe(s,objects);}
 }
 check(presses>=3&&presses<=7,"Cube road used unnecessary spam");
}
void dualRoad(Form second,bool inverted,double direction){
 FormState a;a.floor=91;a.ceiling=451;a.y=inverted?437:105;a.inverted=inverted;a.vx=direction*5.2;a.gravity=.958199;
 auto b=a;b.form=second;b.inverted=!inverted;b.y=b.inverted?437:105;
 std::vector<Obstacle> objects{{190,214,91,119},{430,454,91,119}};
 if(second==Form::Cube){objects.push_back({190,214,423,451});objects.push_back({430,454,423,451});}
 if(second==Form::Ball)objects.push_back({430,454,423,451});
 if(inverted)for(auto&o:objects){double bottom=542-o.top;o.top=542-o.bottom;o.bottom=bottom;}
 if(direction<0)for(auto&o:objects){double left=-o.right;o.right=-o.left;o.left=left;}
 InputPacer pacer;pacer.reset();InputCommitment commitment;int presses=0;bool held=false;
 for(int tick=0;tick<520;tick++){
  a.pressRemaining=b.pressRemaining=pacer.pressRemainingFrames(tick)/4.;bool requested=false;if(!commitment.sample(tick/4.,a,0,requested,&b)){auto decision=chooseDualInput(a,b,objects,{}, {},nullptr,false);commitment.begin(decision.plan,tick/4.,a,0,&b);requested=decision.down;}bool down=pacer.apply(requested,tick);presses+=down&&!held;held=down;
  check(pacer.currentCps()<=16,"Dual exceeded shared CPS limit");
  for(int i=0;i<2;i++){oracle(a,down,.125);oracle(b,down,.125);safe(a,objects);safe(b,objects);}
 }
 check(presses>0&&presses<12,"Dual road produced excessive presses");
}
void independentRoad(bool inverted,double direction){
 FormState a;a.floor=91;a.ceiling=451;a.y=inverted?437:105;a.inverted=inverted;a.vx=direction*5.2;a.gravity=.958199;
 auto b=a;b.floor=491;b.ceiling=851;b.y=inverted?837:505;
 std::vector<Obstacle> objects{{160,184,91,119},{310,334,491,519},{100,230,545,590},{250,400,145,190}};
 if(inverted)for(auto&o:objects){double sum=o.bottom>400?1342:542;double low=sum-o.top;o.top=sum-o.bottom;o.bottom=low;}
 if(direction<0)for(auto&o:objects){double left=-o.right;o.right=-o.left;o.left=left;}
 auto initialA=a,initialB=b;InputPacer first,second;InputCommitment pa,pb;std::vector<Action> events;bool ha=false,hb=false;unsigned split=0,presses=0;
 for(Tick tick=0;tick<400;tick++){
  a.pressRemaining=first.pressRemainingFrames(tick)/4.;b.pressRemaining=second.pressRemainingFrames(tick)/4.;bool da=false,db=false;
  bool va=pa.sample(tick/4.,a,0,da,&b),vb=pb.sample(tick/4.,b,0,db,&a);
  if(!va||!vb){auto decision=chooseDualInput(a,b,objects,{}, {},nullptr,false,true);da=decision.down;db=decision.secondDown;pa.begin(decision.plan,tick/4.,a,0,&b);pb.begin(decision.secondPlan,tick/4.,b,0,&a);}
  bool down=first.apply(da,tick),other=second.apply(db,tick);split+=down!=other;presses+=down&&!ha;presses+=other&&!hb;
  if(down!=ha)appendAction(events,tick,down,1);if(other!=hb)appendAction(events,tick,other,2);ha=down;hb=other;
  check(first.currentCps()<=16&&second.currentCps()<=16,"Independent channel exceeded CPS limit");
  for(int step=0;step<2;step++){oracle(a,down,.125);oracle(b,other,.125);safe(a,objects);safe(b,objects);}
 }
 check(split>0&&presses>=2&&presses<8,"Independent route did not use separate sensible controls");
 // Replay only recorded events through the independent oracle, with no planner.
 auto ra=initialA,rb=initialB;bool down=false,other=false;std::size_t cursor=0;
 for(Tick tick=0;tick<400;tick++){
  while(cursor<events.size()&&events[cursor].tick==tick){auto const&e=events[cursor++];(e.player==1?down:other)=e.down;}
  for(int step=0;step<2;step++){oracle(ra,down,.125);oracle(rb,other,.125);safe(ra,objects);safe(rb,objects);}
 }
 check(std::abs(ra.y-a.y)<1e-8&&std::abs(rb.y-b.y)<1e-8,"Independent replay changed a route");
}

}
int main(){try{
 // A nearby portal/camera corridor must not turn empty ground into a jump game.
 for(auto form:{Form::Cube,Form::Robot,Form::Ball,Form::Spider}){FormState s;s.form=form;s.ceiling=331;s.gravity=.958199;
  for(int tick=0;tick<64;tick++){check(!chooseFormInput(s,{}, {},nullptr,0,false).down,"Unnecessary ground action without an obstacle");s.x+=s.vx*.25;}}
 FormState far;far.y=250;far.vy=4;far.grounded=false;far.ceiling=1000;auto fine=far;std::uint64_t state=0;
 check(advanceForm(far,false,1,{}, {},state),"Coarse physics step failed");for(int i=0;i<4;i++)check(advanceForm(fine,false,.25,{}, {},state),"Fine physics step failed");
 check(std::abs(far.y-fine.y)<1e-9&&std::abs(far.vy-fine.vy)<1e-9,"Far forecast changed gravity integration");
 FormState s;std::uint64_t used=0;Interaction pad;pad.kind=InteractionKind::YellowPad;pad.value=16;pad.area={-20,20,80,110};
 check(advanceForm(s,false,.25,{},std::span(&pad,1),used)&&s.vy>15&&!s.grounded,"Floor landing erased pad impulse");
 std::array<Interaction,2> rings;for(auto&i:rings){i.kind=InteractionKind::YellowOrb;i.area={-20,30,100,180};}
 s={};s.y=130;s.grounded=false;used=0;check(advanceForm(s,true,.25,{},rings,used)&&used==1,"One hold activated multiple orbs");
 advanceForm(s,false,.25,{},rings,used);check(advanceForm(s,true,1,{},rings,used)==false,"Press cooldown was bypassed");
 s.pressRemaining=0;check(advanceForm(s,true,.25,{},rings,used)&&used==3,"Released ring could not activate");
 Interaction portal;portal.kind=InteractionKind::ModePortal;portal.target=Form::Ship;portal.area={35,45,80,150};s={};auto handoff=chooseFormInput(s,{},std::span(&portal,1));check(handoff.survival>0&&handoff.survival<12,"Mode portal did not stop stale forecast");
 // A committed maneuver must stop on a new event, avatar transition or deadline.
 ControlPlan plan{static_cast<unsigned short>(1u<<1),4,1};InputCommitment route;FormState start;bool button=false;
 route.begin(plan,0,start,7);check(route.sample(0,start,7,button)&&!button,"Plan initial release lost");
 auto later=start;later.x+=later.vx*.25;check(route.sample(.25,later,7,button)&&button,"Plan next press was postponed");
 check(!route.sample(.25,later,8,button),"Contact change retained stale maneuver");
 route.begin(plan,0,start,7);check(!route.sample(1,start,7,button),"Expired maneuver stayed active");
 route.begin(plan,0,start,7);check(!route.sample(0,start,7,button,&start),"Dual transition retained solo maneuver");
 route.begin(plan,0,start,7,&start);later=start;later.y+=100;check(!route.sample(0,start,7,button,&later),"Second avatar teleport retained maneuver");
 route.begin(plan,0,start,7);later=start;later.form=Form::Ball;check(!route.sample(0,later,7,button),"Mode change retained maneuver");
 // Walk on the actual triangular surface, rather than its full AABB roof.
 for(bool inv:{false,true})for(double direction:{1.,-1.}){
  Obstacle ramp{0,100,91,191,true};ramp.vertices={Point{0,91},Point{100,91},Point{100,191},Point{}};ramp.vertexCount=3;ramp.ramp=true;
  if(inv){ramp.bottom=351;ramp.top=451;for(int i=0;i<3;i++)ramp.vertices[i].y=542-ramp.vertices[i].y;}
  FormState walker;walker.x=direction>0?25:70;walker.vx=direction*5.2;walker.inverted=inv;walker.ceiling=451;
  auto surface=verticalSpan(ramp,walker.x,walker.halfWidth);walker.y=inv?surface->first-walker.halfHeight:surface->second+walker.halfHeight;
  for(int i=0;i<12;i++){std::uint64_t usedRamp=0;check(advanceForm(walker,false,.25,std::span(&ramp,1),{},usedRamp),"Ramp walk collided");
   auto span=verticalSpan(ramp,walker.x,walker.halfWidth);check(walker.grounded&&std::abs(walker.y-(inv?span->first-walker.halfHeight:span->second+walker.halfHeight))<1e-8,"Ramp surface was not followed");}
 }
 // These fixed dense scenes exhausted the smaller 1.8 beam before a safe escape.
 for(unsigned seed:{22u,70u,156u}){
  std::mt19937 rng(7823+seed);FormState state;state.form=seed%3==0?Form::Ship:seed%3==1?Form::Ufo:Form::Swing;state.floor=91;state.ceiling=451;state.y=170+rng()%200;state.vy=int(rng()%15)-7;state.vx=7.8;state.grounded=false;state.gravity=.958199;state.inverted=seed%2;state.held=seed%4>1;
  std::vector<Obstacle> dense;for(int i=0;i<8;i++){double x=50+i*35+rng()%20,y=91+rng()%300;dense.push_back({x,x+20+rng()%30,y,y+30+rng()%80});}
  auto escape=chooseFormInput(state,dense,{},nullptr,0,false);check(escape.survival>=(seed==22?17:seed==70?19:42),"Near-dead-end rescue regressed");
 }
 // The second player's trace is part of verification, including transition ticks.
 Macro m;m.environment={{"mod",ModVersion}};m.ticks=33;m.outerUpdates=33;m.timings.resize(33,{.25,false,true});
 Sample p0{0,0,105,0,1,1,.25,0},p1{5,10,105,0,1,1,.25,1u<<12};p1.second=PlayerSample{10,437,0,1,1,2};
 Sample p16=p1;p16.tick=16;Sample p20=p0;p20.tick=20;Sample p32=p0;p32.tick=32;m.trace={p0,p1,p16,p20,p32};
 auto j=toJson(m);check(fromJson(j).trace==m.trace,"Dual trace/transition JSON roundtrip failed");auto changed=p1;changed.second->y+=.01;check(!sameSample(p1,changed),"Second player desync missed");
 auto bad=j;bad["trace"][1]["player2"]=nullptr;bool rejected=false;try{fromJson(bad);}catch(...){rejected=true;}check(rejected,"Dual trace accepted missing player 2");
 // Simultaneous presses/releases must keep their separate channels and order.
 std::vector<Action> channels;appendAction(channels,0,true,2);appendAction(channels,0,true,1);appendAction(channels,1,false,1);appendAction(channels,2,false,2);
 check(channels.size()==4&&channels[0].player==1&&channels[1].player==2,"Simultaneous channel ordering lost");
 check(!heldBefore(channels,2,1)&&heldBefore(channels,2,2),"Held state crossed player channels");
 check(expandPlan(compactPlan(channels))==channels,"Session compact format lost P2");
 Macro duo=m;duo.actions=channels;check(fromJson(toJson(duo)).actions==channels,"Macro schema 3 lost P2");
 auto invalid=toJson(duo);std::swap(invalid["actions"][0],invalid["actions"][1]);bool badOrder=false;try{fromJson(invalid);}catch(...){badOrder=true;}check(badOrder,"Reversed simultaneous event order accepted");
 for(bool flip:{false,true})for(int player:{1,2})check(logicalPlayer(nativeButtonSide(player,true,flip),true,flip)==player,"Native player/flip routing mismatch");
 Search multi(1337,120,true);multi.observe(channels,33,40);auto restored=Search::restore(multi.snapshot());bool secondChanged=false;
 for(int i=0;i<128;i++){auto next=multi.next(500);check(next==restored.next(500),"Independent search resume diverged");check(expandPlan(compactPlan(next),500)==next,"Independent mutation produced invalid controls");secondChanged|=next!=channels&&std::any_of(next.begin(),next.end(),[](auto const&a){return a.player==2;});}
 check(secondChanged,"Search did not retain or mutate P2");
 Search emptySecond(1337,120,true);emptySecond.observe({},100,10);auto resumedEmpty=Search::restore(emptySecond.snapshot());bool exploredP2=false;
 for(int i=0;i<128;i++){auto next=emptySecond.next(500);check(next==resumedEmpty.next(500),"Empty P2 channel resume diverged");exploredP2|=std::any_of(next.begin(),next.end(),[](auto const&a){return a.player==2;});}
 check(exploredP2,"Search never explored a previously unused second channel");
 int roads=0;for(bool inv:{false,true})for(double d:{1.,-1.}){cubeRoad(inv,d);++roads;independentRoad(inv,d);++roads;for(auto form:{Form::Cube,Form::Ball}){dualRoad(form,inv,d);++roads;}}
 std::cout<<roads<<" ordinary, shared-dual and independent-player oracle roads; no idle spam, pad impulses, orb rearming, native portal handoff and two-player desync detection: PASS\n";
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
