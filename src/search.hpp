#pragma once
#include "types.hpp"
namespace v2 {
struct Candidate {
    std::vector<Action> actions;
    Tick guideFrom = 0;
    std::uint64_t policy = 0, parent = 0;
    std::optional<Sample> checkpoint;
};
class Search {
  public:
    struct Branch {
        std::uint64_t id = 0, policy = 0, uses = 0, stalls = 0, cursor = 0;
        double percent = 0, anchor = 0;
        Tick death = 0;
        std::vector<Action> actions;
    };
    struct Point {
        int percent = 0;
        Sample sample;
        std::vector<Action> actions;
        unsigned confirmations = 1;
        std::uint64_t attempt = 0;
    };
    void observe(Run const &run, Candidate const &source);
    Candidate next(Tick limit = MaxTicks);
    void checkpoint(int percent, Sample const &s, std::vector<Action> const &a, std::uint64_t attempt);
    void invalidate(Tick tick);
    Json json() const;
    static Search restore(Json const &json);
    std::vector<Branch> const &branches() const {
        return m_branches;
    }

  private:
    std::vector<Branch> m_branches;
    std::vector<Point> m_points;
    std::uint64_t m_id = 1, m_trials = 0, m_active = 0;
};
Json encodeCandidate(Candidate const &candidate);
Candidate decodeCandidate(Json const &json);
} // namespace v2
