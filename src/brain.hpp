#pragma once
#include "types.hpp"
#include "physics/solo.hpp"
#include "physics/input.hpp"
#include "physics/guidance_clock.hpp"
#include <functional>
namespace v2 {
using SceneProvider = std::function<Scene const &(Frame const &)>;
class Brain {
    std::array<rf::Calibration, 2> m_calibration;
    std::array<rf::InputCommitment, 2> m_commit;
    std::array<rf::GuidanceClock, 2> m_clock;
    std::array<double, 2> m_robot{};
    double m_previous = -1;
    std::array<bool, 2> m_desired{};

  public:
    void resetAttempt();
    std::array<bool, 2> choose(Frame frame, std::array<std::uint64_t, 2> edges, std::uint64_t policy,
                               SceneProvider const &scene);
    Json learning() const;
    void restoreLearning(Json const &value);
};
} // namespace v2
