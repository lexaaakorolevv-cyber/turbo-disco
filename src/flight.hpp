#pragma once
#include <span>

namespace rf {
struct FlightObstacle {
    double left, right, bottom, top;
};
enum class FlightMode { Ship, Wave };
struct FlightObservation {
    double x = 0, y = 150, vy = 0, vx = 5.77;
    double halfWidth = 12, halfHeight = 12;
    double bottom = 0, top = 300;
    double scale = 1;
    double gravity = 1;
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
