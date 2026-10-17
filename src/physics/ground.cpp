#include "ground.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rf {
namespace {
using Slices = std::array<std::vector<Obstacle>, 104>;
struct Outcome {
    double survival, clearance;
    bool jumped;
};
struct JumpNode {
    double y, vy, wait, clearance = 1000, firstJump = -1;
    unsigned jumps = 0;
    bool grounded, held, first;
    int trace = 0;
};
// Preserve alternative landings and later takeoffs. A single-jump forecast
// cannot distinguish a good first jump from one that blocks the next jump.
GroundDecision jumpSequence(GroundObservation const &s, std::span<Obstacle const> objects, double bias,
                            Forecast *output, Slices const *slices = nullptr,
                            PlanningBudget *budget = nullptr) {
    constexpr double Step = .5, Horizon = 52;
    constexpr std::size_t Width = 32;
    std::vector<JumpNode> beam{{s.y, s.vy, s.pressRemaining, 1000, -1, 0, s.grounded, s.held, false}}, next;
    beam.reserve(Width);
    next.reserve(2 * Width);
    struct TraceNode {
        Point point;
        int parent;
    };
    std::vector<TraceNode> tree;
    if (output)
        tree.push_back({{s.x, s.y}, -1});
    if (output)
        tree.reserve(1 + Width * 104 * 2);
    std::vector<Obstacle> nearby;
    nearby.reserve(objects.size());
    int bestTrace = 0;
    GroundDecision result{s.held, 0, -1};
    for (double t = 0; t < Horizon; t += Step) {
        next.clear();
        double oldX = s.x + s.vx * t, x = oldX + s.vx * Step;
        std::span<Obstacle const> active;
        if (slices)
            active = (*slices)[static_cast<std::size_t>(t / Step)];
        else {
            horizontalCandidates(objects, oldX, x, s.halfWidth, t, t + Step, nearby);
            active = nearby;
        }
        for (auto const &node : beam)
            for (bool down : {false, true}) {
                if (budget && !budget->consume())
                    goto planningFinished;
                if (down && !node.held && node.wait > 1e-9)
                    continue;
                // Airborne presses have no cube effect; the runtime handles rings.
                if (!node.grounded && down)
                    continue;
                auto child = node;
                child.wait = std::max(0., (down && !node.held ? s.pressInterval : node.wait) - Step);
                child.held = down;
                if (t == 0)
                    child.first = down;
                if (child.grounded && down) {
                    child.vy = s.jumpVelocity;
                    child.grounded = false;
                    ++child.jumps;
                    if (child.firstJump < 0)
                        child.firstJump = t;
                }
                if (!child.grounded)
                    child.vy -= s.gravity * Step;
                child.y += child.vy * Step;
                double landing = -std::numeric_limits<double>::infinity();
                if (node.y - s.halfHeight >= s.floor - 1e-6 && child.y - s.halfHeight <= s.floor)
                    landing = s.floor;
                for (auto const &base : active) {
                    auto o = obstacleAt(base, t + Step);
                    if (o.solid && x + s.halfWidth > o.left && x - s.halfWidth < o.right && child.vy <= 0 &&
                        node.y - s.halfHeight >= o.top - .1 && child.y - s.halfHeight <= o.top)
                        landing = std::max(landing, o.top);
                }
                child.grounded = std::isfinite(landing);
                if (child.grounded) {
                    child.y = landing + s.halfHeight;
                    child.vy = 0;
                }
                bool collision = false;
                for (auto const &base : active) {
                    auto o = obstacleAt(base, t + Step);
                    if (forecastOverlap(oldX, node.y, x, child.y, s.halfWidth, s.halfHeight - .05, base, t,
                                        t + Step)) {
                        collision = true;
                        break;
                    }
                    if (!o.solid && x + s.halfWidth > o.left && x - s.halfWidth < o.right)
                        child.clearance = std::min(child.clearance, child.y - s.halfHeight - o.top);
                }
                if (!collision) {
                    if (output) {
                        tree.push_back({{x, child.y}, node.trace});
                        child.trace = static_cast<int>(tree.size() - 1);
                    }
                    next.push_back(child);
                }
            }
        if (next.empty()) {
            if (output)
                output->collision = Point{x, beam.front().y};
            break;
        }
        auto score = [&](JumpNode const &n) {
            return std::clamp(n.clearance, -100., 100.) - n.jumps * .1 -
                   std::max(0., n.firstJump) * (.002 + std::clamp(bias, -2., 2.) * .002);
        };
        std::stable_sort(next.begin(), next.end(),
                         [&](auto const &a, auto const &b) { return score(a) > score(b); });
        beam.clear();
        for (auto const &node : next) {
            bool duplicate = std::any_of(beam.begin(), beam.end(), [&](auto const &other) {
                return node.first == other.first && node.grounded == other.grounded &&
                       node.held == other.held && std::abs(node.y - other.y) < 1.5 &&
                       std::abs(node.vy - other.vy) < .4;
            });
            if (!duplicate)
                beam.push_back(node);
            if (beam.size() == Width)
                break;
        }
        result = {beam.front().first, t + Step, beam.front().firstJump};
        bestTrace = beam.front().trace;
    }
planningFinished:
    if (output) {
        for (int i = bestTrace; i >= 0; i = tree[i].parent)
            output->path.push_back(tree[i].point);
        std::reverse(output->path.begin(), output->path.end());
        output->down = result.down;
        output->survival = result.passiveSurvival;
    }
    return result;
}
Outcome forecast(GroundObservation const &s, std::span<Obstacle const> objects, double delay,
                 Forecast *output = nullptr, Slices const *slices = nullptr,
                 PlanningBudget *budget = nullptr) {
    constexpr double Step = .5, Horizon = 52;
    double x = s.x, y = s.y, vy = s.vy;
    bool grounded = s.grounded, jumped = false;
    double clearance = 1000;
    if (output) {
        *output = {};
        output->path.push_back({x, y});
    }
    for (double t = 0; t < Horizon; t += Step) {
        if (budget && !budget->consume())
            return {t, clearance, jumped};
        // An already held button may cause a native auto-jump before a release
        // can pass the rate limiter. Forecast that actual constraint as well.
        bool down = (s.held && t < s.holdRemaining) || (!jumped && delay >= 0 && t >= delay);
        if (grounded && down) {
            vy = s.jumpVelocity;
            grounded = false;
            jumped = true;
        }
        double oldX = x, oldY = y;
        x += s.vx * Step;
        if (!grounded)
            vy -= s.gravity * Step;
        y += vy * Step;
        std::span<Obstacle const> active =
            slices ? std::span<Obstacle const>((*slices)[static_cast<std::size_t>(t / Step)]) : objects;
        double landing = -std::numeric_limits<double>::infinity();
        if (oldY - s.halfHeight >= s.floor - 1e-6 && y - s.halfHeight <= s.floor)
            landing = s.floor;
        for (auto const &base : active) {
            auto o = obstacleAt(base, t + Step);
            if (!o.solid || x + s.halfWidth <= o.left || x - s.halfWidth >= o.right)
                continue;
            if (vy <= 0 && oldY - s.halfHeight >= o.top - .1 && y - s.halfHeight <= o.top)
                landing = std::max(landing, o.top);
        }
        grounded = std::isfinite(landing);
        if (grounded) {
            y = landing + s.halfHeight;
            vy = 0;
        }
        for (auto const &base : active) {
            auto o = obstacleAt(base, t + Step);
            // Landing on a solid's top is legal; crossing its side is not.
            if (forecastOverlap(oldX, oldY, x, y, s.halfWidth, s.halfHeight - .05, base, t, t + Step)) {
                if (output) {
                    output->collision = Point{x, y};
                    output->survival = t;
                }
                return {t, clearance, jumped};
            }
            if (x + s.halfWidth > o.left && x - s.halfWidth < o.right && !o.solid)
                clearance = std::min(clearance, y - s.halfHeight - o.top);
        }
        if (output)
            output->path.push_back({x, y});
        // Recheck support, so walking off an elevated block does not invent an
        // infinite floor at the platform's height.
        if (grounded && landing > s.floor + .1) {
            bool supported = false;
            for (auto const &base : active) {
                auto o = obstacleAt(base, t + Step);
                if (o.solid && std::abs(o.top - landing) < .1 && x + s.halfWidth > o.left &&
                    x - s.halfWidth < o.right)
                    supported = true;
            }
            grounded = supported;
        }
    }
    if (output)
        output->survival = Horizon;
    return {Horizon, clearance, jumped};
}
} // namespace
GroundDecision chooseGroundInput(GroundObservation const &input, std::span<Obstacle const> source,
                                 double bias, Forecast *output, PlanningBudget *budget) {
    if (output)
        *output = {};
    if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.vy) ||
        !std::isfinite(input.vx) || input.gravity <= 0 || !std::isfinite(input.gravity))
        return {};
    struct Unflip {
        Forecast *trace;
        bool inverted;
        ~Unflip() {
            if (trace && inverted) {
                for (auto &p : trace->path)
                    p.y = -p.y;
                if (trace->collision)
                    trace->collision->y = -trace->collision->y;
            }
        }
    } unflip{output, input.inverted};
    auto s = input;
    std::vector<Obstacle> transformed;
    if (s.inverted) {
        s.y = -s.y;
        s.vy = -s.vy;
        s.floor = -s.floor;
        transformed.assign(source.begin(), source.end());
        for (auto &o : transformed) {
            double bottom = -o.top;
            o.top = -o.bottom;
            o.bottom = bottom;
            o.vy = -o.vy;
            for (int i = 0; i < o.vertexCount; ++i)
                o.vertices[i].y = -o.vertices[i].y;
        }
        source = transformed;
    }
    auto passive = forecast(s, source, -1, output, nullptr, budget);
    GroundDecision answer{false, passive.survival, -1};
    if (budget && !budget->available())
        return {s.held, 0, -1};
    if (!s.grounded || passive.survival >= 52)
        return answer;
    Slices cache;
    Slices const *slices = nullptr;
    if (source.size() > 16) {
        for (std::size_t i = 0; i < cache.size(); i++) {
            if (budget && !budget->consume())
                return answer;
            double time = i * .5, x = s.x + s.vx * time;
            horizontalCandidates(source, x, x + s.vx * .5, s.halfWidth, time, time + .5, cache[i]);
        }
        slices = &cache;
    }
    double bestScore = passive.survival * 1000 - 10;
    double earliest = s.held ? 0 : s.holdRemaining;
    for (double delay = earliest; delay <= std::min(36., passive.survival); delay += .5) {
        auto outcome = forecast(s, source, delay, nullptr, slices, budget);
        if (budget && !budget->available())
            break;
        double score = outcome.survival * 1000 + std::clamp(outcome.clearance, -100., 100.) - delay * .002;
        if (score > bestScore) {
            bestScore = score;
            answer.jumpDelay = delay;
            answer.down = delay <= std::max(0., .25 + bias);
        }
    }
    // Spend the larger branching budget only when one jump cannot clear the
    // horizon. Retain the simpler predictor on isolated obstacles.
    if (output)
        forecast(s, source, answer.jumpDelay, output, slices);
    if (bestScore < 52000 && (!budget || budget->available())) {
        Forecast sequenceTrace;
        auto sequence = jumpSequence(s, source, bias, output ? &sequenceTrace : nullptr, slices, budget);
        if (sequence.passiveSurvival * 1000 > bestScore + .5) {
            answer.down = sequence.down;
            answer.jumpDelay = sequence.jumpDelay;
            if (output)
                *output = std::move(sequenceTrace);
        }
    }
    if (output)
        output->down = answer.down;
    return answer;
}
} // namespace rf
