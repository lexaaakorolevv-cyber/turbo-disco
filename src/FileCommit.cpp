#include "FileCommit.hpp"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace autobot {
bool replaceTrainingFile(std::filesystem::path const& temporary,
                         std::filesystem::path const& destination,
                         std::error_code& error) {
    error.clear();
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}
}
