#include "native.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace v2 {
namespace {
constexpr ccColor3B Ink{28, 38, 55}, Muted{100, 113, 132};
CCLabelTTF *text(std::string const &s, float size, CCSize bounds = {}) {
    auto t = bounds.width > 0 ? CCLabelTTF::create(s.c_str(), "Arial", size, bounds, kCCTextAlignmentLeft)
                              : CCLabelTTF::create(s.c_str(), "Arial", size);
    t->setColor(Ink);
    return t;
}
CCNode *card(float w, float h, ccColor4F fill) {
    auto n = CCNode::create();
    n->setContentSize({w, h});
    auto d = CCDrawNode::create();
    CCPoint p[24];
    unsigned count = 0;
    float radius = 7;
    for (int c = 0; c < 4; c++) {
        float x = (c == 0 || c == 3) ? w - radius : radius, y = c < 2 ? h - radius : radius;
        for (int i = 0; i < 6; i++) {
            double a = (90 * c + 18 * i) * 3.141592653589793 / 180;
            p[count++] = CCPoint{x + radius * static_cast<float>(std::cos(a)),
                                 y + radius * static_cast<float>(std::sin(a))};
        }
    }
    d->drawPolygon(p, count, fill, .5f, {.84f, .87f, .92f, 1});
    n->addChild(d);
    return n;
}
class Panel final : public Popup {
    Ref<PauseLayer> m_pause;
    CCLabelTTF *m_status = nullptr, *m_metrics = nullptr;
    float m_elapsed = 1;
    void button(char const *caption, CCPoint position, SEL_MenuHandler method, bool primary = false) {
        auto bg = card(202, 34, primary ? ccColor4F{.16f, .39f, .93f, 1} : ccColor4F{.96f, .97f, .985f, 1});
        auto label = text(caption, 12);
        if (primary)
            label->setColor({255, 255, 255});
        label->setPosition({101, 17});
        label->setScale(std::min(1.f, 184.f / std::max(1.f, label->getContentSize().width)));
        bg->addChild(label);
        auto b = CCMenuItemSpriteExtra::create(bg, this, method);
        b->setPosition(position);
        m_buttonMenu->addChild(b);
    }
    bool setup(PauseLayer *pause) {
        if (!Popup::init(460, 314))
            return false;
        m_pause = pause;
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);
        m_mainLayer->setLayout(nullptr, false);
        m_buttonMenu->setLayout(nullptr, false);
        m_buttonMenu->ignoreAnchorPointForPosition(false);
        m_buttonMenu->setAnchorPoint({0, 0});
        m_buttonMenu->setPosition({0, 0});
        m_buttonMenu->setContentSize({460, 314});
        auto size = CCDirector::sharedDirector()->getWinSize();
        m_mainLayer->setScale(std::min({1.f, (size.width - 20) / 460, (size.height - 20) / 314}));
        auto skin = card(460, 314, {1, 1, 1, 1});
        m_mainLayer->addChild(skin, -1);
        auto title = text("Route Finder 2", 20);
        title->setAnchorPoint({0, .5f});
        title->setPosition({24, 286});
        m_mainLayer->addChild(title);
        auto version = text("Новая архитектура · 2.0.0", 9);
        version->setColor(Muted);
        version->setAnchorPoint({1, .5f});
        version->setPosition({436, 287});
        m_mainLayer->addChild(version);
        m_metrics = text("", 13);
        m_metrics->setPosition({230, 252});
        m_mainLayer->addChild(m_metrics);
        button("Найти прохождение", {126, 215}, menu_selector(Panel::search), true);
        button("Остановить", {334, 215}, menu_selector(Panel::stop));
        button("Записать вручную", {126, 174}, menu_selector(Panel::record));
        button("Воспроизвести", {334, 174}, menu_selector(Panel::replay));
        button("Сохранить макрос", {126, 133}, menu_selector(Panel::save));
        button("Загрузить макрос", {334, 133}, menu_selector(Panel::load));
        button("Analyze", {126, 92}, menu_selector(Panel::analyze));
        button("Лимиты поиска", {334, 92}, menu_selector(Panel::settings));
        m_status = text("", 10, {412, 0});
        m_status->setColor(Muted);
        m_status->setPosition({230, 43});
        m_mainLayer->addChild(m_status);
        auto closeText = text("×", 22);
        auto close = CCMenuItemSpriteExtra::create(closeText, this, menu_selector(Panel::close));
        close->setPosition({443, 306});
        m_buttonMenu->addChild(close);
        Native::get().menu(true);
        scheduleUpdate();
        update(0);
        return true;
    }
    void update(float dt) override {
        m_elapsed += dt;
        if (m_elapsed < .1f)
            return;
        m_elapsed = 0;
        auto &n = Native::get();
        n.service();
        auto metrics = fmt::format("Попытки: {}     Лучший: {:.2f}%", n.engine().attempts(), n.best());
        if (metrics != m_metrics->getString())
            m_metrics->setString(metrics.c_str());
        auto status = n.status();
        if (status != m_status->getString())
            m_status->setString(status.c_str());
        m_status->setScale(std::min(1.f, 48.f / std::max(1.f, m_status->getContentSize().height)));
    }
    void onClose(CCObject *s) override {
        Native::get().menu(false);
        Popup::onClose(s);
    }
    void close(CCObject *s) {
        onClose(s);
    }
    void resume() {
        if (!Native::get().active())
            return;
        Native::get().menu(false);
        if (m_pause)
            m_pause->onResume(nullptr);
        Popup::onClose(nullptr);
    }
    void search(CCObject *) {
        Native::get().search();
        resume();
    }
    void record(CCObject *) {
        Native::get().record();
        resume();
    }
    void replay(CCObject *) {
        Native::get().replay();
        resume();
    }
    void stop(CCObject *) {
        Native::get().stop();
        m_elapsed = 1;
    }
    void save(CCObject *) {
        Native::get().save();
        m_elapsed = 1;
    }
    void load(CCObject *) {
        Native::get().load();
        m_elapsed = 1;
    }
    void analyze(CCObject *) {
        Native::get().analyze();
    }
    void settings(CCObject *) {
        openSettingsPopup(Mod::get());
    }

  public:
    static Panel *create(PauseLayer *p) {
        auto n = new Panel;
        if (n->setup(p)) {
            n->autorelease();
            return n;
        }
        delete n;
        return nullptr;
    }
};
class Hud final : public CCNode {
    CCLabelTTF *m_label = nullptr;
    float m_elapsed = 1;
    void update(float dt) override {
        m_elapsed += dt;
        if (m_elapsed < .1f)
            return;
        m_elapsed = 0;
        auto size = CCDirector::sharedDirector()->getWinSize();
        setPosition({size.width - 22, size.height - 52});
        auto value = fmt::format("{:.2f}%", Native::get().best());
        if (value != m_label->getString())
            m_label->setString(value.c_str());
    }

  public:
    static Hud *create() {
        auto h = new Hud;
        if (!h->init()) {
            delete h;
            return nullptr;
        }
        h->autorelease();
        h->m_label = text("0.00%", 14);
        h->m_label->setColor({255, 255, 255});
        h->m_label->setAnchorPoint({1, .5f});
        h->addChild(h->m_label);
        h->scheduleUpdate();
        h->update(0);
        return h;
    }
};
} // namespace
void showMenu(PauseLayer *p) {
    if (!p || Native::get().menu())
        return;
    if (auto layer = PlayLayer::get())
        Native::get().bind(layer);
    if (auto popup = Panel::create(p))
        popup->show();
}
void attachHud(PlayLayer *l) {
    auto parent = l->m_uiLayer;
    if (parent && !parent->getChildByID("routefinder-v2-hud"))
        if (auto h = Hud::create()) {
            h->setID("routefinder-v2-hud");
            parent->addChild(h, 10000);
        }
}
} // namespace v2
