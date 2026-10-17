#include "search.hpp"
#include <iostream>
#include <stdexcept>
using namespace v2;
void check(bool ok, char const *why) {
    if (!ok)
        throw std::runtime_error(why);
}
Run run(std::vector<Action> actions, Tick tick, double percent) {
    Run r;
    r.actions = std::move(actions);
    r.ticks = tick;
    r.percent = percent;
    return r;
}
int main() {
    try {
        Search s;
        auto viable = run({{300, true}, {320, false}, {422, true}, {446, false}}, 480, 35);
        auto trap = run({{20, true}, {40, false}, {700, true}, {720, false}}, 800, 75);
        s.observe(viable, {});
        s.observe(trap, {});
        auto candidate = s.next();
        bool lower = false;
        for (int i = 0; i < 20; i++) {
            if (candidate.parent == 1)
                lower = true;
            s.observe(candidate.parent == 1 ? viable : trap, candidate);
            candidate = s.next();
        }
        check(lower, "Higher percent starved an earlier branch");
        auto encoded = s.json();
        auto resumed = Search::restore(encoded);
        for (int i = 0; i < 64; i++) {
            auto a = s.next(), b = resumed.next();
            check(encodeCandidate(a) == encodeCandidate(b), "Resumed scheduler changed candidate");
            s.observe(a.parent == 1 ? viable : trap, a);
            resumed.observe(b.parent == 1 ? viable : trap, b);
        }
        Search timing;
        auto parent =
            run({{20, true}, {40, false}, {100, true}, {124, false}, {202, true}, {226, false}}, 280, 60);
        timing.observe(parent, {});
        bool repaired = false;
        for (int i = 0; i < 96; i++) {
            auto c = timing.next(500);
            check(validActions(c.actions, 500), "Repair made invalid transitions");
            if (c.actions.size() == 6 && c.actions[4].tick == 200 && c.actions[5].tick == 224) {
                check(prefix(c.actions, 160) == prefix(parent.actions, 160),
                      "Repair changed successful prefix");
                repaired = true;
                break;
            }
        }
        check(repaired, "Near-miss repair not explored");
        Search independent;
        auto dual = run({{10, true, 1, 1},
                         {11, true, 1, 2},
                         {20, false, 1, 1},
                         {25, false, 1, 2},
                         {50, true, 1, 1},
                         {60, false, 1, 1}},
                        90, 30);
        independent.observe(dual, {});
        auto channel = [](std::vector<Action> const &all, int player) {
            std::vector<Action> out;
            for (auto a : all)
                if (a.player == player)
                    out.push_back(a);
            return out;
        };
        unsigned checked = 0;
        for (int i = 0; i < 96; i++) {
            auto c = independent.next(200);
            if (c.guideFrom <= 90)
                continue;
            bool first = channel(c.actions, 1) != channel(dual.actions, 1),
                 second = channel(c.actions, 2) != channel(dual.actions, 2);
            check(!(first && second), "Local repair edited both player channels");
            ++checked;
        }
        check(checked > 10, "Independent repair variants missing");
        Search points;
        Sample sample;
        sample.tick = 32;
        auto actions = std::vector<Action>{{10, true}, {20, false}};
        points.checkpoint(10, sample, actions, 1);
        points.checkpoint(10, sample, actions, 1);
        check(points.json()["points"][0][3] == 1, "One run confirmed a point twice");
        points.checkpoint(10, sample, actions, 2);
        check(points.json()["points"][0][3] == 2, "Repeated native point not confirmed");
        points.invalidate(32);
        check(points.json()["points"].empty(), "Desynchronized point remained available");
        auto bad = encoded;
        bad["branches"][0][3] = -1;
        bool rejected = false;
        try {
            Search::restore(bad);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Invalid scheduler counters accepted");
        std::cout << "Branch fairness, local repair, checkpoint confirmation/invalidation and exact "
                     "scheduler resume PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
