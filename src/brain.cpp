#include "brain.hpp"
namespace v2 {
void Brain::resetAttempt() {
    for (int i = 0; i < 2; i++) {
        m_calibration[i].resetSampling();
        m_commit[i].reset();
        m_clock[i].reset();
        m_robot[i] = 0;
    }
    m_previous = -1;
    m_desired = {};
}
std::array<bool, 2> Brain::choose(Frame f, std::array<std::uint64_t, 2> edges, std::uint64_t policy,
                                  SceneProvider const &provider, unsigned nodeBudget) {
    double now = f.outer / 4., dt = m_previous < 0 ? 0 : std::max(0., now - m_previous);
    m_previous = now;
    for (int i = 0; i < (f.dual ? 2 : 1); i++) {
        auto &s = f.players[i];
        if (s.grounded)
            m_robot[i] = 0;
        else if (!s.held)
            m_robot[i] = 16;
        else if (s.form == rf::Form::Robot && s.vy * (s.inverted ? -1 : 1) > 0)
            m_robot[i] += dt;
        s.robotTime = m_robot[i];
        m_calibration[i].observe({s, now, edges[i], f.disturbed[i]});
    }
    auto contact = f.contacts[0] ^ (f.contacts[1] << 24);
    auto first = m_desired[0], second = m_desired[1];
    bool committed = m_commit[0].sample(now, f.players[0], contact, first, f.dual ? &f.players[1] : nullptr);
    if (f.dual && f.independent)
        committed = m_commit[1].sample(now, f.players[1], contact, second, &f.players[0]) && committed;
    if (committed) {
        m_desired = {first, second};
        return m_desired;
    }
    bool replan = m_clock[0].needsPlan(f.outer, f.players[0], f.contacts[0], edges[0], contact);
    if (f.dual)
        replan = m_clock[1].needsPlan(f.outer, f.players[1], f.contacts[1], edges[1], contact) || replan;
    if (!replan)
        return m_desired;
    auto s = m_calibration[0].adjusted(f.players[0]);
    auto const &scene = provider(f);
    rf::PlanningBudget budget;
    budget.reset(nodeBudget);
    auto bounded = nodeBudget ? &budget : nullptr;
    if (f.dual) {
        auto secondState = m_calibration[1].adjusted(f.players[1]);
        auto d = rf::chooseDualInput(s, secondState, scene.objects, scene.interactions[0],
                                     scene.interactions[1], nullptr, false, f.independent, bounded);
        m_desired = {d.down, d.secondDown};
        m_commit[0].begin(d.plan, now, f.players[0], contact, &f.players[1]);
        if (f.independent)
            m_commit[1].begin(d.secondPlan, now, f.players[1], contact, &f.players[0]);
    } else {
        auto d = rf::chooseSoloInput(s, scene.objects, scene.interactions[0], nullptr, policy, 1337, false,
                                     bounded);
        m_desired = {d.down, false};
        if (d.plan.steps)
            m_commit[0].begin(d.plan, now, f.players[0], contact);
        else if (s.form == rf::Form::Cube && s.grounded && !d.down && scene.objects.empty() &&
                 scene.interactions[0].empty())
            m_commit[0].begin({0, 12, 3}, now, f.players[0], contact);
    }
    return m_desired;
}
Json Brain::learning() const {
    return {m_calibration[0].toJson(), m_calibration[1].toJson()};
}
void Brain::restoreLearning(Json const &j) {
    if (!j.is_array() || j.size() != 2)
        throw std::runtime_error("Invalid learning state");
    m_calibration[0] = rf::Calibration::fromJson(j[0]);
    m_calibration[1] = rf::Calibration::fromJson(j[1]);
    resetAttempt();
}
} // namespace v2
