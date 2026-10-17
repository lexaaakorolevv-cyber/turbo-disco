#include "native.hpp"
#include <Geode/utils/hash.hpp>
#include <Geode/binding/ChanceTriggerGameObject.hpp>
#include <Geode/binding/ItemTriggerGameObject.hpp>
#include <Geode/binding/GameToolbox.hpp>
#include <algorithm>
#include <cmath>
using namespace geode::prelude;
namespace v2 {
constexpr std::uint64_t GameSeed = 0x526f75746546696eULL;
Native &Native::get() {
    static Native value;
    return value;
}
void Native::bind(PlayLayer *l) {
    if (!l || m_layer == l)
        return;
    if (m_layer)
        unbind(m_layer);
    m_layer = l;
    m_engine = {};
    m_loaded.reset();
    m_notice.clear();
    m_scene.bind(l);
    m_record = 0;
    m_recordKey.clear();
    if (l->m_level) {
        m_recordKey = "v2-record-" + std::to_string(static_cast<int>(l->m_level->m_levelID)) + "-" +
                      sha256(std::string(l->m_level->m_levelString)).toString().substr(0, 16);
        auto legacyKey = "record-" + std::to_string(static_cast<int>(l->m_level->m_levelID)) + "-" +
                         sha256(std::string(l->m_level->m_levelString)).toString().substr(0, 16);
        m_record =
            Mod::get()->getSavedValue<double>(m_recordKey, Mod::get()->getSavedValue<double>(legacyKey, 0.));
        if (!std::isfinite(m_record) || m_record < 0 || m_record > 100)
            m_record = 0;
    }
}
void Native::unbind(PlayLayer *l) {
    if (l != m_layer)
        return;
    stop();
    service();
    m_storage.drain();
    release();
    m_scene.bind(nullptr);
    m_layer = nullptr;
    m_menu = false;
    m_options = false;
}
std::string Native::unsupported() const {
    if (!m_layer || m_layer != PlayLayer::get() || !m_layer->m_level || !m_layer->m_levelSettings ||
        !m_layer->m_player1)
        return "Уровень ещё не готов";
    auto s = m_layer->m_levelSettings;
    if (m_layer->m_isPlatformer || s->m_platformerMode)
        return "Platformer пока не поддерживается";
    if (m_layer->m_isPracticeMode || m_layer->m_startPosObject || s->m_startsWithStartPos || s->m_dontReset)
        return "Нужен обычный запуск с начала, без practice/StartPos/Don't Reset";
    if (m_layer->m_isEditor || m_layer->m_useReplay || m_layer->m_ignoreDamage ||
        m_layer->m_isIgnoreDamageEnabled)
        return "Editor, native replay и noclip не поддерживаются";
    if (m_layer->m_objects)
        for (auto o : CCArrayExt<GameObject *>(m_layer->m_objects))
            if (typeinfo_cast<ChanceTriggerGameObject *>(o) || typeinfo_cast<ItemTriggerGameObject *>(o))
                return "Random/item persistence не поддерживаются";
    return {};
}
Json Native::environment() const {
    auto l = m_layer->m_level;
    std::vector<std::string> mods;
    for (auto m : Loader::get()->getAllMods())
        if (m->isLoaded())
            mods.push_back(m->getID() + "@" + m->getVersion().toVString());
    std::sort(mods.begin(), mods.end());
    return {{"gd", GEODE_GD_VERSION_STRING},
            {"geode_sdk", "5.11.1"},
            {"loader", Loader::get()->getVersion().toVString()},
            {"mod", Version},
            {"platform", "win64"},
            {"mods", mods},
            {"level",
             {{"id", static_cast<int>(l->m_levelID)},
              {"revision", l->m_levelRev},
              {"version", l->m_levelVersion},
              {"data", sha256(std::string(l->m_levelString)).toString()},
              {"settings", sha256(std::string(m_layer->m_levelSettings->getSaveString())).toString()}}},
            {"physics",
             {{"clock_hz", 240},
              {"input_boundary", "processCommands/pre/v2"},
              {"half_ticks", true},
              {"seed", GameSeed},
              {"checkpoints", "replayed-prefix-only"},
              {"independent", m_layer->m_levelSettings->m_twoPlayerMode},
              {"flip", GameManager::get()->getGameVariable("0010")},
              {"cbs", false},
              {"cos", false},
              {"time_warp", 1},
              {"ldm", m_layer->m_lowDetailMode},
              {"extra_ldm", m_layer->m_extraLDM},
              {"enable22", m_layer->m_enable22Changes},
              {"fix_negative_scale", m_layer->m_fixNegativeScale}}}};
}
std::filesystem::path Native::directory() const {
    auto e = m_engine.environment();
    if (e.empty())
        e = environment();
    return Mod::get()->getSaveDir() / "v2" /
           (std::to_string(e["level"]["id"].get<int>()) + "_" +
            e["level"]["data"].get<std::string>().substr(0, 16));
}
void Native::prepare() {
    if (auto l = PlayLayer::get())
        bind(l);
    auto error = unsupported();
    if (!error.empty())
        throw std::runtime_error(error);
    if (active())
        throw std::runtime_error("Сначала остановите текущую операцию");
    m_scene.pump();
    if (m_scene.busy())
        throw std::runtime_error("Дождитесь Analyze или нажмите «Остановить»");
    service();
    m_storage.drain();
    m_notice.clear();
    m_started = std::chrono::steady_clock::now();
    m_baseSeconds = 0;
    m_nextSave = 0;
    m_cbs = m_layer->m_clickBetweenSteps;
    m_cos = m_layer->m_clickOnSteps;
    m_options = true;
    m_accumulator = 0;
}
namespace {
Limits limits() {
    auto m = Mod::get();
    return {static_cast<std::uint64_t>(m->getSettingValue<int64_t>("max-attempts")),
            static_cast<double>(m->getSettingValue<int64_t>("max-seconds")),
            static_cast<double>(m->getSettingValue<int64_t>("max-attempt-seconds")),
            m->getSettingValue<bool>("native-search"),
            static_cast<unsigned>(m->getSettingValue<int64_t>("decision-step")),
            32768,
            static_cast<std::uint64_t>(m->getSettingValue<int64_t>("max-decisions"))};
}
} // namespace
void Native::search() {
    try {
        prepare();
        m_loaded.reset();
        auto env = environment();
        std::optional<Session> session;
        if (Mod::get()->getSettingValue<bool>("auto-resume")) {
            auto path = Mod::get()->getSaveDir() / "v2" /
                        (std::to_string(env["level"]["id"].get<int>()) + "_" +
                         env["level"]["data"].get<std::string>().substr(0, 16)) /
                        "search.json";
            auto backup = path;
            backup += ".bak";
            if (std::filesystem::exists(path) || std::filesystem::exists(backup))
                try {
                    auto loaded = decodeSession(readJson(path));
                    if (loaded.best.environment == env) {
                        m_baseSeconds = loaded.seconds;
                        session = std::move(loaded);
                    }
                } catch (std::exception const &e) {
                    m_notice = "Старый/повреждённый сеанс пропущен: " + std::string(e.what());
                }
        }
        m_engine.startSearch(std::move(env), limits(), std::move(session));
    } catch (std::exception const &e) {
        m_notice = e.what();
        if (active())
            m_engine.stop(m_notice);
    }
}
void Native::record() {
    try {
        prepare();
        m_loaded.reset();
        m_engine.startRecording(environment(), limits());
    } catch (std::exception const &e) {
        m_notice = e.what();
    }
}
void Native::stop() {
    if (active())
        m_engine.stop();
    if (m_scene.busy()) {
        m_scene.cancel();
        m_notice = "Анализ остановлен";
    }
}
void Native::load() {
    try {
        if (active())
            throw std::runtime_error("Сначала остановите операцию");
        if (auto l = PlayLayer::get())
            bind(l);
        if (!unsupported().empty())
            throw std::runtime_error(unsupported());
        service();
        m_storage.drain();
        std::string error = "Макрос не найден";
        for (auto name : {"macro.json", "best.json"})
            try {
                auto run = decode(readJson(directory() / name));
                if (run.environment != environment())
                    throw std::runtime_error("Уровень, версия или физика макроса отличаются");
                m_loaded = std::move(run);
                m_notice = "Макрос загружен. Полный файл будет проверен повтором";
                return;
            } catch (std::exception const &e) {
                error = e.what();
            }
        throw std::runtime_error(error);
    } catch (std::exception const &e) {
        m_notice = e.what();
    }
}
void Native::save() {
    try {
        if (active())
            throw std::runtime_error("Остановите операцию перед сохранением");
        if (!m_engine.best())
            throw std::runtime_error("Пока нет записанного участка");
        m_storage.submit(directory(), m_engine.snapshot());
        m_nextSave = elapsed() + 3;
        m_savePending = false;
        m_notice = m_engine.best()->verified ? "Проверенный макрос сохраняется"
                                             : "Сохраняется лучший участок; полного проверенного макроса нет";
    } catch (std::exception const &e) {
        m_notice = e.what();
    }
}
void Native::replay() {
    try {
        auto run = m_loaded ? m_loaded : m_engine.best();
        if (!run)
            throw std::runtime_error("Сначала загрузите или запишите макрос");
        prepare();
        m_engine.startReplay(environment(), limits(), std::move(*run));
    } catch (std::exception const &e) {
        m_notice = e.what();
    }
}
void Native::analyze() {
    if (auto l = PlayLayer::get())
        bind(l);
    if (!m_layer)
        return;
    if (active()) {
        m_notice = "Сначала остановите операцию";
        return;
    }
    if (m_scene.busy()) {
        m_notice = m_scene.analysisStatus();
        return;
    }
    if (m_scene.analysisStatus().find("АНАЛИЗ ГОТОВ") != std::string::npos) {
        FLAlertLayer::create("Analyze", m_scene.report(), "OK")->show();
        return;
    }
    m_scene.analyze();
    m_notice = "Analyze запущен. Кнопка повторно откроет готовый отчёт";
}
void Native::release() {
    if (!m_layer)
        return;
    m_injecting = true;
    m_layer->handleButton(false, 1, true);
    m_layer->handleButton(false, 1, false);
    m_layer->m_queuedButtons.clear();
    m_layer->m_queuedRecordedButtons.clear();
    m_layer->m_queuedRecordedButtonsSize = 0;
    m_layer->m_queuedReplayButtons.clear();
    m_layer->m_jumping = false;
    if (m_layer->m_player1)
        m_layer->m_player1->releaseAllButtons();
    if (m_layer->m_player2)
        m_layer->m_player2->releaseAllButtons();
    m_injecting = false;
}
void Native::inject(Action const &a) {
    m_injecting = true;
    m_layer->handleButton(a.down, a.button,
                          rf::nativeButtonSide(a.player, m_layer->m_levelSettings->m_twoPlayerMode,
                                               GameManager::get()->getGameVariable("0010")));
    m_injecting = false;
}
void Native::reset() {
    release();
    m_layer->m_clickBetweenSteps = m_layer->m_clickOnSteps = false;
    GameToolbox::fast_srand(GameSeed);
    m_layer->m_randomSeed = m_layer->m_replayRandSeed = GameSeed;
    m_resetting = true;
    m_layer->removeAllCheckpoints();
    m_layer->resetLevelFromStart();
    m_resetting = false;
    release();
    GameToolbox::fast_srand(GameSeed);
    m_layer->m_randomSeed = m_layer->m_replayRandSeed = GameSeed;
    m_layer->m_extraDelta = 0;
    m_layer->m_clickBetweenSteps = m_layer->m_clickOnSteps = false;
    auto p = m_layer->m_player1;
    auto rect = p->getObjectRect();
    m_spawnFloor = p->getPositionY() - rect.size.height / 2.;
    m_scene.bind(m_layer);
    m_accumulator = 0;
    m_engine.beginAttempt();
    if (p->m_isDead ||
        (m_layer->m_gameState.m_isDualMode && m_layer->m_player2 && m_layer->m_player2->m_isDead))
        m_engine.markDeath();
}
rf::FormState Native::player(PlayerObject *p, int i) const {
    rf::FormState s;
    s.form = p->m_isShip     ? rf::Form::Ship
             : p->m_isDart   ? rf::Form::Wave
             : p->m_isBird   ? rf::Form::Ufo
             : p->m_isBall   ? rf::Form::Ball
             : p->m_isRobot  ? rf::Form::Robot
             : p->m_isSpider ? rf::Form::Spider
             : p->m_isSwing  ? rf::Form::Swing
                             : rf::Form::Cube;
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.vy = p->m_yVelocity;
    s.vx = std::abs(p->getCurrentXVelocity()) * (p->m_isGoingLeft ? -1 : 1);
    auto r = p->getObjectRect();
    s.halfWidth = std::clamp(static_cast<double>(r.size.width) / 2, 2., 50.);
    s.halfHeight = std::clamp(static_cast<double>(r.size.height) / 2, 2., 50.);
    s.scale = p->m_vehicleSize;
    s.inverted = p->m_isUpsideDown;
    s.grounded = p->m_isOnGround;
    s.gravity = std::abs(p->m_gravity);
    if (s.gravity < 1e-4)
        s.gravity = .958199;
    s.jump = std::abs(p->m_yStart);
    if (s.jump < 1 || s.jump > 30)
        s.jump = 11.18;
    s.gravityMod = std::clamp(static_cast<double>(std::abs(p->m_gravityMod)), .05, 10.);
    s.floor = m_spawnFloor;
    s.ceiling = 1e6;
    s.ringArmed = !p->m_touchedRing;
    s.padContact = p->m_touchedPad;
    s.player = m_layer->m_levelSettings->m_twoPlayerMode ? i + 1 : 0;
    if (m_layer->m_gameState.m_isDualMode || (s.form != rf::Form::Cube && s.form != rf::Form::Robot)) {
        double bottom = m_layer->getMinPortalY(), top = m_layer->getMaxPortalY();
        if (std::isfinite(bottom) && std::isfinite(top) && top - bottom > 2 * s.halfHeight + 8 &&
            s.y >= bottom - 30 && s.y <= top + 30) {
            s.floor = bottom;
            s.ceiling = top;
        } else if (s.form == rf::Form::Ship || s.form == rf::Form::Wave || s.form == rf::Form::Ufo ||
                   s.form == rf::Form::Swing) {
            s.floor = s.y - 150;
            s.ceiling = s.y + 150;
        }
    }
    m_scene.hint(s);
    return s;
}
bool Native::before(float dt, bool half, bool last) {
    if (!active() || pending())
        return false;
    if (dt == 0)
        return true;
    auto const &physics = m_engine.environment().at("physics");
    if (m_layer->m_isPracticeMode || m_layer->m_ignoreDamage || m_layer->m_isIgnoreDamageEnabled ||
        m_layer->m_clickBetweenSteps || m_layer->m_clickOnSteps ||
        std::abs(m_layer->m_gameState.m_timeWarp - 1) > 1e-6 ||
        m_layer->m_levelSettings->m_twoPlayerMode != physics["independent"].get<bool>() ||
        GameManager::get()->getGameVariable("0010") != physics["flip"].get<bool>()) {
        m_engine.stop("Изменился режим управления/физики");
        return false;
    }
    Frame f;
    f.timing = {dt, half, last};
    f.percent = m_layer->getCurrentPercent();
    f.independent = m_layer->m_levelSettings->m_twoPlayerMode;
    f.dual = m_layer->m_gameState.m_isDualMode;
    for (int i = 0; i < (f.dual ? 2 : 1); i++) {
        auto p = i ? m_layer->m_player2 : m_layer->m_player1;
        if (!p || p->m_isSideways) {
            m_engine.stop("Поворот оси на 90°/отсутствующий игрок не поддерживается");
            return false;
        }
        if (p->m_isDead) {
            m_engine.markDeath();
            return false;
        }
        f.players[i] = player(p, i);
        f.contacts[i] = p->m_ringRelatedSet.size() * 256ULL + p->m_touchedPad * 16ULL +
                        p->m_touchedRing * 2ULL + p->m_touchedGravityPortal * 4ULL + p->m_isOnSlope * 8ULL;
        f.disturbed[i] = p->m_isDashing || p->m_touchedRing || p->m_touchedGravityPortal || p->m_isOnSlope;
    }
    try {
        auto decision =
            m_engine.tick(f, [&](Frame const &frame) -> Scene const & { return m_scene.read(frame); });
        for (auto const &a : decision.actions)
            inject(a);
        return decision.advance;
    } catch (std::exception const &e) {
        m_engine.stop("Ошибка контроллера: " + std::string(e.what()));
        return false;
    }
}
bool Native::human(bool down, int button, bool side) {
    if (m_injecting)
        return true;
    if (!active())
        return !m_menu;
    if (m_engine.mode() == Mode::Recording && button == 1 && !m_menu)
        m_engine.human(rf::logicalPlayer(side, m_layer->m_levelSettings->m_twoPlayerMode,
                                         GameManager::get()->getGameVariable("0010")),
                       down);
    return false;
}
void Native::unexpectedReset() {
    if (!m_resetting && active())
        m_engine.stop("Внешний сброс уровня прервал операцию");
}
double Native::elapsed() const {
    return m_baseSeconds +
           std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
}
void Native::service() {
    m_scene.pump();
    if (m_layer && active() && !m_engine.restartRequested())
        m_engine.progress(m_layer->getCurrentPercent());
    m_engine.service(active() ? elapsed() : 0);
    if (m_layer && m_engine.bestPercent() > m_record) {
        m_record = m_engine.bestPercent();
        if (!m_recordKey.empty())
            Mod::get()->setSavedValue<double>(m_recordKey, m_record);
    }
    if (m_engine.consumeRelease()) {
        release();
        if (!active() && m_options && m_layer) {
            m_layer->m_clickBetweenSteps = m_cbs;
            m_layer->m_clickOnSteps = m_cos;
            m_options = false;
        }
    }
    m_savePending = m_engine.consumeSave() || m_savePending;
    if (m_savePending && m_layer && m_engine.best() &&
        (!active() || m_engine.mode() == Mode::Verifying || elapsed() >= m_nextSave))
        try {
            m_storage.submit(directory(), m_engine.snapshot());
            m_nextSave = elapsed() + 3;
            m_savePending = false;
        } catch (std::exception const &e) {
            m_notice = e.what();
        }
    if (m_engine.consumeCompletion() && m_layer) {
        m_finish = true;
        m_layer->levelComplete();
        m_finish = false;
    }
    if (active() && m_engine.restartRequested() && !m_menu && m_layer && !m_layer->m_isPaused)
        reset();
}
std::string Native::status() const {
    auto error = m_storage.error();
    return m_engine.status() +
           (m_scene.busy()     ? "\n" + m_scene.analysisStatus()
            : m_notice.empty() ? ""
                               : "\n" + m_notice) +
           (error.empty() ? "" : "\nОшибка записи: " + error);
}
} // namespace v2
