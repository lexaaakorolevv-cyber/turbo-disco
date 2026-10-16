#pragma once
#include <chrono>
#include <cstdint>
namespace rf {
// A budget only limits approximate planning. It never drops native physics or
// recorded input. Tests may use a deterministic node cap without a deadline.
class PlanningBudget {
    unsigned m_left=0,m_used=0;
    bool m_timed=false,m_started=false;
    double m_milliseconds=0;
    std::chrono::steady_clock::time_point m_deadline{};
public:
    void reset(unsigned nodes,double milliseconds=0){m_left=nodes;m_used=0;m_timed=milliseconds>0;m_milliseconds=milliseconds;m_started=false;}
    bool available(){if(m_timed&&m_started&&std::chrono::steady_clock::now()>=m_deadline)m_left=0;return m_left>0;}
    bool consume(){
        if(!m_left)return false;
        if(m_timed&&!m_started){m_started=true;m_deadline=std::chrono::steady_clock::now()+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(m_milliseconds));}
        if((m_used&63)==0&&!available())return false;--m_left;++m_used;return true;
    }
    unsigned used() const{return m_used;}
};
}
