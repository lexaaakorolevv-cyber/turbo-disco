#include "solo.hpp"
#include "flight.hpp"
#include "ground.hpp"
#include <algorithm>
#include <cmath>
namespace rf {
FormDecision chooseSoloInput(FormState const &s, std::span<Obstacle const> objects,
                             std::span<Interaction const> interactions, FailureMemory const *failures,
                             std::uint64_t policy, std::uint64_t seed, bool trace) {
    bool interactive = std::any_of(interactions.begin(), interactions.end(), [&](auto const &i) {
        return std::abs((i.area.left + i.area.right) / 2 - s.x) <
               std::abs(s.vx) * std::max(42., s.planningHorizon) + s.halfWidth;
    });
    bool ramps = std::any_of(objects.begin(), objects.end(), [](auto const &o) { return o.ramp; });
    // Preserve the stronger default on simple roads. The form controller is
    // a search policy, not an unconditional replacement for proven cube paths.
    if (s.form == Form::Cube && !interactive && !ramps && s.planningHorizon <= 42 &&
        (policy == 0 || policy % 4 != 0)) {
        GroundObservation g{s.x,
                            s.y,
                            s.vy,
                            s.vx,
                            s.halfWidth,
                            s.halfHeight,
                            s.inverted ? s.ceiling : s.floor,
                            s.jump * (s.scale < .9 ? .8 : 1),
                            s.gravity * s.gravityMod,
                            s.held ? 0 : s.pressRemaining,
                            s.grounded,
                            s.inverted,
                            s.held,
                            s.pressInterval,
                            s.pressRemaining};
        static constexpr double timing[]{0, -.25, .25, -.5, .5, -1, 1, -1.5, 1.5, -2, 2};
        FormDecision result;
        auto decision =
            chooseGroundInput(g, objects, timing[policy % 11] + (failures ? failures->timingBias(s) : 0),
                              trace ? &result.forecast : nullptr);
        result.down = decision.down;
        if (trace)
            result.survival = result.forecast.survival;
        return result;
    }
    if (interactive || ramps || (s.form != Form::Ship && s.form != Form::Wave)) {
        double bias = s.form == Form::Cube && policy == 0 ? 0 : (static_cast<int>(policy % 7) - 3) * .07;
        return chooseFormInput(s, objects, interactions, failures, bias, trace);
    }
    FlightObservation f;
    f.mode = s.form == Form::Wave ? FlightMode::Wave : FlightMode::Ship;
    f.x = s.x;
    f.y = s.y;
    f.vy = s.vy;
    f.vx = s.vx;
    f.halfWidth = s.halfWidth;
    f.halfHeight = s.halfHeight;
    f.bottom = s.floor;
    f.top = s.ceiling;
    f.scale = s.scale;
    f.gravity = s.gravityMod;
    f.inverted = s.inverted;
    f.held = s.held;
    f.pressInterval = s.pressInterval;
    f.pressRemaining = s.pressRemaining;
    f.accelerationScales = s.acceleration;
    f.slopeScale = s.waveSlope;
    auto tuning = flightTuning(policy + 1, seed);
    tuning.lookahead = s.planningHorizon > 42 ? s.planningHorizon : 0;
    tuning.targetY = s.routeTarget;
    if (failures)
        tuning.heightBias += failures->heightBias(s);
    FormDecision result;
    result.down = chooseFlightInput(f, objects, tuning, trace ? &result.forecast : nullptr, failures);
    return result;
}
} // namespace rf
