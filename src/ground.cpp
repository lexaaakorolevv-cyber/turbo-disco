#include "ground.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rf {
namespace {
struct Outcome { double survival, clearance; bool jumped; };
Outcome forecast(GroundObservation const& s, std::span<Obstacle const> objects, double delay) {
    constexpr double Step = .5, Horizon = 52;
    double x = s.x, y = s.y, vy = s.vy;
    bool grounded = s.grounded, jumped = false;
    double clearance = 1000;
    for (double t = 0; t < Horizon; t += Step) {
        // An already held button may cause a native auto-jump before a release
        // can pass the rate limiter. Forecast that actual constraint as well.
        bool down = (s.held && t < s.holdRemaining) || (!jumped && delay >= 0 && t >= delay);
        if (grounded && down) { vy = s.jumpVelocity; grounded = false; jumped = true; }
        double oldX = x, oldY = y;
        x += s.vx * Step;
        if (!grounded) vy -= s.gravity * Step;
        y += vy * Step;
        double landing = -std::numeric_limits<double>::infinity();
        if (oldY - s.halfHeight >= s.floor - 1e-6 && y - s.halfHeight <= s.floor) landing = s.floor;
        for (auto const& o : objects) {
            if (!o.solid || x + s.halfWidth <= o.left || x - s.halfWidth >= o.right) continue;
            if (vy <= 0 && oldY - s.halfHeight >= o.top - .1 && y - s.halfHeight <= o.top)
                landing = std::max(landing, o.top);
        }
        grounded = std::isfinite(landing);
        if (grounded) { y = landing + s.halfHeight; vy = 0; }
        for (auto const& o : objects) {
            // Landing on a solid's top is legal; crossing its side is not.
            if (sweptOverlap(oldX, oldY, x, y, s.halfWidth, s.halfHeight - .05, o)) return {t, clearance, jumped};
            if (x + s.halfWidth > o.left && x - s.halfWidth < o.right && !o.solid)
                clearance = std::min(clearance, y - s.halfHeight - o.top);
        }
        // Recheck support, so walking off an elevated block does not invent an
        // infinite floor at the platform's height.
        if (grounded && landing > s.floor + .1) {
            bool supported = false;
            for (auto const& o : objects)
                if (o.solid && std::abs(o.top - landing) < .1 && x + s.halfWidth > o.left && x - s.halfWidth < o.right)
                    supported = true;
            grounded = supported;
        }
    }
    return {Horizon, clearance, jumped};
}
}
GroundDecision chooseGroundInput(GroundObservation const& input, std::span<Obstacle const> source, double bias) {
    if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.vy) ||
        !std::isfinite(input.vx) || input.gravity <= 0 || !std::isfinite(input.gravity)) return {};
    auto s = input;
    std::vector<Obstacle> transformed;
    if (s.inverted) {
        s.y = -s.y; s.vy = -s.vy; s.floor = -s.floor;
        transformed.assign(source.begin(), source.end());
        for (auto& o : transformed) {
            double bottom = -o.top; o.top = -o.bottom; o.bottom = bottom;
            for (int i = 0; i < o.vertexCount; ++i) o.vertices[i].y = -o.vertices[i].y;
        }
        source = transformed;
    }
    auto passive = forecast(s, source, -1);
    GroundDecision answer{false, passive.survival, -1};
    if (!s.grounded || passive.survival >= 52) return answer;
    double bestScore = passive.survival * 1000 - 10;
    double earliest = s.held ? 0 : s.holdRemaining;
    for (double delay = earliest; delay <= std::min(36., passive.survival); delay += .5) {
        auto outcome = forecast(s, source, delay);
        double score = outcome.survival * 1000 + std::clamp(outcome.clearance, -100., 100.) - delay * .002;
        if (score > bestScore) {
            bestScore = score;
            answer.jumpDelay = delay;
            answer.down = delay <= std::max(0., .25 + bias);
        }
    }
    return answer;
}
}
