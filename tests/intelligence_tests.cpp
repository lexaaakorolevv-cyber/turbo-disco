#include "intelligence.hpp"
#include "planner.hpp"
#include "flight.hpp"
#include "ground.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace rf;
namespace{void check(bool b,char const*s){if(!b)throw std::runtime_error(s);}template<class F>void rejects(F f){bool bad=false;try{f();}catch(...){bad=true;}check(bad,"Malformed intelligence accepted");}}
int main(){try{
 Calibration c;FormState s;s.grounded=false;s.gravity=.958199;s.vy=9;s.vx=5.2;
 for(int i=0;i<32;i++){c.observe({s,i*.25,0,false});s.x+=s.vx*.25;s.vy-=.958199*1.12*.25;s.y+=s.vy*.25;}
 auto tuned=c.adjusted(s);check(c.samples()>=20&&std::abs(tuned.gravity/.958199-1.12)<.02,"Gravity calibration did not converge");
 auto restored=Calibration::fromJson(nlohmann::json::parse(c.toJson().dump()));check(restored.toJson()==c.toJson(),"Calibration persistence changed values");
 auto before=c.toJson();s.x+=1000;s.vy=100;c.observe({s,8.25,8,true});check(c.toJson()==before,"Teleport/orb impulse corrupted calibration");
 Calibration wave;s={};s.form=Form::Wave;s.grounded=false;s.held=true;s.x=0;s.y=100;
 for(int i=0;i<20;i++){wave.observe({s,i*.25,0,false});s.x+=s.vx*.25;s.y+=s.vx*.25*1.08;}
 check(std::abs(wave.adjusted(s).waveSlope-1.08)<.01,"Wave slope calibration failed");
 Calibration leftWave;s.x=0;s.vx=-5.2;
 for(int i=0;i<20;i++){leftWave.observe({s,i*.25,0,false});s.x+=s.vx*.25;s.y+=std::abs(s.vx)*.25*1.08;}
 check(std::abs(leftWave.adjusted(s).waveSlope-1.08)<.01,"Left-moving wave calibration failed");
 auto bad=c.toJson();bad[0][0][0]=99;rejects([&]{Calibration::fromJson(bad);});
 MotionTracker motion;Obstacle moving{100,120,0,20};moving.id=12;
 for(int i=0;i<5;i++){auto sample=moving;sample.bottom+=i*2;sample.top+=i*2;motion.update(sample,i*.25);moving.vx=sample.vx;moving.vy=sample.vy;moving.motionHorizon=sample.motionHorizon;}
 check(std::abs(moving.vy-8)<1e-6&&moving.motionHorizon>0,"Moving obstacle velocity not measured");
 auto future=obstacleAt(moving,2);check(future.bottom==16,"Future obstacle position wrong");
 Obstacle hazard{40,50,70,90};hazard.vy=-10;hazard.motionHorizon=8;
 check(!sweptOverlap(0,30,60,30,3,3,hazard),"Static moving-hazard fixture is invalid");
 check(forecastOverlap(0,30,60,30,3,3,hazard,0,6),"Swept moving hazard missed a crossing");
 motion.reset();auto reset=moving;reset.vy=reset.motionHorizon=0;motion.update(reset,0);check(reset.motionHorizon==0,"New attempt reused old motion");
 FailureMemory memory;s={};s.x=200;s.y=105;s.vy=0;
 FailureRecord failure;failure.form=Form::Cube;failure.kind=FailureKind::LatePress;failure.x=200;failure.y=105;failure.held=false;
 for(int i=0;i<4;i++)memory.record(failure);
 check(memory.size()==1&&memory.timingBias(s)>0&&memory.penalty(s,false)>memory.penalty(s,true),"Failure memory did not bias a repeated mistake");
 auto recovered=FailureMemory::fromJson(nlohmann::json::parse(memory.toJson().dump()));check(recovered.toJson()==memory.toJson(),"Failure memory lost on reload");
 auto invalid=memory.toJson();invalid[0][0]=std::uint64_t(1)<<40;rejects([&]{FailureMemory::fromJson(invalid);});
 invalid=memory.toJson();invalid[0][8]=-1;rejects([&]{FailureMemory::fromJson(invalid);});
 for(int i=0;i<400;i++){failure.x=i*100.;memory.record(failure);}check(memory.size()==256,"Failure memory is unbounded");
 FlightObservation f;Forecast trace;bool input=chooseFlightInput(f,{}, {},&trace);
 check(trace.path.size()>2&&trace.down==input&&trace.path.front().x==f.x,"Flight forecast trace missing");
 auto without=chooseFlightInput(f,{});check(input==without,"Drawing changed flight decisions");
 GroundObservation g;std::vector<Obstacle> spike{{140,160,91,120}};Forecast ground;
 auto decision=chooseGroundInput(g,spike,0,&ground);check(ground.path.size()>2&&ground.down==decision.down,"Ground forecast trace missing");
 std::cout<<"Observed physics calibration, disturbance rejection, moving collision prediction, bounded failure memory, persistence and side-effect-free traces: PASS\n";
}catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
