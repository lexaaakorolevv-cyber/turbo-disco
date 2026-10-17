#include "storage.hpp"
#include <fstream>
namespace v2 {
Json readJson(std::filesystem::path const &p) {
    std::string error = "Файл не найден";
    for (auto const &file : {p, std::filesystem::path(p.string() + ".bak")})
        try {
            if (!std::filesystem::exists(file))
                continue;
            if (std::filesystem::file_size(file) > 96 * 1024 * 1024)
                throw std::runtime_error("Слишком большой JSON");
            std::ifstream in(file, std::ios::binary);
            if (!in)
                throw std::runtime_error("Файл не найден");
            return Json::parse(in);
        } catch (std::exception const &e) {
            error = e.what();
        }
    throw std::runtime_error(error);
}
void writeJson(std::filesystem::path const &p, Json const &j) {
    std::filesystem::create_directories(p.parent_path());
    auto temp = p.string() + ".new", backup = p.string() + ".bak";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("Не удалось открыть файл");
        out << j.dump();
        out.flush();
        if (!out)
            throw std::runtime_error("Ошибка записи JSON");
    }
    std::error_code ec;
    std::filesystem::remove(backup, ec);
    if (std::filesystem::exists(p))
        std::filesystem::rename(p, backup);
    try {
        std::filesystem::rename(temp, p);
    } catch (...) {
        if (!std::filesystem::exists(p) && std::filesystem::exists(backup))
            std::filesystem::rename(backup, p);
        throw;
    }
}
Storage::Storage() : m_thread([this] { run(); }) {}
Storage::~Storage() {
    drain();
    {
        std::lock_guard lock(m_mutex);
        m_exit = true;
    }
    m_cv.notify_all();
    m_thread.join();
}
void Storage::submit(std::filesystem::path p, Session s) {
    std::lock_guard lock(m_mutex);
    m_job = Job{std::move(p), std::move(s)};
    m_cv.notify_all();
}
void Storage::drain() {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] { return !m_job && !m_busy; });
}
std::string Storage::error() {
    std::lock_guard lock(m_mutex);
    return m_error;
}
void Storage::run() {
    for (;;) {
        std::optional<Job> job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_job || m_exit; });
            if (m_exit && !m_job)
                return;
            job = std::move(m_job);
            m_job.reset();
            m_busy = true;
        }
        std::string error;
        try {
            writeJson(job->directory / "best.json", encode(job->session.best));
            writeJson(job->directory / "search.json", encode(job->session));
            if (job->session.best.verified)
                writeJson(job->directory / "macro.json", encode(job->session.best));
        } catch (std::exception const &e) {
            error = e.what();
        }
        {
            std::lock_guard lock(m_mutex);
            m_error = std::move(error);
            m_busy = false;
        }
        m_cv.notify_all();
    }
}
} // namespace v2
