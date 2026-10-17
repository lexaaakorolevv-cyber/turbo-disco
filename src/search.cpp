#include "search.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace v2 {
namespace {
void need(bool ok) {
    if (!ok)
        throw std::runtime_error("Invalid search archive");
}
std::uint64_t count(Json const &j) {
    need(j.is_number_integer() && j.get<double>() >= 0 && j.get<double>() <= 1e12);
    return j.get<std::uint64_t>();
}
std::uint64_t family(std::vector<Action> const &a, Tick death) {
    std::uint64_t h = 1469598103934665603ULL;
    auto cutoff = death > 240 ? death - 120 : death;
    for (auto const &x : a) {
        if (x.tick >= cutoff)
            break;
        h = (h ^ (x.tick / 24)) * 1099511628211ULL;
        h = (h ^ (x.player * 2 + x.down)) * 1099511628211ULL;
    }
    return h;
}
} // namespace
void Search::observe(Run const &r, Candidate const &source) {
    if (!r.ticks)
        return;
    auto found = std::find_if(m_branches.begin(), m_branches.end(),
                              [&](auto const &b) { return b.id == source.parent; });
    auto key = family(r.actions, r.ticks);
    if (found != m_branches.end()) {
        if (r.percent > found->anchor + .5) {
            found->stalls = 0;
            found->anchor = r.percent;
        } else
            ++found->stalls;
        if (family(found->actions, found->death) == key) {
            if (r.percent > found->percent || (r.percent == found->percent && r.ticks > found->death)) {
                found->actions = r.actions;
                found->percent = r.percent;
                found->death = r.ticks;
                found->policy = source.policy;
                found->cursor = 0;
            }
            return;
        }
    }
    for (auto &b : m_branches)
        if (family(b.actions, b.death) == key) {
            if (r.percent > b.percent) {
                b.actions = r.actions;
                b.percent = r.percent;
                b.death = r.ticks;
                b.policy = source.policy;
                b.cursor = 0;
            }
            return;
        }
    Branch b;
    b.id = m_id++;
    b.policy = source.policy;
    b.actions = r.actions;
    b.percent = b.anchor = r.percent;
    b.death = r.ticks;
    m_branches.push_back(std::move(b));
    if (m_branches.size() > 12) {
        auto worst = std::max_element(m_branches.begin(), m_branches.end(), [](auto const &a, auto const &b) {
            return std::tuple{a.stalls, a.uses, -a.percent} < std::tuple{b.stalls, b.uses, -b.percent};
        });
        m_branches.erase(worst);
    }
}
Candidate Search::next(Tick limit) {
    Candidate result;
    if (!limit || m_branches.empty())
        return result;
    auto active =
        std::find_if(m_branches.begin(), m_branches.end(), [&](auto const &b) { return b.id == m_active; });
    if (active == m_branches.end() || active->stalls >= 6 || m_trials % 4 == 0) {
        active = std::min_element(m_branches.begin(), m_branches.end(), [](auto const &a, auto const &b) {
            return std::tuple{a.stalls / 4, a.uses, a.death, -a.percent} <
                   std::tuple{b.stalls / 4, b.uses, b.death, -b.percent};
        });
    }
    auto &b = *active;
    m_active = b.id;
    ++b.uses;
    ++m_trials;
    result.parent = b.id;
    result.policy = b.policy;
    // A point is an input prefix checked twice by native runs, never world state.
    if (b.stalls >= 8 && m_trials % 4 == 0) {
        std::vector<Point const *> points;
        for (auto const &p : m_points)
            if (p.confirmations == 2 && p.sample.tick < b.death && p.sample.tick < limit)
                points.push_back(&p);
        std::sort(points.begin(), points.end(),
                  [](auto a, auto c) { return a->sample.tick > c->sample.tick; });
        if (!points.empty()) {
            auto p = points[std::min<std::size_t>((b.stalls - 8) / 8, points.size() - 1)];
            result.actions = p->actions;
            result.guideFrom = p->sample.tick;
            result.checkpoint = p->sample;
            result.policy = (b.policy + m_trials * 17) % 99;
            return result;
        }
    }
    // Timing candidates share a branch and a stable prefix. Repair only one
    // player's hold; the other channel is immutable. Bound all signed shifts.
    std::vector<std::pair<std::size_t, std::size_t>> holds;
    for (std::size_t i = b.actions.size(); i-- > 0 && holds.size() < 3;) {
        if (!b.actions[i].down || b.death - b.actions[i].tick > 480)
            continue;
        auto end = i;
        for (auto j = i + 1; j < b.actions.size(); j++)
            if (b.actions[j].player == b.actions[i].player) {
                end = j;
                break;
            }
        holds.push_back({i, end});
    }
    if (!holds.empty() && b.cursor < 192 && m_trials % 4 != 0) {
        static constexpr int shifts[]{-1, 1, -2, 2, -4, 4, -8, 8, -16, 16, -32, 32};
        for (unsigned retry = 0; retry < 192 && b.cursor < 192; retry++) {
            auto edit = b.cursor++;
            auto [first, last] = holds[(edit / 48) % holds.size()];
            int shift = shifts[(edit / 4) % 12], length = (edit % 4 == 0   ? 0
                                                           : edit % 4 == 1 ? -1
                                                           : edit % 4 == 2 ? 1
                                                                           : -2);
            result.actions = b.actions;
            auto channel = result.actions[first].player;
            if (first == last) {
                Tick back = static_cast<Tick>(std::abs(shift));
                if (b.death <= back || b.death - back <= result.actions[first].tick)
                    continue;
                result.actions.push_back({b.death - back, false, 1, channel});
            } else {
                auto press = static_cast<std::int64_t>(result.actions[first].tick) + shift;
                auto release = static_cast<std::int64_t>(result.actions[last].tick) + shift + length;
                if (press < 0 || release <= press || release >= static_cast<std::int64_t>(limit))
                    continue;
                result.actions[first].tick = press;
                result.actions[last].tick = release;
            }
            std::sort(result.actions.begin(), result.actions.end(), [](auto const &a, auto const &c) {
                return std::pair{a.tick, a.player} < std::pair{c.tick, c.player};
            });
            if (!validActions(result.actions, limit))
                continue;
            result.guideFrom = std::min(limit, std::max(b.death + 1, result.actions.back().tick + 1));
            return result;
        }
    }
    Tick back = std::min<Tick>(32ULL << (std::min<std::uint64_t>(b.stalls / 6, 6)), 2400);
    Tick cut = b.death > back ? b.death - back : 0;
    for (auto i = b.actions.rbegin(); i != b.actions.rend(); ++i)
        if (i->down && b.death - i->tick <= back) {
            cut = std::min(cut, i->tick > 8 ? i->tick - 8 : 0);
            break;
        }
    result.actions = prefix(b.actions, std::min(cut, limit));
    result.guideFrom = std::min(cut, limit);
    result.policy = (b.policy + m_trials * 17) % 99;
    return result;
}
void Search::checkpoint(int percent, Sample const &s, std::vector<Action> const &a, std::uint64_t attempt) {
    if (percent < 5 || percent > 90 || percent % 5 || !s.tick)
        return;
    auto actions = prefix(a, s.tick);
    for (auto &p : m_points)
        if (p.percent == percent) {
            if (p.attempt == attempt)
                return;
            if (p.actions == actions && sameSample(p.sample, s)) {
                p.confirmations = 2;
                p.attempt = attempt;
            } else if (p.confirmations < 2)
                p = {percent, s, std::move(actions), 1, attempt};
            return;
        }
    if (m_points.size() < 18)
        m_points.push_back({percent, s, std::move(actions), 1, attempt});
}
void Search::invalidate(Tick tick) {
    std::erase_if(m_points, [&](auto const &p) { return p.sample.tick == tick; });
}
Json encodeCandidate(Candidate const &c) {
    return {{"actions", encodeActions(c.actions)},
            {"guide", c.guideFrom},
            {"policy", c.policy},
            {"parent", c.parent},
            {"checkpoint", c.checkpoint ? encodeSample(*c.checkpoint) : Json(nullptr)}};
}
Candidate decodeCandidate(Json const &j) {
    Candidate c;
    c.actions = decodeActions(j.at("actions"));
    c.guideFrom = count(j.at("guide"));
    c.policy = count(j.at("policy"));
    c.parent = count(j.at("parent"));
    need(c.guideFrom <= MaxTicks && c.policy < 99 &&
         (c.actions.empty() || c.actions.back().tick < c.guideFrom));
    if (!j.at("checkpoint").is_null()) {
        c.checkpoint = decodeSample(j.at("checkpoint"));
        need(c.checkpoint->tick == c.guideFrom);
    }
    return c;
}
Json Search::json() const {
    Json j{{"id", m_id},
           {"trials", m_trials},
           {"active", m_active},
           {"branches", Json::array()},
           {"points", Json::array()}};
    for (auto const &b : m_branches)
        j["branches"].push_back({b.id, b.policy, b.uses, b.stalls, b.cursor, b.percent, b.anchor, b.death,
                                 encodeActions(b.actions)});
    for (auto const &p : m_points)
        j["points"].push_back(
            {p.percent, encodeSample(p.sample), encodeActions(p.actions), p.confirmations, p.attempt});
    return j;
}
Search Search::restore(Json const &j) {
    Search s;
    s.m_id = count(j.at("id"));
    s.m_trials = count(j.at("trials"));
    s.m_active = count(j.at("active"));
    auto const &bs = j.at("branches");
    need(bs.is_array() && bs.size() <= 12);
    std::size_t actions = 0;
    for (auto const &x : bs) {
        need(x.is_array() && x.size() == 9);
        Branch b;
        b.id = count(x[0]);
        b.policy = count(x[1]);
        b.uses = count(x[2]);
        b.stalls = count(x[3]);
        b.cursor = count(x[4]);
        b.percent = x[5].get<double>();
        b.anchor = x[6].get<double>();
        b.death = count(x[7]);
        need(b.id > 0 && b.id < s.m_id && b.policy < 99 && b.cursor <= 192 && b.death > 0 &&
             b.death <= MaxTicks && std::isfinite(b.percent) && b.percent >= 0 && b.percent <= 100 &&
             std::isfinite(b.anchor) && b.anchor >= 0 && b.anchor <= 100);
        need(std::none_of(s.m_branches.begin(), s.m_branches.end(),
                          [&](auto const &a) { return a.id == b.id; }));
        b.actions = decodeActions(x[8], b.death);
        actions += b.actions.size();
        s.m_branches.push_back(std::move(b));
    }
    auto const &ps = j.at("points");
    need(ps.is_array() && ps.size() <= 18);
    for (auto const &x : ps) {
        need(x.is_array() && x.size() == 5);
        auto p = count(x[0]), confirm = count(x[3]), attempt = count(x[4]);
        need(p >= 5 && p <= 90 && p % 5 == 0 && confirm >= 1 && confirm <= 2 && attempt > 0);
        auto sample = decodeSample(x[1]);
        need(sample.tick > 0 && sample.tick < MaxTicks);
        auto a = decodeActions(x[2], sample.tick);
        actions += a.size();
        need(std::none_of(s.m_points.begin(), s.m_points.end(),
                          [&](auto const &point) { return point.percent == static_cast<int>(p); }));
        s.m_points.push_back(
            {static_cast<int>(p), sample, std::move(a), static_cast<unsigned>(confirm), attempt});
    }
    need(actions <= 4 * MaxTicks);
    return s;
}
} // namespace v2
