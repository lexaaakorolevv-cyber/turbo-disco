#include "Runtime.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/NineSlice.hpp>

using namespace geode::prelude;

namespace rf {
namespace {
char const* systemFont() {
#ifdef GEODE_IS_ANDROID
    return "sans-serif";
#else
    return "Arial";
#endif
}
CCLabelTTF* label(std::string const& text, float size, CCSize bounds = {}) {
    if (bounds.width > 0)
        return CCLabelTTF::create(text.c_str(), systemFont(), size, bounds, kCCTextAlignmentCenter);
    return CCLabelTTF::create(text.c_str(), systemFont(), size);
}
class Menu final : public Popup {
    CCLabelTTF* m_info = nullptr;
    Ref<PauseLayer> m_pause;
    Ref<EndLevelLayer> m_end;
    bool initMenu(PauseLayer* pause, EndLevelLayer* end) {
        if (!Popup::init(420, 300)) return false;
        m_pause = pause;
        m_end = end;
        auto title = label("Route Finder", 20);
        title->setPosition({210, 275});
        m_mainLayer->addChild(title);
        m_buttonMenu->setPosition({0, 0});
        auto add = [&](char const* caption, CCPoint pos, SEL_MenuHandler callback) {
            auto bg = NineSlice::create("GJ_button_01.png");
            bg->setContentSize({184, 30});
            auto text = label(caption, 13);
            text->setPosition({92, 15});
            bg->addChild(text);
            auto button = CCMenuItemSpriteExtra::create(bg, this, callback);
            button->setPosition(pos);
            m_buttonMenu->addChild(button);
        };
        add("Найти прохождение", {110, 230}, menu_selector(Menu::search));
        add("Остановить", {310, 230}, menu_selector(Menu::stopSearch));
        add("Сохранить макрос", {110, 188}, menu_selector(Menu::saveMacro));
        add("Загрузить макрос", {310, 188}, menu_selector(Menu::loadMacro));
        add("Воспроизвести", {110, 146}, menu_selector(Menu::playMacro));
        add("Записать вручную", {310, 146}, menu_selector(Menu::record));
        m_info = label("", 12, {380, 95});
        m_info->setPosition({210, 76});
        m_mainLayer->addChild(m_info);
        auto hint = label("Лимиты задаются в настройках мода Geode", 10);
        hint->setPosition({210, 17});
        m_mainLayer->addChild(hint);
        Runtime::get().setMenuOpen(true);
        scheduleUpdate();
        update(0);
        return true;
    }
    void update(float) override {
        Runtime::get().poll();
        m_info->setString(Runtime::get().summary().c_str());
    }
    void onClose(CCObject* sender) override {
        Runtime::get().setMenuOpen(false);
        Popup::onClose(sender);
    }
    void continueRun() {
        if (!Runtime::get().active()) return;
        Runtime::get().resumeOverlay(m_pause.data(), m_end.data());
        onClose(nullptr);
    }
    void search(CCObject*) { Runtime::get().startSearch(); continueRun(); }
    void stopSearch(CCObject*) { Runtime::get().stop(); }
    void saveMacro(CCObject*) { Runtime::get().save(); }
    void loadMacro(CCObject*) { Runtime::get().load(); }
    void playMacro(CCObject*) { Runtime::get().play(); continueRun(); }
    void record(CCObject*) { Runtime::get().startRecording(); continueRun(); }
public:
    static Menu* create(PauseLayer* pause, EndLevelLayer* end) {
        auto ret = new Menu;
        if (ret->initMenu(pause, end)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }
};

class Hud final : public CCNode {
    CCLabelTTF* m_status = nullptr;
    void open(CCObject*) { showMenu(); }
    void stop(CCObject*) { Runtime::get().stop(); }
    void update(float) override {
        // poll() also runs while the simulation is frozen in the custom menu.
        Runtime::get().poll();
        m_status->setString(Runtime::get().summary().c_str());
    }
public:
    static Hud* create() {
        auto hud = new Hud;
        if (!hud->init()) { delete hud; return nullptr; }
        hud->autorelease();
        auto size = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({size.width - 45, size.height - 48});
        hud->addChild(menu);
        auto bot = ButtonSprite::create("BOT", .6f);
        auto open = CCMenuItemSpriteExtra::create(bot, hud, menu_selector(Hud::open));
        menu->addChild(open);
        auto stop = CCMenuItemSpriteExtra::create(ButtonSprite::create("STOP", .45f), hud, menu_selector(Hud::stop));
        stop->setPosition({0, -33});
        menu->addChild(stop);
        hud->m_status = label("", 9, {size.width - 180, 50});
        hud->m_status->setPosition({size.width / 2, size.height - 45});
        hud->addChild(hud->m_status);
        hud->scheduleUpdate();
        return hud;
    }
};
}
void showMenu(PauseLayer* pause, EndLevelLayer* end) {
    if (!Runtime::get().frozen()) if (auto menu = Menu::create(pause, end)) menu->show();
}
void attachHud(PlayLayer* layer) {
    if (auto hud = Hud::create()) { hud->setID("routefinder-hud"); layer->addChild(hud, 10000); }
}
}
