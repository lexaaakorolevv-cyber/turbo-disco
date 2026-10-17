#include "native.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <cmath>
using namespace geode::prelude;
class $modify(RouteFinder2Play, PlayLayer) {
    bool init(GJGameLevel *l, bool replay, bool dontCreate) {
        if (!PlayLayer::init(l, replay, dontCreate))
            return false;
        v2::Native::get().bind(this);
        return true;
    }
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        v2::Native::get().bind(this);
        v2::attachHud(this);
    }
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        v2::Native::get().bind(this);
        v2::attachHud(this);
    }
    void resetLevel() {
        auto &n = v2::Native::get();
        if (n.owns(this))
            n.unexpectedReset();
        PlayLayer::resetLevel();
    }
    void delayedResetLevel() {
        auto &n = v2::Native::get();
        if (n.owns(this) && n.active())
            return;
        PlayLayer::delayedResetLevel();
    }
    void destroyPlayer(PlayerObject *p, GameObject *o) {
        bool dead = (m_player1 && m_player1->m_isDead) ||
                    (m_gameState.m_isDualMode && m_player2 && m_player2->m_isDead);
        PlayLayer::destroyPlayer(p, o);
        if (!dead && ((m_player1 && m_player1->m_isDead) ||
                      (m_gameState.m_isDualMode && m_player2 && m_player2->m_isDead))) {
            auto &n = v2::Native::get();
            if (n.owns(this))
                n.died();
        }
    }
    void levelComplete() {
        auto &n = v2::Native::get();
        if (!n.owns(this) || !n.active() || n.finishing())
            PlayLayer::levelComplete();
        else
            n.completed();
    }
    void onExit() {
        v2::Native::get().unbind(this);
        PlayLayer::onExit();
    }
};
class $modify(RouteFinder2Game, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool side) {
        auto &n = v2::Native::get();
        if (!n.owns(this) || n.human(down, button, side))
            GJBaseGameLayer::handleButton(down, button, side);
    }
    void processCommands(float dt, bool half, bool last) {
        auto &n = v2::Native::get();
        if (n.owns(this) && n.active() && !n.before(dt, half, last))
            return;
        GJBaseGameLayer::processCommands(dt, half, last);
    }
    void update(float dt) {
        auto &n = v2::Native::get();
        auto current = PlayLayer::get();
        if (current && static_cast<GJBaseGameLayer *>(current) == this) {
            n.bind(current);
            v2::attachHud(current);
        }
        if (!n.owns(this)) {
            GJBaseGameLayer::update(dt);
            return;
        }
        n.service();
        if (n.menu())
            return;
        if (!n.active()) {
            GJBaseGameLayer::update(dt);
            return;
        }
        if (!current || current->m_isPaused) {
            n.accumulator() = 0;
            return;
        }
        if (!std::isfinite(dt) || dt < 0) {
            n.stop();
            return;
        }
        constexpr double step = 1. / 240;
        n.accumulator() += std::min(static_cast<double>(dt), .1);
        unsigned steps = 0;
        while (n.accumulator() + 1e-9 >= step && steps++ < 24) {
            n.accumulator() -= step;
            if (!n.outer())
                break;
            GJBaseGameLayer::update(static_cast<float>(step));
            if (!n.active() || n.pending())
                break;
        }
        n.service();
    }
};
class $modify(RouteFinder2Pause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = CCMenu::create();
        auto size = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({0, 0});
        auto b = CCMenuItemSpriteExtra::create(ButtonSprite::create("BOT", .6f), this,
                                               menu_selector(RouteFinder2Pause::open));
        b->setPosition({size.width - 45, size.height - 48});
        menu->addChild(b);
        addChild(menu, 10000);
    }
    void open(CCObject *) {
        v2::showMenu(this);
    }
};
