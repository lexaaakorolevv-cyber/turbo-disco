#include "Controller.hpp"
#include "BotPopup.hpp"
#include "ProgressStore.hpp"
#include "Safety.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace autobot {
Controller& Controller::get() {
    static Controller instance;
    return instance;
}
bool Controller::ownsBase(GJBaseGameLayer* p) const {
    return m_layer && static_cast<GJBaseGameLayer*>(m_layer) == p;
}
void Controller::preserveLiveProgress() {
    if (m_enabled && m_attemptRunning && m_haveSnapshot)
        m_learner.recordInterrupted(m_last);
}
float Controller::bestPercent() const {
    auto best = m_learner.data().bestProgress;
    if (m_enabled && m_haveSnapshot) best = std::max(best, m_last.progress);
    return static_cast<float>(best);
}
void Controller::loadLevel() {
    m_fingerprint = ProgressStore::fingerprint(m_layer->m_level);
    m_path = ProgressStore::pathFor(m_layer->m_level);
    m_learner.reset(m_fingerprint);
    if (auto data = ProgressStore::load(m_path, m_fingerprint)) {
        if (!m_learner.load(*data, m_fingerprint))
            log::warn("Training file rejected by the learner: {}", m_path.string());
    }
}
void Controller::attach(PlayLayer* layer, bool controllable) {
    if (m_layer == layer) return;
    if (m_layer) { prepareExit(); clearAttachment(); }
    m_layer = layer;
    m_controllableSession = controllable;
    m_input.attach(layer);
    m_analyzer.reset();
    m_quitDepth = 0;
    m_detachPending = false;
    m_finished = false;
    m_haveSnapshot = false;
    loadLevel();
    m_status = "Выключен";
    auto size = CCDirector::get()->getWinSize();
    m_hud = botLabel("", size.width - 24.f, .38f);
    m_hud->setAnchorPoint({.5f, 1.f});
    m_hud->setPosition({size.width * .5f, size.height - 22.f});
    m_hud->setColor({255, 210, 90});
    m_hud->setID("bot-indicator"_spr);
    layer->addChild(m_hud, 10000);
    updateHUD();
}
void Controller::setEnabled(bool value) {
    if (!value) {
        stopWithStatus("Выключен; управление у игрока");
        persist();
        return;
    }
    if (!m_layer || m_enabled) return;
    if (!m_controllableSession || Loader::get()->getGameVersion() != "2.2081") {
        m_status = "Неподдерживаемая версия игры или native replay";
        return;
    }
    if (Mod::get()->getRequestedAction() == ModRequestedAction::Disable) {
        m_status = "Мод ожидает отключения после перезапуска";
        return;
    }
    if (!safety::ready()) {
        m_status = "Защита несовместима: включение запрещено";
        log::error("Bot refused to arm: a required safety hook is absent or disabled");
        updateHUD();
        return;
    }
    if (m_layer->m_isPlatformer || m_layer->m_isPracticeMode || m_layer->m_isTestMode) {
        m_status = "Нужен обычный classic-уровень с начала";
        updateHUD();
        return;
    }
    // Set the session latch BEFORE releasing or generating any game input.
    // It stays set through disabling, death and all restarts until scene exit.
    if (!m_tainted && !m_recordGuard.capture(m_layer)) {
        m_status = "Не удалось защитить сохранение: запуск запрещён";
        return;
    }
    m_tainted = true;
    m_enabled = true;
    m_finished = false;
    m_input.release();
    m_learner.stop();
    m_attemptRunning = false;
    m_waitingForReset = false;
    m_restartQueued = true;
    m_pauseQueued = false;
    m_status = "Готов; закройте окно и продолжите игру";
    updateHUD();
}
void Controller::startTraining() {
    if (!m_enabled) setEnabled(true);
    if (!m_enabled) return;
    preserveLiveProgress();
    persist();
    m_input.release();
    m_learner.stop();
    m_attemptRunning = false;
    m_restartQueued = true;
    m_waitingForReset = false;
    m_finished = false;
    m_status = "Обучение с начала после снятия паузы";
    updateHUD();
}
void Controller::resetProgress() {
    if (!m_layer) return;
    m_input.release();
    m_learner.reset(m_fingerprint);
    persist();
    m_attemptRunning = false;
    m_haveSnapshot = false;
    m_analyzer.reset();
    m_waitingForReset = false;
    m_finished = false;
    m_restartQueued = m_enabled;
    m_status = m_enabled ? "Прогресс сброшен; новая попытка после паузы" : "Прогресс обучения сброшен";
    updateHUD();
}
void Controller::stopWithStatus(std::string text) {
    preserveLiveProgress();
    m_enabled = false;
    m_restartQueued = false;
    m_waitingForReset = false;
    m_attemptRunning = false;
    m_input.release();
    m_learner.stop();
    m_status = std::move(text);
    persist();
    updateHUD();
}
void Controller::onFrame() {
    if (!m_layer) return;
    scrubProgress();
    if (m_enabled && Mod::get()->getRequestedAction() == ModRequestedAction::Disable) {
        setEnabled(false);
        m_status = "Мод отключён; защита сессии сохранена";
    }
    if (m_pauseQueued) {
        m_pauseQueued = false;
        if (!m_layer->m_isPaused) m_layer->pauseGame(false);
    }
    if (m_enabled && !m_layer->m_isPaused && (m_restartQueued ||
        (m_waitingForReset && std::chrono::steady_clock::now() >= m_retryAt))) {
        m_restartQueued = false;
        m_waitingForReset = false;
        // Use GD's own restart; never move the player or create a checkpoint.
        m_layer->resetLevelFromStart();
    }
    updateHUD();
}
void Controller::beforeReset() {
    if (!m_layer) return;
    preserveLiveProgress();
    persist();
    if (m_tainted) m_input.release();
    m_learner.stop();
    m_attemptRunning = false;
    m_waitingForReset = false;
    m_haveSnapshot = false;
    m_analyzer.reset();
}
void Controller::afterReset() {
    if (!m_layer) return;
    scrubProgress();
    auto fingerprint = ProgressStore::fingerprint(m_layer->m_level);
    if (fingerprint != m_fingerprint) {
        persist();
        loadLevel();
        m_status = "Данные уровня изменились; обучение сброшено";
    }
    if (m_enabled) {
        if (m_layer->m_isPracticeMode || m_layer->m_isPlatformer || m_layer->m_isTestMode) {
            stopWithStatus("Режим уровня не поддерживается");
            return;
        }
        m_learner.beginAttempt();
        m_attemptRunning = true;
        m_finished = false;
        m_restartQueued = false;
        m_status = "Анализ препятствий";
    }
    updateHUD();
}
void Controller::onProcess() {
    scrubProgress();
    if (m_enabled && Mod::get()->getRequestedAction() == ModRequestedAction::Disable) {
        setEnabled(false);
        return;
    }
    if (!m_enabled || !m_layer || !m_attemptRunning || m_layer->m_isPaused ||
        m_waitingForReset || m_restartQueued || !m_layer->m_player1 ||
        m_layer->m_player1->m_isDead) return;
    // processCommands can run at both half and full steps. The engine's
    // simulation time is the authority; never advance a replay on render FPS.
    double now = m_layer->m_gameState.m_levelTime;
    if (!std::isfinite(now)) { stopWithStatus("Некорректное время симуляции"); return; }
    if (m_haveSnapshot && now <= m_last.elapsed + 1e-9) return;
    auto s = m_analyzer.sample(m_layer);
    m_last = s;
    m_haveSnapshot = true;
    auto player = m_layer->m_player1;
    if (player->m_isSideways || player->m_isDashing) {
        persist();
        stopWithStatus("Dash или боковая гравитация: управление у игрока");
        return;
    }
    auto decision = m_learner.step(s);
    using S = cubebot::Status;
    switch (decision.status) {
        case S::UnsupportedDual:
            persist(); stopWithStatus("Dual пока не поддерживается; управление у игрока"); return;
        case S::UnsupportedMode:
            persist(); stopWithStatus("Этот режим пока не поддерживается; управление у игрока"); return;
        case S::InvalidState:
            persist(); stopWithStatus("Состояние игры не поддерживается; управление у игрока"); return;
        case S::Replaying: m_status = "Повтор удачной последовательности"; break;
        case S::Planning: m_status = "Выбор прыжка и проверка траекторий"; break;
        default: break;
    }
    m_input.apply(decision.held);
}
void Controller::onDeath(PlayerObject* player) {
    if (!m_enabled || !m_layer || !m_attemptRunning || m_waitingForReset ||
        (player != m_layer->m_player1 && player != m_layer->m_player2)) return;
    if (m_haveSnapshot) {
        auto final = m_last;
        final.progress = std::clamp(static_cast<double>(m_layer->getCurrentPercent()), 0., 100.);
        m_learner.recordDeath(final);
    }
    m_input.release();
    m_attemptRunning = false;
    m_waitingForReset = true;
    m_retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(650);
    m_status = "Гибель: корректировка времени около ошибки";
    persist();
    updateHUD();
}
void Controller::onCompletion() {
    if (!m_layer || m_finished) return;
    m_finished = true;
    if (m_enabled && m_attemptRunning && m_haveSnapshot) {
        auto final = m_last;
        final.progress = 100.;
        m_learner.recordCompletion(final);
        m_status = "Бот достиг конца; результат не засчитан";
    } else m_status = "Сессия завершена; результат не засчитан";
    m_enabled = false;
    m_attemptRunning = false;
    m_waitingForReset = false;
    m_restartQueued = false;
    m_input.release();
    m_learner.stop();
    m_pauseQueued = true;
    persist();
    updateHUD();
}
void Controller::updateHUD() {
    if (!m_hud) return;
    m_hud->setVisible(m_tainted);
    if (!m_tainted) return;
    auto text = fmt::format("{} | попытка {} | лучший {:.2f}% | {}",
        m_enabled ? "ИГРАЕТ БОТ" : "СЕССИЯ БОТА: БЕЗ НАГРАД", attempts(), bestPercent(), m_status);
    m_hud->setString(text.c_str());
    m_hud->limitLabelWidth(CCDirector::get()->getWinSize().width - 24.f, .38f, .18f);
}
void Controller::persist() {
    scrubProgress();
    if (!m_path.empty() && !ProgressStore::save(m_path, m_learner.data()))
        log::error("Could not persist bot progress: {}", m_path.string());
}
void Controller::prepareExit() {
    if (!m_layer) return;
    preserveLiveProgress();
    if (m_tainted) m_input.release();
    m_enabled = false;
    m_attemptRunning = false;
    m_restartQueued = false;
    m_waitingForReset = false;
    m_pauseQueued = false;
    m_learner.stop();
    persist();
}
void Controller::clearAttachment() {
    scrubProgress();
    m_recordGuard.clear();
    m_layer = nullptr;
    m_hud = nullptr;
    m_input.attach(nullptr);
    m_tainted = false;
    m_detachPending = false;
    m_haveSnapshot = false;
    m_controllableSession = false;
    m_status = "Уровень закрыт";
}
void Controller::beginQuit() { ++m_quitDepth; prepareExit(); }
void Controller::endQuit() {
    scrubProgress();
    if (m_quitDepth) --m_quitDepth;
    if (!m_quitDepth && m_detachPending) clearAttachment();
}
void Controller::didExit() {
    scrubProgress();
    m_hud = nullptr;
    if (m_quitDepth) m_detachPending = true;
    else clearAttachment();
}
void Controller::scrubProgress() {
    if (m_tainted) m_recordGuard.restore();
}
}
