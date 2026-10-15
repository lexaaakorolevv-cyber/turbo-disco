#include "geometry.hpp"
#include "guidance_clock.hpp"
#include "planner.hpp"
#include "flight.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace rf;
namespace {void check(bool b,char const*s){if(!b)throw std::runtime_error(s);}}
int main(){try{
 std::mt19937_64 rng(170);auto val=[&](double scale){return (static_cast<double>(rng()%100000)/50000.-1)*scale;};
 for(int i=0;i<30000;i++){
  double x=val(200),y=val(200);Obstacle o{x,x+5+std::abs(val(30)),y,y+5+std::abs(val(30))};
  o.vx=val(10);o.vy=val(10);o.motionHorizon=std::abs(val(8));if(i%3==0)o.radius=(o.right-o.left)/2;
  double x1=val(250),x2=x1+val(80),y1=val(250),y2=y1+val(80),start=std::abs(val(10)),end=start+std::abs(val(2)),rx=std::abs(val(18));
  if(forecastOverlap(x1,y1,x2,y2,rx,12,o,start,end))check(overlapsHorizontal(o,x1,x2,rx,start,end),"Broad phase discarded a possible moving collision");
 }
 GuidanceClock clock;FormState s;check(clock.needsPlan(1,s,0,0),"First plan skipped");
 for(int i=0;i<4;i++)check(!clock.needsPlan(1,s,0,0),"Same native update redundantly replanned");
 check(clock.needsPlan(2,s,0,0),"Next outer update not planned");s.inverted=true;check(clock.needsPlan(2,s,0,0),"Gravity change cached");
 s.grounded=false;check(clock.needsPlan(2,s,0,0),"Takeoff cached");s.form=Form::Ufo;check(clock.needsPlan(2,s,0,0),"Form change cached");
 check(clock.needsPlan(2,s,16,0),"Orb contact cached");check(clock.needsPlan(2,s,16,1),"Input edge cached");s.x+=100;check(clock.needsPlan(2,s,16,1),"Teleport cached");
 clock.reset();check(clock.needsPlan(2,s,16,1),"Attempt reset kept cache");
 FailureMemory memory;double expected=0;for(int i=0;i<256;i++){FailureRecord r;r.x=(i%21-10)*7;r.y=150;r.vy=0;r.held=false;r.count=i%7+1;r.form=Form::Ufo;memory.record(r);}
 s={};s.form=Form::Ufo;s.x=0;s.y=150;s.vy=0;
 for(auto const&r:memory.toJson())if(std::abs(r[2].get<double>())<28)expected+=std::min(r[8].get<unsigned>(),10u)*.6*(1-std::abs(r[2].get<double>())/28);
 check(std::abs(memory.penalty(s,false)-std::min(expected,8.))<1e-12,"Indexed memory differs from exhaustive cost");
 auto copy=memory;memory={};check(std::abs(copy.penalty(s,false)-std::min(expected,8.))<1e-12,"Memory copy kept stale references");
 auto restored=FailureMemory::fromJson(copy.toJson());check(restored.penalty(s,false)==copy.penalty(s,false),"Restored memory index differs");
 s.grounded=false;s.floor=0;s.ceiling=300;auto a=chooseFormInput(s,{}, {});auto b=chooseFormInput(s,{}, {},nullptr,0,false);
 check(a.down==b.down&&a.survival==b.survival&&!a.forecast.path.empty()&&b.forecast.path.empty(),"Hidden path changed planning");
 std::vector<Obstacle> clutter;for(int i=0;i<128;i++)clutter.push_back({10000.+i*20,10010.+i*20,0,300});
 auto c=chooseFormInput(s,clutter,{});check(c.down==a.down&&c.survival==a.survival&&c.forecast.path.size()==a.forecast.path.size(),"Distant objects changed planner");
 for(std::size_t i=0;i<c.forecast.path.size();i++)check(c.forecast.path[i].x==a.forecast.path[i].x&&c.forecast.path[i].y==a.forecast.path[i].y,"Filtered path changed");
 std::cout<<"30,000 moving broad-phase cases; event/reset invalidation, indexed memory equivalence and forecast visibility: PASS\n";
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
