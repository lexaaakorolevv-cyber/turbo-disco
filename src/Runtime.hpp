#pragma once
#include "core.hpp"
#include "flight.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <memory>
#include <optional>

namespace rf {
enum class Mode { Idle, Recording, Searching, Verifying, Playing };

class Runtime {
public:
    static Runtime& get();
    void bind(PlayLayer* layer);
    bool syncActiveLayer();
    bool levelReady() const;
    void unbind(PlayLayer* layer);
    bool active() const { return m_mode != Mode::Idle; }
    bool owns(GJBaseGameLayer* layer) const { return layer == m_layer; }
    bool frozen() const { return m_menuOpen; }
    bool injecting() const { return m_injecting; }
    bool controlledReset() const { return m_resetting; }
    bool pendingRestart() const { return m_restart; }
    void setMenuOpen(bool open);
    void resumeOverlay(PauseLayer* pause, EndLevelLayer* end);
    void startSearch();
    void startRecording();
    void play();
    void stop(std::string reason = "Остановлено пользователем");
    void save();
    void load();
    void poll();
    void service(); // Called only outside the game's update/reset stack.
    bool beforeTick(float dt, bool halfTick, bool lastTick);
    bool advanceOuterUpdate();
    bool humanInput(bool down, int button, bool inputSide);
    void died();
    bool completed(); // Whether to call native levelComplete.
    void unexpectedReset();
    std::string summary() const;
    std::string hudSummary() const;
    std::string const& status() const { return m_status; }
    double& accumulator() { return m_accumulator; }
private:
    PlayLayer* m_layer = nullptr;
    Mode m_mode = Mode::Idle;
    std::string m_status = "Готов. Начните запись или поиск.";
    bool m_injecting = false, m_resetting = false, m_restart = false;
    bool m_menuOpen = false, m_pendingHuman = false, m_held = false;
    bool m_oldCBS = false, m_oldCOS = false, m_haveOptions = false;
    bool m_verifyingSearch = false, m_releasePending = false;
    double m_accumulator = 0, m_trialPercent = 0;
    Tick m_tick = 0, m_tickLimit = MaxTicks;
    Tick m_outerUpdates = 0, m_outerLimit = 240 * 600;
    std::size_t m_cursor = 0, m_traceCursor = 0;
    std::uint64_t m_attempts = 0, m_seed = 1337;
    static constexpr std::uint64_t GameSeed = 0x526f75746546696eULL;
    Budget m_budget;
    std::chrono::steady_clock::time_point m_started;
    nlohmann::json m_environment;
    Macro m_trial;
    std::optional<Macro> m_best, m_selected, m_target;
    std::vector<Action> m_plan;
    std::unique_ptr<Search> m_search;
    Tick m_flightFrom = 0;
    bool m_guidedFlight = false;
    std::vector<FlightObstacle> m_flightObstacles;
    std::string unsupported() const;
    std::string readinessError() const;
    nlohmann::json environment() const;
    bool prepare();
    void beginAttempt();
    void releaseInput();
    void inject(bool down);
    void guideFlight(PlayerObject* player);
    Macro snapshot(bool complete = false) const;
    void remember(Macro const& macro);
    void writeBest();
    void failReplay(std::string const& why);
    std::filesystem::path macroPath(bool best) const;
};
void attachHud(PlayLayer* layer);
void showMenu(PauseLayer* pause = nullptr, EndLevelLayer* end = nullptr);
}
