#include "FileCommit.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
static void write(fs::path const& path, char const* text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    assert(output.good());
}
static std::string read(fs::path const& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main() {
    auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    auto directory = fs::temp_directory_path() / ("cube-learner-test-" + std::to_string(token));
    fs::create_directories(directory);
    auto target = directory / std::filesystem::path(u8"прогресс.json");
    auto temporary = directory / std::filesystem::path(u8"прогресс.json.tmp");
    std::error_code error;
    write(temporary, "first");
    assert(autobot::replaceTrainingFile(temporary, target, error));
    assert(!error && read(target) == "first" && !fs::exists(temporary));
    write(temporary, "second");
    assert(autobot::replaceTrainingFile(temporary, target, error));
    assert(!error && read(target) == "second" && !fs::exists(temporary));
    assert(!autobot::replaceTrainingFile(directory / "missing.tmp", target, error));
    assert(error && read(target) == "second");
    fs::remove_all(directory);
}
