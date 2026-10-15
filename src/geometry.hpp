#pragma once
#include <array>
#include <algorithm>
#include <span>
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
// Conservative horizontal broad phase. Includes the whole moving rectangle
// during this time slice, so no possible swept collision is discarded.
inline bool overlapsHorizontal(Obstacle const& o,double x1,double x2,double rx,double start,double end) {
    double a=o.vx*std::clamp(start,0.,o.motionHorizon),b=o.vx*std::clamp(end,0.,o.motionHorizon);
    return std::max(x1,x2)+rx>=o.left+std::min(a,b) &&
        std::min(x1,x2)-rx<=o.right+std::max(a,b);
}
inline void horizontalCandidates(std::span<Obstacle const> source,double x1,double x2,double rx,
    double start,double end,std::vector<Obstacle>& result) {
    result.clear();
    for(auto const&o:source)if(overlapsHorizontal(o,x1,x2,rx,start,end))result.push_back(o);
}
Obstacle obstacleAt(Obstacle const& object, double time);
bool forecastOverlap(double x1, double y1, double x2, double y2, double halfWidth,
    double halfHeight, Obstacle const& object, double start, double end);
// Geometry is a forecast only. Actual GD collisions still decide each attempt.
bool sweptOverlap(double x1, double y1, double x2, double y2,
    double halfWidth, double halfHeight, Obstacle const& object);
std::optional<std::pair<double, double>> verticalSpan(Obstacle const& object, double x, double halfWidth);
}
