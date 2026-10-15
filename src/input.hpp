#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>

namespace rf {
inline constexpr int SearchCps = 16;
// All timestamps use the fixed outer 240 Hz simulation clock, not render
// frames or processCommands call count (which includes native half ticks).
class InputPacer {
public:
    using Frame = std::uint64_t;
    void reset(int maxCps = SearchCps) {
        // Limit rising edges only. Never delay a release or impose a minimum
        // hold; short taps and sustained holds are both legitimate controls.
        m_limit = maxCps <= 0 ? 0 : std::clamp(maxCps, 1, 20);
        m_dwell = 0;
        m_period = m_limit ? (240 + m_limit - 1) / m_limit : 0;
        m_held = false;
        m_changed = false;
        m_lastEdge = 0;
        m_presses.clear();
    }
    bool apply(bool desired, Frame now) {
        while (!m_presses.empty() && now >= m_presses.front() + 240) m_presses.pop_front();
        if (desired != m_held && remainingFrames(now) == 0) {
            m_held = desired;
            m_changed = true;
            m_lastEdge = now;
            if (desired) m_presses.push_back(now);
        }
        return m_held;
    }
    Frame remainingFrames(Frame now) const {
        return m_held ? 0 : pressRemainingFrames(now);
    }
    Frame pressRemainingFrames(Frame now) const {
        if (!m_limit) return 0;
        Frame until = 0;
        if (!m_presses.empty()) {
            until = std::max(until, m_presses.back() + m_period);
            if (m_presses.size() >= static_cast<std::size_t>(m_limit))
                until = std::max(until, m_presses.front() + 240);
        }
        return until > now ? until - now : 0;
    }
    Frame dwellFrames() const { return m_dwell; }
    Frame pressIntervalFrames() const { return m_period; }
    int limit() const { return m_limit; }
    int currentCps() const { return static_cast<int>(m_presses.size()); }
private:
    int m_limit = SearchCps;
    Frame m_dwell = 0, m_period = 15, m_lastEdge = 0;
    bool m_held = false, m_changed = false;
    std::deque<Frame> m_presses;
};
}
