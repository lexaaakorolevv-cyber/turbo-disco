#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cubebot {

enum class Mode : std::uint8_t {
    Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing, Unknown
};
enum class ObjectKind : std::uint8_t { Hazard, Solid, Orb, Pad, Portal, Other };
enum class Status : std::uint8_t {
    Ready, Planning, Replaying, WaitingForAttempt, UnsupportedMode,
    UnsupportedDual, InvalidState, Completed
};

struct Vec2 { double x = 0.0; double y = 0.0; };
struct Rect {
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool valid() const;
    bool overlaps(Rect const& other, double inset = 0.0) const;
};
struct ObjectSnapshot {
    std::int64_t id = 0;
    ObjectKind kind = ObjectKind::Other;
    Rect bounds;
};
struct PlayerSnapshot {
    Vec2 position;
    // World units / second; the adapter should measure displacement / simulation dt.
    Vec2 velocity;
    double width = 30.0;
    double height = 30.0;
    // Positive magnitude in world units / second^2, or zero while unmeasured.
    double gravityAcceleration = 0.0;
    Mode mode = Mode::Unknown;
    bool grounded = false;
    bool upsideDown = false;
    bool mini = false;
    bool alive = true;
};
struct Snapshot {
    std::uint64_t tick = 0;
    // Time within this attempt, advanced only by the simulation update hook.
    double elapsed = 0.0;
    double dt = 1.0 / 240.0;
    double progress = 0.0; // Percentage in [0, 100].
    std::array<PlayerSnapshot, 2> players;
    bool dual = false;
    std::vector<ObjectSnapshot> objects;
};
struct InputEvent {
    double time = 0.0;
    std::uint8_t channel = 0;
    bool down = false;
};
struct TrainingData {
    static constexpr std::uint32_t Schema = 1;
    std::uint32_t schema = Schema;
    std::string levelFingerprint;
    std::uint64_t attempts = 0;
    std::uint64_t failedTrials = 0;
    double bestProgress = 0.0;
    double frontierTime = 0.0;
    bool completed = false;
    std::vector<InputEvent> bestActions;
};
struct Decision {
    std::array<bool, 2> held{{false, false}};
    Status status = Status::Ready;
    // Diagnostic only; negative means no planned jump.
    double plannedJumpIn = -1.0;
};
struct Metrics {
    std::uint64_t attempt = 0;
    std::uint64_t failedTrials = 0;
    double bestProgress = 0.0;
    Status status = Status::Ready;
    bool completed = false;
};
struct PlannerConfig {
    // Conservative first-attempt model. The strategy calibrates these using
    // observed jump motion; these values do not modify game physics.
    double defaultJumpVelocity = 640.0;
    double defaultGravity = 2800.0;
    double lookAheadSeconds = 0.85;
    double pulseSeconds = 0.035;
    double collisionInset = 1.0;
    double mutationWindowSeconds = 0.7;
    std::size_t maxStoredEvents = 100000;
};
struct StrategyAction {
    bool held = false;
    double plannedJumpIn = -1.0;
};

// Future modes can provide a strategy without changing input ownership, storage,
// replay, or lifecycle. The prototype intentionally registers only Cube.
class ModeStrategy {
public:
    virtual ~ModeStrategy() = default;
    virtual void beginAttempt() = 0;
    virtual StrategyAction decide(Snapshot const& snapshot, std::size_t channel,
                                  double timingBiasSeconds) = 0;
};

class CubeStrategy final : public ModeStrategy {
public:
    explicit CubeStrategy(PlannerConfig config = {});
    void beginAttempt() override;
    StrategyAction decide(Snapshot const&, std::size_t channel,
                          double timingBiasSeconds) override;
private:
    struct Prediction { bool safe = true; double collisionTime = 0.0; };
    PlannerConfig m_config;
    double m_gravity = 2800.0;
    double m_jumpVelocity = 640.0;
    double m_pulseUntil = -1.0;
    bool m_havePrevious = false;
    PlayerSnapshot m_previous;
    void observe(PlayerSnapshot const&, double dt);
    Prediction predict(Snapshot const&, PlayerSnapshot const&, double delay,
                       double horizon) const;
};

class CubeLearner {
public:
    explicit CubeLearner(PlannerConfig config = {});
    // Import rejects corrupt, foreign-level, or unsupported-schema data.
    bool load(TrainingData const& data, std::string const& fingerprint);
    void reset(std::string fingerprint);
    void beginAttempt();
    Decision step(Snapshot const& snapshot);
    // Call before the game's reset clears its last live snapshot.
    void recordDeath(Snapshot const& snapshot);
    void recordCompletion(Snapshot const& snapshot);
    // Preserve an improved bot prefix on disable, manual restart, or exit.
    // An interruption is neither a death nor a completed/official result.
    void recordInterrupted(Snapshot const& snapshot);
    // Clears attempt state and releases both logical inputs immediately.
    void stop();
    TrainingData const& data() const { return m_data; }
    Metrics metrics() const;
private:
    PlannerConfig m_config;
    TrainingData m_data;
    CubeStrategy m_cube;
    bool m_attemptRunning = false;
    std::array<bool, 2> m_held{{false, false}};
    Status m_status = Status::Ready;
    std::vector<InputEvent> m_trace;
    std::vector<InputEvent> m_replay;
    std::size_t m_replayCursor = 0;
    double m_replayUntil = -1.0;
    double m_timingBias = 0.0;
    double m_lastElapsed = -1.0;
    void prepareTrial();
    void finishAttempt(Snapshot const& snapshot, bool completed, bool countFailure);
    Decision emit(std::array<bool, 2> held, Status, double elapsed,
                  double plannedJumpIn = -1.0);
};

char const* statusName(Status status);

} // namespace cubebot
