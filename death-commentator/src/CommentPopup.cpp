#include "CommentPopup.hpp"
#include "CommentSettings.hpp"
#include "CommentOverlay.hpp"
#include <chrono>

using namespace geode::prelude;

namespace {

CCLabelBMFont* text(CCNode* parent, std::string const& value, float scale,
                    CCPoint position, float width = 350.f, bool wrap = false) {
    auto node = wrap
        ? CCLabelBMFont::create(value.c_str(), "comment-font.fnt"_spr, width, kCCTextAlignmentCenter)
        : CCLabelBMFont::create(value.c_str(), "comment-font.fnt"_spr);
    if (!node) node = CCLabelBMFont::create("Font unavailable", "chatFont.fnt");
    node->setPosition(position);
    node->setScale(scale);
    if (!wrap) node->limitLabelWidth(width, scale, 0.25f);
    parent->addChild(node);
    return node;
}

} // namespace

CommentPopup* CommentPopup::create() {
    auto popup = new CommentPopup;
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool CommentPopup::init() {
    if (!Popup::init(390.f, 280.f)) return false;
    m_noElasticity = true;
    setTitle("Комментатор смертей", "comment-font.fnt"_spr, 0.68f, 22.f);
    text(m_mainLayer, "Выбери реакцию на свои фейлы", 0.48f, {195.f, 231.f});

    auto button = [this](std::string const& caption, SEL_MenuHandler callback,
                          CCPoint position, float width, CCLabelBMFont** value = nullptr) {
        auto background = CCLayerColor::create({42, 56, 77, 240}, width, 32.f);
        background->setTouchEnabled(false);
        background->ignoreAnchorPointForPosition(false);
        auto label = text(background, caption, 0.47f, {width / 2.f, 16.f}, width - 10.f);
        if (value) *value = label;
        auto item = CCMenuItemSpriteExtra::create(background, this, callback);
        item->setPosition(position);
        m_buttonMenu->addChild(item);
        return item;
    };
    m_kindButton = button("Добрые", menu_selector(CommentPopup::chooseKind), {75.f, 198.f}, 110.f);
    m_neutralButton = button("Нейтральные", menu_selector(CommentPopup::chooseNeutral), {195.f, 198.f}, 110.f);
    m_roastButton = button("Обидные", menu_selector(CommentPopup::chooseRoast), {315.f, 198.f}, 110.f);

    auto panel = CCLayerColor::create({15, 20, 35, 160}, 350.f, 74.f);
    panel->setPosition({20.f, 98.f});
    m_mainLayer->addChild(panel);
    m_modeLabel = text(m_mainLayer, "", 0.45f, {195.f, 154.f});
    text(m_mainLayer, "Реплики появляются на игровом экране", 0.44f, {195.f, 125.f}, 326.f);

    button("", menu_selector(CommentPopup::toggleEnabled), {108.f, 74.f}, 166.f, &m_enabledLabel);
    button("", menu_selector(CommentPopup::changeFrequency), {282.f, 74.f}, 166.f, &m_frequencyLabel);
    button("На экран", menu_selector(CommentPopup::preview), {79.f, 32.f}, 110.f);
    button("", menu_selector(CommentPopup::changeCooldown), {206.f, 32.f}, 128.f, &m_cooldownLabel);
    button("", menu_selector(CommentPopup::changeLanguage), {323.f, 32.f}, 88.f, &m_languageLabel);
    refresh();
    return true;
}

void CommentPopup::refresh() {
    auto mod = Mod::get();
    auto config = deathcomment::options();
    std::string description;
    if (config.tone == deathcomment::Tone::Kind) description = "Поддержка: ты сможешь!";
    else if (config.tone == deathcomment::Tone::Roast) description = "Жёсткие насмешки с матом";
    else description = "Спокойные комментарии без оценки";
    m_modeLabel->setString(description.c_str());
    m_modeLabel->setColor(deathcomment::toneColor(config.tone));

    auto highlight = [](CCMenuItemSpriteExtra* button, bool selected, deathcomment::Tone tone) {
        auto background = static_cast<CCLayerColor*>(button->getNormalImage());
        background->setColor(selected ? deathcomment::toneColor(tone) : ccColor3B{42, 56, 77});
        for (auto child : CCArrayExt<CCNode*>(background->getChildren())) {
            if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
                label->setColor(selected ? ccColor3B{15, 25, 40} : ccColor3B{255, 255, 255});
            }
        }
    };
    highlight(m_kindButton, config.tone == deathcomment::Tone::Kind, deathcomment::Tone::Kind);
    highlight(m_neutralButton, config.tone == deathcomment::Tone::Neutral, deathcomment::Tone::Neutral);
    highlight(m_roastButton, config.tone == deathcomment::Tone::Roast, deathcomment::Tone::Roast);

    m_enabledLabel->setString(mod->getSettingValue<bool>("enabled") ? "Включён" : "Выключен");
    auto frequency = config.every == 1 ? std::string("Каждую смерть") : fmt::format("Каждую {}-ю", config.every);
    m_frequencyLabel->setString(frequency.c_str());
    m_frequencyLabel->limitLabelWidth(156.f, 0.47f, 0.25f);
    m_cooldownLabel->setString(fmt::format("Интервал: {:g} с", config.cooldownSeconds).c_str());
    m_cooldownLabel->limitLabelWidth(118.f, 0.47f, 0.25f);
    m_languageLabel->setString(config.language == deathcomment::Language::Russian ? "RU" : "EN");

}

void CommentPopup::choose(deathcomment::Tone tone) {
    Mod::get()->setSettingValue("tone", std::string(deathcomment::toneKey(tone)));
    refresh();
}
void CommentPopup::chooseKind(CCObject*) { choose(deathcomment::Tone::Kind); }
void CommentPopup::chooseNeutral(CCObject*) { choose(deathcomment::Tone::Neutral); }
void CommentPopup::chooseRoast(CCObject*) { choose(deathcomment::Tone::Roast); }

void CommentPopup::toggleEnabled(CCObject*) {
    auto mod = Mod::get();
    mod->setSettingValue("enabled", !mod->getSettingValue<bool>("enabled"));
    refresh();
}

void CommentPopup::changeFrequency(CCObject*) {
    auto mod = Mod::get();
    auto current = mod->getSettingValue<std::int64_t>("every");
    auto next = current == 1 ? 3 : current == 3 ? 5 : current == 5 ? 10 : 1;
    mod->setSettingValue<std::int64_t>("every", next);
    refresh();
}

void CommentPopup::changeCooldown(CCObject*) {
    auto mod = Mod::get();
    auto current = mod->getSettingValue<double>("cooldown");
    auto next = current < 1.0 ? 3.0 : current < 4.0 ? 6.0 : current < 7.0 ? 10.0 : 0.0;
    mod->setSettingValue("cooldown", next);
    refresh();
}

void CommentPopup::changeLanguage(CCObject*) {
    auto mod = Mod::get();
    auto current = mod->getSettingValue<std::string>("language");
    mod->setSettingValue("language", std::string(current == "Russian" ? "English" : "Russian"));
    refresh();
}

void CommentPopup::preview(CCObject*) {
    auto config = deathcomment::options();
    config.every = 1;
    config.cooldownSeconds = 0;
    deathcomment::Context example;
    example.percent = 52;
    example.attempt = 12;
    auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (auto phrase = m_previewEngine.onDeath(example, config, now)) {
        deathcomment::showComment(*phrase, example, config);
    }
}

void CommentPopup::onClose(CCObject* sender) {
    if (auto result = Mod::get()->saveData(); !result) {
        log::warn("Death Commentator: could not save settings: {}", result.unwrapErr());
    }
    Popup::onClose(sender);
}
