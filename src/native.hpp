#pragma once
#include "engine.hpp"
#include "scene.hpp"
#include "storage.hpp"
#include <chrono>
namespace v2 {
class Native {
    PlayLayer *m_layer = nullptr;
    Engine m_engine;
    SceneReader m_scene;
    Storage m_storage;
    std::optional<Run> m_loaded;
    std::string m_notice, m_recordKey;
    bool m_injecting = false, m_resetting = false, m_menu = false, m_finish = false, m_options = false,
         m_cbs = false, m_cos = false, m_savePending = false;
    double m_accumulator = 0, m_spawnFloor = 91, m_record = 0, m_baseSeconds = 0, m_nextSave = 0;
    std::chrono::steady_clock::time_point m_started;
    Json environment() const;
    std::filesystem::path directory() const;
    std::string unsupported() const;
    void prepare();
    void release();
    void inject(Action const &action);
    void reset();
    rf::FormState player(PlayerObject *p, int index) const;

  public:
    static Native &get();
    void bind(PlayLayer *layer);
    void unbind(PlayLayer *layer);
    bool owns(GJBaseGameLayer *layer) const {
        return layer == m_layer;
    }
    bool active() const {
        return m_engine.active();
    }
    bool pending() const {
        return m_engine.pending();
    }
    bool menu() const {
        return m_menu;
    }
    void menu(bool value) {
        if (!value && m_scene.busy())
            m_scene.cancel();
        m_menu = value;
        m_accumulator = 0;
    }
    bool finishing() const {
        return m_finish;
    }
    bool resetting() const {
        return m_resetting;
    }
    Engine const &engine() const {
        return m_engine;
    }
    void search();
    void record();
    void stop();
    void load();
    void save();
    void replay();
    void analyze();
    bool human(bool down, int button, bool side);
    bool before(float dt, bool half, bool last);
    bool outer() {
        return m_engine.advanceOuter();
    }
    void died() {
        m_engine.markDeath();
    }
    void completed() {
        m_engine.markComplete();
    }
    void unexpectedReset();
    void service();
    double &accumulator() {
        return m_accumulator;
    }
    double best() const {
        return std::max(m_record, m_engine.bestPercent());
    }
    std::string status() const;
    double elapsed() const;
};
void showMenu(PauseLayer *pause);
void attachHud(PlayLayer *layer);
} // namespace v2
