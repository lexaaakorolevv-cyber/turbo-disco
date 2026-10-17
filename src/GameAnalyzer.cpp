#include "GameAnalyzer.hpp"
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cubebot;
namespace autobot {
namespace {
Mode mode(PlayerObject* p) {
    if (p->m_isShip) return Mode::Ship;
    if (p->m_isBird) return Mode::UFO;
    if (p->m_isBall) return Mode::Ball;
    if (p->m_isDart) return Mode::Wave;
    if (p->m_isRobot) return Mode::Robot;
    if (p->m_isSpider) return Mode::Spider;
    if (p->m_isSwing) return Mode::Swing;
    return Mode::Cube;
}
ObjectKind kind(GameObjectType t) {
    switch (t) {
        case GameObjectType::Hazard: case GameObjectType::AnimatedHazard: return ObjectKind::Hazard;
        case GameObjectType::Solid: case GameObjectType::Breakable: case GameObjectType::Slope: return ObjectKind::Solid;
        case GameObjectType::YellowJumpRing: case GameObjectType::PinkJumpRing:
        case GameObjectType::RedJumpRing: case GameObjectType::GravityRing:
        case GameObjectType::GreenRing: case GameObjectType::DropRing:
        case GameObjectType::CustomRing: case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing: case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb: return ObjectKind::Orb;
        case GameObjectType::YellowJumpPad: case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad: case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad: return ObjectKind::Pad;
        case GameObjectType::ShipPortal: case GameObjectType::CubePortal:
        case GameObjectType::BallPortal: case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal: case GameObjectType::RobotPortal:
        case GameObjectType::SpiderPortal: case GameObjectType::SwingPortal:
        case GameObjectType::InverseGravityPortal: case GameObjectType::NormalGravityPortal:
        case GameObjectType::GravityTogglePortal: case GameObjectType::InverseMirrorPortal:
        case GameObjectType::NormalMirrorPortal: case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal: case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal: case GameObjectType::TeleportPortal: return ObjectKind::Portal;
        default: return ObjectKind::Other;
    }
}
}
Snapshot GameAnalyzer::sample(PlayLayer* layer) {
    Snapshot s;
    s.tick = layer->m_gameState.m_currentProgress;
    s.elapsed = layer->m_gameState.m_levelTime;
    auto delta = m_previousValid ? s.elapsed - m_previous.elapsed : 0.;
    s.dt = delta > 0. && delta <= .25 ? delta : 1. / 240.;
    s.progress = std::clamp(static_cast<double>(layer->getCurrentPercent()), 0., 100.);
    s.dual = layer->m_gameState.m_isDualMode;
    for (unsigned i = 0; i < 2; ++i) {
        auto p = i == 0 ? layer->m_player1 : layer->m_player2;
        auto& out = s.players[i];
        if (!p) { out.alive = false; continue; }
        out.position = {p->m_position.x, p->m_position.y};
        out.mode = mode(p);
        out.grounded = p->m_isOnGround;
        out.upsideDown = p->m_isUpsideDown;
        out.mini = p->m_vehicleSize < .8f;
        out.width = out.height = 30. * std::abs(p->m_vehicleSize);
        out.alive = !p->m_isDead;
        if (m_previousValid && delta > 0. && delta <= .25) {
            out.velocity = {(out.position.x - m_previous.players[i].position.x) / delta,
                            (out.position.y - m_previous.players[i].position.y) / delta};
        }
        // Gravity is estimated by the strategy from these observations; raw GD
        // gravity/velocity fields use internal units and are never guessed here.
    }
    auto const& p = s.players[0];
    auto reach = std::clamp(std::abs(p.velocity.x) * 1.25 + 180., 450., 2200.);
    if (layer->m_objects) {
        for (auto o : geode::cocos::CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!o || o->m_isDisabled || o->m_isDisabled2 || o->m_isNoTouch || o->m_isPassable) continue;
            auto k = kind(o->m_objectType);
            if (k == ObjectKind::Other) continue;
            auto pos = o->getPosition();
            if (std::abs(pos.x - p.position.x) > reach + o->m_width ||
                std::abs(pos.y - p.position.y) > 650. + o->m_height) continue;
            auto const& r = o->getObjectRect();
            ObjectSnapshot object{static_cast<std::int64_t>(o->m_uniqueID), k,
                                  {r.getMinX(), r.getMinY(), r.getMaxX(), r.getMaxY()}};
            if (object.bounds.valid()) s.objects.push_back(object);
        }
    }
    m_previous = s;
    m_previous.objects.clear();
    m_previousValid = true;
    return s;
}
}
