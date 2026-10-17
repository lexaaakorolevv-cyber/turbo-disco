#pragma once

#include "bot/Learner.hpp"
#include <filesystem>
#include <optional>
#include <string>

class GJGameLevel;

namespace autobot {

class ProgressStore final {
public:
    static std::optional<cubebot::TrainingData> load(
        std::filesystem::path const& path, std::string const& fingerprint);
    static bool save(std::filesystem::path const& path,
                     cubebot::TrainingData const& data);
    static std::string fingerprint(GJGameLevel* level);
    static std::filesystem::path pathFor(GJGameLevel* level);
};

} // namespace autobot
