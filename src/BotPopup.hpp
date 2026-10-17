#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace autobot {

// All Russian text uses the bundled Unicode BMFont; GD's stock fonts do not
// contain the complete Cyrillic alphabet.
cocos2d::CCLabelBMFont* botLabel(std::string const& text, float width,
                               float scale = .55f);

class BotPopup final : public geode::Popup {
protected:
    CCMenuItemToggler* m_enabledToggle = nullptr;
    CCMenuItemSpriteExtra* m_trainButton = nullptr;
    CCMenuItemSpriteExtra* m_resetButton = nullptr;
    cocos2d::CCLabelBMFont* m_attemptLabel = nullptr;
    cocos2d::CCLabelBMFont* m_bestLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    bool init() override;
    void onToggle(cocos2d::CCObject*);
    void onTrain(cocos2d::CCObject*);
    void onReset(cocos2d::CCObject*);
    void refresh(float);
    void onExit() override;

public:
    static BotPopup* create();
};

} // namespace autobot
