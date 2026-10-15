#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>

namespace rf {
// All timestamps use the fixed outer 240 Hz simulation clock, not render
// frames or processCommands call count (which includes native half ticks).
class InputPacer {
public:
    using Frame = std::uint64_t;
    void reset(int maxCps = 8) {
        m_limit = std::clamp(maxCps, 1, 20);
        m_dwell = (240 + 2 * m_limit - 1) / (2 * m_limit);
        m_period = (240 + m_limit - 1) / m_limit;
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
        Frame until = m_changed ? m_lastEdge + m_dwell : 0;
        if (!m_held && !m_presses.empty()) {
            until = std::max(until, m_presses.back() + m_period);
            if (m_presses.size() >= static_cast<std::size_t>(m_limit))
                until = std::max(until, m_presses.front() + 240);
        }
        return until > now ? until - now : 0;
    }
    Frame dwellFrames() const { return m_dwell; }
    int limit() const { return m_limit; }
    int currentCps() const { return static_cast<int>(m_presses.size()); }
private:
    int m_limit = 8;
    Frame m_dwell = 15, m_period = 30, m_lastEdge = 0;
    bool m_held = false, m_changed = false;
    std::deque<Frame> m_presses;
};
}
