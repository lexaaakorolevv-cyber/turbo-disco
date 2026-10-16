#include "core.hpp"
#include "intelligence.hpp"
#include "planner.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool ok,char const*why){if(!ok)throw std::runtime_error(why);}
void sample(Calibration&c,double ratio,bool disturbed=false){
    FormState a;a.gravity=.958199;a.grounded=false;a.y=300;a.vy=4;
    auto b=a;b.x+=a.vx*.25;b.vy-=a.gravity*ratio*.25;
    c.resetSampling();c.observe({a,0,0,disturbed});c.observe({b,.25,0,disturbed});
}
void learning(){
    Calibration c;FormState a;a.gravity=.958199;a.grounded=false;a.y=300;a.vy=4;
    for(int i=0;i<32;i++)sample(c,1.18);
    check(std::abs(c.adjusted(a).gravity/a.gravity-1.18)<.01,"Stable observed physics not learned");
    sample(c,.82);
    check(std::abs(c.adjusted(a).gravity/a.gravity-1.18)<.02,"Single outlier replaced the model");
    for(int i=0;i<32;i++)sample(c,.82);
    auto adapted=c.adjusted(a);check(std::abs(adapted.gravity/a.gravity-.82)<.015,"Model did not recover after sustained prediction error");
    auto encoded=c.toJson();auto restored=Calibration::fromJson(encoded);
    check(restored.toJson()==encoded,"Validation/regime state lost on JSON roundtrip");
    for(int i=0;i<8;i++){sample(c,1.1);sample(restored,1.1);}
    check(c.toJson()==restored.toJson(),"Resumed learning diverged");
    auto before=c.toJson();for(int i=0;i<20;i++)sample(c,1.3,true);
    check(c.toJson()==before,"Disturbed native transitions fitted as ordinary physics");
    // Fit on observations above, evaluate an unseen multi-step trajectory.
    auto baseline=a,learned=adapted,truth=a;truth.gravity*=.82;
    for(int i=0;i<24;i++){std::uint64_t used=0;advanceForm(baseline,false,.25,{}, {},used);used=0;advanceForm(learned,false,.25,{}, {},used);
        truth.vy-=.958199*.82*.25;truth.y+=truth.vy*.25;truth.x+=truth.vx*.25;}
    check(std::abs(learned.y-truth.y)<std::abs(baseline.y-truth.y)*.1,"Held-out trajectory did not improve");
    auto bad=encoded;bad[0][4][5]=1.5;bool rejected=false;try{Calibration::fromJson(bad);}catch(...){rejected=true;}
    check(rejected,"Fractional validation count accepted");
    // An imported inaccurate estimate must be quarantined by real samples.
    bad=encoded;bad[0][4][0]=1.3;bad[0][4][3]=0.;bad[0][4][4]=.3;bad[0][4][5]=20;
    check(Calibration::fromJson(bad).adjusted(a).gravity==a.gravity,"Worse-than-default model was applied");
}
std::vector<Action> channel(std::vector<Action>const&all,int player){std::vector<Action> result;for(auto a:all)if(a.player==player)result.push_back(a);return result;}
void repairs(){
    unsigned total=0,worst=0;
    for(int seed:{7,42,1337})for(int press:{-2,-1,1,2})for(int length:{-2,2}){
        Search s(seed,120,true);std::vector<Action> initial{{20,true},{40,false},{100,true},{124,false},
            {180,true,1,2},{Tick(200+press),true},{Tick(224+press+length),false},{250,false,1,2}};
        s.observe(initial,280,60);bool solved=false;
        for(unsigned i=1;i<=96;i++){
            s.preferFailure(1,200+press,press<0?1:-1,false);auto plan=s.next(500);
            check(expandPlan(compactPlan(plan),500)==plan,"Joint edit produced invalid event order");
            auto first=channel(plan,1);
            if(first.size()==6&&first[4].tick==200&&first[5].tick==224){
                check(prefix(plan,160)==prefix(initial,160)&&channel(plan,2)==channel(initial,2),"Repair damaged prefix or other player");
                solved=true;total+=i;worst=std::max(worst,i);break;
            }
            s.observe(plan,280,plan==initial?60:59);
        }
        check(solved,"Coupled timing/hold repair not found");
    }
    Search s(42,120);s.observe({{200,true},{228,false}},280,60);s.preferFailure(1,200,-1,false);
    auto saved=s.snapshot();auto resumed=Search::restore(saved);
    for(int i=0;i<80;i++)check(s.next(500)==resumed.next(500),"Joint repair queue resume diverged");
    auto bad=saved;bad["edits"][0][4]=129;bool rejected=false;try{Search::restore(bad);}catch(...){rejected=true;}
    check(rejected,"Oversized paired offset accepted");
    std::cout<<"48 coupled press/hold repairs: total="<<total<<", worst="<<worst<<"\n";
}
void branches(){
    Search s(42,120);std::vector<Action> early{{40,true},{80,false}};
    s.observe(early,300,20);
    for(int i=0;i<40;i++)s.observe({{Tick(200+i),true},{Tick(260+i),false}},600,40+i*.1);
    auto state=s.snapshot();bool retained=false;for(auto const&e:state["elites"])retained|=e["actions"]==compactPlan(early);
    check(retained,"Lower-progress alternative crowded out by frontier variants");
    for(int i=0;i<32;i++)s.observe({{220,true},{280,false}},640,40);
    bool tried=false;
    for(int i=0;i<96;i++){s.next(1000);auto current=s.snapshot();for(auto const&e:current["elites"])if(e["actions"]==compactPlan(early)&&e["trials"].get<unsigned>()>0)tried=true;}
    check(tried,"Stagnant search never revisited an earlier branch");
    check(s.size()<=Search::ArchiveLimit,"Alternative archive became unbounded");
}
}
int main(){try{learning();repairs();branches();std::cout<<"Held-out calibration, regime recovery, joint repairs and earlier alternatives: PASS\n";}
catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
