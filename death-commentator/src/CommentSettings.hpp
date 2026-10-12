#pragma once

#include <Geode/Geode.hpp>
#include "CommentEngine.hpp"

namespace deathcomment {

inline Options options() {
    auto mod = geode::Mod::get();
    Options result;
    auto tone = mod->getSettingValue<std::string>("tone");
    result.tone = tone == "Kind" ? Tone::Kind : tone == "Roast" ? Tone::Roast : Tone::Neutral;
    result.language = mod->getSettingValue<std::string>("language") == "English"
        ? Language::English : Language::Russian;
    result.every = static_cast<int>(mod->getSettingValue<std::int64_t>("every"));
    result.cooldownSeconds = mod->getSettingValue<double>("cooldown");
    return result;
}

inline char const* toneKey(Tone tone) {
    return tone == Tone::Kind ? "Kind" : tone == Tone::Roast ? "Roast" : "Neutral";
}

inline std::string toneName(Tone tone, Language language) {
    if (language == Language::English) return toneKey(tone);
    return tone == Tone::Kind ? "Добрые" : tone == Tone::Roast ? "Обидные" : "Нейтральные";
}

inline cocos2d::ccColor3B toneColor(Tone tone) {
    if (tone == Tone::Kind) return {130, 245, 175};
    if (tone == Tone::Roast) return {255, 130, 155};
    return {145, 205, 255};
}

} // namespace deathcomment
