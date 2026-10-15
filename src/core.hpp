#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace rf {
using Tick = std::uint64_t;
inline constexpr Tick MaxTicks = 240 * 1800;
inline constexpr char ModVersion[] = "1.1.1";

struct Action {
    Tick tick = 0;
    bool down = false;
    int button = 1;
    int player = 1;
    bool operator==(Action const&) const = default;
};

// A pre-input sample, taken at the same processCommands boundary on both runs.
// This is a desync detector, never a state restoration mechanism.
struct Sample {
    Tick tick = 0;
    double x = 0, y = 0, vy = 0, speed = 0, size = 0, dt = 0;
    std::uint32_t flags = 0;
    bool operator==(Sample const&) const = default;
};

struct Timing {
    double dt = 0;
    bool half = false;
    bool last = false;
    bool operator==(Timing const&) const = default;
};

struct Macro {
    nlohmann::json environment;
    std::vector<Action> actions;
    std::vector<Sample> trace;
    std::vector<Timing> timings;
    Tick ticks = 0;
    Tick outerUpdates = 0;
    double bestPercent = 0;
    bool completed = false;
    bool verified = false; // Provenance only when loaded from disk; runtime must recheck.
    std::uint64_t attempts = 0;
    std::uint64_t searchSeed = 0;
    int searchMaxCps = 0; // Search provenance; 0 means an uncapped manual recording.
    std::string stopReason;
};

void validate(Macro const& macro);
nlohmann::json toJson(Macro const& macro);
Macro fromJson(nlohmann::json const& json);
void saveMacro(std::filesystem::path const& path, Macro const& macro);
Macro loadMacro(std::filesystem::path const& path);
bool sameSample(Sample const& expected, Sample const& actual);
bool heldBefore(std::vector<Action> const& actions, Tick tick);
void appendAction(std::vector<Action>& actions, Tick tick, bool down);
std::vector<Action> prefix(std::vector<Action> const& actions, Tick endExclusive);

// Bounded elitist search scored only by actual game attempts. Each genome is
// replayed from a native reset; forecasts never certify an elite or a success.
class Search {
public:
    struct Elite {
        std::vector<Action> actions;
        Tick deathTick = 0;
        double percent = 0;
        bool flight = false;
    };
    Search(std::uint64_t seed, Tick window);
    void observe(std::vector<Action> const& actions, Tick deathTick, double percent, bool flight = false);
    std::vector<Action> next(Tick maxTicks);
    Elite const& best() const;
    std::size_t size() const { return m_elites.size(); }
    Tick guidanceFrom() const { return m_guidanceFrom; }
private:
    std::mt19937_64 m_rng;
    Tick m_window;
    std::uint64_t m_generation = 0, m_stale = 0;
    Tick m_guidanceFrom = 0;
    std::vector<Elite> m_elites;
};

struct Budget {
    std::uint64_t maxAttempts = 2000;
    double maxSeconds = 600;
    std::string exhausted(std::uint64_t attempts, double elapsed) const;
};
}
