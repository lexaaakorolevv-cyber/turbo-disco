#pragma once
#include "engine.hpp"
#include <stdexcept>
inline void check(bool ok, char const *why) {
    if (!ok)
        throw std::runtime_error(why);
}
inline v2::Json environment() {
    return {{"mod", v2::Version}, {"gd", "2.2081"}, {"id", 1}, {"physics", {{"clock", 240}}}};
}
struct World {
    std::array<rf::FormState, 2> p;
    std::array<bool, 2> held{};
    World() {
        p[1].y = 455;
        p[1].floor = 441;
    }
    v2::Frame frame(unsigned tick, bool dual = true) const {
        v2::Frame f;
        f.players = p;
        f.dual = dual;
        f.independent = dual;
        f.timing = {.25, false, true};
        f.percent = std::min(99., tick * 1.);
        return f;
    }
    void apply(std::vector<v2::Action> const &actions) {
        for (auto const &a : actions)
            held[a.player - 1] = a.down;
    }
    void step(double dt = .25) {
        for (int i = 0; i < 2; i++) {
            auto &s = p[i];
            if (s.grounded && held[i]) {
                s.vy = 11.05;
                s.grounded = false;
            }
            if (!s.grounded)
                s.vy -= .94 * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (s.y - s.halfHeight <= s.floor) {
                s.y = s.floor + s.halfHeight;
                s.vy = 0;
                s.grounded = true;
            }
            s.held = held[i];
        }
    }
};
inline v2::Run recorded() {
    v2::Engine e;
    e.startRecording(environment(), {4, 100, 10});
    e.beginAttempt();
    World w;
    for (unsigned t = 0; t < 64; t++) {
        if (t == 2)
            e.human(1, true);
        if (t == 8)
            e.human(1, false);
        if (t == 5)
            e.human(2, true);
        if (t == 20)
            e.human(2, false);
        check(e.advanceOuter(), "Record outer clock failed");
        auto d = e.tick(w.frame(t), [](auto const &) -> v2::Scene const & {
            throw std::runtime_error("Recording called the planner");
        });
        check(d.advance, "Record physics tick failed");
        w.apply(d.actions);
        w.step();
    }
    e.stop();
    check(e.best().has_value(), "Recording lost");
    return *e.best();
}
