#include "ground.hpp"
#include "input.hpp"
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace rf;
namespace {
void check(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
void runCourse(double speed, double scale, bool inverted, bool reverse, bool platforms, int cps = 8, bool dense = false) {
    double floor = 91, half = 14 * scale;
    std::vector<Obstacle> local{{200, 224, floor, floor + 20 * scale},
        {410, 434, floor, floor + 25 * scale}, {660, 700, floor, floor + 15 * scale}};
    if (platforms) local = {{190, 250, floor, floor + 30, true},
        {380, 450, floor, floor + 55, true}, {550, 574, floor, floor + 20},
        {720, 780, floor, floor + 30, true}};
    if (dense) { local.clear(); for (int i = 0; i < 4; ++i) local.push_back({160. + i * 115, 184. + i * 115, floor, floor + 20}); }
    auto objects = local;
    for (auto& o : objects) {
        if (reverse) { double left = -o.right; o.right = -o.left; o.left = left; }
        if (inverted) { double bottom = -o.top; o.top = -o.bottom; o.bottom = bottom; }
    }
    double x = 0, y = floor + half, vy = 0;
    bool grounded = true, held = false;
    InputPacer pacer; pacer.reset(cps);
    std::vector<Action> actions;
    for (Tick tick = 0; tick < 1000; ++tick) {
        double sign = inverted ? -1 : 1, direction = reverse ? -1 : 1;
        GroundObservation s{x * direction, y * sign, vy * sign, speed * direction, half, half,
            floor * sign, 11.18 * (scale < .9 ? .8 : 1), .9582,
            pacer.remainingFrames(tick) / 4., grounded, inverted, held};
        s.pressInterval = pacer.pressIntervalFrames() / 4.;
        s.pressRemaining = pacer.pressRemainingFrames(tick) / 4.;
        bool down = pacer.apply(chooseGroundInput(s, objects).down, tick);
        if (down != held) appendAction(actions, tick, down);
        held = down;
        check(cps == 0 ? pacer.remainingFrames(tick) == 0 : pacer.currentCps() <= cps, "Cube input profile violated");
        // Independent semi-analytic oracle: different dt, gravity and position
        // integration from the predictor. The real GD is still a separate test.
        if (grounded && down) { vy = 11.05 * (scale < .9 ? .8 : 1); grounded = false; }
        double previousFoot = y - half;
        x += speed * .25;
        y += vy * .25 - (grounded ? 0 : .94 * .25 * .25 / 2);
        if (!grounded) vy -= .94 * .25;
        double landing = -1e9;
        if (previousFoot >= floor - .01 && y - half <= floor) landing = floor;
        for (auto const& o : local) if (o.solid && x + half > o.left && x - half < o.right &&
            vy <= 0 && previousFoot >= o.top - .1 && y - half <= o.top) landing = std::max(landing, o.top);
        grounded = landing > -1e8;
        if (grounded) { y = landing + half; vy = 0; }
        for (auto const& o : local) {
            if (x + half > o.left && x - half < o.right && y + half > o.bottom && y - half < o.top - .05) {
                std::cerr << "speed=" << speed << " scale=" << scale << " inverted=" << inverted
                    << " reverse=" << reverse << " platforms=" << platforms << " tick=" << tick
                    << " x=" << x << " y=" << y << '\n';
                throw std::runtime_error("Cube hit oracle obstacle");
            }
        }
    }
    check(x > 800 && !actions.empty(), "Cube did not finish course");
}
}
int main() {
    try {
        GroundObservation idle;
        check(!chooseGroundInput(idle, {}).down, "Unnecessary jump on empty ground");
        Obstacle triangle{0, 100, 0, 100};
        triangle.vertexCount = 3;
        triangle.vertices = {Point{0, 0}, Point{100, 0}, Point{100, 100}, Point{}};
        check(!sweptOverlap(10, 80, 20, 80, 2, 2, triangle), "Empty triangle half treated as wall");
        check(sweptOverlap(60, 20, 70, 20, 2, 2, triangle), "Triangle collision missed");
        auto span = verticalSpan(triangle, 20, 1);
        check(span && std::abs(span->second - 21) < 1e-6, "Triangle slice is the bounding box");
        Obstacle circle{0, 100, 0, 100, false, 50};
        check(!sweptOverlap(4, 4, 5, 5, 1, 1, circle), "Circle empty corner treated as wall");
        check(sweptOverlap(40, 40, 60, 60, 1, 1, circle), "Circle collision missed");
        int passed = 0;
        for (int cps : {16}) for (double speed : {4., 5.2, 6.6}) for (double scale : {1., .6})
            for (bool inverted : {false, true}) for (bool reverse : {false, true}) {
                runCourse(speed, scale, inverted, reverse, false, cps); ++passed;
            }
        for (int cps : {16}) for (bool inverted : {false, true}) for (bool reverse : {false, true}) {
            runCourse(5.2, 1, inverted, reverse, true, cps); ++passed;
        }
        for (bool inverted : {false, true}) for (bool reverse : {false, true}) {
            runCourse(5.2, 1, inverted, reverse, false, 16, true); ++passed;
        }
        std::cout << passed << " cube obstacle/platform scenarios, 16 CPS; shape geometry: PASS\n";
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
