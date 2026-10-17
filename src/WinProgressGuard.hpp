#pragma once
#include <memory>
class PlayLayer;
class GJGameLevel;

namespace autobot {
// Covers persistent writes which GD 2.2081 inlines on Windows and therefore
// cannot be intercepted using Geode's normal Modify hooks.
class WinProgressGuard {
    struct State;
    std::unique_ptr<State> m_state;
public:
    WinProgressGuard();
    ~WinProgressGuard();
    bool capture(PlayLayer* layer);
    void restore();
    void clear();
    bool owns(GJGameLevel* level) const;
};
}
