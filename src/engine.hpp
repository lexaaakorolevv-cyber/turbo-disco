#pragma once
#include "brain.hpp"
#include "search.hpp"
#include <utility>
#include <algorithm>
#include <cmath>
namespace v2 {
enum class Mode { Idle, Recording, Searching, Verifying, Replaying };
struct Decision {
    bool advance = false;
    std::vector<Action> actions;
};
struct Session {
    Run best;
    Search search;
    Candidate candidate;
    Json learning;
    std::uint64_t attempts = 0;
    double seconds = 0;
};
Json encode(Session const &session);
Session decodeSession(Json const &json);
class Engine {
    enum class Event { None, Death, Complete };
    Mode m_mode = Mode::Idle;
    Event m_event = Event::None;
    Json m_environment;
    Limits m_limits;
    Run m_trial;
    std::optional<Run> m_best, m_target;
    Search m_search;
    Candidate m_candidate;
    Brain m_brain;
    std::array<rf::InputPacer, 2> m_pacer;
    std::array<bool, 2> m_held{}, m_human{}, m_desired{};
    std::array<std::uint64_t, 2> m_edges{};
    Tick m_tick = 0, m_outer = 0;
    std::size_t m_cursor = 0, m_traceCursor = 0;
    std::uint64_t m_attempts = 0;
    double m_seconds = 0;
    bool m_restart = false, m_release = false, m_save = false, m_nativeComplete = false,
         m_previousDual = false;
    int m_checkpointPercent = 0;
    std::string m_status = "Готов";
    void remember();
    void fail(std::string reason);
    void configure(Json environment, Limits limits);

  public:
    void startSearch(Json environment, Limits limits, std::optional<Session> resume = std::nullopt);
    void startRecording(Json environment, Limits limits);
    void startReplay(Json environment, Limits limits, Run run);
    void beginAttempt();
    bool advanceOuter();
    Decision tick(Frame frame, SceneProvider const &scene);
    void markDeath() {
        if (active() && !m_restart)
            m_event = Event::Death;
    }
    void markComplete() {
        if (active() && !m_restart)
            m_event = Event::Complete;
    }
    void service(double elapsed);
    void stop(std::string reason = "Остановлено пользователем");
    bool active() const {
        return m_mode != Mode::Idle;
    }
    bool pending() const {
        return m_restart || m_event != Event::None;
    }
    bool restartRequested() const {
        return m_restart;
    }
    bool consumeRelease() {
        return std::exchange(m_release, false);
    }
    bool consumeSave() {
        return std::exchange(m_save, false);
    }
    bool consumeCompletion() {
        return std::exchange(m_nativeComplete, false);
    }
    Mode mode() const {
        return m_mode;
    }
    Tick tickNumber() const {
        return m_tick;
    }
    Tick outer() const {
        return m_outer;
    }
    std::uint64_t attempts() const {
        return m_attempts;
    }
    bool held(int player) const {
        return m_held[player - 1];
    }
    void human(int player, bool down) {
        if (player >= 1 && player <= 2)
            m_human[player - 1] = down;
    }
    void progress(double percent) {
        if (m_tick && std::isfinite(percent))
            m_trial.percent = std::max(m_trial.percent, std::clamp(percent, 0., 100.));
    }
    std::optional<Run> const &best() const {
        return m_best;
    }
    double bestPercent() const {
        return std::max(m_trial.percent, m_best ? m_best->percent : 0.);
    }
    std::string const &status() const {
        return m_status;
    }
    Json const &environment() const {
        return m_environment;
    }
    Session snapshot() const;
};
} // namespace v2
