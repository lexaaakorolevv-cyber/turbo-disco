#include "core.hpp"
#include "intelligence.hpp"
#include "planner.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
std::vector<Action> channel(std::vector<Action> const&all,int player){std::vector<Action> result;for(auto a:all)if(a.player==player)result.push_back(a);return result;}
void physics(){
    Calibration ufo;
    for(int i=0;i<16;i++){
        ufo.resetSampling();FormState a;a.form=Form::Ufo;a.grounded=false;a.vy=.05;a.y=200;
        auto b=a;b.x+=a.vx*.25;b.vy=-.075;
        ufo.observe({a,0,0});ufo.observe({b,.25,0});
    }
    FormState probe;probe.form=Form::Ufo;
    check(ufo.samples()==0&&ufo.adjusted(probe).gravity==probe.gravity,"UFO apex contaminated gravity calibration");
    Calibration pad;
    for(int i=0;i<16;i++){
        pad.resetSampling();FormState a;auto b=a;b.x+=a.vx*.25;b.grounded=false;b.held=true;b.padContact=true;b.vy=10.2;
        pad.observe({a,0,0});pad.observe({b,.25,1});
    }
    check(pad.samples()==0,"Pad impulse learned as a normal jump");
    FormState normal,mini=normal;mini.scale=.6;mini.halfWidth=mini.halfHeight=8.4;
    check(!equivalentFormStates(normal,mini),"Mini route merged with full-size route");
    auto speed=normal;speed.vx=7.8;check(!equivalentFormStates(normal,speed),"Different speed routes merged");
    auto gravity=normal;gravity.gravityMod=2;check(!equivalentFormStates(normal,gravity),"Different gravity routes merged");
    FormState falling;falling.y=650;falling.grounded=false;falling.vy=0;
    auto landing=chooseFormInput(falling,{}, {},nullptr,0,true);
    check(landing.survival>42&&landing.survival<=66&&std::abs(landing.forecast.path.back().y-105)<.01,"Forecast stopped before checking landing");
    auto walking=chooseFormInput(normal,{}, {},nullptr,0,false);
    check(walking.survival==42&&!walking.down,"Safe walking needlessly extended or jumped");
}
void independentEdits(){
    std::vector<Action> parent{{10,true,1,1},{11,true,1,2},{20,false,1,1},{25,false,1,2},{50,true,1,1},{60,false,1,1}};
    Search search(1337,120,true);search.observe(parent,90,30,true);search.preferPlayer(1);unsigned tested=0;bool repaired=false;
    for(unsigned i=0;i<128;i++){
        auto next=search.next(200);check(expandPlan(compactPlan(next),200)==next,"Focused channel edit lost event ordering");
        if(search.phase()!=SearchPhase::Timing)continue;
        bool first=channel(next,1)!=channel(parent,1),second=channel(next,2)!=channel(parent,2);
        check(!(first&&second),"P1 timing repair moved P2 input");++tested;auto p1=channel(next,1);
        if(!second&&p1.size()==4&&p1[0].tick==9&&p1[1].tick==19&&p1[2].tick==50&&p1[3].tick==60)repaired=true;
    }
    check(tested>=20&&repaired,"Isolated paired timing repair was not explored");
}
void checkpoints(){
    Search search(42,120,true);std::vector<Action> actions{{10,true,1,1},{12,true,1,2},{20,false,1,1},{25,false,1,2},{50,true,1,1},{60,false,1,1}};
    Sample state{32,100,105,0,1,1,.25,1u<<12};state.second=PlayerSample{100,437,0,1,1,2};
    search.observeCheckpoint(10,actions,state,1);search.observeCheckpoint(10,actions,state,1);
    check(search.checkpointCount()==0,"One attempt falsely confirmed a checkpoint");
    auto changed=state;changed.second->y+=1;search.observeCheckpoint(10,actions,changed,2);
    check(search.checkpointCount()==0,"Different P2 endpoint confirmed a checkpoint");
    search.observeCheckpoint(10,actions,state,3);search.observeCheckpoint(10,actions,state,4);
    check(search.checkpointCount()==1,"Repeated native endpoint did not confirm");
    for(int i=0;i<12;i++)search.observe(actions,90,30);
    bool rolled=false;
    for(int i=0;i<8;i++){
        auto next=search.next(200);if(!search.rollbackState())continue;
        check(next==prefix(actions,state.tick)&&search.guidanceFrom()==state.tick&&sameSample(state,*search.rollbackState()),"Rollback did not retain exact replay prefix and endpoint");
        check(search.rollbackPercent()==10,"Wrong rollback milestone");rolled=true;break;
    }
    check(rolled,"Stagnation did not roll back to a confirmed checkpoint");
    auto saved=search.snapshot();auto restored=Search::restore(saved);
    check(restored.rollbackState()&&sameSample(*restored.rollbackState(),state),"Pending rollback endpoint lost on resume");
    for(int i=0;i<32;i++)check(search.next(200)==restored.next(200),"Checkpoint search resume diverged");
    search.invalidateCheckpoint(state.tick);check(search.checkpointCount()==0&&!search.rollbackState(),"Desynchronized checkpoint remained usable");
    auto malformed=saved;malformed["checkpoints"][0]["confirmations"]=3;bool rejected=false;
    try{Search::restore(malformed);}catch(...){rejected=true;}check(rejected,"Untrusted checkpoint confirmation accepted");
    malformed=saved;malformed["checkpoints"][0]["state"][8]=nullptr;rejected=false;
    try{Search::restore(malformed);}catch(...){rejected=true;}check(rejected,"Checkpoint lost required second-player state");
}
}
int main(){try{physics();independentEdits();checkpoints();std::cout<<"Calibration guards, distinct physics routes, landing continuation, isolated P1/P2 edits and confirmed replay checkpoints: PASS\n";}
catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
