#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace rf {
namespace {
using json = nlohmann::json;
void check(bool ok, char const* message) { if (!ok) throw std::runtime_error(message); }
std::uint64_t integer(json const& v) {
    check(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>() >= 0), "Invalid session integer");
    return v.get<std::uint64_t>();
}
std::uint64_t counter(json const& v) {
    auto n = integer(v); check(n <= 1000000000000ULL, "Session counter out of range"); return n;
}
}
nlohmann::json compactPlan(std::vector<Action> const& actions) {
    auto result = nlohmann::json::array();
    for (auto const& a : actions) result.push_back({a.tick,a.down,a.player});
    return result;
}
std::vector<Action> expandPlan(nlohmann::json const& value, Tick end) {
    check(value.is_array() && value.size() <= 2*MaxTicks, "Invalid session action array");
    std::vector<Action> result; result.reserve(value.size());
    bool held[2]{}; Tick previous = 0;int previousPlayer=0;
    for (auto const& row : value) {
        check(row.is_array() && (row.size() == 2 || row.size()==3) && row[1].is_boolean(), "Invalid compact action");
        Tick t = integer(row[0]); bool down = row[1].get<bool>();
        auto player=row.size()==3?integer(row[2]):1;check(player==1||player==2,"Invalid compact player");
        check(t < end && (result.empty() || t > previous || (t==previous&&player>previousPlayer)) && down != held[player-1], "Invalid session action order");
        result.push_back({t,down,1,static_cast<int>(player)}); previous = t; previousPlayer=player;held[player-1] = down;
    }
    return result;
}
nlohmann::json Search::snapshot() const {
    std::ostringstream rng; rng.imbue(std::locale::classic()); rng << m_rng;
    json result{{"schema",1},{"independent",m_independent},{"window",m_window},{"generation",m_generation},{"stale",m_stale},
        {"guidance",m_guidanceFrom},{"policy",m_policy},{"choices",m_choices},{"escapes",m_escapeCount},
        {"phase",static_cast<int>(m_phase)},{"rng",rng.str()},{"focus",compactPlan(m_focusBase)},
        {"focus_death",m_focusDeath},{"focus_policy",m_focusPolicy},{"edits",json::array()},{"elites",json::array()}};
    std::size_t actionCount = m_focusBase.size();
    for (auto const& e : m_elites) {
        actionCount += e.actions.size();
        check(actionCount <= 4 * MaxTicks,"Search session action budget exceeded; best macro remains usable");
        result["elites"].push_back({{"actions",compactPlan(e.actions)},{"death",e.deathTick},{"percent",e.percent},{"flight",e.flight}});
    }
    for (auto const& e : m_focusEdits) result["edits"].push_back({e.first,e.last,e.delta,e.release});
    std::vector<std::uint64_t> issued(m_issued.begin(),m_issued.end());
    std::sort(issued.begin(),issued.end()); result["issued"] = issued;
    return result;
}
Search Search::restore(nlohmann::json const& v) {
    check(v.at("schema") == 1,"Unsupported search session schema");
    auto window = integer(v.at("window")); check(window >= 8 && window <= 2400,"Invalid search window");
    Search s(0,window,v.value("independent",false));
    s.m_generation=counter(v.at("generation")); s.m_stale=counter(v.at("stale"));
    s.m_choices=counter(v.at("choices")); s.m_escapeCount=counter(v.at("escapes"));
    s.m_guidanceFrom=integer(v.at("guidance")); check(s.m_guidanceFrom<=MaxTicks,"Invalid guidance boundary");
    s.m_policy=integer(v.at("policy")); s.m_focusPolicy=integer(v.at("focus_policy"));
    check(s.m_policy<99 && s.m_focusPolicy<99,"Invalid search policy");
    auto phase=integer(v.at("phase")); check(phase<=3,"Invalid search phase"); s.m_phase=static_cast<SearchPhase>(phase);
    auto text=v.at("rng").get<std::string>(); check(text.size()<=10000,"Invalid RNG state length");
    std::istringstream rng(text); rng.imbue(std::locale::classic());
    check(static_cast<bool>(rng >> s.m_rng),"Invalid RNG state"); rng >> std::ws; check(rng.eof(),"Trailing RNG data");
    s.m_focusBase=expandPlan(v.at("focus")); s.m_focusDeath=integer(v.at("focus_death"));
    check(s.m_focusDeath<=MaxTicks,"Invalid focus death boundary");
    check(s.m_focusBase.empty() || s.m_focusBase.back().tick<s.m_focusDeath,"Focus extends beyond its attempt");
    auto const& edits=v.at("edits"); check(edits.is_array() && edits.size()<=256,"Invalid focus queue size");
    for (auto const& e : edits) {
        check(e.is_array() && e.size()==4 && e[2].is_number_integer() && e[3].is_boolean(),"Invalid focus edit");
        check(!e[2].is_number_unsigned() || e[2].get<std::uint64_t>()<=128,"Focus delta overflows signed range");
        auto first=integer(e[0]),last=integer(e[1]); auto delta=e[2].get<std::int64_t>();
        check(first<=last && last<s.m_focusBase.size() && delta>=-128 && delta<=128 && delta!=0,"Invalid focus edit bounds");
        bool release=e[3].get<bool>();
        check(!release || (delta<0 && s.m_focusBase[first].down && first==last && std::none_of(s.m_focusBase.begin()+last+1,s.m_focusBase.end(),[&](auto const&a){return a.player==s.m_focusBase[first].player;})),"Invalid release edit");
        s.m_focusEdits.push_back({static_cast<std::size_t>(first),static_cast<std::size_t>(last),static_cast<int>(delta),release});
    }
    auto const& elites=v.at("elites"); check(elites.is_array() && elites.size()<=ArchiveLimit,"Invalid archive size");
    std::size_t actionCount=s.m_focusBase.size(); double previous=101;
    for (auto const& e : elites) {
        Tick death=integer(e.at("death")); check(death>0 && death<=MaxTicks,"Invalid elite length");
        auto percent=e.at("percent").get<double>();
        check(std::isfinite(percent) && percent>=0 && percent<=100 && percent<=previous,"Invalid archive progress/order");
        auto actions=expandPlan(e.at("actions"),death); actionCount+=actions.size();
        check(actionCount<=4*MaxTicks,"Session action budget exceeded");
        s.m_elites.push_back({std::move(actions),death,percent,e.at("flight").get<bool>()}); previous=percent;
    }
    auto const& issued=v.at("issued"); check(issued.is_array() && issued.size()<=8192,"Invalid duplicate cache");
    for (auto const& h : issued) s.m_issued.insert(integer(h));
    return s;
}
nlohmann::json searchSessionJson(Macro const& best, Search const& search,
    std::vector<Action> const& plan, std::uint64_t seed, std::uint64_t attempts, double seconds) {
    auto draft=best; draft.verified=false;
    return {{"format","routefinder-search-session"},{"schema",1},{"max_cps",16},
        {"best",toJson(draft)},{"search",search.snapshot()},{"plan",compactPlan(plan)},
        {"seed",seed},{"attempts",attempts},{"seconds",seconds}};
}
SearchSessionState parseSearchSession(nlohmann::json const& v) {
    check(v.at("format")=="routefinder-search-session" && v.at("schema")==1 && v.at("max_cps")==16,
        "Incompatible search session format/CPS");
    auto best=fromJson(v.at("best")); best.verified=false;
    auto seed=integer(v.at("seed")); check(seed>=1 && seed<=2147483647,"Invalid search seed");
    auto seconds=v.at("seconds").get<double>();
    check(std::isfinite(seconds) && seconds>=0 && seconds<=1e12,"Invalid session elapsed time");
    return {std::move(best),Search::restore(v.at("search")),expandPlan(v.at("plan")),
        seed,counter(v.at("attempts")),seconds};
}

}
