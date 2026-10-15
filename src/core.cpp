#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace rf {
using json = nlohmann::json;
namespace {
void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint64_t uintField(json const& value) {
    require(value.is_number_unsigned() || value.is_number_integer(), "Expected an integer");
    if (value.is_number_integer() && !value.is_number_unsigned())
        require(value.get<std::int64_t>() >= 0, "Negative integer");
    return value.get<std::uint64_t>();
}
double number(json const& value) {
    require(value.is_number(), "Expected a number");
    auto result = value.get<double>();
    require(std::isfinite(result), "Non-finite number");
    return result;
}
}

void validate(Macro const& m) {
    require(m.environment.is_object(), "Missing environment");
    require(m.ticks > 0 && m.ticks <= MaxTicks, "Invalid tick count");
    require(m.outerUpdates > 0 && m.outerUpdates <= 240 * 1800, "Invalid outer update count");
    require(std::isfinite(m.bestPercent) && m.bestPercent >= 0 && m.bestPercent <= 100,
        "Invalid percentage");
    require(!m.verified || m.completed, "A partial macro cannot be verified");
    require(m.timings.size() == m.ticks, "Missing per-tick physics timing");
    for (auto const& t : m.timings)
        require(std::isfinite(t.dt) && t.dt > 0 && t.dt <= 100, "Invalid physics timing");
    require(m.actions.size() <= m.ticks, "Too many actions");
    bool held = false;
    Tick previous = 0;
    bool first = true;
    for (auto const& a : m.actions) {
        require(a.tick < m.ticks, "Action outside attempt");
        require(first || a.tick > previous, "Actions must have strictly increasing ticks");
        require(a.button == 1 && a.player == 1, "Only jump / player 1 is supported");
        require(a.down != held, "Redundant or invalid transition");
        held = a.down;
        previous = a.tick;
        first = false;
    }
    require(!m.trace.empty() && m.trace.front().tick == 0, "Missing initial trace sample");
    require(m.trace.size() <= m.ticks, "Too many samples");
    first = true;
    for (auto const& s : m.trace) {
        require(s.tick < m.ticks && (first || s.tick > previous), "Invalid trace order");
        require(std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.vy) &&
            std::isfinite(s.dt) && std::isfinite(s.speed) && std::isfinite(s.size) &&
            s.dt > 0 && s.dt <= 100 && std::abs(s.x) < 1e9 && std::abs(s.y) < 1e9,
            "Invalid trace value");
        previous = s.tick;
        first = false;
    }
    require(m.trace.size() == (m.ticks - 1) / 16 + 1, "Trace must cover every 16th tick");
    for (std::size_t i = 0; i < m.trace.size(); ++i)
        require(m.trace[i].tick == i * 16, "Missing trace sample");
}

json toJson(Macro const& m) {
    validate(m);
    json j = {{"format", "routefinder"}, {"schema", 1}, {"environment", m.environment},
        {"ticks", m.ticks}, {"outer_updates", m.outerUpdates}, {"best_percent", m.bestPercent}, {"completed", m.completed},
        {"verified_from_start", m.verified}, {"attempts", m.attempts},
        {"search_seed", m.searchSeed}, {"stop_reason", m.stopReason},
        {"actions", json::array()}, {"trace", json::array()}, {"timings", json::array()}};
    for (auto const& t : m.timings) j["timings"].push_back({t.dt, t.half, t.last});
    for (auto const& a : m.actions)
        j["actions"].push_back({{"tick", a.tick}, {"down", a.down},
            {"button", a.button}, {"player", a.player}});
    for (auto const& s : m.trace)
        j["trace"].push_back({{"tick", s.tick}, {"x", s.x}, {"y", s.y}, {"vy", s.vy},
            {"speed", s.speed}, {"size", s.size}, {"dt", s.dt}, {"flags", s.flags}});
    return j;
}

Macro fromJson(json const& j) {
    require(j.at("format") == "routefinder" && j.at("schema") == 1, "Unsupported format/schema");
    Macro m;
    m.environment = j.at("environment");
    m.ticks = uintField(j.at("ticks"));
    m.outerUpdates = uintField(j.at("outer_updates"));
    m.bestPercent = number(j.at("best_percent"));
    m.completed = j.at("completed").get<bool>();
    m.verified = j.at("verified_from_start").get<bool>();
    m.attempts = uintField(j.at("attempts"));
    m.searchSeed = uintField(j.at("search_seed"));
    m.stopReason = j.at("stop_reason").get<std::string>();
    auto const& actions = j.at("actions");
    auto const& trace = j.at("trace");
    auto const& timings = j.at("timings");
    require(timings.is_array() && timings.size() <= MaxTicks, "Invalid timings array");
    for (auto const& t : timings) {
        require(t.is_array() && t.size() == 3, "Invalid timing entry");
        m.timings.push_back({number(t.at(0)), t.at(1).get<bool>(), t.at(2).get<bool>()});
    }
    require(actions.is_array() && actions.size() <= MaxTicks, "Invalid actions array");
    require(trace.is_array() && trace.size() <= MaxTicks / 16 + 1, "Invalid trace array");
    for (auto const& a : actions) {
        require(uintField(a.at("button")) == 1 && uintField(a.at("player")) == 1,
            "Unsupported button/player");
        m.actions.push_back({uintField(a.at("tick")), a.at("down").get<bool>(), 1, 1});
    }
    for (auto const& s : trace) {
        auto flags = uintField(s.at("flags"));
        require(flags <= UINT32_MAX, "Invalid flags");
        m.trace.push_back({uintField(s.at("tick")), number(s.at("x")), number(s.at("y")),
            number(s.at("vy")), number(s.at("speed")), number(s.at("size")),
            number(s.at("dt")), static_cast<std::uint32_t>(flags)});
    }
    validate(m);
    return m;
}

void saveMacro(std::filesystem::path const& path, Macro const& m) {
    auto contents = toJson(m).dump(2);
    std::filesystem::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create macro temporary file");
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        if (!out) throw std::runtime_error("Cannot flush macro");
    }
    // POSIX rename replaces atomically. Windows needs a backup dance; preserve
    // the previous file on errors and recover .bak on the next load.
    auto backup = path;
    backup += ".bak";
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (!ec) return;
    if (!std::filesystem::exists(path)) throw std::filesystem::filesystem_error("rename", temp, path, ec);
    std::filesystem::remove(backup, ec);
    std::filesystem::rename(path, backup);
    try { std::filesystem::rename(temp, path); }
    catch (...) {
        std::filesystem::rename(backup, path, ec);
        throw;
    }
    std::filesystem::remove(backup, ec);
}

Macro loadMacro(std::filesystem::path const& path) {
    auto source = path;
    if (!std::filesystem::exists(source)) source += ".bak";
    require(std::filesystem::file_size(source) <= 128 * 1024 * 1024, "Macro exceeds 128 MiB");
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open macro");
    return fromJson(json::parse(in));
}

bool sameSample(Sample const& a, Sample const& b) {
    return a.tick == b.tick && a.flags == b.flags &&
        std::abs(a.x - b.x) <= 1e-4 && std::abs(a.y - b.y) <= 1e-4 &&
        std::abs(a.vy - b.vy) <= 1e-7 && std::abs(a.speed - b.speed) <= 1e-7 &&
        std::abs(a.size - b.size) <= 1e-7 && std::abs(a.dt - b.dt) <= 1e-8;
}

bool heldBefore(std::vector<Action> const& actions, Tick tick) {
    bool held = false;
    for (auto const& a : actions) {
        if (a.tick >= tick) break;
        held = a.down;
    }
    return held;
}
void appendAction(std::vector<Action>& actions, Tick tick, bool down) {
    if (!actions.empty() && actions.back().tick == tick) actions.pop_back();
    if (actions.empty() ? down : actions.back().down != down)
        actions.push_back({tick, down, 1, 1});
}
std::vector<Action> prefix(std::vector<Action> const& actions, Tick end) {
    auto it = std::lower_bound(actions.begin(), actions.end(), end,
        [](Action const& a, Tick t) { return a.tick < t; });
    return {actions.begin(), it};
}

Search::Search(std::uint64_t seed, Tick window) : m_rng(seed), m_window(std::clamp<Tick>(window, 8, 2400)) {}
void Search::observe(std::vector<Action> const& actions, Tick death, double percent, bool flight) {
    auto candidate = Elite{prefix(actions, death), death, percent, flight};
    bool improvement = m_elites.empty() || percent > m_elites.front().percent + 1e-6 ||
        (std::abs(percent - m_elites.front().percent) <= 1e-6 && death > m_elites.front().deathTick);
    m_stale = improvement ? 0 : m_stale + 1;
    if (std::any_of(m_elites.begin(), m_elites.end(), [&](Elite const& e) {
        return e.actions == candidate.actions;
    })) return;
    m_elites.push_back(std::move(candidate));
    std::stable_sort(m_elites.begin(), m_elites.end(), [](Elite const& a, Elite const& b) {
        if (std::abs(a.percent - b.percent) > 1e-6) return a.percent > b.percent;
        return a.deathTick > b.deathTick;
    });
    if (m_elites.size() > 8) m_elites.resize(8);
}
Search::Elite const& Search::best() const {
    if (m_elites.empty()) throw std::logic_error("No observed attempts");
    return m_elites.front();
}
std::vector<Action> Search::next(Tick limit) {
    m_guidanceFrom = 0;
    if (!limit) return {};
    if (m_elites.empty()) return {}; // First attempt establishes the no-input frontier.
    auto generation = m_generation++;
    auto const& parent = m_elites[(generation % 7 == 6) ? m_rng() % m_elites.size() : 0];
    Tick window = std::min<Tick>(m_window * (1 + m_stale / 48), 2400);
    Tick death = std::min(parent.deathTick, limit - 1);
    Tick back = 1 + ((generation / 8) * 37) % window;
    Tick start = death > back ? death - back : 0;
    if (generation % 8 == 7) { // Random wider rollback, including previously useful clicks.
        start = death > window ? death - window : 0;
        start += m_rng() % (death - start + 1);
    }
    auto result = prefix(parent.actions, start);
    if (parent.flight) {
        // Keep the successful flight exactly until the rollback boundary.
        // Re-plan the ship/wave tail with a new policy and unrestricted holds.
        m_guidanceFrom = start;
        return result;
    }
    static constexpr Tick holds[]{1, 4, 8, 16, 32, 64, 128, 240, 480, 960};
    if (generation % 8 != 7) {
        // Enumerate a pulse or release before the collision, preserving the
        // exact earlier prefix. A held parent is also explored as a release.
        bool initial = heldBefore(result, start);
        appendAction(result, start, !initial);
        auto end = start + holds[(generation % 7 + generation / 56) % 10];
        // Every seventh candidate keeps holding/releasing until the end. A
        // long climb must never be capped by the old 128-tick pulse limit.
        if (generation % 8 != 6 && end < limit) appendAction(result, end, initial);
    } else {
        auto end = std::min<Tick>(limit, death + 480);
        bool down = heldBefore(result, start);
        for (Tick t = start; t < end; t += 1 + m_rng() % 80) {
            down = !down;
            appendAction(result, t, down);
        }
    }
    m_guidanceFrom = std::max(start, result.empty() ? Tick{0} : result.back().tick + 1);
    return result;
}

std::string Budget::exhausted(std::uint64_t attempts, double seconds) const {
    if (seconds >= maxSeconds) return "Достигнут лимит времени";
    if (attempts >= maxAttempts) return "Достигнут лимит попыток";
    return {};
}
}
