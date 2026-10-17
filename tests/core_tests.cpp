#include <bot/Learner.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace cubebot;

namespace {
Snapshot state(double time = 0.0) {
    Snapshot s;
    s.elapsed = time;
    s.dt = 1.0 / 240.0;
    s.players[0].position = {0.0, 15.0};
    s.players[0].velocity = {300.0, 0.0};
    s.players[0].grounded = true;
    s.players[0].mode = Mode::Cube;
    return s;
}
struct Run {
    bool completed = false;
    double reached = 0.0;
    std::vector<double> jumpTimes;
};
Run simulate(CubeLearner& bot, std::vector<ObjectSnapshot> const& obstacles,
             double jumpVelocity = 640.0, double gravity = 2800.0,
             double finishX = 600.0, bool inverted = false) {
    bot.beginAttempt();
    Snapshot s = state();
    s.objects = obstacles;
    s.players[0].upsideDown = inverted;
    if (inverted) s.players[0].position.y = -15.0;
    Run run;
    double const sign = inverted ? -1.0 : 1.0;
    for (unsigned tick = 0; tick < 2400; ++tick) {
        s.tick = tick;
        s.elapsed = tick * s.dt;
        s.progress = std::min(99.99, 100.0 * s.players[0].position.x / finishX);
        auto const decision = bot.step(s);
        assert(!decision.held[1]);
        auto& p = s.players[0];
        if (decision.held[0] && p.grounded) {
            p.velocity.y = sign * jumpVelocity;
            p.grounded = false;
            run.jumpTimes.push_back(s.elapsed);
        }
        p.position.x += p.velocity.x * s.dt;
        if (!p.grounded) {
            p.position.y += p.velocity.y * s.dt - sign * 0.5 * gravity * s.dt * s.dt;
            p.velocity.y -= sign * gravity * s.dt;
            if (sign * p.position.y <= 15.0) {
                p.position.y = sign * 15.0;
                p.velocity.y = 0.0;
                p.grounded = true;
            }
        }
        Rect const body{p.position.x - 15.0, p.position.y - 15.0,
                        p.position.x + 15.0, p.position.y + 15.0};
        bool dead = false;
        for (auto const& object : obstacles) {
            if (object.kind == ObjectKind::Hazard && body.overlaps(object.bounds, 1.0))
                dead = true;
        }
        run.reached = 100.0 * p.position.x / finishX;
        if (dead) {
            s.elapsed += s.dt;
            s.progress = run.reached;
            p.alive = false;
            bot.recordDeath(s);
            return run;
        }
        if (p.position.x >= finishX) {
            s.elapsed += s.dt;
            s.progress = 100.0;
            bot.recordCompletion(s);
            run.completed = true;
            return run;
        }
    }
    bot.recordDeath(s);
    return run;
}
ObjectSnapshot spike(double x, double height = 30.0) {
    return {static_cast<std::int64_t>(x), ObjectKind::Hazard, {x, 0.0, x + 30.0, height}};
}
}

int main() {
    {
        CubeLearner bot;
        bot.reset("level-a");
        auto const result = simulate(bot, {spike(180.0), spike(390.0)});
        assert(result.completed);
        assert(result.jumpTimes.size() == 2);
        assert(bot.data().bestProgress == 100.0);
        assert(!bot.data().bestActions.empty());
        auto const again = simulate(bot, {spike(180.0), spike(390.0)});
        assert(again.completed);
        assert(again.jumpTimes == result.jumpTimes);
    }
    {
        CubeLearner bot;
        bot.reset("triple");
        auto const result = simulate(bot, {spike(180.0, 20.0), spike(210.0, 20.0), spike(240.0, 20.0)});
        assert(result.completed);
        assert(result.jumpTimes.size() == 1);
    }
    {
        CubeLearner bot;
        bot.reset("empty");
        auto const result = simulate(bot, {});
        assert(result.completed && result.jumpTimes.empty());
    }
    {
        CubeLearner bot;
        bot.reset("inverted");
        ObjectSnapshot const hazard{1, ObjectKind::Hazard, {180.0, -30.0, 210.0, 0.0}};
        auto const result = simulate(bot, {hazard}, 640.0, 2800.0, 400.0, true);
        assert(result.completed && result.jumpTimes.size() == 1);
    }
    {
        CubeLearner bot;
        bot.reset("unsupported");
        bot.beginAttempt();
        auto s = state();
        s.dual = true;
        auto decision = bot.step(s);
        assert(decision.status == Status::UnsupportedDual);
        assert(!decision.held[0] && !decision.held[1]);
        s.dual = false;
        s.players[0].mode = Mode::Ship;
        decision = bot.step(s);
        assert(decision.status == Status::UnsupportedMode && !decision.held[0]);
        bot.stop();
        assert(!bot.step(s).held[0]);
    }
    {
        CubeLearner bot;
        bot.reset("storage");
        auto result = simulate(bot, {spike(180.0, 200.0)}, 640.0, 2800.0, 400.0);
        assert(!result.completed);
        auto saved = bot.data();
        CubeLearner loaded;
        assert(!loaded.load(saved, "changed-level"));
        assert(loaded.load(saved, "storage"));
        saved.bestActions.push_back({-1.0, 0, true});
        assert(!loaded.load(saved, "storage"));
        saved = bot.data();
        saved.schema = 99;
        assert(!loaded.load(saved, "storage"));
        // Identical failed trials advance a deterministic search schedule.
        simulate(bot, {spike(180.0, 200.0)}, 640.0, 2800.0, 400.0);
        assert(bot.data().failedTrials >= 1);
    }
    {
        CubeLearner bot;
        bot.reset("interruption");
        bot.beginAttempt();
        auto s = state();
        s.elapsed = 0.5;
        s.progress = 25.0;
        bot.step(s);
        bot.recordInterrupted(s);
        bot.stop();
        assert(bot.data().bestProgress == 25.0);
        assert(bot.data().failedTrials == 0 && !bot.data().completed);
        assert(!bot.step(s).held[0]);
        // Human movement after ownership ended cannot update training data.
        s.progress = 75.0;
        bot.recordInterrupted(s);
        assert(bot.data().bestProgress == 25.0);
    }
    {
        // Deliberately wrong first-attempt jump model: the real simulator jumps
        // at 480, while the controller initially predicts 640 world units/s.
        // The first arc dies on the trailing edge; deterministic local timing
        // mutations eventually clear it and retain the better input prefix.
        CubeLearner bot;
        bot.reset("local-timing-search");
        auto run = simulate(bot, {spike(180.0)}, 480.0, 2800.0, 400.0);
        assert(!run.completed);
        double best = bot.data().bestProgress;
        for (int attempt = 0; attempt < 24 && !run.completed; ++attempt) {
            run = simulate(bot, {spike(180.0)}, 480.0, 2800.0, 400.0);
            assert(bot.data().bestProgress >= best);
            best = bot.data().bestProgress;
        }
        assert(run.completed);
        assert(bot.data().attempts > 1);
    }
    std::cout << "cube core: all assertions passed\n";
}
