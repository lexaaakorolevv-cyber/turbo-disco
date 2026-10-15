#pragma once
#include <span>
#include "geometry.hpp"

namespace rf {
using FlightObstacle = Obstacle;
enum class FlightMode { Ship, Wave };
struct FlightObservation {
    double x = 0, y = 150, vy = 0, vx = 5.77;
    double halfWidth = 12, halfHeight = 12;
    double bottom = 0, top = 300;
    double scale = 1;
    double gravity = 1;
    // Native 60 Hz units until another rising edge, and press-to-press
    // interval. Releasing is always allowed, including a one-tick tap.
    double pressRemaining = 0, pressInterval = 0;
    FlightMode mode = FlightMode::Ship;
    bool inverted = false, held = false;
};
struct FlightTuning {
    double heightBias = 0; // Fraction of the native flight height.
    double horizonScale = 1;
    double clearance = 2;
};
FlightTuning flightTuning(unsigned long long attempt, unsigned long long seed);

// Only proposes an input. This approximate, bounded beam search never advances
// or restores Geometry Dash. The live game judges the resulting attempt.
bool chooseFlightInput(FlightObservation const& state,
    std::span<FlightObstacle const> obstacles, FlightTuning tuning = {});
}
