#include "core.hpp"
#include <algorithm>
#include <cmath>
namespace rf {
void Search::observeCheckpoint(int percent,std::vector<Action> const& actions,Sample const& state,std::uint64_t attempt){
    if(!attempt||!state.tick||state.tick>=MaxTicks||
        std::find(CheckpointPercents.begin(),CheckpointPercents.end(),percent)==CheckpointPercents.end())return;
    auto found=std::find_if(m_checkpoints.begin(),m_checkpoints.end(),[&](auto const&c){return c.percent==percent;});
    auto route=prefix(actions,state.tick);
    if(found==m_checkpoints.end()){m_checkpoints.push_back({percent,std::move(route),state,1,attempt});return;}
    if(found->attempt==attempt)return;
    if(found->actions==route&&sameSample(found->state,state)){
        found->confirmations=std::min(found->confirmations+1,2u);found->attempt=attempt;
    }else if(found->confirmations<2)*found={percent,std::move(route),state,1,attempt};
}
void Search::invalidateCheckpoint(Tick tick){
    std::erase_if(m_checkpoints,[&](auto const&c){return c.state.tick==tick;});
    if(m_rollbackState&&m_rollbackState->tick==tick)m_rollbackState.reset();
}
int Search::rollbackPercent() const{
    if(m_rollbackState)for(auto const&c:m_checkpoints)if(c.state.tick==m_rollbackState->tick)return c.percent;
    return 0;
}
std::size_t Search::checkpointCount() const{
    return std::count_if(m_checkpoints.begin(),m_checkpoints.end(),[](auto const&c){return c.confirmations>=2;});
}
}
