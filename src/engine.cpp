#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace v2 {
void Engine::configure(Json environment, Limits limits) {
    if (active())
        throw std::runtime_error("Сначала остановите текущую операцию");
    if (!environment.is_object() || limits.attempts < 2 || limits.attempts > 100000 ||
        !std::isfinite(limits.seconds) || limits.seconds < 1 || !std::isfinite(limits.attemptSeconds) ||
        limits.attemptSeconds < 1)
        throw std::runtime_error("Некорректные настройки");
    m_environment = std::move(environment);
    m_limits = limits;
    m_trial = {};
    m_best.reset();
    m_target.reset();
    m_search = {};
    m_nativeSearch.configure(limits.decisionStep, limits.maxDepth, limits.maxDecisions);
    m_candidate = {};
    m_brain = {};
    m_attempts = 0;
    m_seconds = 0;
    m_event = Event::None;
    m_save = false;
    m_nativeComplete = false;
    m_human = {};
    m_held = {};
    m_restart = true;
    m_release = true;
}
void Engine::startSearch(Json e, Limits limits, std::optional<Session> resume) {
    configure(std::move(e), limits);
    m_mode = Mode::Searching;
    if (resume) {
        if (resume->best.environment != m_environment)
            throw std::runtime_error("Сеанс несовместим с уровнем/окружением");
        m_best = std::move(resume->best);
        m_search = std::move(resume->search);
        m_candidate = std::move(resume->candidate);
        m_attempts = resume->attempts;
        m_seconds = resume->seconds;
        m_brain.restoreLearning(resume->learning);
        if (m_limits.nativeSearch && resume->nativeSearch.has_value())
            m_nativeSearch = std::move(*resume->nativeSearch);
        m_nativeSearch.limit(m_limits.maxDecisions);
    } else
        m_candidate = m_search.next();
    if (m_best && m_best->complete) {
        m_target = m_best;
        m_mode = Mode::Verifying;
    }
    m_status = m_limits.nativeSearch ? "Поиск веток через физику игры; рекорд не выбирает маршрут"
                                     : "Приближённый поиск: рекорд хранится отдельно";
}
void Engine::startRecording(Json e, Limits l) {
    configure(std::move(e), l);
    m_mode = Mode::Recording;
    m_status = "Запись на границах физических тиков";
}
void Engine::startReplay(Json e, Limits l, Run run) {
    validate(run);
    if (run.environment != e)
        throw std::runtime_error("Макрос несовместим с уровнем/версией/физикой/модами");
    configure(std::move(e), l);
    m_target = std::move(run);
    m_target->verified = false;
    m_mode = m_target->complete ? Mode::Verifying : Mode::Replaying;
    m_status = "Повтор с начала, без решений планировщика";
}
void Engine::beginAttempt() {
    if (!active())
        return;
    if (m_attempts >= m_limits.attempts || m_seconds >= m_limits.seconds) {
        stop("Достигнут лимит поиска; сохранён лучший участок");
        return;
    }
    ++m_attempts;
    m_tick = m_outer = 0;
    m_cursor = m_traceCursor = 0;
    m_trial = {};
    m_trial.environment = m_environment;
    m_trial.cps = m_target ? m_target->cps : m_mode == Mode::Recording ? 0 : 16;
    m_trial.attempts = m_attempts;
    m_held = {};
    m_human = {};
    m_desired = {};
    m_edges = {};
    m_previousDual = false;
    m_checkpointPercent = 0;
    m_event = Event::None;
    m_restart = false;
    m_brain.resetAttempt();
    m_nativeSearch.beginAttempt();
    for (auto &p : m_pacer)
        p.reset(16);
    if (m_mode == Mode::Searching) {
        m_status = (m_limits.nativeSearch ? "Физика игры · попытка " : "Прогноз · попытка ") +
                   std::to_string(m_attempts);
        if (m_limits.nativeSearch)
            m_status += " · решений " + std::to_string(m_nativeSearch.decisions());
        for (auto const &branch : m_search.branches())
            if (branch.id == m_candidate.parent)
                m_status += " · проверяю ветку " + std::to_string(static_cast<int>(branch.percent)) + "%";
        if (m_candidate.checkpoint)
            m_status += " · повтор до контрольной точки";
    }
}
bool Engine::advanceOuter() {
    if (!active() || pending())
        return false;
    if (m_outer >= static_cast<Tick>(240 * m_limits.attemptSeconds)) {
        if (m_mode == Mode::Searching) {
            m_status = "Лимит длительности попытки";
            m_event = Event::Death;
        } else
            fail("Достигнут лимит длительности попытки");
        return false;
    }
    ++m_outer;
    return true;
}
Decision Engine::tick(Frame f, SceneProvider const &provider) {
    Decision d;
    if (!active() || pending())
        return d;
    if (!std::isfinite(f.timing.dt) || f.timing.dt <= 0 || f.timing.dt > 1 || !std::isfinite(f.percent)) {
        fail("Некорректные данные физики");
        return d;
    }
    if (m_tick >= MaxTicks) {
        fail("Предел физических тиков");
        return d;
    }
    f.tick = m_tick;
    f.outer = m_outer;
    if (m_target && m_tick >= m_target->ticks) {
        if (m_target->complete)
            fail("Конец записи без завершения уровня");
        else
            stop("Конец неполного участка");
        return d;
    }
    if (m_target) {
        auto const &t = m_target->timings[m_tick];
        if (std::abs(t.dt - f.timing.dt) > 1e-8 || t.half != f.timing.half || t.last != f.timing.last) {
            fail("Расхождение физического шага на тике " + std::to_string(m_tick));
            return d;
        }
    }
    for (int i = 0; i < 2; i++) {
        f.players[i].held = m_held[f.independent ? i : 0];
        f.players[i].pressInterval = m_pacer[i].pressIntervalFrames() / 4.;
        f.players[i].pressRemaining = m_pacer[i].pressRemainingFrames(m_outer) / 4.;
    }
    for (int i = 0; i < (f.dual ? 2 : 1); i++) {
        auto const &s = f.players[i];
        if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.vy) || !std::isfinite(s.vx) ||
            !std::isfinite(s.scale) || s.scale <= 0 || !std::isfinite(s.gravityMod) || s.gravityMod <= 0) {
            fail("Нечисловое состояние игрока");
            return d;
        }
    }
    if (m_mode == Mode::Searching && m_limits.nativeSearch &&
        !m_nativeSearch.validateReplay(f, m_trial.actions.size())) {
        fail("Расхождение повторяемого префикса на тике " + std::to_string(m_tick) +
             "; поиск остановлен, эта ветка не признана тупиком");
        return d;
    }
    auto sample = f.sample();
    bool expected =
        m_target && m_traceCursor < m_target->trace.size() && m_target->trace[m_traceCursor].tick == m_tick;
    if (m_tick % 16 == 0 || f.dual != m_previousDual || expected) {
        if (m_target) {
            if (m_traceCursor >= m_target->trace.size() ||
                !sameSample(m_target->trace[m_traceCursor], sample)) {
                fail("Расхождение состояния на тике " + std::to_string(m_tick));
                return d;
            }
            ++m_traceCursor;
        }
        m_trial.trace.push_back(sample);
        if (m_mode == Mode::Searching) {
            int percent = static_cast<int>(f.percent) / 5 * 5;
            if (percent > m_checkpointPercent) {
                m_checkpointPercent = percent;
                m_search.checkpoint(percent, sample, m_trial.actions, m_attempts);
            }
        }
    }
    if (m_mode == Mode::Searching && m_candidate.checkpoint && m_tick == m_candidate.checkpoint->tick) {
        if (!sameSample(*m_candidate.checkpoint, sample)) {
            m_search.invalidate(m_tick);
            m_status = "Контрольная точка разошлась и отключена";
        } else
            m_status = "Контрольная точка повторена; ищу продолжение";
        m_candidate.checkpoint.reset();
        m_brain.resetAttempt();
    }
    m_previousDual = f.dual;
    m_trial.timings.push_back(f.timing);
    m_trial.percent = std::max(m_trial.percent, std::clamp(f.percent, 0., 100.));
    if (m_mode == Mode::Recording)
        m_desired = m_human;
    else {
        auto const &actions = m_target ? m_target->actions : m_candidate.actions;
        while (m_cursor < actions.size() && actions[m_cursor].tick == m_tick) {
            auto const &a = actions[m_cursor++];
            m_desired[a.player - 1] = a.down;
        }
    }
    if (m_mode == Mode::Searching && m_tick >= m_candidate.guideFrom) {
        if (!m_limits.nativeSearch)
            m_desired = m_brain.choose(f, m_edges, m_candidate.policy, provider);
        else if (m_nativeSearch.needsDecision(f)) {
            if (m_nativeSearch.atLimit()) {
                fail("Лимит решений/памяти поиска; лучший участок сохранён");
                return d;
            }
            unsigned legal = 0;
            for (unsigned mask = 0; mask < (f.independent ? 4u : 2u); ++mask) {
                bool allowed = true;
                for (int i = 0; i < (f.independent ? 2 : 1); ++i) {
                    auto pacer = m_pacer[i];
                    allowed = allowed && pacer.apply((mask >> i) & 1u, m_outer) == bool((mask >> i) & 1u);
                }
                if (allowed)
                    legal |= 1u << mask;
            }
            auto suggested = m_brain.choose(f, m_edges, m_candidate.policy, provider, 4096);
            unsigned mask = unsigned(suggested[0]) | (f.independent ? unsigned(suggested[1]) << 1 : 0);
            mask = m_nativeSearch.decide(f, m_trial.actions.size(), mask, legal);
            m_desired = {bool(mask & 1), bool(mask & 2)};
        }
    }
    for (int i = 0; i < (f.independent ? 2 : 1); i++) {
        bool down = m_mode == Mode::Searching ? m_pacer[i].apply(m_desired[i], m_outer) : m_desired[i];
        if (down != m_held[i]) {
            Action action{m_tick, down, 1, i + 1};
            m_trial.actions.push_back(action);
            d.actions.push_back(action);
            m_held[i] = down;
            ++m_edges[i];
        }
    }
    ++m_tick;
    m_trial.ticks = m_tick;
    m_trial.outer = m_outer;
    d.advance = true;
    return d;
}
void Engine::remember() {
    if (!m_trial.ticks)
        return;
    m_trial.reason = m_status;
    if (!m_best || m_trial.complete && !m_best->complete || m_trial.percent > m_best->percent ||
        (m_trial.percent == m_best->percent && m_trial.ticks > m_best->ticks)) {
        m_best = m_trial;
        m_save = true;
    }
}
void Engine::fail(std::string reason) {
    // tick() can fail after sampling but before a native step is committed.
    // Never publish that speculative sample/timing as part of a valid run.
    if (m_trial.timings.size() > m_tick)
        m_trial.timings.resize(m_tick);
    while (!m_trial.trace.empty() && m_trial.trace.back().tick >= m_tick)
        m_trial.trace.pop_back();
    while (!m_trial.actions.empty() && m_trial.actions.back().tick >= m_tick)
        m_trial.actions.pop_back();
    m_status = std::move(reason);
    remember();
    m_mode = Mode::Idle;
    m_event = Event::None;
    m_restart = false;
    m_release = true;
    m_save = m_best.has_value();
}
void Engine::stop(std::string reason) {
    fail(std::move(reason));
}
void Engine::service(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0)
        return;
    m_seconds = std::max(m_seconds, elapsed);
    if (!active())
        return;
    if (m_seconds >= m_limits.seconds) {
        stop("Достигнут лимит времени; лучший участок сохранён");
        return;
    }
    auto event = std::exchange(m_event, Event::None);
    if (event == Event::None)
        return;
    if (event == Event::Death) {
        remember();
        if (m_mode == Mode::Searching) {
            if (m_limits.nativeSearch) {
                std::optional<Candidate> next;
                try {
                    next = m_nativeSearch.backtrack(m_trial);
                } catch (std::exception const &e) {
                    fail(e.what());
                    return;
                }
                if (!next) {
                    fail("Доступные ветки на выбранном шаге исчерпаны; попробуйте шаг 1 или другой лимит");
                    return;
                }
                m_candidate = std::move(*next);
            } else {
                m_search.observe(m_trial, m_candidate);
                m_candidate = m_search.next();
            }
            m_restart = true;
            m_release = true;
            m_save = true;
        } else
            fail("Смерть при записи/повторе; успешный макрос не подтверждён");
        return;
    }
    if (m_mode == Mode::Searching || m_mode == Mode::Recording) {
        m_trial.complete = true;
        m_trial.percent = 100;
        remember();
        m_target = m_trial;
        m_target->verified = false;
        m_mode = Mode::Verifying;
        m_restart = true;
        m_release = true;
        m_status = "Кандидат найден. Проверка полным повтором с начала";
        m_save = true;
        return;
    }
    if (m_target && m_target->complete && m_tick == m_target->ticks && m_outer == m_target->outer &&
        m_traceCursor == m_target->trace.size() && m_cursor == m_target->actions.size()) {
        m_trial.complete = m_trial.verified = true;
        m_trial.percent = 100;
        m_trial.reason = "Полный повтор с начала подтверждён";
        m_best = m_trial;
        m_mode = Mode::Idle;
        m_status = m_trial.reason;
        m_release = m_save = m_nativeComplete = true;
    } else
        fail("Завершение уровня не совпало с макросом");
}
Session Engine::snapshot() const {
    if (!m_best)
        throw std::runtime_error("Нет записанного участка");
    Session s{*m_best, m_search, m_candidate, m_brain.learning(), m_attempts, m_seconds};
    if (m_limits.nativeSearch) {
        auto tree = m_nativeSearch;
        if (!m_restart && m_trial.ticks && m_mode != Mode::Verifying && !m_trial.complete)
            s.candidate = tree.resume(m_trial);
        s.nativeSearch = std::move(tree);
    }
    return s;
}
Json encode(Session const &s) {
    return {{"format", "routefinder-v2-session"},
            {"schema", 2},
            {"best", encode(s.best)},
            {"search", s.search.json()},
            {"candidate", encodeCandidate(s.candidate)},
            {"learning", s.learning},
            {"attempts", s.attempts},
            {"seconds", s.seconds},
            {"native_search", s.nativeSearch ? s.nativeSearch->json() : Json()}};
}
Session decodeSession(Json const &j) {
    if (j.at("format") != "routefinder-v2-session" || j.at("schema") != 2)
        throw std::runtime_error("Неверный формат сеанса");
    auto attempts = j.at("attempts");
    double seconds = j.at("seconds").get<double>();
    if (!attempts.is_number_integer() || attempts.get<double>() < 0 || attempts.get<double>() > 1e12 ||
        !std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::runtime_error("Некорректные лимиты сеанса");
    Session s{decode(j.at("best")), Search::restore(j.at("search")), decodeCandidate(j.at("candidate")),
              j.at("learning"),     attempts.get<std::uint64_t>(),   seconds};
    if (j.contains("native_search") && !j.at("native_search").is_null())
        s.nativeSearch = NativeSearch::restore(j.at("native_search"));
    Brain b;
    b.restoreLearning(s.learning);
    return s;
}
} // namespace v2
