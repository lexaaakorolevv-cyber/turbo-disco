#include "BotPopup.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(AutobotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        // A separate menu avoids assuming Node IDs supplied by another mod.
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setID("bot-pause-menu"_spr);
        auto sprite = ButtonSprite::create(
            "Бот", "bot.fnt"_spr, "GJ_button_01.png", .7f);
        auto button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(AutobotPauseLayer::onBot));
        button->setID("bot-pause-button"_spr);
        auto size = CCDirector::get()->getWinSize();
        button->setPosition({size.width - 48.f, size.height - 36.f});
        menu->addChild(button);
        addChild(menu, 100);
    }

    void onBot(CCObject*) {
        if (auto popup = autobot::BotPopup::create()) popup->show();
    }
};
