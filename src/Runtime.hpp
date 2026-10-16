#pragma once
#include "core.hpp"
#include "save_worker.hpp"
#include "death_analysis.hpp"
#include "flight.hpp"
#include "input.hpp"
#include "ground.hpp"
#include "planner.hpp"
#include "guidance_clock.hpp"
#include <Geode/Geode.hpp>
#include <chrono>
#include <memory>
#include <optional>

namespace rf {
enum class Mode { Idle, Recording, Searching, Assisting, Verifying, Playing };

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
    void takeControl();
    void returnToBot();
    void toggleSmooth() {m_smoothSearch=!m_smoothSearch;m_guidanceClock.reset();geode::Mod::get()->setSettingValue<bool>("smooth-search",m_smoothSearch);}
    bool searching() const {return m_mode==Mode::Searching||m_mode==Mode::Assisting;}
    unsigned cps() const {return std::max(m_inputPacer.currentCps(),twoPlayerEnabled()?m_inputPacer2.currentCps():0);}
    bool smoothSearch() const {return m_smoothSearch;}
    double bestPercent() const {return std::max({m_recordPercent,m_trialPercent,m_best?m_best->bestPercent:0.});}
    std::uint64_t attempts() const {return m_attempts;}
    char const* modeName() const;
    int remainingSeconds() const {return active()?static_cast<int>(std::max(0.,m_budget.maxSeconds-elapsedSeconds())):0;}
    std::string detail() const {return m_status+(m_autosaveStatus.empty()?"":"\n"+m_autosaveStatus);}
    bool assisting() const { return m_mode == Mode::Assisting; }
    Forecast const& forecast() const { return m_forecast; }
    Forecast const& secondForecast() const { return m_secondForecast; }
    bool twoPlayerEnabled() const {return m_layer&&m_layer->m_levelSettings&&m_layer->m_levelSettings->m_twoPlayerMode;}
    bool dualEnabled() const {return m_layer&&m_layer->m_gameState.m_isDualMode;}
    PlayLayer* layer() const { return m_layer; }
    void play();
    void stop(std::string reason = "Остановлено пользователем");
    void save();
    void load();
    void poll();
    void service(); // Called only outside the game's update/reset stack.
    bool beforeTick(float dt, bool halfTick, bool lastTick);
    bool advanceOuterUpdate();
    bool humanInput(bool down, int button, bool inputSide);
    void died(GameObject* object = nullptr,PlayerObject* deadPlayer = nullptr);
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
    bool m_pendingHuman2=false,m_held2=false,m_searchDesired2=false;
    bool m_oldCBS = false, m_oldCOS = false, m_haveOptions = false;
    bool m_verifyingSearch = false, m_releasePending = false;
    double m_accumulator = 0, m_trialPercent = 0,m_recordPercent=0;
    std::string m_recordKey;
    Tick m_tick = 0, m_tickLimit = MaxTicks;
    Tick m_outerUpdates = 0, m_outerLimit = 240 * 600;
    std::size_t m_cursor = 0, m_traceCursor = 0;
    std::uint64_t m_attempts = 0, m_seed = 1337;
    static constexpr std::uint64_t GameSeed = 0x526f75746546696eULL;
    Budget m_budget;
    std::chrono::steady_clock::time_point m_started;
    std::chrono::steady_clock::time_point m_lastAutosave;
    SearchSaveWorker m_saveWorker;
    std::uint64_t m_saveEpoch=0;
    bool m_autosaveDirty = false, m_autosaveUrgent = false;
    bool m_selectedForSearch = false;
    std::uint64_t m_previousAttempts = 0;
    double m_previousSeconds = 0, m_runElapsed = 0;
    std::string m_autosaveStatus;
    nlohmann::json m_environment;
    Macro m_trial;
    std::optional<Macro> m_best, m_selected, m_target;
    std::vector<Action> m_plan;
    std::unique_ptr<Search> m_search;
    Tick m_flightFrom = 0;
    bool m_guidedFlight = false;
    bool m_searchDesired = false;
    InputPacer m_inputPacer,m_inputPacer2;
    InputCommitment m_inputCommitment,m_secondCommitment;
    std::vector<FlightObstacle> m_flightObstacles;
    double m_spawnFloor = 90;
    Calibration m_calibration,m_secondCalibration;
    FailureMemory m_failures;
    PlayerHistory m_history,m_history2;
    DeathDiagnosis m_diagnosis;
    MotionTracker m_motion;
    FormState m_observation,m_secondObservation;
    Forecast m_forecast,m_secondForecast;
    std::vector<Interaction> m_interactions,m_secondInteractions;
    std::uint64_t m_inputEdges = 0,m_inputEdges2=0;
    double m_robotTime = 0, m_observationTime = -1,m_robotTime2=0,m_observationTime2=-1;
    bool m_previousDual=false;
    bool m_showPreview = true,m_smoothSearch=true,m_cachedDesired=false;
    GuidanceClock m_guidanceClock,m_secondGuidanceClock;
    std::vector<geode::Ref<GameObject>> m_obstacleObjects,m_interactionObjects;
    unsigned m_sceneObjectCount=0;
    void rebuildSceneCache();
    std::string m_guidanceNote, m_lastFailure;
    FormState readState(PlayerObject* player) const;
    void updateIntelligence(PlayerObject* player);
    void collectInteractions(PlayerObject* player,double minX, double maxX, double minY, double maxY);
    void rememberFailure(GameObject* object,PlayerObject* deadPlayer);

    std::string unsupported() const;
    std::string readinessError() const;
    nlohmann::json environment() const;
    bool prepare();
    void beginAttempt();
    void releaseInput();
    void inject(bool down,int player=1);
    void guideFlight(PlayerObject* player);
    void collectObstacles(double minX, double maxX, double minY, double maxY);
    Macro snapshot(bool complete = false) const;
    void remember(Macro const& macro);
    void writeBest();
    void autosaveSearch(bool force = false);
    bool restoreSearch();
    double elapsedSeconds() const;
    std::filesystem::path sessionPath() const;
    void failReplay(std::string const& why);
    std::filesystem::path macroPath(bool best) const;
};
void attachHud(PlayLayer* layer);
void showMenu(PauseLayer* pause = nullptr, EndLevelLayer* end = nullptr);
}
