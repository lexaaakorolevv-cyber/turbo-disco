#include "CommentOverlay.hpp"
#include "CommentSettings.hpp"
#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <sstream>
#include <string_view>

using namespace geode::prelude;

namespace {

float measuredWidth(std::string_view text, CCBMFontConfiguration* font, float scale) {
    if (!font) return 0;
    float width = 0;
    for (std::size_t i = 0; i < text.size();) {
        auto first = static_cast<unsigned char>(text[i++]);
        unsigned int code = first;
        unsigned tail = 0;
        if (first >= 0xf0) { code = first & 7; tail = 3; }
        else if (first >= 0xe0) { code = first & 15; tail = 2; }
        else if (first >= 0xc0) { code = first & 31; tail = 1; }
        if (i + tail > text.size()) break;
        while (tail--) code = (code << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        cocos2d::tCCFontDefHashElement* glyph = nullptr;
        HASH_FIND_INT(font->m_pFontDefDictionary, &code, glyph);
        if (glyph) width += glyph->fontDef.xAdvance * scale;
    }
    return width;
}

std::string wrappedText(std::string const& text, CCBMFontConfiguration* font,
                        float scale, float maxWidth) {
    std::istringstream words(text);
    std::string word, line, result;
    while (words >> word) {
        auto candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measuredWidth(candidate, font, scale) > maxWidth) {
            if (!result.empty()) result += '\n';
            result += line;
            line = word;
        } else line = std::move(candidate);
    }
    if (!line.empty()) {
        if (!result.empty()) result += '\n';
        result += line;
    }
    return result;
}

} // namespace

namespace deathcomment {

void hideComment() {
    if (auto old = OverlayManager::get()->getChildByID("comment-bubble"_spr)) {
        old->removeFromParentAndCleanup(true);
    }
}

void showComment(std::string const& text, Context const& context, Options const& config) {
    if (text.empty()) return;
    auto size = CCDirector::get()->getWinSize();
    auto width = std::min(500.f, size.width - 24.f);
    if (width < 80.f) return;
    // Explicit newlines avoid scale-dependent native wrap behavior.
    auto label = CCLabelBMFont::create("", "comment-font.fnt"_spr);
    if (!label || !label->getConfiguration()) {
        log::warn("Death Commentator: screen font could not be loaded");
        return;
    }
    float textScale = 0.85f;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        auto advance = measuredWidth(word, label->getConfiguration(), 1.f);
        if (advance > 0) textScale = std::min(textScale, (width - 36.f) / advance);
    }
    auto wrapped = wrappedText(text, label->getConfiguration(), textScale, width - 36.f);
    label->setAlignment(kCCTextAlignmentCenter);
    label->setString(wrapped.c_str());
    label->setScale(textScale);
    auto height = std::max(98.f, label->getContentSize().height * textScale + 48.f);

    auto bubble = CCNodeRGBA::create();
    bubble->setID("comment-bubble"_spr);
    bubble->setContentSize({width, height});
    bubble->setCascadeOpacityEnabled(true);
    auto position = Mod::get()->getSettingValue<std::string>("position");
    auto y = (size.height - height) / 2.f;
    if (position == "Bottom") y = 28.f;
    else if (position == "Top") y = size.height - height - 40.f;
    bubble->setPosition({(size.width - width) / 2.f, std::max(4.f, y)});
    auto background = CCLayerColor::create({13, 16, 26, 235}, width, height);
    background->setTouchEnabled(false);
    bubble->addChild(background);

    auto accent = toneColor(config.tone);
    auto line = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, width, 4.f);
    line->setTouchEnabled(false);
    line->setPosition({0.f, height - 4.f});
    bubble->addChild(line);
    auto title = toneName(config.tone, config.language);
    if (!context.platformer) title += fmt::format(" | {}%", context.percent);
    auto titleLabel = CCLabelBMFont::create(title.c_str(), "comment-font.fnt"_spr);
    titleLabel->setColor(accent);
    titleLabel->setPosition({width / 2.f, height - 19.f});
    titleLabel->limitLabelWidth(width - 24.f, 0.48f, 0.30f);
    bubble->addChild(titleLabel);
    label->setPosition({width / 2.f, (height - 30.f) / 2.f});
    bubble->addChild(label);

    hideComment();
    // This viewport overlay is drawn after the running scene.
    OverlayManager::get()->addChild(bubble, 10000);
    auto duration = static_cast<float>(Mod::get()->getSettingValue<double>("duration"));
    duration = std::clamp(duration, 1.f, 6.f);
    // Immediately opaque even during very fast chains of deaths.
    bubble->setOpacity(255);
    bubble->runAction(CCSequence::create(
        CCDelayTime::create(duration - 0.25f),
        CCFadeTo::create(0.25f, 0), CCRemoveSelf::create(), nullptr));
}

} // namespace deathcomment
