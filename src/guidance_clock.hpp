#pragma once
#include "intelligence.hpp"
#include <cmath>
#include <cstdint>
namespace rf {
// Expensive planning uses the native outer clock. Input recording/replay still
// runs on every processCommands boundary. Discrete native events invalidate it.
class GuidanceClock {
    bool valid=false;
    std::uint64_t outer=0,events=0,edges=0;
    FormState previous;
public:
    void reset(){valid=false;}
    bool needsPlan(std::uint64_t now,FormState const&s,std::uint64_t event,std::uint64_t edge){
        bool changed=!valid||now!=outer||event!=events||edge!=edges||s.form!=previous.form||
            s.grounded!=previous.grounded||s.inverted!=previous.inverted||s.held!=previous.held||
            std::abs(s.scale-previous.scale)>1e-6||std::abs(s.vx-previous.vx)>1e-6||
            std::abs(s.x-previous.x)>4||std::abs(s.y-previous.y)>8;
        if(changed){valid=true;outer=now;events=event;edges=edge;previous=s;}
        return changed;
    }
};
}
