#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include "CommentEngine.hpp"

class CommentPopup : public geode::Popup {
public:
    static CommentPopup* create();

private:
    bool init() override;
    void refresh(bool changePreview = true);
    void chooseKind(cocos2d::CCObject*);
    void chooseNeutral(cocos2d::CCObject*);
    void chooseRoast(cocos2d::CCObject*);
    void toggleEnabled(cocos2d::CCObject*);
    void changeFrequency(cocos2d::CCObject*);
    void changeCooldown(cocos2d::CCObject*);
    void changeLanguage(cocos2d::CCObject*);
    void preview(cocos2d::CCObject*);
    void onClose(cocos2d::CCObject*) override;
    void choose(deathcomment::Tone tone);

    deathcomment::Engine m_previewEngine;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;
    cocos2d::CCLabelBMFont* m_enabledLabel = nullptr;
    cocos2d::CCLabelBMFont* m_frequencyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_cooldownLabel = nullptr;
    cocos2d::CCLabelBMFont* m_languageLabel = nullptr;
    CCMenuItemSpriteExtra* m_kindButton = nullptr;
    CCMenuItemSpriteExtra* m_neutralButton = nullptr;
    CCMenuItemSpriteExtra* m_roastButton = nullptr;
};
