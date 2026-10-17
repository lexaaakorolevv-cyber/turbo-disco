#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace v2 {
namespace {
void need(bool ok, char const *why) {
    if (!ok)
        throw std::runtime_error(why);
}
Tick integer(Json const &v, Tick max) {
    need(v.is_number_integer() && v.get<double>() >= 0 && v.get<double>() <= static_cast<double>(max),
         "Invalid integer");
    return v.get<Tick>();
}
double number(Json const &v) {
    need(v.is_number(), "Expected number");
    double x = v.get<double>();
    need(std::isfinite(x) && std::abs(x) < 1e9, "Invalid number");
    return x;
}
Json player(rf::FormState const &s) {
    return {s.x,        s.y,        s.vy,  s.vx, s.scale, s.gravityMod, static_cast<int>(s.form),
            s.grounded, s.inverted, s.held};
}
rf::FormState player(Json const &j) {
    need(j.is_array() && j.size() == 10, "Invalid player sample");
    rf::FormState s;
    s.x = number(j[0]);
    s.y = number(j[1]);
    s.vy = number(j[2]);
    s.vx = number(j[3]);
    s.scale = number(j[4]);
    s.gravityMod = number(j[5]);
    s.form = static_cast<rf::Form>(integer(j[6], 7));
    need(s.scale > 0 && s.gravityMod > 0, "Invalid player physics");
    for (int i = 7; i < 10; i++)
        need(j[i].is_boolean(), "Invalid player flags");
    s.grounded = j[7];
    s.inverted = j[8];
    s.held = j[9];
    return s;
}
} // namespace
bool sameSample(Sample const &a, Sample const &b) {
    if (a.tick != b.tick || a.dual != b.dual)
        return false;
    for (int i = 0; i < (a.dual ? 2 : 1); i++) {
        auto const &x = a.players[i];
        auto const &y = b.players[i];
        if (x.form != y.form || x.inverted != y.inverted || x.grounded != y.grounded || x.held != y.held ||
            std::abs(x.x - y.x) > .025 || std::abs(x.y - y.y) > .025 || std::abs(x.vy - y.vy) > .005 ||
            std::abs(x.vx - y.vx) > 1e-5 || std::abs(x.scale - y.scale) > 1e-6 ||
            std::abs(x.gravityMod - y.gravityMod) > 1e-6)
            return false;
    }
    return true;
}
std::vector<Action> prefix(std::vector<Action> const &a, Tick t) {
    return {a.begin(),
            std::lower_bound(a.begin(), a.end(), t, [](auto const &x, Tick tick) { return x.tick < tick; })};
}
bool heldBefore(std::vector<Action> const &a, Tick t, int p) {
    for (auto i = a.rbegin(); i != a.rend(); ++i)
        if (i->tick < t && i->player == p)
            return i->down;
    return false;
}
bool validActions(std::vector<Action> const &a, Tick end) {
    bool held[2]{};
    Tick prev = 0;
    int side = 0;
    bool first = true;
    for (auto const &x : a) {
        if (x.tick >= end || x.button != 1 || x.player < 1 || x.player > 2 ||
            (!first && (x.tick < prev || (x.tick == prev && x.player <= side))) ||
            held[x.player - 1] == x.down)
            return false;
        held[x.player - 1] = x.down;
        prev = x.tick;
        side = x.player;
        first = false;
    }
    return true;
}
void validate(Run const &r) {
    need(r.environment.is_object() && r.ticks > 0 && r.ticks <= MaxTicks && r.timings.size() == r.ticks &&
             r.outer > 0 && r.outer <= MaxTicks,
         "Invalid run length/environment");
    need(std::isfinite(r.percent) && r.percent >= 0 && r.percent <= 100 &&
             (!r.complete || r.percent == 100) && (!r.verified || r.complete),
         "Invalid run outcome");
    need(validActions(r.actions, r.ticks) && r.actions.size() <= 2 * MaxTicks, "Invalid input sequence");
    for (auto const &t : r.timings)
        need(std::isfinite(t.dt) && t.dt > 0 && t.dt <= 1, "Invalid physics timing");
    need(!r.trace.empty() && r.trace.front().tick == 0, "Missing initial trace");
    Tick last = 0;
    for (std::size_t i = 0; i < r.trace.size(); i++) {
        auto const &s = r.trace[i];
        need(s.tick < r.ticks && (!i || s.tick > last), "Invalid trace order");
        for (int p = 0; p < (s.dual ? 2 : 1); p++) {
            auto const &x = s.players[p];
            need(std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.vy) && std::isfinite(x.vx) &&
                     x.scale > 0 && std::isfinite(x.scale) && x.gravityMod > 0 && std::isfinite(x.gravityMod),
                 "Invalid trace state");
        }
        last = s.tick;
    }
    need(r.trace.size() <= r.ticks && r.cps >= 0 && r.cps <= 16, "Invalid trace/CPS");
    // Every mandatory stride must be present, not merely a few favourable samples.
    std::size_t index = 0;
    for (Tick t = 0; t < r.ticks; t += 16) {
        while (index < r.trace.size() && r.trace[index].tick < t)
            ++index;
        need(index < r.trace.size() && r.trace[index].tick == t, "Incomplete trace");
    }
}
Json encodeActions(std::vector<Action> const &a) {
    Json j = Json::array();
    for (auto const &x : a)
        j.push_back({x.tick, x.down, x.button, x.player});
    return j;
}
std::vector<Action> decodeActions(Json const &j, Tick end) {
    need(j.is_array() && j.size() <= 2 * MaxTicks, "Invalid action count");
    std::vector<Action> a;
    for (auto const &x : j) {
        need(x.is_array() && x.size() == 4 && x[1].is_boolean(), "Invalid action");
        a.push_back({integer(x[0], MaxTicks), x[1].get<bool>(), static_cast<int>(integer(x[2], 1)),
                     static_cast<int>(integer(x[3], 2))});
    }
    need(validActions(a, end), "Invalid action order");
    return a;
}
Json encodeSample(Sample const &s) {
    return {s.tick, s.dual, player(s.players[0]), s.dual ? player(s.players[1]) : Json(nullptr)};
}
Sample decodeSample(Json const &j) {
    need(j.is_array() && j.size() == 4 && j[1].is_boolean(), "Invalid trace row");
    Sample s;
    s.tick = integer(j[0], MaxTicks);
    s.dual = j[1];
    s.players[0] = player(j[2]);
    if (s.dual)
        s.players[1] = player(j[3]);
    else
        need(j[3].is_null(), "Unexpected second player");
    return s;
}
Json encode(Run const &r) {
    validate(r);
    Json j{{"format", "routefinder-v2"},
           {"schema", 2},
           {"environment", r.environment},
           {"ticks", r.ticks},
           {"outer", r.outer},
           {"percent", r.percent},
           {"complete", r.complete},
           {"verified", r.verified},
           {"attempts", r.attempts},
           {"cps", r.cps},
           {"reason", r.reason},
           {"actions", encodeActions(r.actions)},
           {"timings", Json::array()},
           {"trace", Json::array()}};
    for (auto const &t : r.timings)
        j["timings"].push_back({t.dt, t.half, t.last});
    for (auto const &s : r.trace)
        j["trace"].push_back(encodeSample(s));
    return j;
}
Run decode(Json const &j) {
    need(j.at("format") == "routefinder-v2" && j.at("schema") == 2, "Incompatible macro format");
    Run r;
    r.environment = j.at("environment");
    r.ticks = integer(j.at("ticks"), MaxTicks);
    r.outer = integer(j.at("outer"), MaxTicks);
    r.percent = number(j.at("percent"));
    need(j.at("complete").is_boolean(), "Invalid completion flag");
    r.complete = j.at("complete");
    r.verified = false;
    r.attempts = integer(j.at("attempts"), 1000000000000ULL);
    r.cps = static_cast<int>(integer(j.at("cps"), 16));
    r.reason = j.value("reason", std::string{});
    need(r.reason.size() < 8192, "Invalid reason length");
    r.actions = decodeActions(j.at("actions"), r.ticks);
    auto const &t = j.at("timings");
    need(t.is_array() && t.size() == r.ticks, "Invalid timings");
    for (auto const &x : t) {
        need(x.is_array() && x.size() == 3 && x[1].is_boolean() && x[2].is_boolean(), "Invalid timing row");
        r.timings.push_back({number(x[0]), x[1], x[2]});
    }
    auto const &trace = j.at("trace");
    need(trace.is_array() && trace.size() <= r.ticks, "Invalid trace length");
    for (auto const &s : trace)
        r.trace.push_back(decodeSample(s));
    validate(r);
    return r;
}
} // namespace v2
