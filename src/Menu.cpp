#include "Runtime.hpp"
#include "ui_geometry.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <algorithm>

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
    float m_statusHeight = 0;
    bool initMenu(PauseLayer* pause, EndLevelLayer* end) {
        auto viewport = CCDirector::sharedDirector()->getWinSize();
        auto geometry = panelGeometry(viewport.width, viewport.height);
        if (!Popup::init(geometry.width, geometry.height)) return false;
        m_pause = pause;
        m_end = end;
        auto title = label(fmt::format("Route Finder {}", ModVersion), 18);
        title->setPosition({geometry.width / 2.f, geometry.titleY});
        m_mainLayer->addChild(title);
        // Popup's CopySizeLayout centers this menu with anchor (0.5, 0.5).
        // Its resulting local origin is already (0, 0). Moving it to zero
        // shifts every button by half the popup width/height.
        m_mainLayer->updateLayout();
        auto add = [&](char const* caption, CCPoint pos, SEL_MenuHandler callback) {
            auto bg = NineSlice::create("GJ_button_01.png");
            bg->setContentSize({geometry.buttonWidth, geometry.buttonHeight});
            auto text = label(caption, 13);
            text->setScale(std::min(1.f, (geometry.buttonWidth - 12.f) /
                std::max(1.f, text->getContentSize().width)));
            text->setPosition({geometry.buttonWidth / 2.f, geometry.buttonHeight / 2.f});
            bg->addChild(text);
            auto button = CCMenuItemSpriteExtra::create(bg, this, callback);
            button->setPosition(pos);
            m_buttonMenu->addChild(button);
        };
        auto row = [&](int i) { return geometry.firstRowY - i * geometry.rowSpacing; };
        add("Найти прохождение", {geometry.leftX, row(0)}, menu_selector(Menu::search));
        add("Остановить", {geometry.rightX, row(0)}, menu_selector(Menu::stopSearch));
        add("Сохранить макрос", {geometry.leftX, row(1)}, menu_selector(Menu::saveMacro));
        add("Загрузить макрос", {geometry.rightX, row(1)}, menu_selector(Menu::loadMacro));
        add("Воспроизвести", {geometry.leftX, row(2)}, menu_selector(Menu::playMacro));
        add("Записать вручную", {geometry.rightX, row(2)}, menu_selector(Menu::record));
        m_statusHeight = geometry.statusHeight;
        m_info = label("", 11, {geometry.width - 32.f, 0});
        m_info->setPosition({geometry.width / 2.f, geometry.statusY});
        m_mainLayer->addChild(m_info);
        auto hint = label(fmt::format("Цель: GD {} | Geode {}", GEODE_GD_VERSION_STRING,
            Loader::get()->getVersion().toVString()), 9);
        hint->setScale(std::min(1.f, (geometry.width - 20.f) / std::max(1.f, hint->getContentSize().width)));
        hint->setPosition({geometry.width / 2.f, 16});
        m_mainLayer->addChild(hint);
        Runtime::get().setMenuOpen(true);
        scheduleUpdate();
        update(0);
        return true;
    }
    void update(float) override {
        Runtime::get().poll();
        m_info->setString(Runtime::get().summary().c_str());
        m_info->setScale(std::min(1.f, m_statusHeight / std::max(1.f, m_info->getContentSize().height)));
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
    CCMenu* m_menu = nullptr;
    CCMenuItemSpriteExtra* m_open = nullptr;
    CCMenuItemSpriteExtra* m_stop = nullptr;
    CCSize m_viewport;
    void open(CCObject*) { showMenu(); }
    void stop(CCObject*) { Runtime::get().stop(); }
    void update(float) override {
        // poll() also runs while the simulation is frozen in the custom menu.
        Runtime::get().poll();
        auto size = CCDirector::sharedDirector()->getWinSize();
        if (size.width != m_viewport.width || size.height != m_viewport.height) {
            m_viewport = size;
            setContentSize(size);
            m_menu->setContentSize(size);
            m_open->setPosition({size.width - 43, size.height - 60});
            m_stop->setPosition({size.width - 43, size.height - 90});
            m_status->setPosition({(size.width - 85) / 2.f, size.height - 52});
        }
        m_status->setString(Runtime::get().hudSummary().c_str());
        m_status->setScale(std::min(1.f, (size.width - 160.f) / std::max(1.f, m_status->getContentSize().width)));
    }
public:
    static Hud* create() {
        auto hud = new Hud;
        if (!hud->init()) { delete hud; return nullptr; }
        hud->autorelease();
        auto menu = CCMenu::create();
        hud->setAnchorPoint({0, 0});
        hud->setPosition({0, 0});
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0, 0});
        menu->setPosition({0, 0});
        hud->m_menu = menu;
        hud->addChild(menu);
        auto bot = ButtonSprite::create("BOT", .5f);
        auto open = CCMenuItemSpriteExtra::create(bot, hud, menu_selector(Hud::open));
        menu->addChild(open);
        hud->m_open = open;
        auto stop = CCMenuItemSpriteExtra::create(ButtonSprite::create("STOP", .45f), hud, menu_selector(Hud::stop));
        hud->m_stop = stop;
        menu->addChild(stop);
        hud->m_status = label("", 9);
        hud->addChild(hud->m_status);
        hud->scheduleUpdate();
        hud->update(0);
        return hud;
    }
};
}
void showMenu(PauseLayer* pause, EndLevelLayer* end) {
    Runtime::get().syncActiveLayer();
    if (!Runtime::get().frozen()) if (auto menu = Menu::create(pause, end)) menu->show();
}
void attachHud(PlayLayer* layer) {
    // UILayer has screen coordinates; PlayLayer also contains world/camera
    // transforms and is not a reliable parent for a fixed HUD.
    auto parent = layer->m_uiLayer;
    if (!parent || parent->getChildByID("routefinder-hud")) return;
    if (auto hud = Hud::create()) { hud->setID("routefinder-hud"); parent->addChild(hud, 10000); }
}
}
