#pragma once
#include "physics/planner.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace v2 {
using Tick = std::uint64_t;
using Json = nlohmann::json;
inline constexpr char Version[] = "2.0.0";
inline constexpr Tick MaxTicks = 432000;
struct Action {
    Tick tick = 0;
    bool down = false;
    int button = 1, player = 1;
    bool operator==(Action const &) const = default;
};
struct Timing {
    double dt = 0;
    bool half = false, last = false;
    bool operator==(Timing const &) const = default;
};
struct Sample {
    Tick tick = 0;
    std::array<rf::FormState, 2> players{};
    bool dual = false;
};
struct Frame {
    Tick tick = 0, outer = 0;
    Timing timing;
    double percent = 0;
    std::array<rf::FormState, 2> players{};
    std::array<std::uint64_t, 2> contacts{};
    std::array<bool, 2> disturbed{};
    bool dual = false, independent = false;
    Sample sample() const {
        return {tick, players, dual};
    }
};
struct Scene {
    std::vector<rf::Obstacle> objects;
    std::array<std::vector<rf::Interaction>, 2> interactions;
};
struct Run {
    Json environment;
    std::vector<Action> actions;
    std::vector<Timing> timings;
    std::vector<Sample> trace;
    Tick ticks = 0, outer = 0;
    double percent = 0;
    bool complete = false, verified = false;
    std::uint64_t attempts = 0;
    int cps = 16;
    std::string reason;
};
struct Limits {
    std::uint64_t attempts = 10000;
    double seconds = 3600, attemptSeconds = 600;
};
bool sameSample(Sample const &a, Sample const &b);
std::vector<Action> prefix(std::vector<Action> const &actions, Tick before);
bool heldBefore(std::vector<Action> const &actions, Tick tick, int player);
bool validActions(std::vector<Action> const &actions, Tick end);
void validate(Run const &run);
Json encode(Run const &run);
Run decode(Json const &value);
Json encodeActions(std::vector<Action> const &actions);
std::vector<Action> decodeActions(Json const &value, Tick end = MaxTicks);
Json encodeSample(Sample const &sample);
Sample decodeSample(Json const &value);
} // namespace v2
