#include <bot/Learner.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cubebot {
namespace {
bool finite(double value) { return std::isfinite(value); }
double clamp(double value, double lo, double hi) { return std::max(lo, std::min(value, hi)); }
double direction(PlayerSnapshot const& p) { return p.upsideDown ? -1.0 : 1.0; }
Rect normalized(Rect const& r, double sign) {
    return sign > 0.0 ? r : Rect{r.minX, -r.maxY, r.maxX, -r.minY};
}
bool validPlayer(PlayerSnapshot const& p) {
    return finite(p.position.x) && finite(p.position.y) &&
           finite(p.velocity.x) && finite(p.velocity.y) &&
           finite(p.width) && finite(p.height) && p.width > 0.0 &&
           p.height > 0.0 && p.width < 500.0 && p.height < 500.0;
}
// The fixed order covers earlier and later launches without random inputs.
double trialBias(std::uint64_t failures) {
    if (failures == 0) return 0.0;
    auto const index = (failures - 1) % 24;
    double const magnitude = static_cast<double>(index / 2 + 1) / 120.0;
    return index % 2 == 0 ? magnitude : -magnitude;
}
}

bool Rect::valid() const {
    return finite(minX) && finite(minY) && finite(maxX) && finite(maxY) &&
           maxX > minX && maxY > minY;
}
bool Rect::overlaps(Rect const& other, double inset) const {
    return maxX - inset > other.minX + inset && minX + inset < other.maxX - inset &&
           maxY - inset > other.minY + inset && minY + inset < other.maxY - inset;
}

CubeStrategy::CubeStrategy(PlannerConfig config)
  : m_config(config), m_gravity(config.defaultGravity),
    m_jumpVelocity(config.defaultJumpVelocity) {}

void CubeStrategy::beginAttempt() {
    m_havePrevious = false;
    m_pulseUntil = -1.0;
}

void CubeStrategy::observe(PlayerSnapshot const& p, double dt) {
    if (m_havePrevious && (p.upsideDown != m_previous.upsideDown || p.mini != m_previous.mini)) {
        m_jumpVelocity = m_config.defaultJumpVelocity * (p.mini ? 0.82 : 1.0);
        m_gravity = m_config.defaultGravity;
        m_havePrevious = false;
    }
    if (finite(p.gravityAcceleration) && p.gravityAcceleration >= 100.0 &&
        p.gravityAcceleration <= 30000.0) {
        m_gravity = p.gravityAcceleration;
    } else if (m_havePrevious && !p.grounded && !m_previous.grounded && dt > 0.0) {
        double const measured = -direction(p) * (p.velocity.y - m_previous.velocity.y) / dt;
        if (measured >= 300.0 && measured <= 20000.0)
            m_gravity = m_gravity * 0.8 + measured * 0.2;
    }
    if (m_havePrevious && m_previous.grounded && !p.grounded) {
        double const measured = direction(p) * p.velocity.y + m_gravity * dt;
        if (measured >= 100.0 && measured <= 3000.0)
            m_jumpVelocity = m_jumpVelocity * 0.4 + measured * 0.6;
    }
    m_previous = p;
    m_havePrevious = true;
}

CubeStrategy::Prediction CubeStrategy::predict(Snapshot const& s,
        PlayerSnapshot const& p, double delay, double horizon) const {
    double const sign = direction(p);
    double const base = sign * p.position.y - p.height * 0.5;
    double foot = base;
    double verticalVelocity = 0.0;
    bool launched = false;
    bool grounded = true;
    double const step = 1.0 / 180.0;
    for (double t = 0.0; t < horizon;) {
        double const next = std::min(t + step, horizon);
        double integration = next - t;
        if (!launched && next >= delay) {
            integration = next - std::max(t, delay);
            verticalVelocity = m_jumpVelocity;
            launched = true;
            grounded = false;
        }
        double const previousFoot = foot;
        if (!grounded) {
            foot += verticalVelocity * integration - 0.5 * m_gravity * integration * integration;
            verticalVelocity -= m_gravity * integration;
            if (foot < base) { foot = base; verticalVelocity = 0.0; grounded = true; }
        }
        double const x = p.position.x + p.velocity.x * next;
        Rect body{x - p.width * 0.5, foot, x + p.width * 0.5, foot + p.height};
        double landing = -std::numeric_limits<double>::infinity();
        for (auto const& object : s.objects) {
            if (!object.bounds.valid()) continue;
            Rect const obstacle = normalized(object.bounds, sign);
            if (object.kind == ObjectKind::Hazard && body.overlaps(obstacle, m_config.collisionInset))
                return {false, next};
            if (object.kind != ObjectKind::Solid) continue;
            bool const xOverlap = body.maxX > obstacle.minX + m_config.collisionInset &&
                                  body.minX < obstacle.maxX - m_config.collisionInset;
            // Objects at/below the current support plane are ordinary floor.
            if (obstacle.maxY <= base + 2.0 || !xOverlap) continue;
            if (verticalVelocity <= 0.0 && previousFoot >= obstacle.maxY - 2.0 &&
                foot <= obstacle.maxY) {
                landing = std::max(landing, obstacle.maxY);
                continue;
            }
            if (body.overlaps(obstacle, m_config.collisionInset)) return {false, next};
        }
        if (finite(landing)) {
            foot = landing;
            verticalVelocity = 0.0;
            // Continue falling in subsequent samples; another crossing resolves
            // the same support. This also lets the forecast leave a platform.
            grounded = false;
        }
        t = next;
    }
    return {true, horizon};
}

StrategyAction CubeStrategy::decide(Snapshot const& s, std::size_t channel, double bias) {
    auto const& p = s.players.at(channel);
    observe(p, s.dt);
    if (s.elapsed < m_pulseUntil) return {true, 0.0};
    if (!p.grounded || std::abs(p.velocity.x) < 5.0) return {};
    double const sign = direction(p);
    double const baseline = sign * p.position.y - p.height * 0.5;
    double const horizontalSign = p.velocity.x >= 0.0 ? 1.0 : -1.0;
    double const front = horizontalSign * p.position.x + p.width * 0.5;
    double nearest = std::numeric_limits<double>::infinity();
    double threatHeight = 0.0;
    double farthest = 0.0;
    double const speed = std::abs(p.velocity.x);
    for (auto const& object : s.objects) {
        if ((object.kind != ObjectKind::Hazard && object.kind != ObjectKind::Solid) ||
            !object.bounds.valid()) continue;
        Rect const bounds = normalized(object.bounds, sign);
        if (bounds.maxY <= baseline + 2.0 || bounds.minY >= baseline + p.height) continue;
        double const nearX = horizontalSign > 0.0 ? bounds.minX : -bounds.maxX;
        double const farX = horizontalSign > 0.0 ? bounds.maxX : -bounds.minX;
        if (farX < front - p.width) continue;
        double const distance = std::max(0.0, nearX - front);
        if (distance < nearest) {
            nearest = distance;
            threatHeight = std::max(1.0, bounds.maxY - baseline);
        }
        if (distance / speed <= m_config.lookAheadSeconds)
            farthest = std::max(farthest, (farX - front + p.width) / speed);
    }
    if (!finite(nearest) || nearest / speed > m_config.lookAheadSeconds) return {};
    double const threatTime = nearest / speed;
    double const requiredRise = std::min(threatHeight + 3.0,
        m_jumpVelocity * m_jumpVelocity / (2.0 * m_gravity) * 0.98);
    double const discriminant = std::max(0.0,
        m_jumpVelocity * m_jumpVelocity - 2.0 * m_gravity * requiredRise);
    double const riseTime = (m_jumpVelocity - std::sqrt(discriminant)) / m_gravity;
    double const desired = std::max(0.0, threatTime - riseTime - 0.025 - bias);
    double const horizon = std::max(0.5, std::min(m_config.lookAheadSeconds + 0.35, farthest + 0.12));
    double chosen = desired;
    double bestDistance = std::numeric_limits<double>::infinity();
    bool foundSafe = predict(s, p, desired, horizon).safe;
    double const scanStep = 1.0 / 120.0;
    double const maxDelay = std::max(0.0, std::min(threatTime, m_config.lookAheadSeconds));
    if (!foundSafe) {
        for (double delay = 0.0; delay <= maxDelay + 1e-9; delay += scanStep) {
            auto const prediction = predict(s, p, delay, horizon);
            double const distance = std::abs(delay - desired);
            if (prediction.safe && distance < bestDistance) {
                foundSafe = true;
                bestDistance = distance;
                chosen = delay;
            }
        }
    }
    // If the approximate model finds no safe arc, keep the deterministic local
    // timing search active. The game's collisions remain authoritative.
    if (!foundSafe) chosen = desired;
    if (chosen <= std::max(s.dt * 0.75, scanStep * 0.55)) {
        m_pulseUntil = s.elapsed + m_config.pulseSeconds;
        return {true, 0.0};
    }
    return {false, chosen};
}

CubeLearner::CubeLearner(PlannerConfig config) : m_config(config), m_cube(config) {}

bool CubeLearner::load(TrainingData const& data, std::string const& fingerprint) {
    if (data.schema != TrainingData::Schema || data.levelFingerprint != fingerprint ||
        !finite(data.bestProgress) || data.bestProgress < 0.0 || data.bestProgress > 100.0 ||
        !finite(data.frontierTime) || data.frontierTime < 0.0 || data.frontierTime > 86400.0 ||
        (data.completed && data.bestProgress != 100.0) ||
        data.bestActions.size() > m_config.maxStoredEvents) return false;
    double previous = -1.0;
    for (auto const& event : data.bestActions) {
        if (!finite(event.time) || event.time < 0.0 || event.time < previous ||
            event.time > data.frontierTime + 0.05 || event.channel > 1) return false;
        previous = event.time;
    }
    stop();
    m_data = data;
    m_status = data.completed ? Status::Completed : Status::Ready;
    return true;
}

void CubeLearner::reset(std::string fingerprint) {
    stop();
    m_data = TrainingData{};
    m_data.levelFingerprint = std::move(fingerprint);
    m_status = Status::Ready;
}

void CubeLearner::prepareTrial() {
    m_replay.clear();
    m_replayCursor = 0;
    m_replayUntil = -1.0;
    m_timingBias = trialBias(m_data.failedTrials);
    if (m_data.bestActions.empty()) return;
    double cut = std::max(0.0, m_data.frontierTime - m_config.mutationWindowSeconds);
    // Mutate the final jump in the frontier window, preserving everything that
    // already worked before it. A down/up pair moves together.
    for (auto it = m_data.bestActions.rbegin(); it != m_data.bestActions.rend(); ++it) {
        if (it->channel == 0 && it->down && it->time >= cut) {
            cut = std::max(0.0, it->time - 0.12);
            break;
        }
    }
    for (auto event : m_data.bestActions) {
        // Completed traces are replayed exactly; failed traces get a local shift.
        if (!m_data.completed && event.time >= cut)
            event.time = std::max(cut, event.time - m_timingBias);
        m_replay.push_back(event);
    }
    std::stable_sort(m_replay.begin(), m_replay.end(), [](auto const& a, auto const& b) {
        return a.time < b.time;
    });
    m_replayUntil = m_replay.back().time;
    // If no action existed near the frontier, stop replaying before it so that
    // geometry analysis can propose the missing jump.
    if (m_replayUntil < cut) m_replayUntil = cut;
}

void CubeLearner::beginAttempt() {
    stop();
    ++m_data.attempts;
    m_attemptRunning = true;
    m_cube.beginAttempt();
    prepareTrial();
    m_status = m_replay.empty() ? Status::Planning : Status::Replaying;
}

Decision CubeLearner::emit(std::array<bool, 2> held, Status status, double elapsed,
                          double plannedJumpIn) {
    for (std::size_t i = 0; i != held.size(); ++i) {
        if (held[i] != m_held[i] && m_trace.size() < m_config.maxStoredEvents)
            m_trace.push_back({std::max(0.0, elapsed), static_cast<std::uint8_t>(i), held[i]});
    }
    m_held = held;
    m_status = status;
    return {held, status, plannedJumpIn};
}

Decision CubeLearner::step(Snapshot const& s) {
    if (!m_attemptRunning) return {{false, false}, m_status, -1.0};
    if (!finite(s.elapsed) || !finite(s.dt) || s.elapsed < 0.0 || s.dt <= 0.0 ||
        s.dt > 0.25 || s.elapsed + 1e-6 < m_lastElapsed ||
        !finite(s.progress) || !validPlayer(s.players[0])) {
        return emit({{false, false}}, Status::InvalidState, std::max(0.0, m_lastElapsed));
    }
    m_lastElapsed = s.elapsed;
    if (s.dual) return emit({{false, false}}, Status::UnsupportedDual, s.elapsed);
    if (s.players[0].mode != Mode::Cube)
        return emit({{false, false}}, Status::UnsupportedMode, s.elapsed);
    if (!s.players[0].alive)
        return emit({{false, false}}, Status::WaitingForAttempt, s.elapsed);
    if (s.elapsed <= m_replayUntil + 1e-7) {
        auto held = m_held;
        while (m_replayCursor < m_replay.size() &&
               m_replay[m_replayCursor].time <= s.elapsed + s.dt * 0.5) {
            auto const& event = m_replay[m_replayCursor++];
            held[event.channel] = event.down;
        }
        held[1] = false;
        return emit(held, Status::Replaying, s.elapsed);
    }
    // Consume the final event even if a variable dt skipped over the replay end.
    // The planner immediately takes ownership and returns a fresh held state.
    m_replayCursor = m_replay.size();
    auto const action = m_cube.decide(s, 0, m_timingBias);
    return emit({{action.held, false}}, Status::Planning, s.elapsed, action.plannedJumpIn);
}

void CubeLearner::finishAttempt(Snapshot const& s, bool completed, bool countFailure) {
    if (!m_attemptRunning) return;
    double const progress = completed ? 100.0 : clamp(finite(s.progress) ? s.progress : 0.0, 0.0, 100.0);
    double const elapsed = finite(s.elapsed) ? std::max(0.0, s.elapsed) : std::max(0.0, m_lastElapsed);
    bool const improved = completed || progress > m_data.bestProgress + 0.001;
    if (improved) {
        m_data.bestProgress = progress;
        m_data.frontierTime = elapsed;
        m_data.bestActions = m_trace;
        m_data.completed = completed;
        m_data.failedTrials = 0;
    } else if (countFailure) {
        ++m_data.failedTrials;
    }
    m_attemptRunning = false;
    m_held = {{false, false}};
    m_status = completed ? Status::Completed : Status::WaitingForAttempt;
}
void CubeLearner::recordDeath(Snapshot const& snapshot) { finishAttempt(snapshot, false, true); }
void CubeLearner::recordCompletion(Snapshot const& snapshot) { finishAttempt(snapshot, true, false); }
void CubeLearner::recordInterrupted(Snapshot const& snapshot) { finishAttempt(snapshot, false, false); }
void CubeLearner::stop() {
    m_attemptRunning = false;
    m_held = {{false, false}};
    m_trace.clear();
    m_replay.clear();
    m_replayCursor = 0;
    m_replayUntil = -1.0;
    m_lastElapsed = -1.0;
    m_status = m_data.completed ? Status::Completed : Status::Ready;
}
Metrics CubeLearner::metrics() const {
    return {m_data.attempts, m_data.failedTrials, m_data.bestProgress, m_status, m_data.completed};
}
char const* statusName(Status status) {
    switch (status) {
        case Status::Ready: return "Ready";
        case Status::Planning: return "Planning";
        case Status::Replaying: return "Replaying learned inputs";
        case Status::WaitingForAttempt: return "Waiting for restart";
        case Status::UnsupportedMode: return "Unsupported player mode";
        case Status::UnsupportedDual: return "Dual mode is unsupported";
        case Status::InvalidState: return "Invalid simulation snapshot";
        case Status::Completed: return "Training run completed";
    }
    return "Unknown";
}
} // namespace cubebot
