#include "Controller.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

using namespace geode::prelude;
class $modify(AutobotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool replay, bool skipObjects) {
        if (!PlayLayer::init(level, replay, skipObjects)) return false;
        autobot::Controller::get().attach(this, !replay && !skipObjects);
        return true;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto& bot = autobot::Controller::get();
        if (bot.owns(this)) bot.onFrame();
    }
    void resetLevel() {
        auto& bot = autobot::Controller::get();
        auto owned = bot.owns(this);
        if (owned) bot.beforeReset();
        PlayLayer::resetLevel();
        if (owned) bot.afterReset();
    }
    void destroyPlayer(PlayerObject* player, GameObject* cause) {
        PlayLayer::destroyPlayer(player, cause);
        auto& bot = autobot::Controller::get();
        if (bot.owns(this) && player && player->m_isDead) bot.onDeath(player);
    }
    void onQuit() {
        auto& bot = autobot::Controller::get();
        auto owned = bot.owns(this);
        if (owned) bot.beginQuit();
        PlayLayer::onQuit();
        if (owned) bot.endQuit();
    }
    void onExit() {
        auto& bot = autobot::Controller::get();
        auto owned = bot.owns(this);
        if (owned) bot.prepareExit();
        PlayLayer::onExit();
        if (owned) bot.didExit();
    }
};
class $modify(AutobotBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool halfTick, bool lastTick) {
        auto& bot = autobot::Controller::get();
        if (bot.ownsBase(this)) bot.onProcess();
        GJBaseGameLayer::processCommands(dt, halfTick, lastTick);
        if (bot.ownsBase(this)) bot.scrubProgress();
    }
    void handleButton(bool down, int button, bool player1) {
        auto& bot = autobot::Controller::get();
        if (bot.ownsBase(this) && bot.blocksHumanInput() &&
            !bot.injecting() && button == static_cast<int>(PlayerButton::Jump)) return;
        GJBaseGameLayer::handleButton(down, button, player1);
        if (bot.ownsBase(this)) bot.scrubProgress();
    }
    void pickupItem(EffectGameObject* object) {
        GJBaseGameLayer::pickupItem(object);
        auto& bot = autobot::Controller::get();
        if (bot.ownsBase(this)) bot.scrubProgress();
    }
};
$on_mod(DataSaved) { autobot::Controller::get().persist(); }
