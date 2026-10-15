#include "Runtime.hpp"
#include <Geode/utils/hash.hpp>
#include <Geode/binding/ChanceTriggerGameObject.hpp>
#include <Geode/binding/ItemTriggerGameObject.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace rf {
Runtime& Runtime::get() { static Runtime runtime; return runtime; }
void Runtime::bind(PlayLayer* layer) {
    if (!layer || m_layer == layer) return;
    if (m_layer && m_layer != layer) unbind(m_layer);
    m_layer = layer;
    m_best.reset();
    m_selected.reset();
    m_selectedForSearch = false;
    m_target.reset();
    m_attempts = 0;
    m_trialPercent = 0;
    m_search.reset();
    m_previousAttempts = 0;
    m_previousSeconds = m_runElapsed = 0;
    m_autosaveStatus.clear();
    m_autosaveDirty = m_autosaveUrgent = false;
    m_trial = {};
    m_calibration = {}; m_failures = {}; m_motion.reset(); m_forecast = {};
    m_guidanceNote.clear(); m_lastFailure.clear();
    m_showPreview=Mod::get()->getSettingValue<bool>("show-preview");
    m_smoothSearch=Mod::get()->getSettingValue<bool>("smooth-search");
    m_status = "Готов. Только обычный режим, один игрок.";
    log::info("Route Finder {} attached to active level", ModVersion);
}
bool Runtime::levelReady() const {
    return m_layer && m_layer == PlayLayer::get() && m_layer->m_level &&
        m_layer->m_levelSettings && m_layer->m_player1;
}
std::string Runtime::readinessError() const {
    if (!m_layer || m_layer != PlayLayer::get()) return "Активный PlayLayer не найден. Откройте уровень заново.";
    if (!m_layer->m_level) return "Игра ещё создаёт данные уровня. Закройте меню и подождите.";
    if (!m_layer->m_levelSettings) return "Игра ещё создаёт настройки уровня. Закройте меню и подождите.";
    if (!m_layer->m_player1) return "Игрок P1 ещё не создан. Закройте меню и подождите.";
    return {};
}
bool Runtime::syncActiveLayer() {
    // init() can finish before the asynchronous level setup, and this mod's
    // pause/end menu can outlive an earlier init/onExit notification.
    // Resolve the actual current layer at the point of use, not just at init.
    auto current = PlayLayer::get();
    if (current) {
        auto menuOpen = m_menuOpen;
        bind(current);
        m_menuOpen = menuOpen;
    }
    if (levelReady()) return true;
    m_status = readinessError();
    log::warn("Route Finder {}: {}; current={}, bound={}, level={}, settings={}, player1={}",
        ModVersion, m_status, current != nullptr, m_layer != nullptr,
        current && current->m_level, current && current->m_levelSettings, current && current->m_player1);
    return false;
}
void Runtime::unbind(PlayLayer* layer) {
    if (m_layer != layer) return;
    if (active()) stop("Выход из уровня");
    autosaveSearch(true);
    releaseInput();
    m_obstacleObjects.clear();m_interactionObjects.clear();m_guidanceClock.reset();
    m_layer = nullptr;
    m_menuOpen = false;
    m_releasePending = false;
}
void Runtime::setMenuOpen(bool open) {
    m_menuOpen = open;
    m_accumulator = 0; // Menu time must never accumulate into catch-up ticks.
    if (open) m_pendingHuman = false;
}
void Runtime::resumeOverlay(PauseLayer* pause, EndLevelLayer* end) {
    // The native result/pause screen must close before a queued run can begin.
    // Native onReplay may reset once; the actual macro then starts after our
    // own full controlled reset, outside the native update stack.
    m_resetting = true;
    if (end) end->onReplay(nullptr);
    else if (pause) pause->onResume(nullptr);
    m_resetting = false;
}

std::string Runtime::unsupported() const {
    if (!levelReady()) return readinessError();
    auto s = m_layer->m_levelSettings;
    if (m_layer->m_isPlatformer || s->m_platformerMode) return "Platformer не поддерживается";
    if (s->m_twoPlayerMode || m_layer->m_level->m_twoPlayerMode) return "Два игрока не поддерживаются";
    if (s->m_startDual || m_layer->m_gameState.m_isDualMode) return "Dual не поддерживается";
    if (m_layer->m_isPracticeMode) return "Выйдите из practice mode";
    if (m_layer->m_startPosObject || s->m_startsWithStartPos) return "Start Position не поддерживается";
    if (s->m_dontReset) return "Уровни с Don't Reset не поддерживаются";
    if (m_layer->m_isEditor || m_layer->m_useReplay) return "Editor/replay игры не поддерживается";
    if (m_layer->m_ignoreDamage || m_layer->m_isIgnoreDamageEnabled) return "Отключите ignore damage/noclip";
    if (m_layer->m_objects) {
        for (auto object : CCArrayExt<GameObject*>(m_layer->m_objects)) {
            if (object->m_objectType == GameObjectType::DualPortal) return "Уровни с dual portal не поддерживаются";
            if (typeinfo_cast<ChanceTriggerGameObject*>(object)) return "Random triggers не поддерживаются";
            if (typeinfo_cast<ItemTriggerGameObject*>(object)) return "Item edit/compare/persistence не поддерживаются";
        }
    }
    return {};
}

nlohmann::json Runtime::environment() const {
    using json = nlohmann::json;
    auto level = m_layer->m_level;
    auto settings = m_layer->m_levelSettings;
    std::vector<std::string> mods;
    for (auto mod : Loader::get()->getAllMods())
        if (mod->isLoaded()) mods.push_back(fmt::format("{}@{}", mod->getID(), mod->getVersion().toVString()));
    std::sort(mods.begin(), mods.end());
    auto content = std::string(level->m_levelString);
    auto settingsString = std::string(settings->getSaveString());
    return json{{"gd", GEODE_GD_VERSION_STRING}, {"mod", ModVersion}, {"sdk", "5.11.1"},
        {"loader", Loader::get()->getVersion().toVString()},
#if defined(GEODE_IS_WINDOWS)
        {"platform", "win64"},
#elif defined(GEODE_IS_ANDROID64)
        {"platform", "android64"},
#else
        {"platform", "android32"},
#endif
        {"mods", mods},
        {"level", {{"id", static_cast<int>(level->m_levelID)}, {"revision", level->m_levelRev},
            {"version", level->m_levelVersion}, {"data_sha256", sha256(content).toString()},
            {"settings_sha256", sha256(settingsString).toString()}}},
        {"physics", {{"outer_updates_per_second", 240}, {"outer_step_seconds", 1.0 / 240.0},
            {"tick_unit", "positive-dt processCommands call, including native half steps"},
            {"input_boundary", "GJBaseGameLayer::processCommands/pre/v1"},
            {"click_between_steps", false}, {"click_on_steps", false},
            {"time_warp", 1.0}, {"start_from_beginning", true}, {"checkpoints", false},
            {"game_seed", GameSeed}, {"trace_stride", 16},
            {"flip_controls", GameManager::get()->getGameVariable("0010")},
            {"ldm", m_layer->m_lowDetailMode}, {"extra_ldm", m_layer->m_extraLDM},
            {"enable22", m_layer->m_enable22Changes}, {"fix_negative_scale", m_layer->m_fixNegativeScale}}}};
}

bool Runtime::prepare() {
    if (active()) { m_status = "Сначала остановите текущую операцию"; return false; }
    if (m_layer && m_search && (m_autosaveDirty || m_autosaveUrgent)) autosaveSearch(true);
    if (!syncActiveLayer()) return false;
    auto error = unsupported();
    if (!error.empty()) { m_status = error; return false; }
    m_environment = environment();
    auto mod = Mod::get();
    m_budget = {static_cast<std::uint64_t>(mod->getSettingValue<int64_t>("max-attempts")),
        static_cast<double>(mod->getSettingValue<int64_t>("max-seconds"))};
    m_outerLimit = 240 * mod->getSettingValue<int64_t>("max-attempt-seconds");
    m_tickLimit = MaxTicks;
    m_seed = mod->getSettingValue<int64_t>("search-seed");
    m_inputPacer.reset(SearchCps);
    m_calibration = {}; m_failures = {}; m_motion.reset(); m_forecast = {};
    m_showPreview = mod->getSettingValue<bool>("show-preview");
    m_smoothSearch = mod->getSettingValue<bool>("smooth-search");
    m_guidanceNote.clear(); m_lastFailure.clear();
    m_started = std::chrono::steady_clock::now();
    m_attempts = 0;
    m_previousAttempts = 0;
    m_previousSeconds = 0;
    m_runElapsed = 0;
    m_autosaveDirty = false;
    m_autosaveUrgent = false;
    m_autosaveStatus.clear();
    m_lastAutosave = m_started;
    m_oldCBS = m_layer->m_clickBetweenSteps;
    m_oldCOS = m_layer->m_clickOnSteps;
    m_haveOptions = true;
    m_releasePending = false;
    m_target.reset();
    m_best.reset();
    m_search.reset();
    m_verifyingSearch = false;
    return true;
}
void Runtime::startRecording() {
    if (!prepare()) return;
    m_mode = Mode::Recording;
    m_plan.clear();
    m_restart = true;
    m_status = "Запись: ввод округляется к физическим тикам";
}
void Runtime::startSearch() {
    if (!prepare()) return;
    bool resume = Mod::get()->getSettingValue<bool>("auto-resume");
    bool selected = m_selectedForSearch && m_selected && m_selected->environment == m_environment;
    if (!selected && resume && restoreSearch()) return;
    m_search = std::make_unique<Search>(m_seed, Mod::get()->getSettingValue<int64_t>("mutation-window"));
    if ((selected || resume) && m_selected && m_selected->environment == m_environment) {
        bool flight = !m_selected->trace.empty() && (m_selected->trace.back().flags & ((1u << 2) | (1u << 5)));
        m_search->observe(m_selected->actions, m_selected->ticks, m_selected->bestPercent, flight);
        m_best = m_selected;
    }
    m_selectedForSearch = false;
    m_mode = Mode::Searching;
    m_plan = m_search->next(m_tickLimit);
    m_flightFrom = m_search->guidanceFrom();
    m_restart = true;
    m_status = "Поиск: прогноз всех 8 форм; лимит 16 CPS";
    if (m_best && m_best->completed) {
        m_target = m_best;
        m_plan = m_best->actions;
        m_verifyingSearch = true;
        m_mode = Mode::Verifying;
        m_status = "Восстановлен полный кандидат. Проверка с начала";
    }
    m_autosaveUrgent = true;
}
void Runtime::play() {
    if (!m_selected) { m_status = "Сначала загрузите или найдите макрос"; return; }
    auto selected = *m_selected;
    if (!prepare()) return;
    if (selected.environment != m_environment) {
        m_status = "Несовместимы версия, уровень, физика, платформа или список модов";
        m_releasePending = true;
        return;
    }
    m_selected = selected;
    m_target = selected;
    m_plan = selected.actions;
    m_mode = Mode::Playing;
    m_tickLimit = std::max(m_tickLimit, selected.ticks);
    m_restart = true;
    m_status = "Воспроизведение с начала; контроль рассинхронизации";
}

void Runtime::releaseInput() {
    if (!m_layer) return;
    m_injecting = true;
    m_layer->handleButton(false, 1, true);
    m_layer->handleButton(false, 1, false);
    m_layer->m_queuedButtons.clear();
    m_layer->m_queuedRecordedButtons.clear();
    m_layer->m_queuedRecordedButtonsSize = 0;
    m_layer->m_queuedReplayButtons.clear();
    m_layer->m_jumping = false;
    if (m_layer->m_player1) m_layer->m_player1->releaseAllButtons();
    if (m_layer->m_player2) m_layer->m_player2->releaseAllButtons();
    m_injecting = false;
    m_pendingHuman = false;
    m_held = false;
}
void Runtime::inject(bool down) {
    m_injecting = true;
    // In normal single-player mode both physical input sides route to P1.
    // Use the main side (false), as used by the native queued command path.
    m_layer->handleButton(down, 1, false);
    m_injecting = false;
    m_held = down;
    ++m_inputEdges;
    appendAction(m_trial.actions, m_tick, down);
}


void Runtime::beginAttempt() {
    releaseInput();
    // delayedResetLevel has no float argument and is not a schedule selector.
    // The PlayLayer hook suppresses stale native death callbacks while active.
    m_layer->m_clickBetweenSteps = false;
    m_layer->m_clickOnSteps = false;
    GameToolbox::fast_srand(GameSeed);
    m_layer->m_randomSeed = GameSeed;
    m_layer->m_replayRandSeed = GameSeed;
    m_resetting = true;
    m_layer->removeAllCheckpoints();
    m_layer->resetLevelFromStart();
    m_resetting = false;
    releaseInput();
    GameToolbox::fast_srand(GameSeed);
    m_layer->m_randomSeed = GameSeed;
    m_layer->m_replayRandSeed = GameSeed;
    m_layer->m_extraDelta = 0;
    m_layer->m_clickBetweenSteps = false;
    m_layer->m_clickOnSteps = false;
    m_trial = {};
    if (auto player = m_layer->m_player1) {
        auto rect = player->getObjectRect();
        m_spawnFloor = player->getPositionY() - rect.size.height / 2.;
    }
    m_trial.environment = m_environment;
    m_tick = 0;
    m_outerUpdates = 0;
    m_cursor = 0;
    m_traceCursor = 0;
    m_guidedFlight = false;
    m_searchDesired = false;
    m_inputPacer.reset(m_inputPacer.limit());
    m_flightObstacles.clear();
    m_motion.reset(); m_calibration.resetSampling(); m_forecast = {};
    m_observationTime = -1; m_robotTime = 0; m_inputEdges = 0;
    m_guidanceClock.reset();m_cachedDesired=false;rebuildSceneCache();
    m_trialPercent = 0;
    m_accumulator = 0;
    ++m_attempts;
    m_restart = false;
}

Macro Runtime::snapshot(bool complete) const {
    auto result = m_trial;
    result.ticks = m_tick;
    result.outerUpdates = m_outerUpdates;
    result.bestPercent = complete ? 100 : m_trialPercent;
    result.completed = complete;
    result.verified = false;
    result.attempts = m_attempts;
    result.searchSeed = m_seed;
    result.searchMaxCps = m_target ? m_target->searchMaxCps : (m_mode == Mode::Searching || m_mode == Mode::Assisting) ? m_inputPacer.limit() : 0;
    return result;
}
void Runtime::remember(Macro const& macro) {
    if (!macro.ticks) return;
    if (!m_best || (macro.completed && !m_best->completed) || macro.bestPercent > m_best->bestPercent + 1e-6 ||
        (std::abs(macro.bestPercent - m_best->bestPercent) <= 1e-6 && macro.ticks > m_best->ticks))
    {
        m_best = macro;
        m_autosaveDirty = true;
        if (macro.completed) m_autosaveUrgent = true;
    }
}

std::filesystem::path Runtime::macroPath(bool best) const {
    auto e = environment();
    auto id = e["level"]["id"].get<int>();
    auto hash = e["level"]["data_sha256"].get<std::string>().substr(0, 16);
    return Mod::get()->getSaveDir() / "macros" /
        fmt::format("{}_{}{}.json", id, hash, best ? ".best" : "");
}
void Runtime::writeBest() {
    if (!m_best || !m_best->ticks) return;
    auto draft = *m_best;
    draft.verified = false;
    draft.attempts = m_attempts;
    draft.stopReason = m_status;
    try {
        saveMacro(macroPath(true), draft);
        m_status += "; лучший результат сохранён в .best.json";
    } catch (std::exception const& e) {
        m_status += fmt::format("; ошибка сохранения: {}", e.what());
        log::error("Route Finder save failed: {}", e.what());
    }
}
void Runtime::stop(std::string reason) {
    if (!active()) { m_status = std::move(reason); return; }
    if (!m_restart && m_tick) remember(snapshot());
    if (m_mode == Mode::Assisting) { m_plan=prefix(m_trial.actions,m_tick); m_flightFrom=m_tick; }
    m_status = std::move(reason);
    m_runElapsed = elapsedSeconds();
    m_mode = Mode::Idle;
    m_restart = false;
    m_accumulator = 0;
    m_releasePending = true;
    if (m_best && (!m_selected || !m_selected->verified)) m_selected = m_best;
    writeBest();
    m_autosaveUrgent = true;
}
void Runtime::poll() {
    if (!active()) return;
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
    // The last allowed attempt may finish; prevent only the NEXT attempt.
    if (elapsed >= m_budget.maxSeconds) stop("Достигнут лимит времени; прохождение не гарантировано");
}
void Runtime::service() {
    poll();
    if (m_releasePending) {
        releaseInput();
        if (m_haveOptions && m_layer) {
            m_layer->m_clickBetweenSteps = m_oldCBS;
            m_layer->m_clickOnSteps = m_oldCOS;
        }
        m_haveOptions = false;
        m_releasePending = false;
    }
    autosaveSearch();
    if (!active() || !m_restart || m_menuOpen || !m_layer || m_layer->m_isPaused) return;
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
    auto exhausted = m_budget.exhausted(m_attempts, elapsed);
    if (!exhausted.empty()) { stop(exhausted + "; проверенное прохождение не найдено"); return; }
    beginAttempt();
}

bool Runtime::beforeTick(float dt, bool halfTick, bool lastTick) {
    if (!active() || m_restart) return false;
    if (!std::isfinite(dt) || dt < 0) {
        stop("Некорректный физический dt"); return false;
    }
    if (dt == 0) return true; // No physics advance, therefore no new input tick.
    if (m_layer->m_gameState.m_isDualMode || m_layer->m_isPracticeMode ||
        m_layer->m_ignoreDamage || m_layer->m_isIgnoreDamageEnabled ||
        std::abs(m_layer->m_gameState.m_timeWarp - 1.f) > 1e-6 ||
        m_layer->m_clickBetweenSteps || m_layer->m_clickOnSteps) {
        stop("Изменился неподдерживаемый режим: dual/practice/noclip/time warp/CBS"); return false;
    }
    if (m_target && m_tick >= m_target->ticks) {
        if (m_target->completed) failReplay("Конец записи без levelComplete");
        else stop("Конец неполного макроса; это лучший участок, а не прохождение");
        return false;
    }
    if (m_tick >= m_tickLimit) { stop("Достигнут предел 432000 физических шагов в попытке"); return false; }
    if (m_target) {
        auto const& expected = m_target->timings[m_tick];
        if (std::abs(expected.dt - dt) > 1e-8 || expected.half != halfTick || expected.last != lastTick) {
            failReplay(fmt::format("Другой физический шаг на тике {}", m_tick)); return false;
        }
    }
    auto p = m_layer->m_player1;
    if ((m_mode == Mode::Searching || m_mode == Mode::Assisting) && p->m_isSideways) {
        stop("Поворот физической оси на 90° пока не поддерживается");
        return false;
    }
    if (m_mode == Mode::Searching || m_mode == Mode::Assisting) updateIntelligence(p);
    auto flags = static_cast<unsigned>(p->m_isOnGround) | (p->m_isUpsideDown << 1) |
        (p->m_isShip << 2) | (p->m_isBird << 3) | (p->m_isBall << 4) | (p->m_isDart << 5) |
        (p->m_isRobot << 6) | (p->m_isSpider << 7) | (p->m_isSwing << 8) |
        (p->m_isGoingLeft << 9) | (p->m_isSideways << 10) | (p->m_isDashing << 11);
    Sample sample{m_tick, p->getPositionX(), p->getPositionY(), p->m_yVelocity,
        p->m_playerSpeed, p->m_vehicleSize, dt, flags};
    if (m_tick % 16 == 0) {
        if (m_target) {
            if (m_traceCursor >= m_target->trace.size() || !sameSample(m_target->trace[m_traceCursor], sample)) {
                failReplay(fmt::format("Расхождение состояния на тике {}", m_tick)); return false;
            }
            ++m_traceCursor;
        }
        m_trial.trace.push_back(sample);
    }
    m_trial.timings.push_back({dt, halfTick, lastTick});
    if (m_mode == Mode::Recording || m_mode == Mode::Assisting) {
        bool desired = m_mode == Mode::Assisting ? m_inputPacer.apply(m_pendingHuman, m_outerUpdates) : m_pendingHuman;
        if (desired != m_held) inject(desired);
    } else if (m_cursor < m_plan.size() && m_plan[m_cursor].tick == m_tick) {
        auto down = m_plan[m_cursor++].down;
        if (m_mode == Mode::Searching) m_searchDesired = down;
        else inject(down);
    }
    // Verification and replay consume only saved actions; they must never
    // make a fresh decision from the obstacle forecast.
    if (m_mode == Mode::Searching) {
        guideFlight(p);
        auto down = m_inputPacer.apply(m_searchDesired, m_outerUpdates);
        if (down != m_held) inject(down);
    }
    ++m_tick; // Count consumed input boundaries, including the eventual death tick.
    m_trialPercent = std::max(m_trialPercent, static_cast<double>(std::clamp(m_layer->getCurrentPercent(), 0.f, 100.f)));
    return true;
}

bool Runtime::advanceOuterUpdate() {
    if (m_outerUpdates >= m_outerLimit) {
        stop("Достигнут лимит длительности одной попытки");
        return false;
    }
    ++m_outerUpdates;
    return true;
}

bool Runtime::humanInput(bool down, int button, bool /*inputSide*/) {
    if (m_injecting) return true;
    if (!active()) return !m_menuOpen;
    // Two-player and dual levels are rejected, so either mouse/keyboard side
    // is the same P1 jump. Never infer player numbering from a binding's name.
    if ((m_mode == Mode::Recording || m_mode == Mode::Assisting) && button == 1 && !m_menuOpen)
        m_pendingHuman = down;
    return false; // No physical input can contaminate search or replay queues.
}
void Runtime::died(GameObject* object) {
    if (!active() || m_restart) return;
    auto trial = snapshot();
    remember(trial);
    if (m_mode == Mode::Searching || m_mode == Mode::Assisting) {
        rememberFailure(object);
        m_mode = Mode::Searching;
        auto player = m_layer->m_player1;
        m_search->observe(trial.actions, trial.ticks, trial.bestPercent, player && (player->m_isShip || player->m_isDart));
        auto timing = m_failures.timingBias(m_observation);
        m_search->preferTiming(timing > 0 ? -1 : timing < 0 ? 1 : 0);
        m_plan = m_search->next(m_tickLimit);
        m_flightFrom = m_search->guidanceFrom();
        m_restart = true;
    } else if (m_mode == Mode::Verifying || m_mode == Mode::Playing) {
        failReplay(fmt::format("Смерть при повторе на тике {}", m_tick));
    } else stop("Запись завершена смертью; сохранён черновик");
}
void Runtime::failReplay(std::string const& why) {
    // A failed candidate is never serialized as a verified success.
    if (m_mode == Mode::Verifying && m_verifyingSearch) {
        stop("Проверка не пройдена: " + why + "; возможна недетерминированность уровня/модов");
    } else stop("Воспроизведение остановлено: " + why);
}
bool Runtime::completed() {
    if (!active()) return true;
    if (m_restart) return false;
    if (m_mode == Mode::Searching || m_mode == Mode::Assisting || m_mode == Mode::Recording) {
        auto candidate = snapshot(true);
        if (!candidate.ticks) { stop("Завершение без физической попытки"); return false; }
        remember(candidate);
        m_target = candidate;
        m_plan = candidate.actions;
        m_verifyingSearch = m_mode == Mode::Searching || m_mode == Mode::Assisting;
        m_mode = Mode::Verifying;
        m_restart = true;
        m_status = "Кандидат найден. Полная проверка с начала без чекпоинтов";
        return false; // No end screen/statistics for the unverified candidate.
    }
    if (!m_target || !m_target->completed || m_tick != m_target->ticks || m_outerUpdates != m_target->outerUpdates ||
        m_cursor != m_plan.size() || m_traceCursor != m_target->trace.size()) {
        failReplay("Завершение не совпало с концом макроса"); return false;
    }
    auto verified = snapshot(true);
    verified.verified = true;
    verified.stopReason = "Полная проверка с начала пройдена";
    m_selected = verified;
    m_best = verified;
    m_runElapsed = elapsedSeconds();
    m_mode = Mode::Idle;
    m_restart = false;
    m_releasePending = true;
    try {
        saveMacro(macroPath(false), verified);
        std::error_code ec;
        std::filesystem::remove(sessionPath(), ec);
        std::filesystem::remove(sessionPath().string() + ".bak", ec);
        m_autosaveDirty = m_autosaveUrgent = false;
        m_status = "100%: проверка с начала пройдена, макрос сохранён";
    } catch (std::exception const& e) {
        m_status = fmt::format("Проверка пройдена; ошибка записи: {}. Нажмите «Сохранить макрос»", e.what());
    }
    return true;
}
void Runtime::unexpectedReset() {
    if (active() && !m_resetting) stop("Сброс уровня извне; попытка отменена");
}

void Runtime::save() {
    if (active()) { m_status = "Остановите операцию перед ручным сохранением"; return; }
    if (!syncActiveLayer()) return;
    try {
        if (m_selected && m_selected->verified) {
            saveMacro(macroPath(false), *m_selected);
            m_status = "Проверенный макрос сохранён";
        } else if (m_best) {
            m_status = "Полного проверенного прохождения нет";
            writeBest();
        } else m_status = "Нет макроса для сохранения";
    } catch (std::exception const& e) { m_status = fmt::format("Ошибка записи: {}", e.what()); }
}
void Runtime::load() {
    if (active()) { m_status = "Остановите операцию перед загрузкой"; return; }
    if (!syncActiveLayer()) return;
    try {
        auto path = macroPath(false);
        if (!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".bak")) path = macroPath(true);
        auto loaded = loadMacro(path);
        if (loaded.environment != environment()) throw std::runtime_error("Incompatible environment/level");
        // A JSON flag is not proof of successful replay in this process.
        loaded.verified = false;
        m_selected = loaded;
        m_selectedForSearch = true;
        m_best = loaded;
        m_status = loaded.completed ? "Макрос загружен. «Воспроизвести» повторно проверит его" :
            "Загружен неполный лучший участок; можно продолжить поиск";
    } catch (std::exception const& e) { m_status = fmt::format("Ошибка загрузки: {}", e.what()); }
}
std::string Runtime::summary() const {
    double best = m_best ? m_best->bestPercent : 0;
    best = std::max(best, m_trialPercent);
    auto remaining = static_cast<int>(std::max(0., m_budget.maxSeconds - elapsedSeconds()));
    char const* phase = "Прогноз";
    if (m_search) switch (m_search->phase()) {
        case SearchPhase::Timing: phase = "Правка таймингов"; break;
        case SearchPhase::Explore: phase = "Перебор участка"; break;
        case SearchPhase::Restart: phase = "Новый маршрут"; break;
        case SearchPhase::Guided: break;
    }
    return fmt::format("Попыток: {} / {}   Лучший: {:.2f}%\nОсталось: {}м {}с | {}\n{}{}{}",
        m_attempts, m_budget.maxAttempts, best, remaining / 60, remaining % 60, phase, m_status,
        m_autosaveStatus.empty() ? "" : "\n", m_autosaveStatus) +
        fmt::format("\n{} | Калибровка: {} | Память ошибок: {}", m_guidanceNote, m_calibration.samples(), m_failures.size());
}
char const* Runtime::modeName() const {
    switch(m_mode){case Mode::Recording:return "ЗАПИСЬ";case Mode::Searching:return "ПОИСК";
    case Mode::Assisting:return "РУЧНАЯ ПОМОЩЬ";case Mode::Verifying:return "ПРОВЕРКА";
    case Mode::Playing:return "ВОСПРОИЗВЕДЕНИЕ";case Mode::Idle:return "ГОТОВ";}return "ГОТОВ";
}
std::string Runtime::hudSummary() const {
    char const* state = "Готов";
    switch (m_mode) {
        case Mode::Recording: state = "Запись"; break;
        case Mode::Searching: state = "Поиск"; break;
        case Mode::Assisting: state = "Помощь игрока (16 CPS)"; break;
        case Mode::Verifying: state = "Проверка"; break;
        case Mode::Playing: state = "Повтор"; break;
        case Mode::Idle: break;
    }
    double best = std::max(m_best ? m_best->bestPercent : 0., m_trialPercent);
    std::string flight;
    if (m_mode == Mode::Searching && levelReady()) {
        flight = fmt::format(" | {}: {}", formName(m_observation.form), m_held ? "ЗАЖИМ" : "отпущено");
        flight += fmt::format(" | {}/16 CPS", m_inputPacer.currentCps());
    }
    return fmt::format("RF {} | {}{}\nПопыток: {} | Лучший: {:.1f}%", ModVersion, state, flight, m_attempts, best);
}
}
