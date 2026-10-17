#include "Controller.hpp"
#include "Safety.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>

using namespace geode::prelude;

namespace {
    bool blocked() {
        return autobot::Controller::get().tainted();
    }

    constexpr auto required = std::to_array<std::string_view>({
        "PlayLayer::init", "PlayLayer::postUpdate", "PlayLayer::resetLevel",
        "PlayLayer::destroyPlayer", "PlayLayer::onQuit", "PlayLayer::onExit",
        "GJBaseGameLayer::processCommands", "GJBaseGameLayer::handleButton",
        "GJBaseGameLayer::pickupItem", "GJGameLevel::encodeWithCoder",
        "GameStatsManager::encodeDataTo",
        "PlayLayer::levelComplete",
        "PlayLayer::updateAttempts",
        "PlayLayer::updateAttemptTime",
        "PlayLayer::commitJumps",
        "PlayLayer::showNewBest",
        "GJGameLevel::savePercentage",
        "GJGameLevel::saveNewScore",
        "GameStatsManager::awardCurrencyForLevel",
        "GameStatsManager::awardDiamondsForLevel",
        "GameStatsManager::completedLevel",
        "GameStatsManager::completedDemonLevel",
        "GameStatsManager::completedStarLevel",
        "GameStatsManager::completedDailyLevel",
        "GameStatsManager::markLevelAsCompletedAndClaimed",
        "GameStatsManager::storeSecretCoin",
        "GameStatsManager::storeUserCoin",
        "GameStatsManager::incrementStat",
        "GameStatsManager::setStat",
        "GameStatsManager::incrementChallenge",
        "GameStatsManager::awardSecretKey",
        "GameStatsManager::registerRewardsFromItem",
        "GameManager::reportAchievementWithID",
        "GameLevelManager::ProcessHttpRequest"
    });

    // Run before ordinary handlers in compatible mods. Other mods which write
    // stats directly, bypass the GD request manager, or override these hooks
    // still require a separate compatibility test.
    template <class T>
    void protect(T& self, std::initializer_list<char const*> names) {
        for (auto name : names) {
            if (auto result = self.setHookPriority(name, Priority::First); !result) {
                log::error("Cannot prioritize safety hook {}: {}", name, result.unwrapErr());
            }
        }
    }
}

namespace autobot::safety {
    bool ready() {
        auto hooks = Mod::get()->getHooks();
        return std::all_of(required.begin(), required.end(), [&](std::string_view name) {
            return std::any_of(hooks.begin(), hooks.end(), [&](Hook* hook) {
                return hook->getDisplayName() == name && hook->isEnabled();
            });
        });
    }
}

namespace autobot {
    bool safetyAvailable() {
        return safety::ready();
    }
}

class $modify(AutoBotSafePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        protect(self, {
            "PlayLayer::levelComplete", "PlayLayer::updateAttempts",
            "PlayLayer::updateAttemptTime", "PlayLayer::commitJumps",
            "PlayLayer::showNewBest"
        });
    }

    void levelComplete() {
        auto& bot = autobot::Controller::get();
        if (blocked()) {
            // The normal completion path is never entered for a tainted run.
            // Controller records its own 100%, releases input and pauses.
            if (bot.owns(this)) bot.onCompletion();
            return;
        }
        PlayLayer::levelComplete();
    }

    void updateAttempts() {
        if (!blocked()) PlayLayer::updateAttempts();
    }

    void updateAttemptTime(float attemptTime) {
        if (!blocked()) PlayLayer::updateAttemptTime(attemptTime);
    }

    void commitJumps() {
        if (!blocked()) PlayLayer::commitJumps();
    }

    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        if (!blocked()) PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }
};

class $modify(AutoBotSafeLevel, GJGameLevel) {
    static void onModify(auto& self) {
        protect(self, {"GJGameLevel::savePercentage", "GJGameLevel::saveNewScore", "GJGameLevel::encodeWithCoder"});
    }
    void encodeWithCoder(DS_Dictionary* dictionary) {
        if (blocked()) autobot::Controller::get().scrubProgress();
        GJGameLevel::encodeWithCoder(dictionary);
    }

    void savePercentage(int percent, bool practice, int clicks, int attempts, bool valid) {
        if (!blocked()) GJGameLevel::savePercentage(percent, practice, clicks, attempts, valid);
    }

    void saveNewScore(int value, int type, int ticks, int clicks, int coins, gd::string inputs, bool save) {
        if (!blocked()) GJGameLevel::saveNewScore(value, type, ticks, clicks, coins, inputs, save);
    }
};

class $modify(AutoBotSafeStats, GameStatsManager) {
    static void onModify(auto& self) {
        protect(self, {
            "GameStatsManager::awardCurrencyForLevel",
            "GameStatsManager::awardDiamondsForLevel",
            "GameStatsManager::completedLevel", "GameStatsManager::completedDemonLevel",
            "GameStatsManager::completedStarLevel", "GameStatsManager::completedDailyLevel",
            "GameStatsManager::markLevelAsCompletedAndClaimed",
            "GameStatsManager::storeSecretCoin", "GameStatsManager::storeUserCoin",
            "GameStatsManager::encodeDataTo", "GameStatsManager::incrementStat",
            "GameStatsManager::setStat", "GameStatsManager::incrementChallenge",
            "GameStatsManager::awardSecretKey", "GameStatsManager::registerRewardsFromItem"
        });
    }

    void awardCurrencyForLevel(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::awardCurrencyForLevel(level);
    }

    void awardDiamondsForLevel(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::awardDiamondsForLevel(level);
    }

    void completedLevel(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::completedLevel(level);
    }

    void completedDemonLevel(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::completedDemonLevel(level);
    }

    void completedStarLevel(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::completedStarLevel(level);
    }

    GJRewardItem* completedDailyLevel(GJGameLevel* level) {
        return blocked() ? nullptr : GameStatsManager::completedDailyLevel(level);
    }

    void markLevelAsCompletedAndClaimed(GJGameLevel* level) {
        if (!blocked()) GameStatsManager::markLevelAsCompletedAndClaimed(level);
    }

    void storeSecretCoin(char const* key) {
        if (!blocked()) GameStatsManager::storeSecretCoin(key);
    }

    void storeUserCoin(char const* key) {
        if (!blocked()) GameStatsManager::storeUserCoin(key);
    }

    void encodeDataTo(DS_Dictionary* dictionary) {
        if (blocked()) autobot::Controller::get().scrubProgress();
        GameStatsManager::encodeDataTo(dictionary);
    }

    void incrementStat(char const* key, int amount) {
        if (!blocked()) GameStatsManager::incrementStat(key, amount);
    }

    void setStat(char const* key, int value) {
        if (!blocked()) GameStatsManager::setStat(key, value);
    }

    void incrementChallenge(GJChallengeType type, int amount) {
        if (!blocked()) GameStatsManager::incrementChallenge(type, amount);
    }

    bool awardSecretKey() {
        return !blocked() && GameStatsManager::awardSecretKey();
    }

    void registerRewardsFromItem(GJRewardItem* item) {
        if (!blocked()) GameStatsManager::registerRewardsFromItem(item);
    }
};

class $modify(AutoBotSafeAchievements, GameManager) {
    static void onModify(auto& self) {
        protect(self, {"GameManager::reportAchievementWithID"});
    }

    void reportAchievementWithID(char const* key, int percent, bool dontNotify) {
        if (!blocked()) GameManager::reportAchievementWithID(key, percent, dontNotify);
    }
};

class $modify(AutoBotSafeRequests, GameLevelManager) {
    static void onModify(auto& self) {
        protect(self, {"GameLevelManager::ProcessHttpRequest"});
    }

    void ProcessHttpRequest(gd::string endpoint, gd::string params, gd::string tag, GJHttpType type) {
        // Fail closed for stock GD network operations during the protected
        // session, including leaderboard requests that carry a best percentage.
        // This intentionally also prevents browsing network menus in that run.
        if (!blocked()) GameLevelManager::ProcessHttpRequest(endpoint, params, tag, type);
    }
};
