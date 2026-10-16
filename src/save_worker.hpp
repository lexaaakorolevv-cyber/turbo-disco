#pragma once
#include "core.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace rf {
struct SearchSaveJob {
    std::filesystem::path bestPath,sessionPath;
    Macro best;
    Search search;
    std::vector<Action> plan;
    std::uint64_t seed=0,attempts=0,epoch=0;
    double seconds=0;
    Tick guidance=0;
    nlohmann::json learning,secondLearning,failures;
    bool writeBest=true;
};
struct SearchSaveResult {std::uint64_t epoch;std::string error;double percent;std::uint64_t attempts;};
// Owns copies only: no Geometry Dash/Geode objects or pointers cross threads.
// At most one running and one pending save; a new pending snapshot replaces it.
class SearchSaveWorker {
    std::mutex m_mutex;std::condition_variable m_ready,m_idle;
    std::optional<SearchSaveJob> m_pending;
    std::optional<SearchSaveResult> m_result;
    bool m_busy=false,m_stopping=false;
    std::thread m_thread;
    void run();
public:
    SearchSaveWorker();
    ~SearchSaveWorker();
    void submit(SearchSaveJob job);
    void drain();
    std::optional<SearchSaveResult> result();
};
}
