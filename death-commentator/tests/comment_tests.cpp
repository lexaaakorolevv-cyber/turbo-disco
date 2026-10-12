#include "../src/CommentEngine.hpp"

#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

using namespace deathcomment;

namespace {

void require(bool condition, char const* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Options immediate(Tone tone = Tone::Neutral, Language language = Language::Russian) {
    Options result;
    result.tone = tone;
    result.language = language;
    result.cooldownSeconds = 0.0;
    return result;
}

Context middle() {
    Context context;
    context.percent = 50;
    return context;
}

bool validUtf8(std::string const& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        auto const head = static_cast<unsigned char>(text[i++]);
        if (head <= 0x7f) {
            continue;
        }
        std::uint32_t codepoint = 0;
        std::size_t continuation = 0;
        std::uint32_t minimum = 0;
        if (head >= 0xc2 && head <= 0xdf) {
            continuation = 1; codepoint = head & 0x1f; minimum = 0x80;
        } else if (head >= 0xe0 && head <= 0xef) {
            continuation = 2; codepoint = head & 0x0f; minimum = 0x800;
        } else if (head >= 0xf0 && head <= 0xf4) {
            continuation = 3; codepoint = head & 0x07; minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i < continuation) {
            return false;
        }
        for (std::size_t count = 0; count < continuation; ++count) {
            auto const byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

std::set<std::string> sample(Context context, Options options, unsigned seed = 1) {
    Engine engine(seed);
    std::set<std::string> phrases;
    for (int death = 0; death < 2000; ++death) {
        auto phrase = engine.onDeath(context, options, death);
        require(phrase.has_value(), "immediate settings must produce a phrase");
        require(validUtf8(*phrase), "phrase must be valid UTF-8");
        require(phrase->find('{') == std::string::npos, "all placeholders must be replaced");
        phrases.insert(*phrase);
    }
    return phrases;
}

} // namespace

int main() {
    std::vector<std::pair<char const*, std::function<void()>>> const tests{
        {"default Russian neutral options", [] {
            Engine engine(5);
            auto const phrase = engine.onDeath(middle(), {}, 0.0);
            require(phrase.has_value(), "first death must display immediately");
            require(phrase->find("Попытка") != std::string::npos ||
                    static_cast<unsigned char>((*phrase)[0]) >= 0xc0,
                    "default language must be Russian");
            require(!engine.onDeath(middle(), {}, 1.0), "default cooldown must apply");
        }},
        {"every third actual death", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = 3;
            for (int death = 1; death <= 12; ++death) {
                require(engine.onDeath(middle(), options, death).has_value() == (death % 3 == 0),
                        "every-N must count all deaths");
            }
        }},
        {"frequency and cooldown compose without queued messages", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = 2;
            options.cooldownSeconds = 5.0;
            require(!engine.onDeath(middle(), options, 0.0), "first death is gated");
            require(engine.onDeath(middle(), options, 1.0).has_value(), "second death displays");
            require(!engine.onDeath(middle(), options, 6.0), "third death stays frequency-gated");
            require(engine.onDeath(middle(), options, 6.1).has_value(), "fourth death displays");
            require(!engine.onDeath(middle(), options, 6.2), "fifth death is frequency-gated");
            require(!engine.onDeath(middle(), options, 6.3), "sixth death is cooldown-gated");
            require(!engine.onDeath(middle(), options, 12.0), "seventh death has no queued popup");
            require(engine.onDeath(middle(), options, 12.1).has_value(), "eighth death displays");
        }},
        {"cooldown boundary and session persistence", [] {
            Engine engine(1);
            Options options;
            require(engine.onDeath(middle(), options, 10.0).has_value(), "first popup");
            require(!engine.onDeath(middle(), options, 12.999), "cooldown below boundary");
            engine.resetLevel();
            require(!engine.onDeath(middle(), options, 12.999), "level reset must retain cooldown");
            require(engine.onDeath(middle(), options, 13.0).has_value(), "exact boundary permits popup");
        }},
        {"clock rewind never creates a popup", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = 2;
            require(!engine.onDeath(middle(), options, 100.0), "frequency gate observes time");
            require(!engine.onDeath(middle(), options, 99.0), "clock rewind is suppressed");
            require(!engine.onDeath(middle(), options, 101.0), "third death stays frequency-gated");
            require(engine.onDeath(middle(), options, 102.0).has_value(), "clock recovers");
        }},
        {"invalid time is suppressed and still counts a death", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = 2;
            require(!engine.onDeath(middle(), options, -1.0), "negative time invalid");
            require(engine.onDeath(middle(), options, 0.0).has_value(), "invalid death still counts");
            require(!engine.onDeath(middle(), options, std::numeric_limits<double>::quiet_NaN()),
                    "NaN invalid");
            require(engine.onDeath(middle(), options, 1.0).has_value(), "NaN must not poison clock");
            require(!engine.onDeath(middle(), options, std::numeric_limits<double>::infinity()),
                    "infinite time invalid");
            require(engine.onDeath(middle(), options, 2.0).has_value(), "infinity must not poison clock");
        }},
        {"options normalize invalid frequency and cooldown", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = -8;
            options.cooldownSeconds = -3.0;
            require(engine.onDeath(middle(), options, 0.0).has_value(), "negative options normalize");
            options.every = 0;
            options.cooldownSeconds = std::numeric_limits<double>::quiet_NaN();
            require(engine.onDeath(middle(), options, 0.0).has_value(), "zero frequency and NaN normalize");
            options.cooldownSeconds = std::numeric_limits<double>::infinity();
            require(engine.onDeath(middle(), options, 0.0).has_value(), "infinite cooldown normalizes");
        }},
        {"level reset restarts frequency", [] {
            Engine engine(1);
            auto options = immediate();
            options.every = 3;
            require(!engine.onDeath(middle(), options, 0.0), "first death");
            require(!engine.onDeath(middle(), options, 1.0), "second death");
            engine.resetLevel();
            require(!engine.onDeath(middle(), options, 2.0), "reset first death");
            require(!engine.onDeath(middle(), options, 3.0), "reset second death");
            require(engine.onDeath(middle(), options, 4.0).has_value(), "reset third death");
        }},
        {"same phrase never repeats in any bank", [] {
            for (auto language : {Language::Russian, Language::English}) {
                for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                    Engine engine(25);
                    auto options = immediate(tone, language);
                    std::string previous;
                    for (int death = 0; death < 500; ++death) {
                        auto phrase = engine.onDeath(middle(), options, death);
                        require(phrase && *phrase != previous, "consecutive phrases must differ");
                        previous = *phrase;
                        if (death % 3 == 0) {
                            engine.resetLevel();
                        }
                    }
                }
            }
        }},
        {"switching tone and language preserves bank history", [] {
            Engine engine(25);
            std::array<std::string, 6> previous;
            double time = 0;
            for (int cycle = 0; cycle < 100; ++cycle) {
                std::size_t index = 0;
                for (auto language : {Language::Russian, Language::English}) {
                    for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                        auto phrase = engine.onDeath(middle(), immediate(tone, language), time++);
                        require(phrase && *phrase != previous[index], "bank history survives switches");
                        if (language == Language::English) {
                            require(std::all_of(phrase->begin(), phrase->end(), [](unsigned char byte) {
                                return byte < 0x80;
                            }), "English bank contains English text");
                        } else {
                            require(std::any_of(phrase->begin(), phrase->end(), [](unsigned char byte) {
                                return byte >= 0xc0;
                            }), "Russian bank contains Cyrillic text");
                        }
                        previous[index++] = *phrase;
                    }
                }
            }
        }},
        {"all phrase banks are varied valid UTF-8", [] {
            for (auto language : {Language::Russian, Language::English}) {
                for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                    require(sample(middle(), immediate(tone, language)).size() == 15,
                            "each tone and language needs 15 unique general phrases");
                }
            }
        }},
        {"linear early and late context include suitable phrases", [] {
            for (auto language : {Language::Russian, Language::English}) {
                for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                    auto context = middle();
                    context.percent = 5;
                    auto const early = sample(context, immediate(tone, language));
                    require(early.size() == 18, "early bank has three contextual phrases");
                    require(std::any_of(early.begin(), early.end(), [](std::string const& phrase) {
                        return phrase.find("5%") != std::string::npos;
                    }), "early percentage must be formatted");
                    context.percent = 95;
                    auto const late = sample(context, immediate(tone, language));
                    require(late.size() == 18, "late bank has three contextual phrases");
                    require(std::any_of(late.begin(), late.end(), [](std::string const& phrase) {
                        return phrase.find("95%") != std::string::npos;
                    }), "late percentage must be formatted");
                }
            }
        }},
        {"platformer ignores linear progress categories", [] {
            for (auto language : {Language::Russian, Language::English}) {
                for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                    auto context = middle();
                    context.platformer = true;
                    context.percent = 0;
                    auto const beginning = sample(context, immediate(tone, language));
                    context.percent = 99;
                    auto const ending = sample(context, immediate(tone, language));
                    require(beginning == ending && beginning.size() == 15,
                            "platformer must use only general bank at any percentage");
                    for (auto const& phrase : ending) {
                        require(phrase.find('%') == std::string::npos,
                                "platformer must not claim percentage progress");
                    }
                }
            }
        }},
        {"practice adds context without fake platformer progress", [] {
            for (auto language : {Language::Russian, Language::English}) {
                for (auto tone : {Tone::Kind, Tone::Neutral, Tone::Roast}) {
                    auto context = middle();
                    context.practice = true;
                    context.platformer = true;
                    context.percent = 99;
                    auto const phrases = sample(context, immediate(tone, language));
                    require(phrases.size() == 17, "practice adds two phrases");
                    for (auto const& phrase : phrases) {
                        require(phrase.find('%') == std::string::npos, "no platformer percentage");
                    }
                }
            }
        }},
        {"dynamic values clamp malformed context", [] {
            auto context = middle();
            context.percent = -25;
            context.attempt = -4;
            auto const phrases = sample(context, immediate(Tone::Neutral, Language::English));
            require(phrases.count("Death at 0%.") == 1, "negative percent clamps to zero");
            require(phrases.count("Attempt 1 ended.") == 1, "negative attempt clamps to one");
            context.percent = 120;
            context.attempt = 42;
            auto const late = sample(context, immediate(Tone::Neutral, Language::English));
            require(late.count("Death at 100%, near the end.") == 1, "percentage clamps to 100");
            require(late.count("Attempt 42 ended.") == 1, "attempt placeholder is formatted");
        }},
        {"seeded engines have reproducible output", [] {
            Engine left(99);
            Engine right(99);
            for (int death = 0; death < 50; ++death) {
                require(left.onDeath(middle(), immediate(), death) ==
                        right.onDeath(middle(), immediate(), death), "seed must reproduce choices");
            }
        }}
    };

    int failed = 0;
    for (auto const& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (std::exception const& error) {
            ++failed;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size()
              << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
