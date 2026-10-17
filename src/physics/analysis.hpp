#pragma once
#include "intelligence.hpp"
#include <atomic>
#include <mutex>
#include <stop_token>
#include <thread>

namespace rf {
enum class MapKind { Solid, Hazard, Orb, Pad, Mode, Gravity, Size, Speed, Dual, Complex, Script, Count };
struct MapRect {
    double left, right, bottom, top;
};
struct MapObject {
    double x = 0, y = 0;
    MapKind kind = MapKind::Hazard;
    Form form = Form::Cube;
    bool uncertain = false;
    std::optional<MapRect> bounds;
};
struct AnalysisHint {
    double horizon = 42;
    unsigned interactions = 0, obstacles = 0;
    std::optional<double> targetY;
};
struct LevelAnalysis {
    std::array<unsigned, static_cast<unsigned>(MapKind::Count)> counts{};
    std::array<bool, 8> forms{};
    std::vector<MapObject> objects;
    unsigned scanned = 0, uncertain = 0, chains = 0;
    double minX = 0, maxX = 1;
    double maxHalfWidth = 0;
    bool twoPlayer = false, startsDual = false;
    std::string report;
    nlohmann::json toJson() const;
    // A structural hint only. It never replaces live native collision/input.
    AnalysisHint hint(FormState const &state) const;
};
struct AnalysisJob {
    std::vector<MapObject> objects;
    unsigned scanned = 0;
    Form startForm = Form::Cube;
    bool twoPlayer = false, startsDual = false;
};
LevelAnalysis analyzeLevel(AnalysisJob job, std::stop_token stop = {});
// Contains copied numbers, never pointers or methods from Geometry Dash.
class AnalysisWorker {
    std::jthread m_thread;
    std::atomic<bool> m_busy = false;
    std::mutex m_mutex;
    std::optional<LevelAnalysis> m_result;
    std::string m_error;

  public:
    ~AnalysisWorker();
    bool start(AnalysisJob job);
    void cancel() {
        m_thread.request_stop();
    }
    bool busy() const {
        return m_busy.load();
    }
    std::optional<LevelAnalysis> take(std::string &error);
};
} // namespace rf
