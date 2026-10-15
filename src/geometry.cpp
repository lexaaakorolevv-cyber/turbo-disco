#include "geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rf {
namespace {
double pointSegment(Point p, Point a, Point b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double length = dx * dx + dy * dy;
    double t = length > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0., 1.) : 0;
    double x = p.x - a.x - t * dx, y = p.y - a.y - t * dy;
    return x * x + y * y;
}
}
bool sweptOverlap(double x1, double y1, double x2, double y2, double rx, double ry, Obstacle const& o) {
    if (std::max(x1, x2) + rx <= o.left || std::min(x1, x2) - rx >= o.right ||
        std::max(y1, y2) + ry <= o.bottom || std::min(y1, y2) - ry >= o.top) return false;
    if (o.radius > 0) {
        double cx = (o.left + o.right) / 2, cy = (o.bottom + o.top) / 2;
        // Distance between the motion segment and a rectangle centred on the
        // circle. This refines the AABB's falsely blocked circular corners.
        double enter = 0, leave = 1;
        auto clip = [&](double start, double delta, double low, double high) {
            if (std::abs(delta) < 1e-12) return start >= low && start <= high;
            double a = (low - start) / delta, b = (high - start) / delta;
            if (a > b) std::swap(a, b);
            enter = std::max(enter, a); leave = std::min(leave, b);
            return enter <= leave;
        };
        if (clip(x1, x2 - x1, cx - rx, cx + rx) && clip(y1, y2 - y1, cy - ry, cy + ry)) return true;
        double distance = std::numeric_limits<double>::infinity();
        for (Point p : {Point{x1, y1}, Point{x2, y2}}) {
            double dx = std::max(0., std::abs(p.x - cx) - rx);
            double dy = std::max(0., std::abs(p.y - cy) - ry);
            distance = std::min(distance, dx * dx + dy * dy);
        }
        for (double x : {cx - rx, cx + rx}) for (double y : {cy - ry, cy + ry})
            distance = std::min(distance, pointSegment({x, y}, {x1, y1}, {x2, y2}));
        return distance < o.radius * o.radius;
    }
    if (o.vertexCount < 3) return true;
    auto separated = [&](double nx, double ny) {
        if (std::abs(nx) + std::abs(ny) < 1e-12) return false;
        double a = x1 * nx + y1 * ny, b = x2 * nx + y2 * ny;
        double radius = rx * std::abs(nx) + ry * std::abs(ny);
        double low = std::min(a, b) - radius, high = std::max(a, b) + radius;
        double objectLow = std::numeric_limits<double>::infinity(), objectHigh = -objectLow;
        for (int i = 0; i < o.vertexCount; ++i) {
            double p = o.vertices[i].x * nx + o.vertices[i].y * ny;
            objectLow = std::min(objectLow, p); objectHigh = std::max(objectHigh, p);
        }
        return high <= objectLow || low >= objectHigh;
    };
    if (separated(1, 0) || separated(0, 1) || separated(-(y2 - y1), x2 - x1)) return false;
    for (int i = 0; i < o.vertexCount; ++i) {
        auto a = o.vertices[i], b = o.vertices[(i + 1) % o.vertexCount];
        if (separated(-(b.y - a.y), b.x - a.x)) return false;
    }
    return true;
}

std::optional<std::pair<double, double>> verticalSpan(Obstacle const& o, double x, double rx) {
    double left = x - rx, right = x + rx;
    if (right < o.left || left > o.right) return {};
    if (o.radius > 0) {
        double cx = (o.left + o.right) / 2, cy = (o.bottom + o.top) / 2;
        double dx = std::max({0., left - cx, cx - right});
        if (dx > o.radius) return {};
        double dy = std::sqrt(std::max(0., o.radius * o.radius - dx * dx));
        return std::pair{cy - dy, cy + dy};
    }
    if (o.vertexCount < 3) return std::pair{o.bottom, o.top};
    double low = std::numeric_limits<double>::infinity(), high = -low;
    auto include = [&](double y) { low = std::min(low, y); high = std::max(high, y); };
    for (int i = 0; i < o.vertexCount; ++i) {
        auto a = o.vertices[i], b = o.vertices[(i + 1) % o.vertexCount];
        if (a.x >= left && a.x <= right) include(a.y);
        for (double edge : {left, right}) {
            if (std::abs(b.x - a.x) < 1e-12) continue;
            double t = (edge - a.x) / (b.x - a.x);
            if (t >= 0 && t <= 1) include(a.y + t * (b.y - a.y));
        }
    }
    if (low > high) return {};
    return std::pair{low, high};
}
}
