#pragma once
#include <array>
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
};
// Geometry is a forecast only. Actual GD collisions still decide each attempt.
bool sweptOverlap(double x1, double y1, double x2, double y2,
    double halfWidth, double halfHeight, Obstacle const& object);
std::optional<std::pair<double, double>> verticalSpan(Obstacle const& object, double x, double halfWidth);
}
