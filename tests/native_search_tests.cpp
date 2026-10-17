#include "test_world.hpp"
#include <iostream>
using namespace v2;
namespace {
Frame frame(Tick tick, unsigned mask = 0, bool independent = false) {
    Frame f;
    f.tick = tick;
    f.outer = tick + 1;
    f.timing = {.25, false, true};
    f.independent = f.dual = independent;
    f.players[0].x = double(tick);
    f.players[1].x = double(tick);
    f.players[0].held = mask & 1;
    f.players[1].held = mask & 2;
    return f;
}
void append(Run &r, Tick tick, unsigned before, unsigned after, bool independent) {
    for (int i = 0; i < (independent ? 2 : 1); ++i)
        if (((before ^ after) >> i) & 1)
            r.actions.push_back({tick, bool((after >> i) & 1), 1, i + 1});
    r.ticks = tick + 1;
}
void allDualMasks() {
    NativeSearch s;
    s.configure(2, 64, 1000);
    unsigned seen = 0;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        s.beginAttempt();
        auto f = frame(0, 0, true);
        check(s.validateReplay(f, 0), "Dual fork replay rejected");
        auto mask = s.decide(f, 0, 3, 15);
        check(!(seen & (1u << mask)), "Native search repeated a joint input");
        seen |= 1u << mask;
        Run r;
        append(r, 0, 0, mask, true);
        auto c = s.backtrack(r);
        check(bool(c) == (attempt < 3), "Joint branch exhaustion incorrect");
        if (c)
            check(c->guideFrom == 0 && c->actions.empty(), "Fork retained modified input");
        s = NativeSearch::restore(s.json());
    }
    check(seen == 15 && s.exhausted(), "Not all P1/P2 combinations explored");
}
void witnessesAndResume() {
    NativeSearch s;
    s.configure(2, 64, 1000);
    s.beginAttempt();
    Run r;
    auto f = frame(0);
    check(s.decide(f, 0, 0, 3) == 0, "Suggested first branch ignored");
    f = frame(2);
    auto chosen = s.decide(f, 0, 1, 3);
    append(r, 2, 0, chosen, false);
    auto resume = s.resume(r);
    check(resume.guideFrom == 2 && resume.actions.empty(), "Resume included fork action");
    auto j = s.json();
    s = NativeSearch::restore(j);
    s.beginAttempt();
    check(s.validateReplay(frame(0), 0), "Valid ancestor rejected");
    auto wrong = frame(2);
    wrong.players[0].vy += .1;
    check(!s.validateReplay(wrong, 0), "Native state drift accepted");
    s = NativeSearch::restore(j);
    s.beginAttempt();
    check(s.validateReplay(frame(0), 0) && s.validateReplay(frame(2), 0), "Resume witnesses failed");
    check(s.decide(frame(2), 0, 0, 3) == 1, "Resume changed selected branch");
    bool bad = false;
    j["path"][1][0][0] = 0;
    try {
        NativeSearch::restore(j);
    } catch (...) {
        bad = true;
    }
    check(bad, "Invalid tree path accepted");
}
void widenedRollback() {
    NativeSearch s;
    s.configure(1, 1024, 100000);
    Run run;
    Candidate c;
    bool escaped = false;
    // An early held input yields a deceptive 90% branch whose late decisions
    // all die. A release at tick zero yields the actual viable route. The
    // approximate suggestion always chooses the dead end deliberately.
    for (unsigned attempt = 0; attempt < 400 && !escaped; ++attempt) {
        s.beginAttempt();
        run = {};
        unsigned mask = 0;
        std::size_t cursor = 0;
        for (Tick t = 0; t < 200; ++t) {
            auto f = frame(t, mask);
            check(s.validateReplay(f, run.actions.size()), "Native replay witness mismatch");
            unsigned next = mask;
            while (cursor < c.actions.size() && c.actions[cursor].tick == t) {
                auto a = c.actions[cursor++];
                next = a.down ? 1 : 0;
            }
            if (t >= c.guideFrom && s.needsDecision(f))
                next = s.decide(f, run.actions.size(), 1, 3);
            append(run, t, mask, next, false);
            mask = next;
            if (t == 0 && !mask) {
                escaped = true;
                break;
            }
        }
        if (escaped)
            break;
        run.percent = 90;
        auto next = s.backtrack(run);
        check(bool(next), "High-percent dead end exhausted without trying initial alternative");
        c = *next;
        check(validActions(c.actions, c.guideFrom), "Backtrack generated invalid prefix");
    }
    check(escaped, "Widening failed to escape deceptive high-percent branch");
}
void limitedAndPaced() {
    NativeSearch s;
    s.configure(1, 64, 1000);
    for (Tick t = 0; t < 64; ++t)
        s.decide(frame(t), 0, 0, 1);
    check(s.atLimit(), "Path memory limit ignored");
    // A release-only legal mask set must never schedule a disallowed press.
    NativeSearch paced;
    paced.configure(2, 64, 1000);
    check(paced.decide(frame(0), 0, 1, 1) == 0, "Illegal rising edge scheduled");
    Run r;
    r.ticks = 2;
    check(!paced.backtrack(r), "CPS-blocked alternative entered frontier");
}
void sessionResume() {
    Engine e;
    e.startSearch(environment(), {100, 100, 10});
    Scene empty;
    World w;
    auto provider = [&](auto const &) -> Scene const & { return empty; };
    for (unsigned t = 0; t < 40; ++t) {
        e.advanceOuter();
        auto d = e.tick(w.frame(t, false), provider);
        w.apply(d.actions);
        w.step();
    }
    e.markDeath();
    e.service(.1);
    e.beginAttempt();
    w = World{};
    for (unsigned t = 0; t < 48; ++t) {
        e.advanceOuter();
        auto d = e.tick(w.frame(t, false), provider);
        check(d.advance, "Second native trial failed");
        w.apply(d.actions);
        w.step();
    }
    e.stop();
    auto snapshot = e.snapshot();
    check(snapshot.nativeSearch.has_value(), "Native frontier missing from session");
    snapshot = decodeSession(encode(snapshot));
    auto actions = snapshot.best.actions;
    Engine resumed;
    resumed.startSearch(environment(), {100, 100, 10}, std::move(snapshot));
    resumed.beginAttempt();
    w = World{};
    std::vector<Action> replayed;
    for (unsigned t = 0; t < 48; ++t) {
        resumed.advanceOuter();
        auto d = resumed.tick(w.frame(t, false), provider);
        check(d.advance, "Persisted native prefix did not replay");
        replayed.insert(replayed.end(), d.actions.begin(), d.actions.end());
        w.apply(d.actions);
        w.step();
    }
    check(replayed == actions, "Resuming an interrupted native search changed its chosen inputs");
    Engine drift;
    // Copy a real pending backtrack, then die before its selected fork.
    resumed.markDeath();
    resumed.service(.2);
    auto pending = decodeSession(encode(resumed.snapshot()));
    drift.startSearch(environment(), {100, 100, 10}, std::move(pending));
    drift.beginAttempt();
    drift.markDeath();
    drift.service(.3);
    check(!drift.active(), "Death during native prefix replay was mistaken for a valid branch result");
}
void engineFindsNativeSolution() {
    Engine e;
    e.startSearch(environment(), {100, 100, 10});
    Scene empty;
    unsigned found = 0;
    for (unsigned attempt = 0; attempt < 100 && e.active(); ++attempt) {
        e.beginAttempt();
        World w;
        bool failed = false;
        for (unsigned t = 0; t < 40; ++t) {
            e.advanceOuter();
            auto d = e.tick(w.frame(t, false), [&](auto const &) -> Scene const & { return empty; });
            check(d.advance, "Integrated native search stopped early");
            w.apply(d.actions);
            w.step();
            // The deliberately unmodelled level requires a press at tick 20
            // or 21. Only this synthetic live-world rule decides survival.
            if (t == 21 && !w.held[0]) {
                e.markDeath();
                failed = true;
                break;
            }
        }
        if (!failed) {
            e.markComplete();
            ++found;
        }
        e.service(attempt * .01);
        if (e.best() && e.best()->verified)
            break;
    }
    check(found == 2 && e.best() && e.best()->verified, "Native route was not found and fully verified");
}
} // namespace
int main() {
    try {
        allDualMasks();
        witnessesAndResume();
        widenedRollback();
        limitedAndPaced();
        sessionResume();
        engineFindsNativeSolution();
        std::cout << "Native input tree: joint masks, exact replay witnesses, resume, dead-end widening, "
                     "memory/CPS bounds and live-outcome discovery plus full verification PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
