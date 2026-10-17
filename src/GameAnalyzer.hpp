#pragma once
#include "bot/Learner.hpp"
class PlayLayer;
namespace autobot {
class GameAnalyzer {
    bool m_previousValid = false;
    cubebot::Snapshot m_previous;
public:
    void reset() { m_previousValid = false; }
    cubebot::Snapshot sample(PlayLayer* layer);
};
}
