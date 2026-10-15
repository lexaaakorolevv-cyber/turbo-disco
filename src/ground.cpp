#include "ground.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rf {
namespace {
struct Outcome { double survival, clearance; bool jumped; };
struct JumpNode {
    double y, vy, wait, clearance = 1000, firstJump = -1;
    unsigned jumps = 0;
    bool grounded, held, first;
};
// Preserve alternative landings and later takeoffs. A single-jump forecast
// cannot distinguish a good first jump from one that blocks the next jump.
GroundDecision jumpSequence(GroundObservation const& s, std::span<Obstacle const> objects, double bias) {
    constexpr double Step = .5, Horizon = 52;
    constexpr std::size_t Width = 32;
    std::vector<JumpNode> beam{{s.y, s.vy, s.holdRemaining, 1000, -1, 0, s.grounded, s.held, false}}, next;
    beam.reserve(Width); next.reserve(2 * Width);
    GroundDecision result;
    for (double t = 0; t < Horizon; t += Step) {
        next.clear();
        double oldX = s.x + s.vx * t, x = oldX + s.vx * Step;
        for (auto const& node : beam) for (bool down : {false, true}) {
            if (node.wait > 1e-9 && down != node.held) continue;
            // Airborne presses have no cube effect; the runtime handles rings.
            if (!node.grounded && down && node.wait <= 1e-9) continue;
            auto child = node;
            child.wait = std::max(0., node.wait - Step);
            child.held = down;
            if (t == 0) child.first = down;
            if (child.grounded && down) {
                child.vy = s.jumpVelocity; child.grounded = false; ++child.jumps;
                if (child.firstJump < 0) child.firstJump = t;
            }
            if (!child.grounded) child.vy -= s.gravity * Step;
            child.y += child.vy * Step;
            double landing = -std::numeric_limits<double>::infinity();
            if (node.y - s.halfHeight >= s.floor - 1e-6 && child.y - s.halfHeight <= s.floor) landing = s.floor;
            for (auto const& o : objects) if (o.solid && x + s.halfWidth > o.left && x - s.halfWidth < o.right &&
                child.vy <= 0 && node.y - s.halfHeight >= o.top - .1 && child.y - s.halfHeight <= o.top)
                landing = std::max(landing, o.top);
            child.grounded = std::isfinite(landing);
            if (child.grounded) { child.y = landing + s.halfHeight; child.vy = 0; }
            bool collision = false;
            for (auto const& o : objects) {
                if (sweptOverlap(oldX, node.y, x, child.y, s.halfWidth, s.halfHeight - .05, o)) {
                    collision = true; break;
                }
                if (!o.solid && x + s.halfWidth > o.left && x - s.halfWidth < o.right)
                    child.clearance = std::min(child.clearance, child.y - s.halfHeight - o.top);
            }
            if (!collision) next.push_back(child);
        }
        if (next.empty()) break;
        auto score = [&](JumpNode const& n) {
            return std::clamp(n.clearance, -100., 100.) - n.jumps * .1 -
                std::max(0., n.firstJump) * (.002 + std::clamp(bias, -2., 2.) * .002);
        };
        std::stable_sort(next.begin(), next.end(), [&](auto const& a, auto const& b) { return score(a) > score(b); });
        beam.clear();
        for (auto const& node : next) {
            bool duplicate = std::any_of(beam.begin(), beam.end(), [&](auto const& other) {
                return node.first == other.first && node.grounded == other.grounded && node.held == other.held &&
                    std::abs(node.y - other.y) < 1.5 && std::abs(node.vy - other.vy) < .4;
            });
            if (!duplicate) beam.push_back(node);
            if (beam.size() == Width) break;
        }
        result = {beam.front().first, t + Step, beam.front().firstJump};
    }
    return result;
}
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
    // Spend the larger branching budget only when one jump cannot clear the
    // horizon. Retain the simpler predictor on isolated obstacles.
    if (bestScore < 52000 && s.holdRemaining == 0) {
        auto sequence = jumpSequence(s, source, bias);
        if (sequence.passiveSurvival * 1000 > bestScore + .5) {
            answer.down = sequence.down;
            answer.jumpDelay = sequence.jumpDelay;
        }
    }
    return answer;
}
}
