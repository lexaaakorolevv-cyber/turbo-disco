#include "flight.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace rf {
FlightTuning flightTuning(unsigned long long attempt, unsigned long long seed) {
    // Revisit the neutral policy regularly; explore other gap heights and
    // braking horizons on later attempts without touching the saved prefix.
    static constexpr double biases[]{0, .08, -.08, .18, -.18, .3, -.3, .04, -.04};
    if (attempt <= 1) return {};
    auto phase = (attempt - 2 + seed % 9) % 9;
    return {biases[phase], .85 + .15 * ((attempt + seed) % 3),
        1.5 + static_cast<double>((attempt + seed) % 3)};
}
namespace {
struct Node {
    double y, vy, cost, wait;
    bool held, first;
};
}
bool chooseFlightInput(FlightObservation const& s, std::span<FlightObstacle const> obstacles, FlightTuning tuning) {
    if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.vy) ||
        !std::isfinite(s.vx) || !std::isfinite(s.bottom) || !std::isfinite(s.top) ||
        s.top <= s.bottom + 2 * s.halfHeight) return s.held;
    double rx = s.halfWidth + tuning.clearance;
    double ry = s.halfHeight + tuning.clearance;
    double low = s.bottom + ry, high = s.top - ry;
    if (high <= low) return s.held;
    double desired = std::clamp((low + high) / 2 + tuning.heightBias * (high - low), low, high);

    // Select a free vertical gap ahead, rather than always aiming through the
    // centre of a wall. Union overlapping obstacle intervals at this slice.
    // A ship needs time to arrest its existing vertical speed before climbing.
    // Wave can turn immediately, so its target slice may be much closer.
    double lookX = s.x + s.vx * (s.mode == FlightMode::Ship ? 32 : 18) * tuning.horizonScale;
    std::vector<std::pair<double, double>> blocked;
    for (auto const& o : obstacles) {
        auto span = verticalSpan(o, lookX, rx);
        if (span && span->second + ry > low && span->first - ry < high)
            blocked.emplace_back(std::max(low, span->first - ry), std::min(high, span->second + ry));
    }
    std::sort(blocked.begin(), blocked.end());
    double cursor = low, target = desired, best = std::numeric_limits<double>::infinity();
    auto gap = [&](double a, double b) {
        if (b <= a + 2) return;
        auto y = std::clamp(desired, a + std::min(8., (b - a) / 4), b - std::min(8., (b - a) / 4));
        double cost = std::abs(y - desired) + .25 * std::abs(y - s.y);
        if (cost < best) { best = cost; target = y; }
    };
    for (auto const& interval : blocked) {
        gap(cursor, interval.first);
        cursor = std::max(cursor, interval.second);
    }
    gap(cursor, high);

    constexpr int Width = 16, Depth = 20;
    // Native velocity uses GD's legacy 60 Hz time unit. One forecast segment
    // spans two such units, independently of render FPS or the input tick dt.
    double segment = (s.mode == FlightMode::Wave ? 1. : 2.) * std::clamp(tuning.horizonScale, .7, 1.3);
    double mini = s.scale < .9 ? .85 : 1.;
    double sign = s.inverted ? -1. : 1.;
    double minVelocity = (s.inverted ? -8. : -6.4) / mini;
    double maxVelocity = (s.inverted ? 6.4 : 8.) / mini;
    std::vector<Node> beam{{s.y, s.vy, 0, s.holdRemaining, s.held, s.held}}, next;
    beam.reserve(Width); next.reserve(2 * Width);
    bool answer = s.holdRemaining > 0 ? s.held :
        ((target - s.y) - (s.mode == FlightMode::Ship ? s.vy * 8 : 0)) * sign > 0;
    for (int depth = 0; depth < Depth; ++depth) {
        next.clear();
        double x1 = s.x + s.vx * segment * depth;
        double x2 = x1 + s.vx * segment;
        for (auto const& node : beam) for (bool down : {false, true}) {
            if (down != node.held && node.wait > 1e-9) continue;
            auto child = node;
            child.wait = std::max(0., (down != node.held ? s.minHold : node.wait) - segment);
            child.first = depth == 0 ? down : node.first;
            child.held = down;
            if (s.mode == FlightMode::Wave) {
                // Wave changes direction immediately; mini wave has 2:1 slope.
                child.vy = std::abs(s.vx) * (s.scale < .9 ? 2 : 1) * sign * (down ? 1 : -1);
                child.y = node.y + child.vy * segment;
            } else {
                bool falling = node.vy * sign < 0;
                double acceleration = down ? (falling ? .4791 : .3833) : -(falling ? .3066 : .4599);
                child.vy = std::clamp(node.vy + sign * acceleration * s.gravity * segment / mini, minVelocity, maxVelocity);
                child.y = node.y + (node.vy + child.vy) * segment / 2;
            }
            if (child.y < low || child.y > high) continue;
            bool collision = false;
            for (auto const& obstacle : obstacles)
                if (sweptOverlap(x1, node.y, x2, child.y, rx, ry, obstacle)) { collision = true; break; }
            if (collision) continue;
            // A small height error is preferable to continuous correction.
            double error = std::max(0., std::abs(child.y - target) - 6.);
            child.cost += error * error * .001 + child.vy * child.vy * .06 + (down != node.held ? 3. : 0);
            next.push_back(child);
        }
        if (next.empty()) break; // Keep the input with the longest safe forecast.
        std::stable_sort(next.begin(), next.end(), [](Node const& a, Node const& b) { return a.cost < b.cost; });
        // Keep diverse height/velocity states so a cheap coasting path cannot
        // crowd all early braking/avoidance candidates out of the beam.
        beam.clear();
        for (auto const& candidate : next) {
            bool duplicate = std::any_of(beam.begin(), beam.end(), [&](Node const& n) {
                return n.first == candidate.first && std::abs(n.y - candidate.y) < 2 &&
                    std::abs(n.vy - candidate.vy) < .4 && std::abs(n.wait - candidate.wait) < .25;
            });
            if (!duplicate) beam.push_back(candidate);
            if (beam.size() == Width) break;
        }
        answer = beam.front().first;
    }
    return answer;
}
}
