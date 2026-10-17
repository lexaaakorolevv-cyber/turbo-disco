#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <unordered_set>
#include <deque>
#include <nlohmann/json.hpp>

namespace rf {
using Tick = std::uint64_t;
inline constexpr Tick MaxTicks = 240 * 1800;
inline constexpr char ModVersion[] = "1.14.0";

struct Action {
    Tick tick = 0;
    bool down = false;
    int button = 1;
    int player = 1;
    bool operator==(Action const&) const = default;
};

// A pre-input sample, taken at the same processCommands boundary on both runs.
// This is a desync detector, never a state restoration mechanism.
struct PlayerSample {
    double x=0,y=0,vy=0,speed=0,size=0;
    std::uint32_t flags=0;
    bool operator==(PlayerSample const&) const=default;
};
struct Sample {
    Tick tick = 0;
    double x = 0, y = 0, vy = 0, speed = 0, size = 0, dt = 0;
    std::uint32_t flags = 0;
    std::optional<PlayerSample> second;
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
    int searchMaxCps = 0; // Search provenance; 0 means uncapped input (manual or automatic).
    std::string stopReason;
};

void validate(Macro const& macro);
nlohmann::json toJson(Macro const& macro);
Macro fromJson(nlohmann::json const& json);
void saveMacro(std::filesystem::path const& path, Macro const& macro);
Macro loadMacro(std::filesystem::path const& path);
void saveJsonFile(std::filesystem::path const& path, nlohmann::json const& value, int indent = 2);
nlohmann::json loadJsonFile(std::filesystem::path const& path);
nlohmann::json compactPlan(std::vector<Action> const& actions);
std::vector<Action> expandPlan(nlohmann::json const& value, Tick end = MaxTicks);
bool sameSample(Sample const& expected, Sample const& actual);
bool heldBefore(std::vector<Action> const& actions, Tick tick, int player=1);
void appendAction(std::vector<Action>& actions, Tick tick, bool down, int player=1);
std::vector<Action> prefix(std::vector<Action> const& actions, Tick endExclusive);

// Bounded elitist search scored only by actual game attempts. Each genome is
// replayed from a native reset; forecasts never certify an elite or a success.
enum class SearchPhase { Guided, Timing, Explore, Restart };
class Search {
public:
    static constexpr std::size_t ArchiveLimit = 24;
    static constexpr std::array<int,12> CheckpointPercents{1,2,5,10,20,30,40,50,60,70,80,90};
    struct Checkpoint {int percent=0;std::vector<Action> actions;Sample state;unsigned confirmations=1;std::uint64_t attempt=0;};
    struct Elite {
        std::vector<Action> actions;
        Tick deathTick = 0;
        double percent = 0;
        bool flight = false;
        std::uint64_t policy=0,trials=0,routeKey=0;
        std::uint64_t stalls=0;
        double frontier=0;
    };
    Search(std::uint64_t seed, Tick window,bool independent=false);
    void observe(std::vector<Action> const& actions, Tick deathTick, double percent, bool flight = false);
    std::vector<Action> next(Tick maxTicks);
    Elite const& best() const;
    std::size_t size() const { return m_elites.size(); }
    Tick guidanceFrom() const { return m_guidanceFrom; }
    std::uint64_t policy() const { return m_policy; }
    SearchPhase phase() const { return m_phase; }
    std::uint64_t staleAttempts() const { return m_stale; }
    std::optional<double> branchPercent() const;
    Tick window() const { return m_window; }
    void preferTiming(int direction);
    void preferPlayer(int player);
    void preferFailure(int player,Tick actionTick,int direction,bool duration);
    void observeCheckpoint(int percent,std::vector<Action> const& actions,Sample const& state,std::uint64_t attempt);
    void invalidateCheckpoint(Tick tick);
    std::optional<Sample> const& rollbackState() const{return m_rollbackState;}
    int rollbackPercent() const;
    std::size_t checkpointCount() const;
    nlohmann::json snapshot() const;
    static Search restore(nlohmann::json const& value);
private:
    std::mt19937_64 m_rng;
    bool m_independent=false;
    Tick m_window;
    std::uint64_t m_generation = 0, m_stale = 0;
    Tick m_guidanceFrom = 0;
    std::uint64_t m_policy = 0;
    struct FocusEdit { std::size_t first, last; int delta; bool release = false; int tailDelta = 0; };
    std::deque<FocusEdit> m_focusEdits;
    std::vector<Action> m_focusBase;
    Tick m_focusDeath = 0;
    std::uint64_t m_focusPolicy = 0, m_choices = 0;
    std::uint64_t m_escapeCount = 0;
    SearchPhase m_phase = SearchPhase::Guided;
    bool issue(std::vector<Action> const& actions,std::optional<std::uint64_t> route=std::nullopt);
    std::uint64_t routeStalls(std::uint64_t route) const;
    std::optional<std::size_t> alternative(std::uint64_t excluded) const;
    std::optional<std::uint64_t> m_pendingRoute;
    std::unordered_set<std::uint64_t> m_issued;
    std::vector<Elite> m_elites;
    std::vector<Checkpoint> m_checkpoints;
    std::optional<Sample> m_rollbackState;
};

struct SearchSessionState {
    Macro best;
    Search search;
    std::vector<Action> plan;
    std::uint64_t seed, attempts;
    double seconds;
};
nlohmann::json searchSessionJson(Macro const& best, Search const& search,
    std::vector<Action> const& plan, std::uint64_t seed, std::uint64_t attempts, double seconds);
SearchSessionState parseSearchSession(nlohmann::json const& value);

struct Budget {
    std::uint64_t maxAttempts = 2000;
    double maxSeconds = 600;
    std::string exhausted(std::uint64_t attempts, double elapsed) const;
};
}
