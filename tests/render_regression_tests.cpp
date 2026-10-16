#include "planner.hpp"
#include "core.hpp"
#include "ground.hpp"
#include <stdexcept>
#include "input.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
using namespace rf;
void step(FormState&s,bool down){double sign=s.inverted?-1:1;bool press=down&&!s.held;if(s.form==Form::Cube&&s.grounded&&down){s.vy=sign*11.05*(s.scale<.9?.8:1);s.grounded=false;}if(s.form==Form::Ball&&s.grounded&&press){s.vy=sign*6.7*(s.scale<.9?.8:1);s.inverted=!s.inverted;s.grounded=false;}sign=s.inverted?-1:1;double v=s.vy;if(!s.grounded)s.vy-=sign*(s.form==Form::Ball?.57:.94)*.125;s.x+=s.vx*.125;s.y+=(v+s.vy)*.125/2;s.held=down;s.grounded=false;if(!s.inverted&&s.y- s.halfHeight<=s.floor){s.y=s.floor+s.halfHeight;s.vy=0;s.grounded=true;}if(s.inverted&&s.y+s.halfHeight>=s.ceiling){s.y=s.ceiling-s.halfHeight;s.vy=0;s.grounded=true;}}
struct Result{bool passed=false;std::vector<Action> events;};
Result run(unsigned seed,std::span<unsigned const> partitions,bool legacy){std::mt19937 rng(seed+23057);FormState s;s.floor=91;s.ceiling=451;s.gravity=.958199;s.vx=seed%3==0?7.8:seed%3==1?6.457:5.2;s.inverted=seed%2;s.form=seed<96?Form::Cube:Form::Ball;s.scale=seed%4==0?.6:1;s.halfHeight=s.halfWidth=14*s.scale;s.y=s.inverted?s.ceiling-s.halfHeight:s.floor+s.halfHeight;
 std::vector<Obstacle> objects;double x=150;for(int i=0;i<4;i++){double h=18+rng()%24;objects.push_back({x,x+16+rng()%23,91,91+h});if(s.form==Form::Ball)objects.push_back({x+75,x+100,451-h,451});x+=115+rng()%140;}if(s.inverted)for(auto&o:objects){double b=542-o.top;o.top=542-o.bottom;o.bottom=b;}
 Result result;bool held=false;unsigned frame=0,nextRender=0;PlanningBudget budget;InputPacer pacer;InputCommitment c;bool requested=false;int ticks=std::ceil((x+60)/(s.vx*.25));for(int t=0;t<ticks;t++){if(unsigned(t)==nextRender){budget.reset(6000);nextRender+=partitions[frame++%partitions.size()];}s.pressRemaining=pacer.pressRemainingFrames(t)/4.;bool desired=requested;if(!c.sample(t/4.,s,0,desired)){if(!legacy||budget.available()){auto d=chooseFormInput(s,objects,{},nullptr,0,false,legacy?&budget:nullptr);desired=d.down;c.begin(d.plan,t/4.,s,0);}}requested=desired;bool down=pacer.apply(desired,t);if(down!=held)appendAction(result.events,t,down);held=down;for(int i=0;i<2;i++){step(s,down);for(auto const&o:objects)if(s.x+s.halfWidth>o.left&&s.x-s.halfWidth<o.right&&s.y+s.halfHeight>o.bottom+.05&&s.y-s.halfHeight<o.top-.05)return result;}}
 result.passed=true;return result;}


void check(bool ok,char const*why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 // These roads use independent .125-unit physics, not advanceForm. The
 // 1.10.0 render cap loses a necessary jump/flip even without a wall timer.
 const unsigned sixty[]{4},fifteen[]{16};
 check(!run(1,fifteen,true).passed,"15 FPS fixture no longer exposes old truncation");
 check(!run(111,sixty,true).passed,"60 FPS fixture no longer exposes old truncation");
 const std::vector<std::vector<unsigned>> rates{{1},{1,2,2},{4},{8},{16}};
 for(unsigned seed:{1,6,13,111}){
  auto reference=run(seed,sixty,false);check(reference.passed,"Full forecast failed a regression road");
  for(auto const&rate:rates){auto actual=run(seed,rate,false);check(actual.passed,"Render partition changed survival");check(actual.events==reference.events,"Render FPS changed recorded physics-tick inputs");}
 }
 // The ground controller used to retain a held input when its passive
 // forecast exhausted the frame budget. Empty ground needs a release.
 GroundObservation ground;ground.held=true;PlanningBudget empty;empty.reset(0);
 check(chooseGroundInput(ground,{},0,nullptr,&empty).down,"Old stale-hold fixture changed");
 check(!chooseGroundInput(ground,{},0,nullptr).down,"Live ground controller retained stale hold");
 std::cout<<"Render-cap failures reproduced at 15/60 FPS; 4 independent roads x 5 render schedules pass with identical tick events; ground stale-hold regression: PASS\n";
 }catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
