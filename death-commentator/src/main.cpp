#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "CommentEngine.hpp"
#include "CommentOverlay.hpp"
#include "CommentPopup.hpp"
#include "CommentSettings.hpp"
#include "DeathCycle.hpp"
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace {

deathcomment::Engine& engine() {
    static deathcomment::Engine instance;
    return instance;
}

void openCommentPopup() {
    if (auto popup = CommentPopup::create()) popup->show();
}

} // namespace

class $modify(CommentPlayLayer, PlayLayer) {
    struct Fields {
        bool ready = false;
        unsigned resetDepth = 0;
        deathcomment::DeathCycle cycle;
        deathcomment::Context lastContext;
    };

    bool eligible() {
        return m_fields->ready && m_fields->resetDepth == 0 && !m_isTestMode &&
            !m_useReplay && !m_hasCompletedLevel && !m_levelEndAnimationStarted;
    }

    bool dead() {
        return m_playerDied || (m_player1 && m_player1->m_isDead) ||
            (m_gameState.m_isDualMode && m_player2 && m_player2->m_isDead);
    }

    deathcomment::Context contextNow() {
        deathcomment::Context context;
        context.platformer = m_isPlatformer;
        context.practice = m_isPracticeMode;
        context.attempt = m_attempts;
        if (!context.platformer && m_player1) {
            auto progress = getCurrentPercent();
            if (std::isfinite(progress)) context.percent = static_cast<int>(std::clamp(progress, 0.f, 100.f));
        }
        return context;
    }

    void reportDeath(deathcomment::Context const& context) {
        if (!eligible() || !m_fields->cycle.claim(dead())) return;
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return;
        if (context.practice && !mod->getSettingValue<bool>("practice")) return;
        auto config = deathcomment::options();
        auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (auto phrase = engine().onDeath(context, config, now)) {
            deathcomment::showComment(*phrase, context, config);
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->ready = !useReplay && !dontCreateObjects;
        m_fields->cycle.resetBoundary(dead());
        m_fields->lastContext = contextNow();
        engine().resetLevel();
        deathcomment::hideComment();
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto candidate = eligible() && player && (player == m_player1 || player == m_player2);
        auto probe = m_anticheatSpike && object == m_anticheatSpike;
        auto context = candidate ? contextNow() : m_fields->lastContext;
        PlayLayer::destroyPlayer(player, object);
        // The caller may have set dead flags before this hook. The shared latch
        // deduplicates collision, dual and frame callbacks.
        if (candidate && probe) {
            if (dead()) m_fields->cycle.suppress();
            return;
        }
        if (candidate) reportDeath(context);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!eligible()) return;
        if (dead()) reportDeath(m_fields->lastContext);
        else {
            m_fields->cycle.alive();
            m_fields->lastContext = contextNow();
        }
    }

    void resetLevel() {
        ++m_fields->resetDepth;
        PlayLayer::resetLevel();
        --m_fields->resetDepth;
        if (m_fields->resetDepth == 0) {
            m_fields->cycle.resetBoundary(dead());
            if (m_fields->ready) m_fields->lastContext = contextNow();
        }
        // The screen banner stays visible during the next attempt.
    }

    void onQuit() {
        deathcomment::hideComment();
        PlayLayer::onQuit();
    }

    void onExit() {
        deathcomment::hideComment();
        PlayLayer::onExit();
    }
};

class $modify(CommentMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto sprite = ButtonSprite::create("Comments", "bigFont.fnt", "GJ_button_04.png", 0.65f);
        sprite->setScale(0.52f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(CommentMenuLayer::onComment));
        button->setID("comment-settings-button"_spr);
        if (auto menu = getChildByID("right-side-menu")) {
            menu->addChild(button);
            menu->updateLayout();
        } else {
            auto fallback = CCMenu::create();
            fallback->setID("comment-settings-menu"_spr);
            fallback->setPosition({CCDirector::get()->getWinSize().width - 49.f, 161.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }
    void onComment(CCObject*) { openCommentPopup(); }
};

class $modify(CommentPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto sprite = ButtonSprite::create("Comments", "bigFont.fnt", "GJ_button_04.png", 0.65f);
        sprite->setScale(0.52f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(CommentPauseLayer::onComment));
        button->setID("comment-settings-button"_spr);
        auto menu = CCMenu::create();
        menu->setID("comment-settings-menu"_spr);
        auto size = CCDirector::get()->getWinSize();
        // Session Recap's pause button is above this one; both mods can coexist.
        menu->setPosition({size.width - 49.f, size.height - 78.f});
        menu->addChild(button);
        addChild(menu, 100);
    }
    void onComment(CCObject*) { openCommentPopup(); }
};

$on_mod(Loaded) {
    auto mod = Mod::get();
    // Apply the requested visible roast preset once when upgrading from 1.0.
    if (!mod->getSavedValue<bool>("screen-roast-v110")) {
        mod->setSettingValue("enabled", true);
        mod->setSettingValue("tone", std::string("Roast"));
        mod->setSettingValue("position", std::string("Center"));
        mod->setSettingValue<std::int64_t>("every", 1);
        mod->setSettingValue("cooldown", 0.0);
        mod->setSettingValue("duration", 3.5);
        mod->setSavedValue("screen-roast-v110", true);
        if (auto result = mod->saveData(); !result) {
            log::warn("Death Commentator: could not save screen preset: {}", result.unwrapErr());
        }
    }
}
