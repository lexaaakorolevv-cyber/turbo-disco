#include "analysis.hpp"
#include "planner.hpp"
#include "flight.hpp"
#include "input.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool ok, char const *message) {
    if (!ok)
        throw std::runtime_error(message);
}
AnalysisJob map() {
    AnalysisJob job;
    job.scanned = 80;
    job.objects = {{350, 150, MapKind::Orb},
                   {50, 140, MapKind::Orb},
                   {100, 110, MapKind::Hazard},
                   {700, 200, MapKind::Mode, Form::Ship},
                   {900, 200, MapKind::Dual},
                   {950, 200, MapKind::Speed},
                   {1050, 200, MapKind::Mode, Form::Wave}};
    return job;
}
void maps() {
    auto job = map();
    job.twoPlayer = true;
    auto result = analyzeLevel(std::move(job));
    check(result.scanned == 80 && result.twoPlayer, "Analysis lost snapshot metadata");
    check(result.counts[static_cast<unsigned>(MapKind::Orb)] == 2 && result.chains == 1,
          "Orb-chain analysis failed");
    check(result.forms[0] && result.forms[1] && result.forms[2], "Initial/portal forms missing");
    check(result.objects.front().x == 50 && result.objects.back().x == 1050, "Map not ordered");
    auto json = result.toJson();
    check(json["schema"] == 1 && json["transitions"].size() == 4, "Analysis report JSON incomplete");
    check(result.report.find("Ограничения") != std::string::npos, "Report hid static-snapshot limitations");
    FormState state;
    state.vx = 5.2;
    auto hint = result.hint(state);
    check(hint.horizon == 98 && hint.interactions == 2, "Separated future chain did not extend forecast");
    state.y = 1000;
    check(result.hint(state).horizon == 42, "Other player lane changed guidance");
    state = {};
    state.vx = -5.2;
    state.x = 400;
    check(result.hint(state).horizon == 98, "Reverse-direction map query failed");
    auto changed = map();
    changed.objects.push_back({200, 120, MapKind::Mode, Form::Ship});
    check(analyzeLevel(changed).hint(FormState{}).horizon == 42, "Hint crossed native form handoff");
    changed = map();
    changed.objects.push_back({200, 120, MapKind::Script});
    check(analyzeLevel(changed).hint(FormState{}).horizon == 42, "Hint inferred a scripted future");
    changed = map();
    changed.objects[0].uncertain = true;
    check(analyzeLevel(changed).hint(FormState{}).horizon == 42,
          "Conditional orb treated as a guaranteed chain");
    changed = map();
    changed.objects[0].x = std::numeric_limits<double>::infinity();
    bool rejected = false;
    try {
        analyzeLevel(changed);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Invalid geometry accepted");
    std::stop_source cancellation;
    cancellation.request_stop();
    rejected = false;
    try {
        analyzeLevel(map(), cancellation.get_token());
    } catch (...) {
        rejected = true;
    }
    check(rejected, "Analysis ignored cancellation");
}
void guidance() {
    FormState state;
    auto basic = chooseFormInput(state, {}, {}, nullptr, 0, false);
    state.planningHorizon = 98;
    auto extended = chooseFormInput(state, {}, {}, nullptr, 0, false);
    check(basic.survival == 42 && extended.survival == 98, "Analysis horizon not consumed by form planner");
    check(!extended.down, "Analysis made an empty cube floor jump");
    auto dual = chooseDualInput(state, state, {}, {}, {}, nullptr, false, true);
    check(dual.survival == 98, "Independent dual ignored analysis horizon");
    state = {};
    check(chooseFormInput(state, {}, {}, nullptr, 0, false).survival == basic.survival,
          "Default planner behavior changed");
    FlightObservation ship;
    ship.y = 200;
    ship.bottom = 0;
    ship.top = 500;
    Forecast shortPath, longPath;
    FlightTuning tuning;
    chooseFlightInput(ship, {}, tuning, &shortPath);
    tuning.lookahead = 98;
    chooseFlightInput(ship, {}, tuning, &longPath);
    check(longPath.survival >= 98 && longPath.survival > shortPath.survival,
          "Ship ignored structural lookahead");
    ship.mode = FlightMode::Wave;
    chooseFlightInput(ship, {}, tuning, &longPath);
    check(longPath.survival >= 98, "Wave ignored structural lookahead");
    AnalysisJob dense;
    for (unsigned i = 0; i < 5000; i++)
        dense.objects.push_back({100. + i * .001, 110, MapKind::Hazard});
    check(analyzeLevel(std::move(dense)).hint(FormState{}).horizon == 42,
          "Dense map query did not fall back safely");
}
void worker() {
    AnalysisWorker worker;
    check(worker.start(map()), "Worker did not start");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (worker.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    check(!worker.busy(), "Worker exceeded test limit");
    std::string error;
    check(worker.take(error).has_value() && error.empty(), "Numeric worker failed");
    check(!worker.take(error), "Worker delivered result twice");
    check(worker.start(map()), "Worker could not restart");
    worker.cancel();
}
// Independent wave physics through a fork. The lower corridor looks safe
// locally but ends behind a long divider; its far closure requires choosing
// the upper corridor before the divider begins.
bool forkRoad(bool analyzed) {
    std::vector<Obstacle> geometry;
    AnalysisJob job;
    for (int i = 0; i < 22; i++) {
        double x = 50 + i * 30;
        geometry.push_back({x, x + 30, 140, 160});
        job.objects.push_back(
            {x + 15, 150, MapKind::Hazard, Form::Cube, false, MapRect{x, x + 30, 140, 160}});
    }
    geometry.push_back({350, 750, 0, 140});
    job.objects.push_back({550, 70, MapKind::Hazard, Form::Cube, false, MapRect{350, 750, 0, 140}});
    auto map = analyzeLevel(std::move(job));
    InputPacer pacer;
    FlightObservation s;
    s.mode = FlightMode::Wave;
    s.x = 0;
    s.y = 150;
    s.vx = 5.2;
    s.bottom = 0;
    s.top = 300;
    for (unsigned tick = 0; tick < 440; tick++) {
        s.pressRemaining = pacer.pressRemainingFrames(tick) / 4.;
        s.pressInterval = 3.75;
        FlightTuning tuning;
        if (analyzed) {
            FormState state;
            state.form = Form::Wave;
            state.x = s.x;
            state.y = s.y;
            state.vx = s.vx;
            state.halfWidth = state.halfHeight = 12;
            state.floor = 0;
            state.ceiling = 300;
            auto hint = map.hint(state);
            if (hint.horizon > 42)
                tuning.lookahead = hint.horizon;
            tuning.targetY = hint.targetY;
        }
        bool down = pacer.apply(chooseFlightInput(s, geometry, tuning), tick);
        s.held = down;
        for (int i = 0; i < 2; i++) {
            s.x += s.vx * .125;
            s.y += (down ? 1 : -1) * s.vx * .125;
            if (s.y - 12 < 0 || s.y + 12 > 300)
                return false;
            for (auto const &o : geometry)
                if (s.x + 12 > o.left && s.x - 12 < o.right && s.y + 12 > o.bottom && s.y - 12 < o.top)
                    return false;
        }
    }
    return true;
}
} // namespace
int main() {
    try {
        maps();
        guidance();
        worker();
        check(!forkRoad(false), "Fork fixture did not expose local shortsightedness");
        check(forkRoad(true), "Analyzed fork route failed independent wave physics");
        std::cout << "Structural analysis, conditional/reverse/lane queries, native handoffs, extended full "
                     "horizons, independent fork road, dense fallback and numeric worker: PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
