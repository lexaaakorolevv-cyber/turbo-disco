#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <optional>
#include <utility>

namespace rf {
struct Point { double x = 0, y = 0; };
struct Obstacle {
    double left, right, bottom, top;
    bool solid = false;
    double radius = 0;
    std::array<Point, 4> vertices{};
    int vertexCount = 0;
    double vx = 0, vy = 0, motionHorizon = 0;
    std::uint64_t id = 0;
};
struct Forecast {
    std::vector<Point> path;
    std::optional<Point> collision;
    bool down = false;
    double survival = 0;
};
Obstacle obstacleAt(Obstacle const& object, double time);
bool forecastOverlap(double x1, double y1, double x2, double y2, double halfWidth,
    double halfHeight, Obstacle const& object, double start, double end);
// Geometry is a forecast only. Actual GD collisions still decide each attempt.
bool sweptOverlap(double x1, double y1, double x2, double y2,
    double halfWidth, double halfHeight, Obstacle const& object);
std::optional<std::pair<double, double>> verticalSpan(Obstacle const& object, double x, double halfWidth);
}
