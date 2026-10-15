#include "core.hpp"
#include "flight.hpp"
#include "input.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace rf;
namespace {
void check(bool ok, char const* message) {
    if (!ok) throw std::runtime_error(message);
}
// A deliberately independent, simple flight oracle, not Geometry Dash physics.
// Different acceleration/integration from the forecast tests feedback and
// robustness rather than merely repeating the planner's implementation.
void advance(FlightObservation& s, bool down) {
    constexpr double dt = .25;
    if (s.mode == FlightMode::Wave)
        s.vy = std::abs(s.vx) * (s.scale < .9 ? 2 : 1) * (down != s.inverted ? 1 : -1);
    else {
        double acceleration = (down ? .41 : -.36) * (s.inverted ? -1 : 1) / (s.scale < .9 ? .85 : 1);
        s.vy = std::clamp(s.vy + acceleration * dt, s.inverted ? -8. : -6.4, s.inverted ? 6.4 : 8.);
    }
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.held = down;
}
bool safe(FlightObservation const& s, std::vector<FlightObstacle> const& objects) {
    if (s.y - s.halfHeight <= s.bottom || s.y + s.halfHeight >= s.top) return false;
    for (auto const& o : objects)
        if (s.x + s.halfWidth > o.left && s.x - s.halfWidth < o.right &&
            s.y + s.halfHeight > o.bottom && s.y - s.halfHeight < o.top) return false;
    return true;
}
void course(FlightMode mode, bool inverted, double scale, double direction) {
    FlightObservation state;
    state.mode = mode; state.inverted = inverted; state.scale = scale; state.vx *= direction;
    state.halfWidth = state.halfHeight = scale < .9 ? 7 : 12;
    std::vector<FlightObstacle> objects{{240, 330, 0, 205}, {650, 740, 110, 300}, {1050, 1120, 0, 180}};
    if (direction < 0) for (auto& o : objects) { double left = -o.right; o.right = -o.left; o.left = left; }
    auto initial = state;
    std::vector<Action> macro;
    InputPacer pacer;
    pacer.reset(8);
    bool held = false;
    for (Tick tick = 0; tick < 900; ++tick) {
        state.holdRemaining = pacer.remainingFrames(tick) / 4.;
        state.minHold = pacer.dwellFrames() / 4.;
        bool down = pacer.apply(chooseFlightInput(state, objects), tick);
        check(pacer.currentCps() <= 8, "Flight exceeded the configured CPS limit");
        if (down != held) { appendAction(macro, tick, down); held = down; }
        advance(state, down);
        if (!safe(state, objects)) {
            std::cerr << "mode=" << (mode == FlightMode::Wave ? "wave" : "ship")
                << " inverted=" << inverted << " scale=" << scale << " dir=" << direction
                << " tick=" << tick << " x=" << state.x << " y=" << state.y << '\n';
            throw std::runtime_error("Flight controller hit an oracle obstacle/boundary");
        }
    }
    check(std::abs(state.x) > 1120 + state.halfWidth, "Did not finish flight oracle course");
    // Replay only the recorded transitions, with no planner running.
    auto replay = initial;
    std::size_t cursor = 0;
    held = false;
    for (Tick tick = 0; tick < 900; ++tick) {
        if (cursor < macro.size() && macro[cursor].tick == tick) held = macro[cursor++].down;
        advance(replay, held);
    }
    check(replay.x == state.x && replay.y == state.y && replay.vy == state.vy,
        "Recorded flight inputs did not replay the same trajectory");
}
}
int main() {
    try {
        FlightObservation s;
        s.y = 40;
        check(chooseFlightInput(s, {}), "Ship must boost when low");
        s.y = 260;
        check(!chooseFlightInput(s, {}), "Ship must release when high");
        s.y = 120; s.vy = 7;
        check(!chooseFlightInput(s, {}), "Ship did not brake its upward inertia");
        s.mode = FlightMode::Wave;
        check(chooseFlightInput(s, {}), "Wave incorrectly used ship inertia");
        s.inverted = true;
        check(!chooseFlightInput(s, {}), "Inverted wave input is reversed");

        for (auto mode : {FlightMode::Ship, FlightMode::Wave}) {
            s = {}; s.mode = mode; s.y = 35; s.top = 1400;
            InputPacer pacer;
            pacer.reset(8);
            Tick longest = 0, current = 0;
            for (int tick = 0; tick < 600; ++tick) {
                s.holdRemaining = pacer.remainingFrames(tick) / 4.;
                s.minHold = pacer.dwellFrames() / 4.;
                bool down = pacer.apply(chooseFlightInput(s, {}), tick);
                check(pacer.currentCps() <= 8, "Climb exceeded the configured CPS limit");
                current = down ? current + 1 : 0;
                longest = std::max(longest, current);
                advance(s, down);
                check(safe(s, {}), "Long-hold climb left flight bounds");
            }
            check(longest > 128, "Long hold was truncated to the old pulse limit");
            check(s.y > 400, "Long hold failed to gain altitude");
            for (bool inverted : {false, true}) for (double scale : {1., .6})
                for (double direction : {1., -1.}) course(mode, inverted, scale, direction);
        }
        Search search(1337, 120);
        std::vector<Action> previous{{10, true}, {30, false}, {200, true}, {500, false}, {650, true}};
        search.observe(previous, 700, 30, true);
        auto next = search.next(2000);
        check(next == prefix(previous, search.guidanceFrom()), "Flight retry changed the successful prefix");
        check(search.guidanceFrom() < 700, "Flight retry did not roll back before death");
        check(700 - search.guidanceFrom() >= 100, "Flight retry left no time for a sustained correction");
        check(search.next(0).empty(), "Empty search horizon underflow");
        std::cout << "Ship/wave guidance, long holds, reversed gravity, mini, reverse travel, prefix and input replay: PASS\n";
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
