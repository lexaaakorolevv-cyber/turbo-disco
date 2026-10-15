#include "Runtime.hpp"
#include "ui_geometry.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <algorithm>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>

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
    std::vector<CCMenuItemSpriteExtra*> m_primary, m_extra;
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
            m_primary.push_back(button);
        };
        auto row = [&](int i) { return geometry.firstRowY - i * geometry.rowSpacing; };
        add("Найти прохождение", {geometry.leftX, row(0)}, menu_selector(Menu::search));
        add("Остановить", {geometry.rightX, row(0)}, menu_selector(Menu::stopSearch));
        add("Сохранить макрос", {geometry.leftX, row(1)}, menu_selector(Menu::saveMacro));
        add("Загрузить макрос", {geometry.rightX, row(1)}, menu_selector(Menu::loadMacro));
        add("Воспроизвести", {geometry.leftX, row(2)}, menu_selector(Menu::playMacro));
        add("Дополнительно", {geometry.rightX, row(2)}, menu_selector(Menu::more));
        auto primary = std::move(m_primary);m_primary.clear();
        add("Записать вручную", {geometry.leftX,row(0)}, menu_selector(Menu::record));
        add("Взять управление", {geometry.rightX,row(0)}, menu_selector(Menu::assist));
        add("Вернуть боту", {geometry.leftX,row(1)}, menu_selector(Menu::giveBack));
        add("Показать / скрыть путь", {geometry.rightX,row(1)}, menu_selector(Menu::path));
        add("Назад", {geometry.leftX,row(2)}, menu_selector(Menu::back));
        m_extra=std::move(m_primary);m_primary=std::move(primary);
        for(auto b:m_extra){b->setVisible(false);b->setEnabled(false);}
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
    void pages(bool extra){for(auto b:m_primary){b->setVisible(!extra);b->setEnabled(!extra);}for(auto b:m_extra){b->setVisible(extra);b->setEnabled(extra);}}
    void more(CCObject*){pages(true);}
    void back(CCObject*){pages(false);}
    void assist(CCObject*){Runtime::get().takeControl();continueRun();}
    void giveBack(CCObject*){Runtime::get().returnToBot();continueRun();}
    void path(CCObject*){Runtime::get().togglePreview();}
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
    CCMenuItemSpriteExtra* m_help = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCSize m_viewport;
    void open(CCObject*) { showMenu(); }
    void stop(CCObject*) { Runtime::get().stop(); }
    void help(CCObject*){auto&r=Runtime::get();if(r.assisting())r.returnToBot();else r.takeControl();}
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
            m_help->setPosition({size.width - 43, size.height - 120});
            m_status->setPosition({(size.width - 85) / 2.f, size.height - 52});
        }
        auto& runtime=Runtime::get();
        m_draw->clear();
        auto layer=runtime.layer();
        if(runtime.showPreview()&&layer&&layer->m_player1&&layer->m_player1->getParent()){
            auto parent=layer->m_player1->getParent();
            auto screen=[&](Point p){return convertToNodeSpace(parent->convertToWorldSpace({static_cast<float>(p.x),static_cast<float>(p.y)}));};
            auto const& trace=runtime.forecast();
            ccColor4F green{.2f,.95f,.55f,.85f},red{1.f,.25f,.2f,.9f};
            for(std::size_t i=1;i<trace.path.size()&&i<160;i++)m_draw->drawSegment(screen(trace.path[i-1]),screen(trace.path[i]),.8f,green);
            if(trace.collision)m_draw->drawDot(screen(*trace.collision),3.f,red);
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
        hud->m_help=CCMenuItemSpriteExtra::create(ButtonSprite::create("HELP",.45f),hud,menu_selector(Hud::help));
        menu->addChild(hud->m_help);
        hud->m_draw=CCDrawNode::create();hud->addChild(hud->m_draw,-1);
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
