#pragma once
#include "intelligence.hpp"
#include "core.hpp"
namespace rf {
struct HistoryFrame {FormState state;Tick tick=0;double time=0;};
class PlayerHistory {
    std::array<HistoryFrame,120> m_frames{};unsigned m_begin=0,m_count=0;
public:
    void reset(){m_begin=m_count=0;}
    void record(FormState const&s,Tick tick,double time){
        if(m_count&&at(m_count-1).time==time){m_frames[(m_begin+m_count-1)%120]={s,tick,time};return;}
        if(m_count==120){m_begin=(m_begin+1)%120;--m_count;}
        m_frames[(m_begin+m_count++)%120]={s,tick,time};
    }
    unsigned size()const{return m_count;}
    HistoryFrame const& at(unsigned index)const{return m_frames[(m_begin+index)%120];}
};
struct DeathDiagnosis {FailureKind kind=FailureKind::Unknown;Tick actionTick=0;int timingDirection=0;bool duration=false;};
// Observational hypothesis only; it never restores the world or certifies cause.
DeathDiagnosis diagnoseDeath(FormState const& state,PlayerHistory const& history,
    std::optional<Obstacle> const& hazard,Tick deathTick);
}
