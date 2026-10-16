#pragma once
#include "intelligence.hpp"
#include <algorithm>
#include <cmath>
namespace rf {
struct ControlPlan {
    unsigned short bits=0;
    unsigned steps=0;
    double duration=0;
    bool at(double elapsed) const {
        if(!steps)return false;
        unsigned index=elapsed<2?static_cast<unsigned>(std::max(0.,std::floor(elapsed*4+1e-7))):8+static_cast<unsigned>(std::floor((elapsed-2)*2+1e-7));
        return bits&(1u<<std::min(index,steps-1));
    }
};
// Execute a short selected route instead of postponing its next press on each
// new forecast. All native transitions and input recording still run normally.
class InputCommitment {
    ControlPlan plan;
    FormState first,second;
    double began=0;
    std::uint64_t event=0;
    bool valid=false,dual=false;
    bool matches(FormState const&a,FormState const&b,double elapsed) const {
        return a.form==b.form&&a.inverted==b.inverted&&a.grounded==b.grounded&&
            std::abs(a.scale-b.scale)<1e-6&&std::abs(a.vx-b.vx)<1e-6&&std::abs(a.gravityMod-b.gravityMod)<1e-6&&
            std::abs(a.x-b.x-b.vx*elapsed)<4&&std::abs(a.y-b.y)<80;
    }
public:
    void reset(){valid=false;}
    void begin(ControlPlan p,double time,FormState const&a,std::uint64_t events,FormState const*b=nullptr){
        plan=p;began=time;first=a;event=events;dual=b!=nullptr;if(b)second=*b;valid=p.steps&&p.duration>0;
    }
    bool sample(double time,FormState const&a,std::uint64_t events,bool&down,FormState const*b=nullptr){
        double elapsed=time-began;
        if(!valid||elapsed<0||elapsed+1e-8>=plan.duration||events!=event||dual!=(b!=nullptr)||!matches(a,first,elapsed)||(b&&!matches(*b,second,elapsed))){valid=false;return false;}
        down=plan.at(elapsed);return true;
    }
};
}
