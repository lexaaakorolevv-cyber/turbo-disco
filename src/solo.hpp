#pragma once
#include "planner.hpp"
namespace rf {
// Shared live/offline entry point. Ordinary cubes retain the ground default;
// one quarter of alternative policies try the form planner without Analyze.
FormDecision chooseSoloInput(FormState const& state,std::span<Obstacle const> obstacles,
    std::span<Interaction const> interactions,FailureMemory const* failures,
    std::uint64_t policy,std::uint64_t seed,bool includeForecast=false);
}
