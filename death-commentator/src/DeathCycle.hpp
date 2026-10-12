#pragma once

namespace deathcomment {

// Collision and frame callbacks can observe the same death. The native game
// may already have marked the player dead before a collision hook runs.
class DeathCycle {
public:
    void alive() { m_reported = false; m_suppressed = false; }
    void resetBoundary(bool dead) {
        m_reported = dead;
        m_suppressed = dead;
    }
    void suppress() { m_suppressed = true; }
    bool claim(bool dead) {
        if (!dead || m_reported || m_suppressed) return false;
        m_reported = true;
        return true;
    }

private:
    bool m_reported = false;
    bool m_suppressed = false;
};

} // namespace deathcomment
