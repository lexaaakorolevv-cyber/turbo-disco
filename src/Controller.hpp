#pragma once
#include "bot/Learner.hpp"
#include "GameAnalyzer.hpp"
#include "InputDriver.hpp"
#include "WinProgressGuard.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

class PlayLayer;
class GJBaseGameLayer;
class PlayerObject;
namespace cocos2d { class CCLabelBMFont; }

namespace autobot {
class Controller {
    PlayLayer* m_layer = nullptr; // borrowed; cleared by the PlayLayer exit hooks
    cocos2d::CCLabelBMFont* m_hud = nullptr; // owned by PlayLayer
    InputDriver m_input;
    WinProgressGuard m_recordGuard;
    GameAnalyzer m_analyzer;
    cubebot::CubeLearner m_learner;
    cubebot::Snapshot m_last;
    std::filesystem::path m_path;
    std::string m_fingerprint;
    std::string m_status = "Выключен";
    bool m_enabled = false;
    bool m_tainted = false;
    bool m_attemptRunning = false;
    bool m_restartQueued = false;
    bool m_waitingForReset = false;
    bool m_pauseQueued = false;
    bool m_finished = false;
    bool m_haveSnapshot = false;
    bool m_controllableSession = false;
    bool m_detachPending = false;
    unsigned m_quitDepth = 0;
    std::chrono::steady_clock::time_point m_retryAt{};
    void updateHUD();
    void loadLevel();
    void stopWithStatus(std::string text);
    void preserveLiveProgress();
    void clearAttachment();
public:
    static Controller& get();
    bool enabled() const { return m_enabled; }
    bool tainted() const { return m_tainted; }
    bool attached() const { return m_layer != nullptr; }
    bool owns(PlayLayer* p) const { return p && p == m_layer; }
    bool ownsBase(GJBaseGameLayer* p) const;
    bool injecting() const { return m_input.injecting(); }
    bool blocksHumanInput() const { return m_enabled; }
    void setEnabled(bool enabled);
    void startTraining();
    void resetProgress();
    std::string status() const { return m_status; }
    std::uint64_t attempts() const { return m_learner.data().attempts; }
    float bestPercent() const;
    void attach(PlayLayer*, bool controllable = true);
    void onProcess();
    void onFrame();
    void beforeReset();
    void afterReset();
    void onDeath(PlayerObject*);
    void onCompletion();
    void prepareExit();
    void beginQuit();
    void endQuit();
    void didExit();
    void persist();
    void scrubProgress();
};
}
