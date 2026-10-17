#pragma once
#include "search.hpp"
namespace v2 {
// Branches are judged only by subsequent live-game ticks. A Node is an input
// decision and a replay witness, never a snapshot of the game's world.
class NativeSearch {
    struct Node {
        Sample sample;
        Timing timing;
        Tick outer = 0;
        std::size_t actions = 0;
        std::array<std::uint64_t, 2> contacts{};
        unsigned held = 0, chosen = 0, remaining = 0;
        bool independent = false;
    };
    std::vector<Node> m_path;
    unsigned m_step = 2, m_mask = 0;
    std::size_t m_cap = 32768, m_cursor = 0;
    Tick m_next = 0;
    std::uint64_t m_decisions = 0, m_limit = 3000000, m_deaths = 0;
    bool m_fork = false, m_exhausted = false;

  public:
    void configure(unsigned step, std::size_t cap, std::uint64_t limit);
    void beginAttempt();
    void limit(std::uint64_t decisions) {
        m_limit = decisions;
    }
    bool validateReplay(Frame const &frame, std::size_t actions);
    bool needsDecision(Frame const &frame) const;
    unsigned decide(Frame const &frame, std::size_t actions, unsigned suggested, unsigned legal);
    std::optional<Candidate> backtrack(Run const &run);
    Candidate resume(Run const &run);
    bool exhausted() const {
        return m_exhausted;
    }
    bool atLimit() const;
    std::uint64_t decisions() const {
        return m_decisions;
    }
    std::size_t depth() const {
        return m_path.size();
    }
    Json json() const;
    static NativeSearch restore(Json const &json);
};
} // namespace v2
