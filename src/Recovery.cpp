#include "Runtime.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace rf {
double Runtime::elapsedSeconds() const {
    return active() ? std::chrono::duration<double>(std::chrono::steady_clock::now()-m_started).count() : m_runElapsed;
}
std::filesystem::path Runtime::sessionPath() const {
    auto const& level=m_environment.at("level");
    auto id=level.at("id").get<int>();
    auto hash=level.at("data_sha256").get<std::string>().substr(0,16);
    return Mod::get()->getSaveDir()/"macros"/fmt::format("{}_{}.search.json",id,hash);
}
bool Runtime::restoreSearch() {
    auto path=sessionPath();
    if (std::filesystem::exists(path) || std::filesystem::exists(path.string()+".bak")) {
        try {
            auto payload=loadJsonFile(path);
            auto saved=parseSearchSession(payload);
            if (saved.best.environment!=m_environment || saved.seed!=m_seed ||
                saved.search.window()!=static_cast<Tick>(Mod::get()->getSettingValue<int64_t>("mutation-window")))
                throw std::runtime_error("Changed level/environment/seed/search window");
            Calibration calibration; FailureMemory failures;
            if(payload.contains("learning"))calibration=Calibration::fromJson(payload.at("learning"));
            if(payload.contains("failures"))failures=FailureMemory::fromJson(payload.at("failures"));
            Tick guidance=saved.search.guidanceFrom();
            if(payload.contains("pending_guidance")){
                auto const& v=payload.at("pending_guidance");
                if(!v.is_number_integer()||v.get<double>()<0||v.get<double>()>MaxTicks)throw std::runtime_error("Invalid pending guidance");
                guidance=v.get<Tick>();
            }
            m_calibration=std::move(calibration);m_failures=std::move(failures);
            m_best=std::move(saved.best);
            m_previousAttempts=saved.attempts; m_previousSeconds=saved.seconds;
            m_search=std::make_unique<Search>(std::move(saved.search));
            m_plan=std::move(saved.plan); m_flightFrom=guidance;
            m_mode=Mode::Searching;
            if (m_best->completed) {
                m_target=m_best; m_plan=m_best->actions; m_verifyingSearch=true; m_mode=Mode::Verifying;
            }
            m_restart=true;
            m_status=m_mode==Mode::Verifying ? "Восстановлен полный кандидат. Проверка с начала" :
                "Поиск продолжен: архив и варианты восстановлены; 16 CPS";
            m_autosaveStatus=fmt::format("Восстановлено {:.2f}% | Всего попыток: {}",m_best->bestPercent,m_previousAttempts);
            return true;
        } catch (std::exception const& e) {
            log::warn("Route Finder could not restore session: {}",e.what());
            m_autosaveStatus="Сеанс несовместим/повреждён; ищу сохранённый лучший макрос";
        }
    }
    // A valid draft remains useful if the larger session is unavailable.
    auto bestPath=macroPath(true);
    if (std::filesystem::exists(bestPath) || std::filesystem::exists(bestPath.string()+".bak")) {
        try {
            auto draft=loadMacro(bestPath);
            if (draft.environment!=m_environment) throw std::runtime_error("Changed environment");
            draft.verified=false;
            m_selected=std::move(draft);
            m_autosaveStatus="Загружен лучший участок; архив поиска создаётся заново";
        } catch (std::exception const& e) {
            log::warn("Route Finder draft unavailable: {}",e.what());
            m_autosaveStatus="Сохранение несовместимо с текущей версией; начат новый поиск";
        }
    }
    return false;
}
void Runtime::autosaveSearch(bool force) {
    if (!m_search || m_environment.empty() || (m_best && m_best->verified)) return;
    if (!active() && !force && !m_autosaveDirty && !m_autosaveUrgent) return;
    auto now=std::chrono::steady_clock::now();
    auto since=std::chrono::duration<double>(now-m_lastAutosave).count();
    if (!force && !m_autosaveUrgent && since<30 && (!m_autosaveDirty || since<5)) return;
    // Called outside the game's simulation stack. Include a better in-flight
    // prefix; replaying it still restarts the real game, never restores state.
    if ((m_mode==Mode::Searching || m_mode==Mode::Assisting) && !m_restart && m_tick>0 &&
        (!m_best || m_trialPercent>m_best->bestPercent+1e-6)) remember(snapshot());
    if (!m_best || !m_best->ticks) return;
    m_lastAutosave=now;
    m_autosaveUrgent=false;
    try {
        auto draft=*m_best; draft.verified=false;
        draft.attempts=m_previousAttempts+m_attempts;
        draft.stopReason="Автосохранение поиска: "+m_status;
        // Write the independent best draft first: it survives even if saving
        // the richer search state exceeds its size budget or fails afterwards.
        if (m_autosaveDirty || force) saveMacro(macroPath(true),draft);
        auto pending=m_mode==Mode::Assisting?prefix(m_trial.actions,m_tick):m_plan;
        auto saved=searchSessionJson(draft,*m_search,pending,m_seed,m_previousAttempts+m_attempts,
            m_previousSeconds+elapsedSeconds());
        saved["learning"]=m_calibration.toJson();saved["failures"]=m_failures.toJson();
        saved["pending_guidance"]=m_mode==Mode::Assisting?m_tick:m_flightFrom;
        saveJsonFile(sessionPath(),saved,-1);
        m_autosaveDirty=false;
        m_autosaveStatus=fmt::format("Автосохранено {:.2f}% | Всего попыток: {}",draft.bestPercent,m_previousAttempts+m_attempts);
    } catch (std::exception const& e) {
        m_autosaveStatus=fmt::format("Ошибка автосохранения: {}",e.what());
        log::error("Route Finder autosave failed: {}",e.what());
    }
}
}
