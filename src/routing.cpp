#include "core.hpp"
#include <algorithm>
#include <tuple>
namespace rf {
std::optional<double> Search::branchPercent()const{
    std::optional<double> value;if(m_pendingRoute)for(auto const&e:m_elites)if(e.routeKey==*m_pendingRoute)value=std::max(value.value_or(0),e.percent);return value;
}
std::uint64_t Search::routeStalls(std::uint64_t route)const{
    std::uint64_t value=0;for(auto const&e:m_elites)if(e.routeKey==route)value=std::max(value,e.stalls);return value;
}
std::optional<std::size_t> Search::alternative(std::uint64_t excluded)const{
    std::optional<std::size_t> best;
    // Compare failed attempts and actual issued trials, rather than percent.
    // Earlier branches win ties so frontier variants cannot crowd them out.
    auto score=[&](std::size_t i){auto const&e=m_elites[i];return std::tuple{routeStalls(e.routeKey),e.trials,e.deathTick,-e.percent};};
    for(std::size_t i=0;i<m_elites.size();++i)if(m_elites[i].routeKey!=excluded&&(!best||score(i)<score(*best)))best=i;
    return best;
}
}
