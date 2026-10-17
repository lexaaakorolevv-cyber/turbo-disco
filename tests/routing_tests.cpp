#include "core.hpp"
#include "solo.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool ok,char const*why){if(!ok)throw std::runtime_error(why);}
bool winner(std::vector<Action>const&plan){
    return plan.size()==4&&plan[0]==Action{300,true}&&plan[1]==Action{320,false}&&plan[2]==Action{420,true}&&plan[3]==Action{440,false};
}
void deceptiveFrontier(){
    // Independent timing-gate search oracle: the early fork reaches a fatal
    // 75% frontier; the later fork currently reaches only 35% and needs repair.
    // Percent is an observed score, never evidence that a continuation exists.
    for(int seed:{7,42,1337,2026}){
        Search s(seed,120);std::vector<Action> viable{{300,true},{320,false},{422,true},{442,false}};
        std::vector<Action> trap{{20,true},{40,false},{700,true},{720,false}};
        s.observe(viable,450,35);s.observe(trap,800,75);
        for(int i=0;i<8;i++)s.observe(trap,800,75);
        auto plan=s.next(1000);
        check(s.phase()==SearchPhase::Explore&&prefix(plan,350)==prefix(viable,350)&&s.guidanceFrom()<=422,"High-percent trap retained control instead of the earlier alternative");
        check(s.best().percent==75,"Branch selection changed the HUD progress record");
        check(s.branchPercent()==35,"Pause status cannot distinguish the selected branch from the record");
        auto pending=s.snapshot();auto resumed=Search::restore(pending);
        check(resumed.snapshot()==pending,"Pending alternative attribution lost on resume");
        // Native guidance replayed the viable fork but still missed its next
        // timing gate. This exact duplicate must seed repairs despite 35<75.
        s.observe(viable,450,35);resumed.observe(viable,450,35);
        auto focus=s.snapshot();check(focus["focus"]==compactPlan(viable),"Lower-progress branch could not become a repair parent");
        bool solved=false;
        for(int i=0;i<64;i++){
            s.preferFailure(1,422,-1,false);resumed.preferFailure(1,422,-1,false);
            plan=s.next(1000);auto replay=resumed.next(1000);
            check(plan==replay&&s.guidanceFrom()==resumed.guidanceFrom(),"Alternative repairs diverged after resume");
            if(winner(plan)){s.observe(plan,1000,100);solved=true;break;}
            bool bad=!plan.empty()&&plan.front().tick<100;
            s.observe(plan,bad?800:450,bad?75:35);resumed.observe(plan,bad?800:450,bad?75:35);
        }
        check(solved&&s.best().percent==100,"Search could not repair the lower-percent route past a deceptive frontier");
    }
}
void failureAccounting(){
    Search s(42,120);std::vector<Action> trap{{20,true},{40,false},{700,true},{720,false}};
    s.observe(trap,800,75);for(int i=0;i<8;i++)s.observe(trap,800,75);
    auto saved=s.snapshot();check(saved["elites"][0]["stalls"]==8,"Duplicate deaths did not count as branch failures");
    s.observe(trap,800,76);check(s.snapshot()["elites"][0]["stalls"]==0,"Meaningful native progress did not clear the stall counter");
    bool rejected=false;auto bad=saved;bad["elites"][0]["stalls"]=-1;try{Search::restore(bad);}catch(...){rejected=true;}check(rejected,"Negative failure count accepted");
    bad=saved;bad["elites"][0]["frontier"]=101;rejected=false;try{Search::restore(bad);}catch(...){rejected=true;}check(rejected,"Invalid branch frontier accepted");
    bad=saved;bad["pending_route"]=0;rejected=false;try{Search::restore(bad);}catch(...){rejected=true;}check(rejected,"Unknown pending branch accepted");
}
void liveCube(){
    FormState falling;falling.y=650;falling.grounded=false;
    auto d=chooseSoloInput(falling,{}, {},nullptr,4,1337,true);
    check(d.plan.steps>0&&d.survival>42&&d.survival<=66,"Live solo entry did not use extended landing forecast for an ordinary cube");
    check(std::abs(d.forecast.path.back().y-105)<.01,"Live cube forecast stopped before landing");
    FormState normal;auto empty=chooseSoloInput(normal,{}, {},nullptr,0,1337,false);
    check(!empty.down,"Default cube needlessly jumped on empty ground");
}
}
int main(){try{deceptiveFrontier();failureAccounting();liveCube();std::cout<<"Deceptive 75/35-percent frontier, lower-branch repairs, duplicate death attribution, resume and live cube entry: PASS\n";}
catch(std::exception const&e){std::cerr<<e.what()<<'\n';return 1;}}
