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
    require(m.searchMaxCps >= 0 && m.searchMaxCps <= 20, "Invalid search CPS limit");
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
        require(static_cast<bool>(s.flags&(1u<<12))==s.second.has_value(),"Missing/unexpected second player trace");
        if(s.second){auto const&p=*s.second;require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.vy)&&
            std::isfinite(p.speed)&&std::isfinite(p.size)&&std::abs(p.x)<1e9&&std::abs(p.y)<1e9,"Invalid second player trace");}
        previous = s.tick;
        first = false;
    }
    Tick required=0;bool wasDual=false;
    for(auto const&s:m.trace){
        require(s.tick<=required,"Missing regular trace sample");
        if(s.tick==required)required+=16;
        else require(s.second.has_value()!=wasDual,"Unexpected intermediate trace sample");
        wasDual=s.second.has_value();
    }
    require(required>=m.ticks,"Trace must cover every 16th tick");
}

json toJson(Macro const& m) {
    validate(m);
    json j = {{"format", "routefinder"}, {"schema", 2}, {"environment", m.environment},
        {"ticks", m.ticks}, {"outer_updates", m.outerUpdates}, {"best_percent", m.bestPercent}, {"completed", m.completed},
        {"verified_from_start", m.verified}, {"attempts", m.attempts},
        {"search_seed", m.searchSeed}, {"search_max_cps", m.searchMaxCps}, {"stop_reason", m.stopReason},
        {"actions", json::array()}, {"trace", json::array()}, {"timings", json::array()}};
    for (auto const& t : m.timings) j["timings"].push_back({t.dt, t.half, t.last});
    for (auto const& a : m.actions)
        j["actions"].push_back({{"tick", a.tick}, {"down", a.down},
            {"button", a.button}, {"player", a.player}});
    for (auto const& s : m.trace) {
        j["trace"].push_back({{"tick", s.tick}, {"x", s.x}, {"y", s.y}, {"vy", s.vy},
            {"speed", s.speed}, {"size", s.size}, {"dt", s.dt}, {"flags", s.flags}});
        auto& entry=j["trace"].back();entry["player2"]=nullptr;
        if(s.second){auto const&p=*s.second;entry["player2"]={{"x",p.x},{"y",p.y},{"vy",p.vy},{"speed",p.speed},{"size",p.size},{"flags",p.flags}};}
    }
    return j;
}

Macro fromJson(json const& j) {
    require(j.at("format") == "routefinder" && (j.at("schema") == 1 || j.at("schema") == 2), "Unsupported format/schema");
    Macro m;
    m.environment = j.at("environment");
    m.ticks = uintField(j.at("ticks"));
    m.outerUpdates = uintField(j.at("outer_updates"));
    m.bestPercent = number(j.at("best_percent"));
    m.completed = j.at("completed").get<bool>();
    m.verified = j.at("verified_from_start").get<bool>();
    m.attempts = uintField(j.at("attempts"));
    m.searchSeed = uintField(j.at("search_seed"));
    if (j.contains("search_max_cps")) {
        auto cps = uintField(j.at("search_max_cps"));
        require(cps <= 20, "Invalid search CPS limit");
        m.searchMaxCps = static_cast<int>(cps);
    }
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
    require(trace.is_array() && trace.size() <= MaxTicks, "Invalid trace array");
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
        if(j.at("schema")==2){
            auto const&p=s.at("player2");if(!p.is_null()){
                auto pf=uintField(p.at("flags"));require(pf<=UINT32_MAX,"Invalid player2 flags");
                m.trace.back().second=PlayerSample{number(p.at("x")),number(p.at("y")),number(p.at("vy")),number(p.at("speed")),number(p.at("size")),static_cast<std::uint32_t>(pf)};
            }
        }
    }
    validate(m);
    return m;
}

void saveMacro(std::filesystem::path const& path, Macro const& m) {
    saveJsonFile(path, toJson(m));
}
void saveJsonFile(std::filesystem::path const& path, json const& value, int indent) {
    auto contents = value.dump(indent);
    require(contents.size() <= 128 * 1024 * 1024, "JSON exceeds 128 MiB");
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
    return fromJson(loadJsonFile(path));
}
json loadJsonFile(std::filesystem::path const& path) {
    auto source = path;
    if (!std::filesystem::exists(source)) source += ".bak";
    require(std::filesystem::file_size(source) <= 128 * 1024 * 1024, "Macro exceeds 128 MiB");
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open macro");
    return json::parse(in);
}

bool sameSample(Sample const& a, Sample const& b) {
    if(a.second.has_value()!=b.second.has_value())return false;
    if(a.second){auto const&x=*a.second;auto const&y=*b.second;
        if(!(x.flags==y.flags&&std::abs(x.x-y.x)<=1e-4&&std::abs(x.y-y.y)<=1e-4&&std::abs(x.vy-y.vy)<=1e-7&&
            std::abs(x.speed-y.speed)<=1e-7&&std::abs(x.size-y.size)<=1e-7))return false;
    }
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
    if (improvement && !candidate.actions.empty()) {
        // A compact queue of edits shares one immutable parent; do not retain
        // dozens of complete long-level macros just to shift a few ticks.
        m_focusBase = candidate.actions;
        m_focusDeath = death;
        m_focusPolicy = m_policy;
        m_focusEdits.clear();
        unsigned pulses = 0;
        for (std::size_t i = m_focusBase.size(); i-- > 0 && pulses < 4;) {
            if (!m_focusBase[i].down) continue;
            if (m_focusBase[i].tick + std::max<Tick>(m_window, 240) < death) break;
            ++pulses;
            std::size_t release = i + 1 < m_focusBase.size() ? i + 1 : i;
            if (release == i) for (int beforeDeath : {1, 2, 4, 8, 16, 32, 64, 96, 128})
                m_focusEdits.push_back({i, i, -beforeDeath, true});
            for (int shift : {-1, 1, -2, 2, -4, 4, -8, 8, -16, 16, -32, 32}) {
                m_focusEdits.push_back({i, release, shift}); // Move the whole hold.
                if (release != i) m_focusEdits.push_back({release, release, shift}); // Adjust duration.
                // Coordinate later pulses as well; single-edge edits cannot
                // repair a whole section that started several ticks too late.
                if (pulses > 1) m_focusEdits.push_back({i, m_focusBase.size() - 1, shift});
            }
        }
    }
    m_elites.push_back(std::move(candidate));
    std::stable_sort(m_elites.begin(), m_elites.end(), [](Elite const& a, Elite const& b) {
        if (a.percent != b.percent) return a.percent > b.percent;
        if (a.actions.size() != b.actions.size()) return a.actions.size() < b.actions.size();
        return a.deathTick > b.deathTick;
    });
    // Retain several routes to the frontier instead of filling the archive
    // entirely with near-identical failures at the same percentage.
    std::vector<Elite> diverse;
    for (auto& elite : m_elites) {
        auto bucket = static_cast<int>(elite.percent * 4);
        auto count = std::count_if(diverse.begin(), diverse.end(), [&](auto const& e) {
            return static_cast<int>(e.percent * 4) == bucket;
        });
        if (count < 4) diverse.push_back(std::move(elite));
        if (diverse.size() == ArchiveLimit) break;
    }
    m_elites = std::move(diverse);
}
Search::Elite const& Search::best() const {
    if (m_elites.empty()) throw std::logic_error("No observed attempts");
    return m_elites.front();
}
void Search::preferTiming(int direction) {
    if(!direction)return;
    std::stable_sort(m_focusEdits.begin(),m_focusEdits.end(),[&](auto const&a,auto const&b){
        bool preferredA=a.delta*direction>0,preferredB=b.delta*direction>0;
        if(preferredA!=preferredB)return preferredA;
        return std::abs(a.delta)<std::abs(b.delta);
    });
}
bool Search::issue(std::vector<Action> const& actions) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t v) { hash = (hash ^ v) * 1099511628211ULL; };
    mix(m_guidanceFrom); mix(m_policy);
    for (auto const& a : actions) { mix(a.tick); mix(a.down); }
    if (m_issued.size() >= 8192) m_issued.clear();
    return m_issued.insert(hash).second;
}
std::vector<Action> Search::next(Tick limit) {
    m_guidanceFrom = 0;
    m_phase = SearchPhase::Guided;
    if (!limit) return {};
    if (m_elites.empty()) { m_policy = 0; m_generation = 1; return {}; }
    if (m_stale >= 128 && (m_choices + 1) % 32 == 0) {
        ++m_choices; ++m_escapeCount;
        m_policy = (m_rng() + m_escapeCount * 29) % 99;
        m_phase = SearchPhase::Restart;
        if (issue({})) return {};
        m_phase = SearchPhase::Guided;
    }
    // Reserve one in four attempts for alternate policies / broader rollback.
    // The other attempts optimize the current frontier before abandoning it.
    if (m_choices++ % 4 != 0) while (!m_focusEdits.empty()) {
        auto edit = m_focusEdits.front(); m_focusEdits.pop_front();
        if (m_focusBase.empty() || m_focusBase.back().tick >= limit) continue;
        if (edit.release) {
            auto tick = static_cast<std::int64_t>(m_focusDeath) + edit.delta;
            if (tick <= static_cast<std::int64_t>(m_focusBase.back().tick) || tick >= static_cast<std::int64_t>(limit)) continue;
            auto result = m_focusBase;
            appendAction(result, static_cast<Tick>(tick), false);
            m_policy = m_focusPolicy;
            m_guidanceFrom = std::min(limit, m_focusDeath + 1);
            if (issue(result)) { m_phase = SearchPhase::Timing; return result; }
            continue;
        }
        auto first = static_cast<std::int64_t>(m_focusBase[edit.first].tick) + edit.delta;
        auto last = static_cast<std::int64_t>(m_focusBase[edit.last].tick) + edit.delta;
        auto low = edit.first ? m_focusBase[edit.first - 1].tick + 1 : 0;
        auto high = edit.last + 1 < m_focusBase.size() ? m_focusBase[edit.last + 1].tick - 1 : limit - 1;
        if (first < static_cast<std::int64_t>(low) || last > static_cast<std::int64_t>(high)) continue;
        auto result = m_focusBase;
        for (auto i = edit.first; i <= edit.last; ++i)
            result[i].tick = static_cast<Tick>(static_cast<std::int64_t>(result[i].tick) + edit.delta);
        m_policy = m_focusPolicy;
        m_guidanceFrom = std::min(limit, std::max(m_focusDeath + 1, result.back().tick + 1));
        if (issue(result)) { m_phase = SearchPhase::Timing; return result; }
    }
    std::vector<Action> result;
    for (int retry = 0; retry < 64; ++retry) {
        auto generation = m_generation++;
        auto const& parent = m_elites[generation % 3 == 2 ? m_rng() % m_elites.size() : 0];
        Tick window = std::min<Tick>(m_window * (1 + m_stale / 16), 2400);
        Tick death = std::min(parent.deathTick, limit - 1);
        static constexpr Tick rollback[]{120, 64, 192, 32, 256, 96, 384, 512};
        Tick back = std::min(window, rollback[generation % 8]);
        if (parent.flight) back = std::min(window, std::max<Tick>(back, 120));
        Tick start = death > back ? death - back : 0;
        auto kind = generation % 6;
        m_policy = (generation * 17 + generation / 6) % 99;
        if (m_stale >= 32 && m_choices % 8 == 0) {
            // Sample a complete tail from a wider rollback, rather than
            // repeatedly changing one ineffective input at the same frontier.
            start = death > window ? death - window : 0;
            result = prefix(parent.actions, start);
            bool held = heldBefore(result, start);
            static constexpr Tick intervals[]{30, 60, 90, 120, 180, 240, 360, 480, 720};
            for (Tick t = start, edits=0; t < std::min(limit, death + 120) && edits<6; t += intervals[m_rng() % 9],++edits)
                appendAction(result, t, held = !held);
            m_guidanceFrom = result.empty() ? start : result.back().tick + 1;
            m_phase = SearchPhase::Explore;
        } else if (kind == 0 || kind == 1 || kind == 4) {
            // A cube cannot correct an already committed jump while airborne.
            // Start before the takeoff that led to the failure.
            if (!parent.flight) for (auto i = parent.actions.rbegin(); i != parent.actions.rend(); ++i)
                if (i->down && i->tick + window >= death) {
                    start = std::min(start, i->tick > 8 ? i->tick - 8 : 0);
                    break;
                }
            result = prefix(parent.actions, start);
            m_guidanceFrom = start;
        } else if (kind == 2 && std::any_of(parent.actions.begin(), parent.actions.end(),
            [&](auto const& a) { return a.tick < limit; })) {
            result = prefix(parent.actions, limit);
            auto index = result.size() - 1 - m_rng() % std::min<std::size_t>(6, result.size());
            static constexpr int shifts[]{-1, 1, -2, 2, -4, 4, -8, 8, -16, 16, -32, 32};
            auto shifted = static_cast<std::int64_t>(result[index].tick) + shifts[(generation / 6) % 12];
            result[index].tick = static_cast<Tick>(std::clamp<std::int64_t>(shifted, 0, limit - 1));
            std::stable_sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.tick < b.tick; });
            auto raw = std::move(result); result.clear();
            for (auto const& a : raw) appendAction(result, a.tick, a.down);
            m_guidanceFrom = result.empty() ? 0 : result.back().tick + 1;
        } else {
            if (kind == 5) start = death > window ? death - window : 0;
            result = prefix(parent.actions, start);
            bool down = heldBefore(result, start);
            if (kind == 5) {
                for (Tick t = start; t < std::min(limit, death + 120); t += 15 + m_rng() % 105)
                    appendAction(result, t, down = !down);
            } else {
                static constexpr Tick holds[]{1, 4, 8, 16, 32, 64, 128, 240, 480};
                appendAction(result, start, !down);
                Tick end = start + holds[(generation / 6) % 9];
                if (generation % 18 != 3 && end < limit) appendAction(result, end, down);
            }
            m_guidanceFrom = result.empty() ? start : result.back().tick + 1;
        }
        if (issue(result)) break;
    }
    return result;
}

std::string Budget::exhausted(std::uint64_t attempts, double seconds) const {
    if (seconds >= maxSeconds) return "Достигнут лимит времени";
    if (attempts >= maxAttempts) return "Достигнут лимит попыток";
    return {};
}
}
