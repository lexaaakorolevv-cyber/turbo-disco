#include "test_world.hpp"
#include <iostream>
using namespace v2;
namespace {
void recordReplay() {
    auto macro = recorded();
    check(macro.actions ==
              std::vector<Action>{{2, true, 1, 1}, {5, true, 1, 2}, {8, false, 1, 1}, {20, false, 1, 2}},
          "Independent recorded inputs changed");
    validate(macro);
    Engine e;
    e.startReplay(environment(), {4, 100, 10}, macro);
    e.beginAttempt();
    World w;
    std::vector<Action> actual;
    for (unsigned t = 0; t < 64; t++) {
        check(e.advanceOuter(), "Replay outer failed");
        auto d = e.tick(w.frame(t), [](auto const &) -> Scene const & {
            throw std::runtime_error("Replay called the planner");
        });
        check(d.advance, "Replay desynchronized");
        actual.insert(actual.end(), d.actions.begin(), d.actions.end());
        w.apply(d.actions);
        w.step();
    }
    check(actual == macro.actions, "Replay input differs");
    e.advanceOuter();
    auto d = e.tick(w.frame(64), {});
    check(!d.advance && !e.active(), "Incomplete replay continued beyond its end");
    Engine bad;
    bad.startReplay(environment(), {4, 100, 10}, macro);
    bad.beginAttempt();
    World wrong;
    for (unsigned t = 0; t < 32 && bad.active(); t++) {
        bad.advanceOuter();
        auto f = wrong.frame(t);
        if (t == 16)
            f.players[1].y += 1;
        auto result = bad.tick(f, {});
        wrong.apply(result.actions);
        wrong.step();
    }
    check(!bad.active() && bad.status().find("Расхождение") != std::string::npos,
          "Second-player desync accepted");
}
void verification() {
    Engine e;
    e.startRecording(environment(), {2, 100, 10});
    e.beginAttempt();
    World w;
    for (unsigned t = 0; t < 32; t++) {
        if (t == 2)
            e.human(1, true);
        if (t == 10)
            e.human(1, false);
        e.advanceOuter();
        auto d = e.tick(w.frame(t, false), {});
        w.apply(d.actions);
        w.step();
    }
    e.markComplete();
    check(!e.restartRequested(), "Completion changed world inside the physics callback");
    e.service(1);
    check(e.mode() == Mode::Verifying && e.restartRequested() && !e.consumeCompletion(),
          "Unverified success reached native completion");
    e.beginAttempt();
    w = World{};
    for (unsigned t = 0; t < 32; t++) {
        e.advanceOuter();
        auto d = e.tick(w.frame(t, false), {});
        check(d.advance, "Verification desynchronized");
        w.apply(d.actions);
        w.step();
    }
    e.markComplete();
    e.service(2);
    check(e.best()->verified && e.best()->complete && e.consumeCompletion() && !e.active(),
          "Full verification was not required/accepted");
}
void emptyPlanningAndLimits() {
    Engine e;
    e.startSearch(environment(), {2, 100, 10});
    e.beginAttempt();
    Scene scene;
    World w;
    unsigned calls = 0;
    for (unsigned t = 0; t < 96; t++) {
        e.advanceOuter();
        auto d = e.tick(w.frame(0, false), [&](auto const &) -> Scene const & {
            ++calls;
            return scene;
        });
        check(d.advance && d.actions.empty(), "Empty-ground controller jumped or stopped");
        w.apply(d.actions);
        w.step();
    }
    check(calls <= 10 && calls >= 2, "Short input plan did not reduce redundant scene/forecast calls");
    e.markDeath();
    check(!e.restartRequested(), "Death restarted inside native callback");
    e.service(1);
    check(e.restartRequested(), "Deferred death was not handled");
    e.beginAttempt();
    check(!e.held(1) && e.tickNumber() == 0 && e.outer() == 0, "Attempt did not reset input/clock");
    e.markDeath();
    e.service(2);
    e.beginAttempt();
    check(!e.active() && e.attempts() == 2, "Attempt limit ignored");
    Engine timed;
    timed.startSearch(environment(), {2, 1, 10});
    timed.beginAttempt();
    timed.service(1);
    check(!timed.active(), "Wall time limit ignored");
}
void cps() {
    auto draft = recorded();
    draft.complete = false;
    Search s;
    Candidate c;
    c.guideFrom = 2000;
    for (Tick t = 0; t < 960; t++)
        for (int p = 1; p <= 2; p++)
            c.actions.push_back({t, t % 2 == 0, 1, p});
    Session resume{draft, s, c, Brain{}.learning(), 0, 0};
    Engine e;
    e.startSearch(environment(), {4, 100, 10}, resume);
    e.beginAttempt();
    World w;
    std::array<std::vector<Tick>, 2> presses;
    for (unsigned outer = 0; outer < 480; outer++) {
        e.advanceOuter();
        for (unsigned part = 0; part < 2; part++) {
            auto frame = w.frame(0);
            frame.timing = {.125, part == 0, part == 1};
            auto d = e.tick(frame, {});
            for (auto const &action : d.actions)
                if (action.down)
                    presses[action.player - 1].push_back(outer);
            w.apply(d.actions);
            w.step(.125);
        }
    }
    for (auto const &list : presses) {
        check(list.size() >= 20, "CPS test produced no useful input");
        for (auto t : list) {
            unsigned count = 0;
            for (auto x : list)
                if (x >= t && x < t + 240)
                    ++count;
            check(count <= 16, "Search exceeded rolling 16 CPS on one player");
        }
    }
}
void halfTickReplay() {
    Engine e;
    e.startRecording(environment(), {4, 100, 10});
    e.beginAttempt();
    World w;
    for (unsigned outer = 0; outer < 20; outer++) {
        e.advanceOuter();
        for (unsigned part = 0; part < 2; part++) {
            auto tick = outer * 2 + part;
            if (tick == 3)
                e.human(1, true);
            if (tick == 9)
                e.human(1, false);
            auto frame = w.frame(tick);
            frame.timing = {.125, part == 0, part == 1};
            auto d = e.tick(frame, {});
            w.apply(d.actions);
            w.step(.125);
        }
    }
    e.stop();
    auto run = *e.best();
    validate(run);
    check(run.ticks == 40 && run.outer == 20, "Half ticks changed outer clock");
    Engine replay;
    replay.startReplay(environment(), {4, 100, 10}, run);
    replay.beginAttempt();
    w = World{};
    for (unsigned outer = 0; outer < 20; outer++) {
        replay.advanceOuter();
        for (unsigned part = 0; part < 2; part++) {
            auto frame = w.frame(outer * 2 + part);
            frame.timing = {.125, part == 0, part == 1};
            auto d = replay.tick(frame, {});
            check(d.advance, "Exact half-tick replay failed");
            w.apply(d.actions);
            w.step(.125);
        }
    }
    Engine mismatch;
    mismatch.startReplay(environment(), {4, 100, 10}, run);
    mismatch.beginAttempt();
    mismatch.advanceOuter();
    auto wrong = World{}.frame(0);
    check(!mismatch.tick(wrong, {}).advance && !mismatch.active(),
          "Different native timing pattern accepted");
}
} // namespace
int main() {
    try {
        recordReplay();
        verification();
        emptyPlanningAndLimits();
        cps();
        halfTickReplay();
        std::cout << "Native-boundary protocol: recording, exact P1/P2 replay, desync, full verification, "
                     "deferred reset, planning cache, input reset and limits PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
