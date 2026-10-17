#pragma once
#include "types.hpp"
#include "physics/analysis.hpp"
#include <Geode/Geode.hpp>
namespace v2 {
class SceneReader {
    PlayLayer *m_layer = nullptr;
    unsigned m_count = 0;
    std::vector<geode::Ref<GameObject>> m_objects;
    rf::MotionTracker m_motion;
    Scene m_scene;
    rf::AnalysisWorker m_worker;
    rf::AnalysisJob m_job;
    std::optional<rf::LevelAnalysis> m_map;
    std::size_t m_index = 0;
    bool m_capturing = false;
    std::string m_analysis = "Analyze не запускался";

  public:
    void bind(PlayLayer *layer);
    void reset();
    Scene const &read(Frame const &frame);
    std::string report() const;
    void analyze();
    void pump();
    void cancel();
    bool busy() const {
        return m_capturing || m_worker.busy();
    }
    void hint(rf::FormState &state) const;
    std::string const &analysisStatus() const {
        return m_analysis;
    }
};
} // namespace v2
