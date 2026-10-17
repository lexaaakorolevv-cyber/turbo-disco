#pragma once
#include <filesystem>
#include <system_error>

namespace autobot {
// Both files must reside on the same volume. A failed commit keeps the old file.
bool replaceTrainingFile(std::filesystem::path const& temporary,
                         std::filesystem::path const& destination,
                         std::error_code& error);
}
