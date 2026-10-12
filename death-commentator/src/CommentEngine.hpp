#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace deathcomment {

enum class Tone { Kind, Neutral, Roast };
enum class Language { Russian, English };

struct Context {
    int percent = 0;
    int attempt = 1;
    bool platformer = false;
    bool practice = false;
};

struct Options {
    Tone tone = Tone::Neutral;
    Language language = Language::Russian;
    int every = 1;
    double cooldownSeconds = 3.0;
};

namespace detail {

struct PhraseBank {
    std::array<std::string_view, 15> general;
    std::array<std::string_view, 3> early;
    std::array<std::string_view, 3> late;
    std::array<std::string_view, 2> practice;
};

inline PhraseBank const& bank(Tone tone, Language language) {
    static constexpr std::array<PhraseBank, 6> banks{{
        // Russian: encouraging, factual, and deliberately playful roasting.
        {{"У тебя получится!", "Ещё попытка — и ты ближе.",
          "Ты справишься. Продолжай!", "Каждая попытка чему-то учит.",
          "Ошибка — часть пути.", "Не спеши. Поймай свой ритм.",
          "Соберись и попробуй снова.", "Верю в твою следующую попытку!",
          "Отдохни секунду и продолжай.", "Ты учишься. Это уже прогресс.",
          "Сложный момент можно освоить.", "Спокойно. Всё получится.",
          "Ещё один шанс пройти дальше.", "Не сдавайся, ты можешь!",
          "Попытка {attempt} — ещё один шаг."},
         {"Начало тоже требует практики.", "Всего {percent}%. Впереди много шансов.",
          "Разберись с началом — дальше будет легче."},
         {"{percent}%! Ты уже очень близко.", "Почти финиш. Ты способен пройти его!",
          "Последний участок тебе по силам."},
         {"Практика для этого и нужна.", "Тренируй этот момент, и он станет проще."}},
        {{"Попытка {attempt} завершена.", "Можно начать следующую попытку.",
          "Попытка остановлена.", "Смерть зарегистрирована.",
          "Дальше — новая попытка.", "Уровень пока не пройден.",
          "Это конец текущей попытки.", "Следующий запуск готов.",
          "Зафиксирована ещё одна смерть.", "Пора повторить участок.",
          "Текущий заход завершён.", "Можно повторить маршрут.",
          "Возвращение к игре.", "Ещё одна попытка позади.",
          "Переход к следующему заходу."},
         {"Смерть на {percent}%.", "Попытка закончилась в начале уровня.",
          "Начальный участок не пройден."},
         {"Смерть на {percent}%, возле конца уровня.", "Попытка завершилась перед финишем.",
          "Остался заключительный участок."},
         {"Смерть в режиме практики.", "Этот участок можно повторить в практике."}},
        {{"Шип умнее тебя. Пока что.", "Твой кубик подал заявление на отпуск.",
          "Клик был смелый. И бесполезный.", "Уровень смеётся. Я тоже.",
          "Талант попадать в препятствия — 10 из 10.",
          "Ну что, чемпион перезапусков?", "Кубик просил другого водителя.",
          "Гравитация снова оказалась умнее.", "Красивый заход. Особенно падение.",
          "Попытка {attempt}. Коллекция фейлов растёт.",
          "Твой план был умереть? Выполнено.", "Кнопка рестарта уже тебя узнаёт.",
          "Рефлексы ушли за чаем.", "Это был клик или случайный чих?",
          "Даже меню удивилось твоей игре."},
         {"{percent}%? Ты вообще начал играть?", "Первый участок уже празднует победу.",
          "Разминка закончилась раньше, чем началась."},
         {"{percent}% и в стену. Драматургия!", "Финиш видел тебя. И захлопнул дверь.",
          "Почти прошёл — любимое оправдание."},
         {"Даже в практике кубику страшно с тобой.", "Чекпоинт опять спасает твою репутацию."}},
        // English equivalents use their own natural, short phrasing.
        {{"You can do this!", "One more try, one step closer.",
          "Keep going. You've got this!", "Every attempt teaches you something.",
          "Mistakes are part of learning.", "Take your time. Find your rhythm.",
          "Focus and give it another try.", "I believe in your next attempt!",
          "Take a breath, then try again.", "You're learning. That counts.",
          "You can master this section.", "Stay calm. You'll get there.",
          "Another chance to go further.", "Don't give up. You can do it!",
          "Attempt {attempt}: another step forward."},
         {"The opening takes practice too.", "Only {percent}%. Plenty of chances ahead.",
          "Learn the opening and build from there."},
         {"{percent}%! You're so close.", "Almost at the finish. You can reach it!",
          "You can master the final stretch."},
         {"That's what practice is for.", "Train this section and it will get easier."}},
        {{"Attempt {attempt} ended.", "You can start the next attempt.",
          "Attempt stopped.", "Death recorded.", "Next up: another attempt.",
          "The level is still unfinished.", "This attempt has ended.",
          "Ready for another run.", "Another death has been recorded.",
          "Time to repeat the section.", "The current run is over.",
          "You can repeat the route.", "Returning to the game.",
          "Another attempt is behind you.", "Moving to the next run."},
         {"Death at {percent}%.", "The attempt ended near the start.",
          "The opening section remains unfinished."},
         {"Death at {percent}%, near the end.", "The attempt ended before the finish.",
          "The final section remains."},
         {"Death in practice mode.", "This section can be repeated in practice."}},
        {{"The spike is smarter than you. For now.", "Your cube just requested a vacation.",
          "A brave click. A useless one, too.", "The level is laughing. So am I.",
          "Obstacle magnet: ten out of ten.", "Back again, restart champion?",
          "Your cube requested a new driver.", "Gravity outsmarted you again.",
          "Nice run. Especially the falling part.",
          "Attempt {attempt}. The fail collection grows.",
          "Was dying the plan? Mission accomplished.",
          "The restart button knows your name.", "Your reflexes went for a snack.",
          "Was that a click or a sneeze?", "Even the menu is judging your gameplay."},
         {"{percent}%? Did you even start playing?", "The opening is already celebrating.",
          "The warm-up ended before it started."},
         {"{percent}% and a crash. Such drama!", "The finish saw you and slammed the door.",
          "Almost beat it: your favorite excuse."},
         {"Your cube fears you even in practice.", "That checkpoint is saving your reputation."}}
    }};

    auto const toneIndex = tone == Tone::Kind ? 0u : tone == Tone::Roast ? 2u : 1u;
    auto const languageIndex = language == Language::English ? 3u : 0u;
    return banks[languageIndex + toneIndex];
}

inline std::size_t bankIndex(Tone tone, Language language) {
    auto const toneIndex = tone == Tone::Kind ? 0u : tone == Tone::Roast ? 2u : 1u;
    return (language == Language::English ? 3u : 0u) + toneIndex;
}

inline void replaceAll(std::string& text, std::string_view token, std::string const& value) {
    std::size_t offset = 0;
    while ((offset = text.find(token, offset)) != std::string::npos) {
        text.replace(offset, token.size(), value);
        offset += value.size();
    }
}

} // namespace detail

class Engine {
public:
    explicit Engine(unsigned seed = std::random_device{}()) : m_random(seed) {}

    // Level-local frequency resets; cooldown and phrase history remain session-wide.
    void resetLevel() { m_deathCount = 0; }

    // The caller must call once for each actual death, including deaths with no popup.
    std::optional<std::string> onDeath(Context const& context, Options const& options,
                                      double monotonicNowSeconds) {
        ++m_deathCount;
        if (!std::isfinite(monotonicNowSeconds) || monotonicNowSeconds < 0.0) {
            return std::nullopt;
        }
        if (m_lastObservedTime && monotonicNowSeconds < *m_lastObservedTime) {
            return std::nullopt;
        }
        m_lastObservedTime = monotonicNowSeconds;

        auto const every = static_cast<std::uint64_t>(std::max(1, options.every));
        if (m_deathCount % every != 0) {
            return std::nullopt;
        }
        auto const cooldown = std::isfinite(options.cooldownSeconds)
            ? std::max(0.0, options.cooldownSeconds) : 0.0;
        if (m_lastShownTime && monotonicNowSeconds - *m_lastShownTime < cooldown) {
            return std::nullopt;
        }

        auto const& bank = detail::bank(options.tone, options.language);
        std::vector<std::string_view> candidates(bank.general.begin(), bank.general.end());
        auto const percent = std::clamp(context.percent, 0, 100);
        // Platformer progress is not a percentage of a linear level.
        if (!context.platformer && percent <= 10) {
            candidates.insert(candidates.end(), bank.early.begin(), bank.early.end());
        } else if (!context.platformer && percent >= 90) {
            candidates.insert(candidates.end(), bank.late.begin(), bank.late.end());
        }
        if (context.practice) {
            candidates.insert(candidates.end(), bank.practice.begin(), bank.practice.end());
        }

        auto& previous = m_lastTemplates[detail::bankIndex(options.tone, options.language)];
        candidates.erase(std::remove(candidates.begin(), candidates.end(), previous), candidates.end());
        std::uniform_int_distribution<std::size_t> distribution(0, candidates.size() - 1);
        auto const chosen = candidates[distribution(m_random)];
        previous = std::string(chosen);
        m_lastShownTime = monotonicNowSeconds;

        std::string result(chosen);
        detail::replaceAll(result, "{percent}", std::to_string(percent));
        detail::replaceAll(result, "{attempt}", std::to_string(std::max(1, context.attempt)));
        return result;
    }

private:
    std::mt19937 m_random;
    std::uint64_t m_deathCount = 0;
    std::optional<double> m_lastObservedTime;
    std::optional<double> m_lastShownTime;
    std::array<std::string, 6> m_lastTemplates;
};

} // namespace deathcomment
