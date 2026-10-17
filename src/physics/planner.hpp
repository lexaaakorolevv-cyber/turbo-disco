#pragma once
#include "planning_budget.hpp"
#include "intelligence.hpp"
#include "control_plan.hpp"
namespace rf {
enum class InteractionKind {
    YellowOrb,
    PinkOrb,
    RedOrb,
    BlueOrb,
    GreenOrb,
    BlackOrb,
    YellowPad,
    PinkPad,
    RedPad,
    BluePad,
    GravityPortal,
    ModePortal,
    SizePortal,
    SpeedPortal,
    SpiderOrb,
    SpiderPad,
    NativeBoundary
};
struct Interaction {
    Obstacle area;
    InteractionKind kind = InteractionKind::YellowOrb;
    double value = 0;
    Form target = Form::Cube;
    bool inverted = false, reverse = false;
    unsigned slot = 64;
};
bool hasInteractionChain(FormState const &state, std::span<Interaction const> interactions);
void interactionCandidates(std::span<Interaction const> source, double left, double right, double radius,
                           double start, double end, std::vector<Interaction> &result);
double approachCost(FormState const &state, std::span<Obstacle const> objects, double time, double dt);
double formCost(FormState const &state, FormState const &previous, bool down, double dt, double centre,
                double bias = 0);
bool equivalentFormStates(FormState const &a, FormState const &b);
struct DualDecision {
    bool down = false;
    double survival = 0;
    Forecast first, second;
    ControlPlan plan;
    bool secondDown = false;
    ControlPlan secondPlan;
};
DualDecision chooseDualInput(FormState const &first, FormState const &second,
                             std::span<Obstacle const> obstacles,
                             std::span<Interaction const> firstInteractions,
                             std::span<Interaction const> secondInteractions,
                             FailureMemory const *failures = nullptr, bool includeForecast = true,
                             bool independent = false, PlanningBudget *budget = nullptr);
struct FormDecision {
    bool down = false;
    double survival = 0;
    Forecast forecast;
    ControlPlan plan;
    bool handoff = false;
};
// Approximate forward model only; it never mutates a Geometry Dash object.
// The real game and full replay remain the final judges.
bool advanceForm(FormState &state, bool down, double dt, std::span<Obstacle const> obstacles,
                 std::span<Interaction const> interactions, std::uint64_t &used, double time = 0);
FormDecision chooseFormInput(FormState const &state, std::span<Obstacle const> obstacles,
                             std::span<Interaction const> interactions,
                             FailureMemory const *failures = nullptr, double bias = 0,
                             bool includeForecast = true, PlanningBudget *budget = nullptr);
} // namespace rf
