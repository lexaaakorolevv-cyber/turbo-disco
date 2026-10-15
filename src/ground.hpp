#pragma once
#include "geometry.hpp"
#include <span>

namespace rf {
struct GroundObservation {
    double x = 0, y = 105, vy = 0, vx = 5.2;
    double halfWidth = 14, halfHeight = 14, floor = 91;
    double jumpVelocity = 11.18, gravity = .9582;
    double holdRemaining = 0;
    bool grounded = true, inverted = false, held = false;
    double pressInterval = 0, pressRemaining = 0;
};
struct GroundDecision {
    bool down = false;
    double passiveSurvival = 0, jumpDelay = -1;
};
GroundDecision chooseGroundInput(GroundObservation const& state, std::span<Obstacle const> objects,
    double timingBias = 0, Forecast* forecast = nullptr);
}
