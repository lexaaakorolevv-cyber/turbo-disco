#include "core.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace rf;
namespace {
int checks = 0;
void check(bool ok, char const* msg) {
    ++checks;
    if (!ok) throw std::runtime_error(msg);
}
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (std::exception const&) { rejected = true; }
    check(rejected, "Invalid macro accepted");
}
Macro fixture() {
    Macro m;
    m.environment = {{"gd", "2.2081"}, {"level_id", 1}};
    m.ticks = 33;
    m.outerUpdates = 33;
    m.timings.resize(m.ticks, {.25, false, true});
    m.actions = {{0, true}, {10, false}, {20, true}, {32, false}};
    m.trace = {{0, 1, 2, 0, 1, 1, .25}, {16, 20, 30, 2, 1, 1, .25}, {32, 40, 20, -1, 1, 1, .25}};
    return m;
}
}
int main() {
    try {
        auto m = fixture();
        auto j = toJson(m);
        check(toJson(fromJson(j)) == j, "JSON roundtrip changed macro");
        auto temp = std::filesystem::temp_directory_path() / "routefinder-core-tests" / "macro.json";
        saveMacro(temp, m);
        m.bestPercent = 52;
        saveMacro(temp, m);
        check(loadMacro(temp).bestPercent == 52, "File replacement failed");
        std::filesystem::rename(temp, temp.string() + ".bak");
        check(loadMacro(temp).bestPercent == 52, "Backup recovery failed");
        std::filesystem::remove_all(temp.parent_path());

        for (auto field : {"tick", "button", "player"}) {
            auto bad = j;
            bad["actions"][0][field] = -1;
            rejects([&] { fromJson(bad); });
        }
        for (auto tick : {0, 33}) {
            auto bad = j;
            bad["actions"][1]["tick"] = tick;
            rejects([&] { fromJson(bad); });
        }
        auto bad = j;
        bad["actions"][1]["down"] = true;
        rejects([&] { fromJson(bad); });
        bad = j; bad["ticks"] = 1e100;
        rejects([&] { fromJson(bad); });
        bad = j; bad["trace"].erase(1);
        rejects([&] { fromJson(bad); });
        bad = j; bad["verified_from_start"] = true;
        rejects([&] { fromJson(bad); });
        bad = j; bad["trace"][0]["dt"] = "NaN";
        rejects([&] { fromJson(bad); });
        bad = j; bad["timings"].erase(0);
        rejects([&] { fromJson(bad); });
        check(sameSample(m.trace[0], m.trace[0]), "Identical trace rejected");
        auto changed = m.trace[0]; changed.y += .01;
        check(!sameSample(m.trace[0], changed), "Position desync missed");
        changed = m.trace[0]; changed.flags = 8;
        check(!sameSample(m.trace[0], changed), "Mode desync missed");

        std::vector<Action> actions;
        appendAction(actions, 0, false);
        appendAction(actions, 1, true);
        appendAction(actions, 1, false);
        check(actions.empty(), "Same-tick coalescing failed");
        appendAction(actions, 2, true);
        check(!heldBefore(actions, 2) && heldBefore(actions, 3), "Input boundary off by one");

        Budget budget{3, 20};
        check(budget.exhausted(2, 19).empty(), "Budget stopped early");
        check(!budget.exhausted(3, 19).empty(), "Attempt budget ignored");
        check(!budget.exhausted(0, 20).empty(), "Time budget ignored");

        // A miniature deterministic physics oracle: jumping at 50..60 and
        // releasing after >=4 ticks clears the obstacle at tick 65.
        // Search must discover it without a model or restoring any state.
        Search search(1337, 120), repeat(1337, 120);
        check(search.next(240).empty(), "Initial no-input run missing");
        bool solved = false;
        for (int attempt = 0; attempt < 3000; ++attempt) {
            auto seq = search.next(240);
            auto again = repeat.next(240);
            check(seq == again, "Search seed not reproducible");
            Tick death = 65;
            for (std::size_t i = 0; i + 1 < seq.size(); ++i)
                if (seq[i].down && seq[i].tick >= 50 && seq[i].tick <= 60 &&
                    !seq[i+1].down && seq[i+1].tick - seq[i].tick >= 4) death = 200;
            search.observe(seq, death, static_cast<double>(death));
            repeat.observe(seq, death, static_cast<double>(death));
            auto valid = fixture();
            valid.ticks = 240;
            valid.timings.resize(valid.ticks, {.25, false, true});
            valid.trace.clear();
            for (Tick t = 0; t < valid.ticks; t += 16) valid.trace.push_back({t, 0, 0, 0, 1, 1, .25});
            valid.actions = seq;
            validate(valid);
            check(search.size() <= 8, "Elite archive unbounded");
            if (death == 200) { solved = true; break; }
        }
        check(solved, "Search failed simple obstacle oracle");
        check(search.best().deathTick == 200, "Best sequence not preserved");
        std::cout << "Passed " << checks << " checks\n";
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
