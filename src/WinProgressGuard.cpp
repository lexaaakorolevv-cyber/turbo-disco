#include "WinProgressGuard.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/utils/cocos.hpp>

namespace autobot {
struct WinProgressGuard::State {
    geode::Ref<GJGameLevel> level;
    geode::Ref<cocos2d::CCDictionary> pendingCoins;
    int attempts, jumps, clicks, attemptTime, normalPercent, orbCompletion;
    int newNormalPercent, practicePercent, firstCoin, secondCoin, thirdCoin;
    int verified, checksum;
    bool verifiedRaw, legitimate, checksumValid;
    gd::string recordString, personalBests;
};

WinProgressGuard::WinProgressGuard() = default;
WinProgressGuard::~WinProgressGuard() = default;

bool WinProgressGuard::capture(PlayLayer* layer) {
    if (!layer || !layer->m_level) return false;
    auto stats = GameStatsManager::sharedState();
    if (!stats || !stats->m_pendingUserCoins) return false;
    auto copy = cocos2d::CCDictionary::createWithDictionary(stats->m_pendingUserCoins);
    if (!copy) return false;
    auto state = std::make_unique<State>();
    auto level = layer->m_level;
    state->level = level;
    state->pendingCoins = copy;
    state->attempts = level->m_attempts.value();
    state->jumps = level->m_jumps.value();
    state->clicks = level->m_clicks.value();
    state->attemptTime = level->m_attemptTime.value();
    state->normalPercent = level->m_normalPercent.value();
    state->orbCompletion = level->m_orbCompletion.value();
    state->newNormalPercent = level->m_newNormalPercent2.value();
    state->practicePercent = level->m_practicePercent;
    state->firstCoin = level->m_firstCoinVerified.value();
    state->secondCoin = level->m_secondCoinVerified.value();
    state->thirdCoin = level->m_thirdCoinVerified.value();
    state->verified = level->m_isVerified.value();
    state->verifiedRaw = level->m_isVerifiedRaw;
    state->legitimate = level->m_isCompletionLegitimate;
    state->checksum = level->m_chk;
    state->checksumValid = level->m_isChkValid;
    state->recordString = level->m_recordString;
    state->personalBests = level->m_personalBests;
    m_state = std::move(state);
    return true;
}

void WinProgressGuard::restore() {
    if (!m_state) return;
    auto const& state = *m_state;
    auto level = state.level.data();
    level->m_attempts = state.attempts;
    level->m_jumps = state.jumps;
    level->m_clicks = state.clicks;
    level->m_attemptTime = state.attemptTime;
    level->m_normalPercent = state.normalPercent;
    level->m_orbCompletion = state.orbCompletion;
    level->m_newNormalPercent2 = state.newNormalPercent;
    level->m_practicePercent = state.practicePercent;
    level->m_firstCoinVerified = state.firstCoin;
    level->m_secondCoinVerified = state.secondCoin;
    level->m_thirdCoinVerified = state.thirdCoin;
    level->m_isVerified = state.verified;
    level->m_isVerifiedRaw = state.verifiedRaw;
    level->m_isCompletionLegitimate = state.legitimate;
    level->m_chk = state.checksum;
    level->m_isChkValid = state.checksumValid;
    level->m_recordString = state.recordString;
    level->m_personalBests = state.personalBests;
    // The pending-coin setter is inline on Windows. Preserve the original
    // dictionary instead of trying to hook an address which does not exist.
    auto stats = GameStatsManager::sharedState();
    if (stats && stats->m_pendingUserCoins) {
        auto destination = stats->m_pendingUserCoins;
        destination->removeAllObjects();
        for (auto [key, value] : geode::cocos::CCDictionaryExt<gd::string, cocos2d::CCObject*>(state.pendingCoins.data())) {
            destination->setObject(value, key);
        }
    }
}
void WinProgressGuard::clear() { m_state.reset(); }
bool WinProgressGuard::owns(GJGameLevel* level) const {
    return m_state && m_state->level.data() == level;
}
}
