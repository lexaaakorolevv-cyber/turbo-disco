#include "core.hpp"
#include "input.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace rf;
namespace {
void check(bool ok, char const* message) { if (!ok) throw std::runtime_error(message); }
struct Result { Tick death; double percent; };
// Independent timing-gate oracle. The first two gates form a successful
// prefix; the third requires both a precise press and a precise hold length.
// This tests local optimization of an existing near-miss, not GD physics.
Result evaluate(std::vector<Action> const& actions) {
    struct Gate { Tick press, hold; };
    int cleared = 0;
    for (auto gate : {Gate{20, 20}, Gate{100, 24}, Gate{200, 24}}) {
        bool safe = false;
        for (std::size_t i = 0; i + 1 < actions.size(); ++i)
            if (actions[i].down && !actions[i+1].down && actions[i].tick >= gate.press - 1 &&
                actions[i].tick <= gate.press + 1 && actions[i+1].tick - actions[i].tick == gate.hold) safe = true;
        if (!safe) return {gate.press + 60, cleared * 30.};
        ++cleared;
    }
    return {420, 100};
}
void validateActions(std::vector<Action> const& actions) {
    bool held = false;
    Tick previous = 0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        check(actions[i].tick < 500 && (!i || actions[i].tick > previous), "Mutation reordered / overlapped actions");
        check(actions[i].down != held, "Mutation introduced a redundant transition");
        previous = actions[i].tick; held = actions[i].down;
    }
}
}
int main() {
    try {
        int total = 0, worst = 0;
        for (int seed : {1337, 42, 7, 2026}) for (int error : {-16, -8, -4, -2, 2, 4, 8, 16}) {
            Search search(seed, 120), repeated(seed, 120);
            std::vector<Action> parent{{20,true},{40,false},{100,true},{124,false},
                {static_cast<Tick>(200 + error),true},{static_cast<Tick>(224 + error),false}};
            search.next(500); repeated.next(500);
            auto initial = evaluate(parent);
            search.observe(parent, initial.death, initial.percent);
            repeated.observe(parent, initial.death, initial.percent);
            bool solved = false;
            for (int attempt = 1; attempt <= 64; ++attempt) {
                auto plan = search.next(500);
                check(plan == repeated.next(500), "Focused search is not seed reproducible");
                validateActions(plan);
                auto result = evaluate(plan);
                if (result.percent == 100) {
                    check(prefix(plan, 160) == prefix(parent, 160), "Successful prefix was lost");
                    // In this oracle ticks are outer physics steps. Ensure the
                    // winning macro really survives the live 16 CPS gate.
                    InputPacer pacer; pacer.reset(SearchCps);
                    bool held = false; std::size_t cursor = 0;
                    std::vector<Action> actual;
                    for (Tick t = 0; t < result.death; ++t) {
                        if (cursor < plan.size() && plan[cursor].tick == t) held = plan[cursor++].down;
                        bool down = pacer.apply(held, t);
                        if (actual.empty() ? down : down != actual.back().down) appendAction(actual,t,down);
                    }
                    check(evaluate(actual).percent == 100, "Focused winner requires more than 16 CPS");
                    total += attempt; worst = std::max(worst, attempt); solved = true; break;
                }
                search.observe(plan,result.death,result.percent);
                repeated.observe(plan,result.death,result.percent);
                check(search.size() <= Search::ArchiveLimit, "Archive grew without a limit");
            }
            check(solved,"Focused search failed to repair a near-miss");
        }
        Search open(1337,120);
        open.next(500);
        std::vector<Action> held{{100,true}};
        open.observe(held,200,30,true);
        bool foundRelease = false;
        for(int i=0;i<32;++i){ auto plan=open.next(500); validateActions(plan);
            if(plan.size()==2 && plan.front()==held.front() && !plan.back().down && plan.back().tick==192) foundRelease=true;
        }
        check(foundRelease,"No release variation for a fatal sustained hold");
        auto tiny = open.next(1);
        check(std::all_of(tiny.begin(),tiny.end(),[](auto const& a){return a.tick<1;}) && open.next(0).empty(),"Narrow horizon corrupted a focused mutation");
        std::cout << "32 near-miss repairs: total attempts=" << total << ", worst=" << worst
            << "; prefix, 16 CPS, sustained-hold release, bounds and reproducibility: PASS\n";
    } catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
