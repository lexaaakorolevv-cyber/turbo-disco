#include "Runtime.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
void addOverlayMenu(CCNode* parent, CCObject* target, SEL_MenuHandler callback) {
    auto menu = CCMenu::create();
    auto size = CCDirector::sharedDirector()->getWinSize();
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({0, 0});
    menu->setPosition({0, 0});
    menu->setContentSize(size);
    auto button = CCMenuItemSpriteExtra::create(ButtonSprite::create("BOT", .6f), target, callback);
    button->setPosition({size.width - 45, size.height - 48});
    menu->addChild(button);
    parent->addChild(menu, 10000);
}
}

class $modify(RouteFinderPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        addOverlayMenu(this, this, menu_selector(RouteFinderPauseLayer::openBot));
    }
    void openBot(CCObject*) { rf::showMenu(this, nullptr); }
};

class $modify(RouteFinderEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        addOverlayMenu(this, this, menu_selector(RouteFinderEndLevelLayer::openBot));
    }
    void openBot(CCObject*) { rf::showMenu(nullptr, this); }
};

// Keep game calls on the game thread. Restarts run after update has unwound,
// never from destroyPlayer, collision handling or processCommands itself.
class $modify(RouteFinderPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        rf::Runtime::get().bind(this);
        return true;
    }
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        rf::Runtime::get().bind(this);
        rf::attachHud(this);
    }
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        rf::Runtime::get().bind(this);
        rf::attachHud(this);
    }
    void resetLevel() {
        auto& runtime = rf::Runtime::get();
        if (runtime.owns(this)) runtime.unexpectedReset();
        PlayLayer::resetLevel();
    }
    void delayedResetLevel() {
        auto& runtime = rf::Runtime::get();
        if (runtime.owns(this) && runtime.active()) return;
        PlayLayer::delayedResetLevel();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto wasDead = (m_player1 && m_player1->m_isDead)||(m_gameState.m_isDualMode&&m_player2&&m_player2->m_isDead);
        PlayLayer::destroyPlayer(player, object);
        if (!wasDead && ((m_player1&&m_player1->m_isDead)||(m_gameState.m_isDualMode&&m_player2&&m_player2->m_isDead)) && rf::Runtime::get().owns(this))
            rf::Runtime::get().died(object,player);
    }
    void levelComplete() {
        auto& runtime = rf::Runtime::get();
        if (!runtime.owns(this) || runtime.completed()) PlayLayer::levelComplete();
    }
    void onExit() {
        rf::Runtime::get().unbind(this);
        PlayLayer::onExit();
    }
};

class $modify(RouteFinderGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        auto& runtime = rf::Runtime::get();
        if (!runtime.owns(this) || runtime.humanInput(down, button, player1))
            GJBaseGameLayer::handleButton(down, button, player1);
    }
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& runtime = rf::Runtime::get();
        if (runtime.owns(this) && runtime.active()) {
            if (!runtime.beforeTick(dt, isHalfTick, isLastTick)) return;
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
    void update(float dt) {
        auto& runtime = rf::Runtime::get();
        // Also recover when init/setup hooks ran before the layer became the
        // GameManager's current PlayLayer. Never bind an editor/game preview.
        auto current = PlayLayer::get();
        if (current && static_cast<GJBaseGameLayer*>(current) == this) {
            runtime.bind(current);
            rf::attachHud(current);
        }
        if (!runtime.owns(this)) { GJBaseGameLayer::update(dt); return; }
        runtime.service();
        if (runtime.frozen() && runtime.levelReady()) return;
        if (!runtime.active()) { GJBaseGameLayer::update(dt); return; }
        auto play = PlayLayer::get();
        if (!play || play->m_isPaused) { runtime.accumulator() = 0; return; }
        if (!std::isfinite(dt) || dt < 0) { runtime.stop("Некорректный dt игры"); return; }
        // Run exactly one vanilla outer update per 1/240 s. Never change
        // getModifiedDelta or patch physics constants. Slow machines slow the
        // simulation instead of dropping a physics step inside an attempt.
        constexpr double step = 1.0 / 240.0;
        runtime.accumulator() += std::min(static_cast<double>(dt), .1);
        int steps = 0;
        while (runtime.accumulator() + 1e-9 >= step && steps++ < 24) {
            runtime.accumulator() -= step;
            if (!runtime.advanceOuterUpdate()) break;
            GJBaseGameLayer::update(static_cast<float>(step));
            if (!runtime.active() || runtime.pendingRestart()) break;
        }
        runtime.service();
    }
};
