#pragma once
#include "engine.hpp"
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
namespace v2 {
Json readJson(std::filesystem::path const &path);
void writeJson(std::filesystem::path const &path, Json const &value);
class Storage {
    struct Job {
        std::filesystem::path directory;
        Session session;
    };
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<Job> m_job;
    bool m_exit = false, m_busy = false;
    std::thread m_thread;
    std::string m_error;
    void run();

  public:
    Storage();
    ~Storage();
    void submit(std::filesystem::path directory, Session session);
    void drain();
    std::string error() const;
};
} // namespace v2
