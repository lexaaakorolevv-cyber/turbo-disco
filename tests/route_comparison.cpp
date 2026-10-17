#include "planner.hpp"
#include "solo.hpp"
#include "ground.hpp"
#include <string_view>
#include "input.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
using namespace rf;
void step(FormState&s,bool down){double sign=s.inverted?-1:1;bool press=down&&!s.held;if(s.form==Form::Cube&&s.grounded&&down){s.vy=sign*11.05*(s.scale<.9?.8:1);s.grounded=false;}if(s.form==Form::Ball&&s.grounded&&press){s.vy=sign*6.7*(s.scale<.9?.8:1);s.inverted=!s.inverted;s.grounded=false;}sign=s.inverted?-1:1;double v=s.vy;if(!s.grounded)s.vy-=sign*(s.form==Form::Ball?.57:.94)*.125;s.x+=s.vx*.125;s.y+=(v+s.vy)*.125/2;s.held=down;s.grounded=false;if(!s.inverted&&s.y- s.halfHeight<=s.floor){s.y=s.floor+s.halfHeight;s.vy=0;s.grounded=true;}if(s.inverted&&s.y+s.halfHeight>=s.ceiling){s.y=s.ceiling-s.halfHeight;s.vy=0;s.grounded=true;}}
bool run(unsigned seed,bool legacy=false){std::mt19937 rng(seed+23057);FormState s;s.floor=91;s.ceiling=451;s.gravity=.958199;s.vx=seed%3==0?7.8:seed%3==1?6.457:5.2;s.inverted=seed%2;s.form=seed<96?Form::Cube:Form::Ball;s.scale=seed%4==0?.6:1;s.halfHeight=s.halfWidth=14*s.scale;s.y=s.inverted?s.ceiling-s.halfHeight:s.floor+s.halfHeight;
 std::vector<Obstacle> objects;double x=150;for(int i=0;i<4;i++){double h=18+rng()%24;objects.push_back({x,x+16+rng()%23,91,91+h});if(s.form==Form::Ball)objects.push_back({x+75,x+100,451-h,451});x+=115+rng()%140;}if(s.inverted)for(auto&o:objects){double b=542-o.top;o.top=542-o.bottom;o.bottom=b;}
 InputPacer pacer;InputCommitment c;int ticks=std::ceil((x+60)/(s.vx*.25));for(int t=0;t<ticks;t++){s.pressRemaining=pacer.pressRemainingFrames(t)/4.;bool desired=false;if(legacy&&s.form==Form::Cube){GroundObservation g{s.x,s.y,s.vy,s.vx,s.halfWidth,s.halfHeight,s.inverted?s.ceiling:s.floor,s.jump*(s.scale<.9?.8:1),s.gravity*s.gravityMod,0,s.grounded,s.inverted,s.held,s.pressInterval,s.pressRemaining};desired=chooseGroundInput(g,objects,0,nullptr).down;}else if(!c.sample(t/4.,s,0,desired)){auto d=chooseSoloInput(s,objects,{},nullptr,0,1337,false);desired=d.down;c.begin(d.plan,t/4.,s,0);}bool down=pacer.apply(desired,t);for(int i=0;i<2;i++){step(s,down);for(auto const&o:objects)if(s.x+s.halfWidth>o.left&&s.x-s.halfWidth<o.right&&s.y+s.halfHeight>o.bottom+.05&&s.y-s.halfHeight<o.top-.05)return false;}}
 return true;}

void forecasts(){for(unsigned seed=0;seed<512;seed++){std::mt19937 rng(7823+seed);FormState s;s.form=seed%3==0?Form::Ship:seed%3==1?Form::Ufo:Form::Swing;s.floor=91;s.ceiling=451;s.y=170+rng()%200;s.vy=int(rng()%15)-7;s.vx=7.8;s.grounded=false;s.gravity=.958199;s.inverted=seed%2;s.held=seed%4>1;std::vector<Obstacle> objects;for(int i=0;i<8;i++){double x=50+i*35+rng()%20,y=91+rng()%300;objects.push_back({x,x+20+rng()%30,y,y+30+rng()%80});}auto d=chooseFormInput(s,objects,{},nullptr,0,false);std::cout<<seed<<" "<<d.survival<<"\n";}}

int main(int argc,char**argv){bool legacy=argc>1&&std::string_view(argv[1])=="--legacy";if(argc>1&&!legacy){forecasts();return 0;}for(unsigned i=0;i<128;i++)std::cout<<i<<" "<<run(i,legacy)<<"\n";}
