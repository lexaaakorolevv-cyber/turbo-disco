#include "planner.hpp"
#include "flight.hpp"
#include "ground.hpp"
#include "death_analysis.hpp"
#include "save_worker.hpp"
#include "input.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool ok,char const*why){if(!ok)throw std::runtime_error(why);}
FormState moved(FormState state){state.x+=state.vx*.25;return state;}
void calibration(){
 for(auto form:{Form::Cube,Form::Robot,Form::Ball,Form::Ufo,Form::Swing}){
  Calibration c;FormState a;a.form=form;a.gravity=.958199;a.grounded=form==Form::Cube||form==Form::Robot||form==Form::Ball;a.y=250;a.vy=form==Form::Swing?4:-1;
  for(int i=0;i<12;i++){c.resetSampling();auto b=moved(a);b.held=true;b.grounded=false;
   if(form==Form::Cube)b.vy=a.jump*1.1-a.gravity*.25;
   if(form==Form::Robot)b.vy=a.jump*.5*1.1;
   if(form==Form::Ball){b.inverted=true;b.vy=a.jump*.6*1.1+a.gravity*.6*.25;}
   if(form==Form::Ufo)b.vy=7*1.1-a.gravity*.6*.25;
   if(form==Form::Swing){b.inverted=true;b.vy=a.vy*.8*1.1+a.gravity*.4*.25;}
   c.observe({a,0,0});c.observe({b,.25,1});
  }
  auto learned=c.adjusted(a);double ratio=form==Form::Ufo||form==Form::Swing?learned.controlScale:learned.jump/a.jump;
  check(std::abs(ratio-1.1)<.02,"Form impulse/control calibration failed");
 }
 Calibration ship;FormState a;a.form=Form::Ship;a.grounded=false;
 for(int branch=0;branch<4;branch++)for(int i=0;i<12;i++){
  ship.resetSampling();a.held=branch<2;a.vy=branch%2?-3:3;auto b=moved(a);double acc=branch==0?.3833:branch==1?.4791:branch==2?-.4599:-.3066;b.vy=a.vy+acc*1.1*.25;ship.observe({a,0,0});ship.observe({b,.25,0});
 }
 for(auto factor:ship.adjusted(a).acceleration)check(std::abs(factor-1.1)<.02,"Ship acceleration branch failed");
 auto before=ship.adjusted(a).acceleration[3];ship.resetSampling();a.held=false;a.vy=-3;auto b=moved(a);b.vy=a.vy-.3066*1.35*.25;ship.observe({a,0,0});ship.observe({b,.25,0});check(std::abs(ship.adjusted(a).acceleration[3]-before)<.02,"Single outlier moved established physics too far");
 Calibration robot;FormState r;r.form=Form::Robot;r.grounded=false;r.held=true;r.vy=6;r.robotTime=12;r.gravity=.958199;
 for(int i=0;i<12;i++){robot.resetSampling();auto middle=moved(r);middle.robotTime=12.25;auto end=moved(middle);end.robotTime=12.5;end.vy-=r.gravity*.9*.25;robot.observe({r,0,0});robot.observe({middle,.25,0});robot.observe({end,.5,0});}
 check(std::abs(robot.adjusted(r).robotHold-12.25)<.2,"Robot hold duration did not learn");
 auto encoded=robot.toJson();check(Calibration::fromJson(encoded).toJson()==encoded,"Extended calibration did not persist");
}
void diagnosis(){
 PlayerHistory history;FormState state;history.record(state,0,0);state.held=true;history.record(state,20,5);state.held=false;history.record(state,30,7.5);
 state.grounded=false;state.vy=4;Obstacle front{190,220,91,119};state.x=185;state.y=120;
 auto late=diagnoseDeath(state,history,front,40);check(late.kind==FailureKind::LatePress&&late.actionTick==20&&late.timingDirection<0,"Late takeoff not diagnosed");
 state.vy=-4;check(diagnoseDeath(state,history,front,40).kind==FailureKind::EarlyPress,"Early takeoff not diagnosed");
 state.form=Form::Robot;Obstacle ceiling{170,220,150,170};auto release=diagnoseDeath(state,history,ceiling,40);check(release.kind==FailureKind::NeedRelease&&release.duration&&release.actionTick==30,"Robot ceiling release not diagnosed");
 state.form=Form::Ufo;check(diagnoseDeath(state,history,front,40).kind==FailureKind::LatePress,"UFO flap was confused with a sustained hold");
 check(diagnoseDeath(state,history,std::nullopt,40).kind==FailureKind::Unknown,"Unknown collision invented a cause");
 FailureMemory memory;FailureRecord record;record.x=state.x;record.y=state.y;record.vy=state.vy;record.player=2;record.form=state.form;record.held=false;memory.record(record);state.player=1;check(memory.penalty(state,false)==0,"P2 failure affected P1");state.player=2;check(memory.penalty(state,false)>0,"P2 failure did not affect P2");check(FailureMemory::fromJson(memory.toJson()).toJson()==memory.toJson(),"Player-specific memory lost");
 for(int i=0;i<1000;i++)history.record(state,i,100+i*.25);check(history.size()==120,"Death history is unbounded");
}
void budgets(){
 FormState state;state.form=Form::Ufo;state.grounded=false;state.y=180;state.floor=0;state.ceiling=400;
 PlanningBudget budget;budget.reset(43);auto decision=chooseFormInput(state,{}, {},nullptr,0,false,&budget);check(budget.used()<=43&&decision.plan.steps>0,"Form exceeded planning cap or lost complete layers");
 budget.reset(43);chooseDualInput(state,state,{}, {},{},nullptr,false,true,&budget);check(budget.used()<=43,"Dual exceeded planning cap");
 FlightObservation flight;budget.reset(43);chooseFlightInput(flight,{}, {},nullptr,nullptr,&budget);check(budget.used()<=43,"Flight exceeded planning cap");
 GroundObservation ground;budget.reset(43);chooseGroundInput(ground,{},0,nullptr,&budget);check(budget.used()<=43,"Ground exceeded planning cap");
 budget.reset(0);check(!chooseFormInput(state,{}, {},nullptr,0,false,&budget).plan.steps,"Empty planning budget produced a new route");
 std::vector<Interaction> interactions(5);for(auto&i:interactions)i.area={1000,1020,0,500};interactions[1].area={-20,30,100,180};interactions[4].area={60,90,100,400};state={};state.y=130;state.grounded=false;
 std::vector<Interaction> active;interactionCandidates(interactions,0,2,14,0,.25,active);std::uint64_t used=0;check(advanceForm(state,true,.25,{},active,used)&&used==(1u<<1),"Interaction filtering changed stable orb slots");check(hasInteractionChain(state,interactions),"Upcoming interaction chain was missed");
}
bool chainRoad(unsigned enabled,bool bounded){FormState s;s.y=170;s.vy=-2;s.grounded=false;s.gravity=.958199;
 std::vector<Obstacle> objects{{80,240,91,115,false},{240,450,91,125,true}};
 std::vector<Interaction> rings(2);rings[0].area={50,70,130,170};rings[1].area={140,160,150,200};
 std::uint64_t used=0;PlanningBudget budget;InputCommitment plan;InputPacer pacer;bool armed=true;
 for(int t=0;t<300;t++){
  if(t%4==0)budget.reset(6000);
  s.pressRemaining=pacer.pressRemainingFrames(t)/4.;s.ringArmed=armed;bool desired=false;std::vector<Interaction> active;for(unsigned i=0;i<2;i++)if((enabled&(1u<<i))&&!(used&(1u<<i)))active.push_back(rings[i]);
  if(!plan.sample(t/4.,s,used,desired)){auto d=chooseFormInput(s,objects,active,nullptr,0,false,bounded?&budget:nullptr);desired=d.down;plan.begin(d.plan,t/4.,s,used);}
  bool down=pacer.apply(desired,t);if(!down)armed=true;
  for(int step=0;step<2;step++){
   double old=s.y;if(s.grounded&&down){s.vy=11.05;s.grounded=false;}double oldVy=s.vy;if(!s.grounded)s.vy-=.94*.125;s.x+=s.vx*.125;s.y+=(oldVy+s.vy)*.125/2;s.held=down;
   for(unsigned i=0;i<2;i++){auto const&r=rings[i].area;if((enabled&(1u<<i))&&!(used&(1u<<i))&&armed&&down&&s.x+14>r.left&&s.x-14<r.right&&s.y+14>r.bottom&&s.y-14<r.top){s.vy=11.05;s.grounded=false;armed=false;used|=1u<<i;}}
   s.grounded=false;if(s.y-14<=91&&s.vy<=0){s.y=105;s.vy=0;s.grounded=true;}
   for(auto const&o:objects)if(o.solid&&s.x+14>o.left&&s.x-14<o.right&&old-14>=o.top-.1&&s.y-14<=o.top&&s.vy<=0){s.y=o.top+14;s.vy=0;s.grounded=true;}
   for(auto const&o:objects)if(s.x+14>o.left&&s.x-14<o.right&&s.y+14>o.bottom+.05&&s.y-14<o.top-.05)return false;
  }
 }
 return used==3;}
void alternatives(){
 Search search(1337,120);for(int i=0;i<4;i++)search.observe({{40u+unsigned(i),true},{80u+unsigned(i),false}},600,40);
 search.observe({{220,true},{300,false}},600,40);auto saved=search.snapshot();bool alternate=false;for(auto const&e:saved["elites"])alternate|=e["actions"][0][0]==220;check(alternate,"Archive crowded out a different early route");
 auto resumed=Search::restore(saved);for(int i=0;i<128;i++)check(search.next(1000)==resumed.next(1000),"Alternative-route usage did not resume exactly");
}
Macro draft(){Macro m;m.environment={{"mod",ModVersion}};m.ticks=33;m.outerUpdates=33;m.bestPercent=10;m.timings.resize(33,{.25,false,true});for(Tick tick:{0,16,32})m.trace.push_back({tick,0,105,0,1,1,.25,0});return m;}
void background(){
 auto dir=std::filesystem::temp_directory_path()/"routefinder-background-tests";std::filesystem::remove_all(dir);SearchSaveWorker worker;
 for(unsigned i=1;i<=8;i++){auto best=draft();best.bestPercent=i*10;SearchSaveJob job{dir/"best.json",dir/"session.json",best,Search(1337,120)};job.seed=1337;job.attempts=i;job.epoch=i;worker.submit(std::move(job));}
 worker.drain();check(loadMacro(dir/"best.json").bestPercent==80,"Latest background draft was lost");check(parseSearchSession(loadJsonFile(dir/"session.json")).attempts==8,"Latest search snapshot was lost");auto result=worker.result();check(result&&result->error.empty()&&result->epoch==8,"Background completion failed");
 auto bad=draft();bad.ticks=0;SearchSaveJob invalid{dir/"invalid.json",dir/"unused.json",bad,Search(1337,120)};worker.submit(std::move(invalid));worker.drain();auto error=worker.result();check(error&&!error->error.empty(),"Background error was hidden");std::filesystem::remove_all(dir);
}
}
int main(){try{calibration();diagnosis();budgets();alternatives();check(chainRoad(3,false)&&chainRoad(3,true),"Two-orb/landing chain failed");check(!chainRoad(1,false)&&!chainRoad(2,false),"Chain fixture did not require both orbs");background();std::cout<<"Per-form robust calibration, observed death diagnosis, bounded planning, diverse routes and background saves: PASS\n";}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
