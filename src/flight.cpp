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
    auto targetAt = [&](double sampleX) {
        std::vector<std::pair<double, double>> blocked;
        for (auto const& o : obstacles) {
            auto span = verticalSpan(o, sampleX, rx);
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
        return target;
    };
    double target = targetAt(lookX);

    // Wave needs additional routes through successive gaps. Ship inertia
    // benefits from a stable anticipatory target rather than chasing each
    // local free-space centre.
    bool const wave = s.mode == FlightMode::Wave;
    int const Width = wave ? 48 : 24;
    int const Depth = wave ? 40 : 32;
    // Resolve imminent turns at the outer native 240 Hz step, then use longer
    // segments further away. The old uniform 1/2-unit forecast skipped short
    // wave turns and forced ship corrections to start several inputs early.
    double mini = s.scale < .9 ? .85 : 1.;
    double sign = s.inverted ? -1. : 1.;
    double minVelocity = (s.inverted ? -8. : -6.4) / mini;
    double maxVelocity = (s.inverted ? 6.4 : 8.) / mini;
    std::vector<Node> beam{{s.y, s.vy, 0, s.holdRemaining, s.held, s.held}}, next;
    beam.reserve(Width); next.reserve(2 * Width);
    bool answer = s.holdRemaining > 0 ? s.held :
        ((target - s.y) - (s.mode == FlightMode::Ship ? s.vy * 8 : 0)) * sign > 0;
    double time = 0;
    for (int depth = 0; depth < Depth; ++depth) {
        double segment = wave ? (depth < 8 ? .25 : depth < 16 ? .5 : 1.) :
            (depth < 4 ? .25 : depth < 8 ? .5 : 2.);
        // Preserve exact near-field resolution when exploring other horizons.
        if (depth >= 8) segment *= std::clamp(tuning.horizonScale, .7, 1.3);
        next.clear();
        double x1 = s.x + s.vx * time;
        double x2 = x1 + s.vx * segment;
        // Future wave states aim six units ahead of their own position, so a
        // climb starts before the wall without chasing an obsolete gap.
        double localTarget = wave ? targetAt(x2 + s.vx * 6) : target;
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
            double error = std::max(0., std::abs(child.y - localTarget) - 6.);
            child.cost += (error * error * .001 + child.vy * child.vy * .06) * segment / 2 +
                (down != node.held ? (s.mode == FlightMode::Wave ? .25 : 2.) : 0);
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
        time += segment;
    }
    return answer;
}
}
