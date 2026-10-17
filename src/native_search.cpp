#include "native_search.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace v2 {
namespace {
void need(bool yes) {
    if (!yes)
        throw std::runtime_error("Invalid native search state");
}
std::uint64_t integer(Json const &j, std::uint64_t max) {
    need(j.is_number_integer() && j.get<double>() >= 0 && j.get<double>() <= double(max));
    return j.get<std::uint64_t>();
}
unsigned held(Frame const &f) {
    return unsigned(f.players[0].held) | (f.independent ? unsigned(f.players[1].held) << 1 : 0);
}
bool sameTiming(Timing const &a, Timing const &b) {
    return std::abs(a.dt - b.dt) <= 1e-8 && a.half == b.half && a.last == b.last;
}
} // namespace
void NativeSearch::configure(unsigned step, std::size_t cap, std::uint64_t limit) {
    need(step >= 1 && step <= 8 && cap >= 64 && cap <= 65536 && limit >= 64 && limit <= 50000000);
    *this = {};
    m_step = step;
    m_cap = cap;
    m_limit = limit;
}
void NativeSearch::beginAttempt() {
    m_cursor = 0;
    m_next = 0;
    m_mask = 0;
}
bool NativeSearch::validateReplay(Frame const &f, std::size_t actions) {
    if (m_cursor >= m_path.size())
        return true;
    auto const &n = m_path[m_cursor];
    if (f.tick < n.sample.tick)
        return true;
    if (f.tick != n.sample.tick || f.outer != n.outer || n.independent != f.independent ||
        n.held != held(f) || n.contacts != f.contacts || n.actions != actions ||
        !sameTiming(n.timing, f.timing) || !sameSample(n.sample, f.sample()))
        return false;
    ++m_cursor;
    return true;
}
bool NativeSearch::needsDecision(Frame const &f) const {
    return (m_fork && !m_path.empty() && f.tick == m_path.back().sample.tick) || f.outer >= m_next;
}
bool NativeSearch::atLimit() const {
    return m_decisions >= m_limit || (!m_fork && m_path.size() >= m_cap);
}
unsigned NativeSearch::decide(Frame const &f, std::size_t actions, unsigned suggested, unsigned legal) {
    need(legal > 0 && legal <= (f.independent ? 15u : 3u));
    if (m_fork) {
        need(!m_path.empty() && f.tick == m_path.back().sample.tick && m_cursor == m_path.size());
        m_mask = m_path.back().chosen;
        need(legal & (1u << m_mask));
        m_fork = false;
    } else {
        need(!atLimit());
        // The approximate controller orders alternatives. Its score cannot
        // prune a route or certify that an input actually survives.
        unsigned chosen = suggested;
        if (!(legal & (1u << chosen))) {
            chosen = held(f);
            if (!(legal & (1u << chosen)))
                chosen = std::countr_zero(legal);
        }
        Node n;
        n.sample = f.sample();
        n.timing = f.timing;
        n.outer = f.outer;
        n.actions = actions;
        n.contacts = f.contacts;
        n.held = held(f);
        n.chosen = chosen;
        n.remaining = legal & ~(1u << chosen);
        n.independent = f.independent;
        m_path.push_back(std::move(n));
        m_mask = chosen;
        m_cursor = m_path.size();
    }
    ++m_decisions;
    m_next = f.outer + m_step;
    return m_mask;
}
std::optional<Candidate> NativeSearch::backtrack(Run const &run) {
    ++m_deaths;
    if (m_fork) {
        // A reset/replay died before reaching a previously observed decision.
        // No branch conclusion is justified for a different native state.
        throw std::runtime_error("Повтор префикса разошёлся: смерть до точки ветвления");
    }
    std::optional<std::size_t> selected;
    // Widen the repair window periodically. A locally surviving late input
    // must not trap the search forever in a high-percent dead end. This is
    // bounded backtracking, not a claim of exhaustive search of all inputs.
    Tick window = 0;
    if (m_deaths % 8 == 0) {
        unsigned scale = std::min<unsigned>(std::countr_zero(m_deaths / 8), 10);
        window = Tick(32) << scale;
    }
    Tick cutoff = run.ticks > window ? run.ticks - window : 0;
    for (std::size_t i = m_path.size(); i-- > 0;)
        if (m_path[i].remaining && (!window || m_path[i].sample.tick <= cutoff)) {
            selected = i;
            break;
        }
    if (!selected)
        for (std::size_t i = m_path.size(); i-- > 0;)
            if (m_path[i].remaining) {
                selected = i;
                break;
            }
    if (!selected) {
        m_exhausted = true;
        return std::nullopt;
    }
    auto &n = m_path[*selected];
    need(n.actions <= run.actions.size());
    // Try maintaining the previous hold before a different combination; in
    // two-player mode remaining includes all four joint button states.
    unsigned pick = (n.remaining & (1u << n.held)) ? n.held : std::countr_zero(n.remaining);
    n.remaining &= ~(1u << pick);
    n.chosen = pick;
    Candidate result;
    result.actions.assign(run.actions.begin(), run.actions.begin() + n.actions);
    result.guideFrom = n.sample.tick;
    result.checkpoint = n.sample;
    m_path.resize(*selected + 1);
    m_fork = true;
    return result;
}
Candidate NativeSearch::resume(Run const &run) {
    Candidate c;
    if (m_path.empty())
        return c;
    auto const &n = m_path.back();
    need(n.actions <= run.actions.size());
    c.actions.assign(run.actions.begin(), run.actions.begin() + n.actions);
    c.guideFrom = n.sample.tick;
    c.checkpoint = n.sample;
    m_fork = true;
    return c;
}
Json NativeSearch::json() const {
    Json nodes = Json::array();
    for (auto const &n : m_path)
        nodes.push_back({encodeSample(n.sample),
                         {n.timing.dt, n.timing.half, n.timing.last},
                         n.outer,
                         n.actions,
                         n.contacts,
                         n.held,
                         n.chosen,
                         n.remaining,
                         n.independent});
    return {{"schema", 1},      {"step", m_step},           {"cap", m_cap},
            {"limit", m_limit}, {"decisions", m_decisions}, {"deaths", m_deaths},
            {"fork", m_fork},   {"exhausted", m_exhausted}, {"path", std::move(nodes)}};
}
NativeSearch NativeSearch::restore(Json const &j) {
    need(j.at("schema") == 1);
    NativeSearch s;
    s.configure(integer(j.at("step"), 8), integer(j.at("cap"), 65536), integer(j.at("limit"), 50000000));
    s.m_decisions = integer(j.at("decisions"), 50000001);
    s.m_deaths = integer(j.at("deaths"), 100000);
    s.m_fork = j.at("fork").get<bool>();
    s.m_exhausted = j.at("exhausted").get<bool>();
    auto const &path = j.at("path");
    need(path.is_array() && path.size() <= s.m_cap);
    for (auto const &x : path) {
        need(x.is_array() && x.size() == 9 && x[1].is_array() && x[1].size() == 3);
        Node n;
        n.sample = decodeSample(x[0]);
        n.timing = {x[1][0].get<double>(), x[1][1].get<bool>(), x[1][2].get<bool>()};
        n.outer = integer(x[2], MaxTicks);
        n.actions = integer(x[3], 2 * MaxTicks);
        n.contacts = x[4].get<std::array<std::uint64_t, 2>>();
        n.held = integer(x[5], 3);
        n.chosen = integer(x[6], 3);
        n.remaining = integer(x[7], 15);
        n.independent = x[8].get<bool>();
        need(n.sample.tick < MaxTicks && n.outer > 0 && std::isfinite(n.timing.dt) && n.timing.dt > 0 &&
             n.timing.dt <= 1 && !(n.remaining & (1u << n.chosen)));
        need(n.independent || (n.held <= 1 && n.chosen <= 1 && n.remaining <= 3));
        if (!s.m_path.empty()) {
            auto const &p = s.m_path.back();
            need(n.sample.tick > p.sample.tick && n.outer >= p.outer && n.actions >= p.actions);
        }
        s.m_path.push_back(std::move(n));
    }
    need(!s.m_fork || !s.m_path.empty());
    s.beginAttempt();
    return s;
}
} // namespace v2
