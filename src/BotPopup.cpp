#include "BotPopup.hpp"
#include "Controller.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

using namespace geode::prelude;

namespace autobot {

CCLabelBMFont* botLabel(std::string const& text, float width, float scale) {
    auto label = CCLabelBMFont::create(text.c_str(), "bot.fnt"_spr);
    label->limitLabelWidth(width, scale, .2f);
    return label;
}

bool BotPopup::init() {
    if (!Popup::init(380.f, 290.f)) return false;
    setID("bot-popup"_spr);
    setTitle("Бот", "bot.fnt"_spr, .85f, 22.f);

    m_enabledToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(BotPopup::onToggle), .7f);
    m_enabledToggle->setPosition({64.f, 232.f});
    m_enabledToggle->setID("enabled-toggle");
    m_buttonMenu->addChild(m_enabledToggle);

    auto enabledLabel = botLabel("Включить бота", 244.f, .65f);
    enabledLabel->setAnchorPoint({0.f, .5f});
    enabledLabel->setPosition({89.f, 232.f});
    m_mainLayer->addChild(enabledLabel);

    m_attemptLabel = botLabel("", 325.f);
    m_attemptLabel->setPosition({190.f, 202.f});
    m_mainLayer->addChild(m_attemptLabel);
    m_bestLabel = botLabel("", 325.f);
    m_bestLabel->setPosition({190.f, 180.f});
    m_mainLayer->addChild(m_bestLabel);
    m_statusLabel = botLabel("", 333.f, .48f);
    m_statusLabel->setPosition({190.f, 153.f});
    m_statusLabel->setColor({255, 230, 130});
    m_mainLayer->addChild(m_statusLabel);

    auto trainSprite = ButtonSprite::create(
        "Начать обучение", 270, true, "bot.fnt"_spr,
        "GJ_button_01.png", 34.f, .6f);
    m_trainButton = CCMenuItemSpriteExtra::create(
        trainSprite, this, menu_selector(BotPopup::onTrain));
    m_trainButton->setPosition({190.f, 113.f});
    m_trainButton->setID("start-training");
    m_buttonMenu->addChild(m_trainButton);

    auto resetSprite = ButtonSprite::create(
        "Сбросить прогресс", 270, true, "bot.fnt"_spr,
        "GJ_button_06.png", 34.f, .6f);
    m_resetButton = CCMenuItemSpriteExtra::create(
        resetSprite, this, menu_selector(BotPopup::onReset));
    m_resetButton->setPosition({190.f, 73.f});
    m_resetButton->setID("reset-training");
    m_buttonMenu->addChild(m_resetButton);

    auto safety = botLabel("Результаты бота не засчитываются", 343.f, .42f);
    safety->setPosition({190.f, 38.f});
    safety->setColor({255, 190, 100});
    m_mainLayer->addChild(safety);
    auto limitation = botLabel("Прототип: куб. Защита до выхода из уровня.", 347.f, .36f);
    limitation->setPosition({190.f, 19.f});
    m_mainLayer->addChild(limitation);

    refresh(0.f);
    schedule(schedule_selector(BotPopup::refresh), .2f);
    return true;
}

void BotPopup::refresh(float) {
    auto& controller = Controller::get();
    m_enabledToggle->toggle(controller.enabled());
    auto attached = controller.attached();
    m_enabledToggle->setEnabled(attached);
    m_trainButton->setEnabled(attached);
    m_resetButton->setEnabled(attached);
    m_attemptLabel->setString(fmt::format("Попытка: {}", controller.attempts()).c_str());
    m_attemptLabel->limitLabelWidth(325.f, .55f, .2f);
    m_bestLabel->setString(fmt::format("Лучший результат: {:.2f}%", controller.bestPercent()).c_str());
    m_bestLabel->limitLabelWidth(325.f, .55f, .2f);
    m_statusLabel->setString(("Статус: " + controller.status()).c_str());
    m_statusLabel->limitLabelWidth(333.f, .48f, .2f);
}

void BotPopup::onToggle(CCObject*) {
    // CCMenuItemToggler invokes its callback before updating its own image.
    // Read controller state, release the actual game input synchronously, then
    // let refresh synchronize the UI after the current touch has completed.
    auto& controller = Controller::get();
    controller.setEnabled(!controller.enabled());
}

void BotPopup::onTrain(CCObject*) {
    Controller::get().startTraining();
    refresh(0.f);
}

void BotPopup::onReset(CCObject*) {
    Controller::get().resetProgress();
    refresh(0.f);
}

void BotPopup::onExit() {
    unschedule(schedule_selector(BotPopup::refresh));
    Popup::onExit();
}

BotPopup* BotPopup::create() {
    auto popup = new BotPopup();
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

} // namespace autobot
