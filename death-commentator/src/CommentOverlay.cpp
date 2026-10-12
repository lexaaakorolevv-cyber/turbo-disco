#include "CommentOverlay.hpp"
#include "CommentSettings.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace deathcomment {

void showComment(CCNode* parent, std::string const& text,
                 Context const& context, Options const& config) {
    if (!parent || text.empty()) return;
    auto size = CCDirector::get()->getWinSize();
    auto width = std::min(420.f, size.width - 24.f);
    if (width < 80.f) return;
    auto label = CCLabelBMFont::create(text.c_str(), "comment-font.fnt"_spr,
        width - 28.f, kCCTextAlignmentCenter);
    if (!label) {
        log::warn("Death Commentator: comment font could not be loaded");
        return;
    }
    label->setScale(0.58f);
    auto textHeight = label->getContentSize().height * label->getScale();
    auto height = std::max(66.f, textHeight + 38.f);
    auto bubble = CCLayerColor::create({16, 19, 31, 225}, width, height);
    bubble->setID("comment-bubble"_spr);
    bubble->setTouchEnabled(false);
    bubble->setCascadeOpacityEnabled(true);
    auto bottom = Mod::get()->getSettingValue<std::string>("position") == "Bottom";
    auto y = bottom ? 36.f : size.height - height - 52.f;
    bubble->setPosition({(size.width - width) / 2.f, std::max(4.f, y)});

    auto accent = toneColor(config.tone);
    auto line = CCLayerColor::create({accent.r, accent.g, accent.b, 255}, width, 3.f);
    line->setPosition({0.f, height - 3.f});
    bubble->addChild(line);

    auto title = toneName(config.tone, config.language);
    if (!context.platformer) title += fmt::format(" | {}%", context.percent);
    auto titleLabel = CCLabelBMFont::create(title.c_str(), "comment-font.fnt"_spr);
    titleLabel->setScale(0.40f);
    titleLabel->setColor(accent);
    titleLabel->setPosition({width / 2.f, height - 16.f});
    bubble->addChild(titleLabel);

    label->setPosition({width / 2.f, (height - 24.f) / 2.f});
    bubble->addChild(label);
    // Replacing an existing bubble avoids a stack of comments during retries.
    if (auto old = parent->getChildByID("comment-bubble"_spr)) {
        old->removeFromParentAndCleanup(true);
    }
    parent->addChild(bubble, 10000);
    auto duration = static_cast<float>(Mod::get()->getSettingValue<double>("duration"));
    duration = std::clamp(duration, 1.f, 6.f);
    bubble->setOpacity(0);
    bubble->runAction(CCSequence::create(
        CCFadeTo::create(0.10f, 225),
        CCDelayTime::create(duration - 0.30f),
        CCFadeTo::create(0.20f, 0),
        CCRemoveSelf::create(), nullptr));
}

} // namespace deathcomment
