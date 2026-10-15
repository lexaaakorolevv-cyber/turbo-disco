#include "planner.hpp"
#include "flight.hpp"
#include "ground.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
using namespace rf;
int main(){
 std::vector<Obstacle> dense;for(int i=0;i<509;i++){double x=-80+(i%64)*9,y=(i/64<4?-160+(i/64)*40:340+(i/64-4)*40);dense.push_back({x,x+6,y,y+12});if(i%7==0){dense.back().vy=.3;dense.back().vx=.1;dense.back().motionHorizon=8;}}
 dense.push_back({90,110,91,116});dense.push_back({210,230,91,121});dense.push_back({330,350,91,116});
 FailureMemory memory;for(int i=0;i<256;i++){FailureRecord r;r.form=i%2?Form::Ship:Form::Ufo;r.x=-6000+i*50;r.y=150+(i%5)*10;r.vy=i%7;r.kind=FailureKind::NeedHold;memory.record(r);}
 auto run=[&](char const*name,auto fn){std::uint64_t hash=1469598103934665603ULL;auto start=std::chrono::steady_clock::now();for(int i=0;i<160;i++){auto value=fn(i);hash^=value;hash*=1099511628211ULL;}auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<name<<" ms="<<std::fixed<<std::setprecision(3)<<ms<<" fingerprint="<<hash<<'\n';};
 for(bool preview:{false,true}){
  std::string suffix=preview?"/path":"/no-path";
  run(("ship"+suffix).c_str(),[&](int i){FlightObservation s;s.x=i%20;s.y=130+i%12;s.vy=(i%9-4)*.6;s.pressInterval=3.75;Forecast path;return chooseFlightInput(s,dense,{},preview?&path:nullptr,&memory);});
  run(("wave"+suffix).c_str(),[&](int i){FlightObservation s;s.mode=FlightMode::Wave;s.x=i%20;s.y=130+i%12;s.pressInterval=3.75;Forecast path;return chooseFlightInput(s,dense,{},preview?&path:nullptr,&memory);});
 }
 run("ufo/path",[&](int i){FormState s;s.form=Form::Ufo;s.grounded=false;s.x=i%20;s.y=130+i%12;s.floor=0;s.ceiling=300;s.vy=(i%9-4)*.6;auto r=chooseFormInput(s,dense,{},&memory);return r.down;});
 run("cube/path",[&](int i){GroundObservation s;s.x=i%20;Forecast path;return chooseGroundInput(s,dense,0,&path).down;});
}
