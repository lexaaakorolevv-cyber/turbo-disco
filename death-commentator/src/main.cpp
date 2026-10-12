#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "CommentEngine.hpp"
#include "CommentOverlay.hpp"
#include "CommentPopup.hpp"
#include "CommentSettings.hpp"
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
        bool reported = false;
        unsigned resetDepth = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->ready = !useReplay && !dontCreateObjects;
        m_fields->reported = false;
        engine().resetLevel();
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& state = *m_fields.self();
        auto gamePlayer = player && (player == m_player1 || player == m_player2);
        auto deadBefore = m_playerDied || (m_player1 && m_player1->m_isDead) ||
                          (player && player->m_isDead);
        // A checkpoint restore may revive the player without a full reset.
        // A live player starts a fresh death cycle; an inactive second player
        // cannot suppress comments for the active player.
        if (!deadBefore && state.resetDepth == 0) state.reported = false;
        auto candidate = state.ready && state.resetDepth == 0 && !state.reported && gamePlayer &&
            !deadBefore && !m_hasCompletedLevel && !m_isTestMode && !m_useReplay &&
            !(m_anticheatSpike && object == m_anticheatSpike);
        deathcomment::Context context;
        context.platformer = m_isPlatformer;
        context.practice = m_isPracticeMode;
        context.attempt = m_attempts;
        if (candidate && !context.platformer) {
            auto progress = getCurrentPercent();
            if (std::isfinite(progress)) context.percent = static_cast<int>(std::clamp(progress, 0.f, 100.f));
        }

        PlayLayer::destroyPlayer(player, object);
        auto deadAfter = m_playerDied || (m_player1 && m_player1->m_isDead) ||
                         (player && player->m_isDead);
        // Collision callbacks intercepted by noclip are not actual deaths.
        // In dual mode only the first confirmed death creates a comment.
        if (!candidate || !deadAfter || state.reported) return;
        state.reported = true;
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return;
        if (context.practice && !mod->getSettingValue<bool>("practice")) return;
        auto config = deathcomment::options();
        auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (auto phrase = engine().onDeath(context, config, now)) {
            deathcomment::showComment(this, *phrase, context, config);
        }
    }

    void resetLevel() {
        auto& state = *m_fields.self();
        ++state.resetDepth;
        PlayLayer::resetLevel();
        --state.resetDepth;
        if (state.resetDepth == 0) state.reported = false;
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
