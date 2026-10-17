#include "ProgressStore.hpp"
#include "FileCommit.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <matjson.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <exception>
#include <limits>
#include <string_view>

namespace autobot {
namespace {

constexpr std::uintmax_t MaxFileBytes = 8u * 1024u * 1024u;
constexpr std::size_t MaxEvents = 100000;
// Change this key whenever action timing, observation, or planner semantics change.
constexpr std::string_view AlgorithmKey = "cube-trajectory-frontier-v1-gd22081";

class StableHash {
    std::uint64_t m_value = 14695981039346656037ull;
    void byte(unsigned char value) {
        m_value ^= value;
        m_value *= 1099511628211ull;
    }
public:
    void add(std::string_view value) {
        // Length framing avoids ambiguity when level data contains separators.
        auto size = static_cast<std::uint64_t>(value.size());
        for (unsigned shift = 0; shift < 64; shift += 8)
            byte(static_cast<unsigned char>((size >> shift) & 0xffu));
        for (unsigned char character : value) byte(character);
    }
    void addNumber(std::int64_t value) { add(std::to_string(value)); }
    std::string hex() const { return fmt::format("{:016x}", m_value); }
};

bool unsignedField(matjson::Value const& object, std::string_view key,
                   std::uint64_t& value) {
    if (!object.contains(key)) return false;
    auto const& field = object[key];
    if (!field.isExactlyInt() && !field.isExactlyUInt()) return false;
    if (field.isExactlyInt()) {
        auto signedValue = field.asInt();
        if (!signedValue || signedValue.unwrap() < 0) return false;
    }
    auto parsed = field.asUInt();
    if (!parsed || parsed.unwrap() > std::numeric_limits<std::uint64_t>::max())
        return false;
    value = static_cast<std::uint64_t>(parsed.unwrap());
    return true;
}

bool numberField(matjson::Value const& object, std::string_view key, double& value) {
    if (!object.contains(key)) return false;
    auto parsed = object[key].asDouble();
    if (!parsed || !std::isfinite(parsed.unwrap())) return false;
    value = parsed.unwrap();
    return true;
}

bool booleanField(matjson::Value const& object, std::string_view key, bool& value) {
    if (!object.contains(key)) return false;
    auto parsed = object[key].asBool();
    if (!parsed) return false;
    value = parsed.unwrap();
    return true;
}

bool validData(cubebot::TrainingData const& data) {
    if (data.schema != cubebot::TrainingData::Schema ||
        data.levelFingerprint.empty() || data.levelFingerprint.size() > 256 ||
        !std::isfinite(data.bestProgress) || data.bestProgress < 0.0 ||
        data.bestProgress > 100.0 || !std::isfinite(data.frontierTime) ||
        data.frontierTime < 0.0 || data.frontierTime > 86400.0 ||
        data.bestActions.size() > MaxEvents) return false;
    double previous = -1.0;
    for (auto const& event : data.bestActions) {
        if (!std::isfinite(event.time) || event.time < 0.0 ||
            event.time < previous || event.time > data.frontierTime + .05 ||
            event.channel > 1) return false;
        previous = event.time;
    }
    return true;
}

// Protect the recursive JSON parser against excessively nested corrupt files.
// The saved format has only three structural levels; strings may contain braces.
bool boundedNesting(std::string_view source) {
    unsigned depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char character : source) {
        if (inString) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inString = false;
        }
        else if (character == '"') inString = true;
        else if (character == '{' || character == '[') {
            if (++depth > 8) return false;
        }
        else if (character == '}' || character == ']') {
            if (depth == 0) return false;
            --depth;
        }
    }
    return !inString && depth == 0;
}

} // namespace

std::optional<cubebot::TrainingData> ProgressStore::load(
    std::filesystem::path const& path, std::string const& fingerprint) {
    try {
        if (path.empty() || fingerprint.empty()) return std::nullopt;
        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec);
        if (ec) {
            geode::log::warn("Cannot inspect bot training file: {}", ec.message());
            return std::nullopt;
        }
        if (!exists) return std::nullopt;
        auto size = std::filesystem::file_size(path, ec);
        if (ec || size == 0 || size > MaxFileBytes) {
            geode::log::warn("Rejected bot training file: unreadable or invalid size");
            return std::nullopt;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            geode::log::warn("Cannot open bot training file for reading");
            return std::nullopt;
        }
        std::string source(static_cast<std::size_t>(size), '\0');
        input.read(source.data(), static_cast<std::streamsize>(size));
        if (!input || input.peek() != std::char_traits<char>::eof() ||
            !boundedNesting(source)) return std::nullopt;
        auto parsed = matjson::parse(source);
        if (!parsed) {
            geode::log::warn("Rejected malformed bot training JSON");
            return std::nullopt;
        }
        auto const& json = parsed.unwrap();
        if (!json.isObject() || !json.contains("algorithm") ||
            !json.contains("fingerprint") || !json.contains("actions")) return std::nullopt;
        auto algorithm = json["algorithm"].asString();
        auto storedFingerprint = json["fingerprint"].asString();
        if (!algorithm || algorithm.unwrap() != AlgorithmKey ||
            !storedFingerprint || storedFingerprint.unwrap() != fingerprint)
            return std::nullopt;
        cubebot::TrainingData data;
        std::uint64_t schema = 0;
        if (!unsignedField(json, "schema", schema) ||
            schema != cubebot::TrainingData::Schema ||
            !unsignedField(json, "attempts", data.attempts) ||
            !unsignedField(json, "failedTrials", data.failedTrials) ||
            !numberField(json, "bestProgress", data.bestProgress) ||
            !numberField(json, "frontierTime", data.frontierTime) ||
            !booleanField(json, "completed", data.completed)) return std::nullopt;
        data.schema = static_cast<std::uint32_t>(schema);
        data.levelFingerprint = storedFingerprint.unwrap();
        auto actions = json["actions"].asArray();
        if (!actions || actions.unwrap().size() > MaxEvents) return std::nullopt;
        data.bestActions.reserve(actions.unwrap().size());
        for (auto const& action : actions.unwrap()) {
            cubebot::InputEvent event;
            std::uint64_t channel = 0;
            if (!action.isObject() || !numberField(action, "time", event.time) ||
                !unsignedField(action, "channel", channel) || channel > 1 ||
                !booleanField(action, "down", event.down)) return std::nullopt;
            event.channel = static_cast<std::uint8_t>(channel);
            data.bestActions.push_back(event);
        }
        if (!validData(data)) return std::nullopt;
        // The controller must also pass this through CubeLearner::load so its
        // active PlannerConfig and future schema rules remain authoritative.
        return data;
    }
    catch (std::exception const& error) {
        geode::log::warn("Cannot load bot training: {}", error.what());
        return std::nullopt;
    }
}

bool ProgressStore::save(std::filesystem::path const& path,
                         cubebot::TrainingData const& data) {
    std::filesystem::path temporary;
    try {
        if (path.empty() || !validData(data)) return false;
        std::error_code ec;
        auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                geode::log::warn("Cannot create bot training directory: {}", ec.message());
                return false;
            }
        }
        matjson::Value json;
        json["algorithm"] = std::string(AlgorithmKey);
        json["schema"] = data.schema;
        json["fingerprint"] = data.levelFingerprint;
        json["attempts"] = data.attempts;
        json["failedTrials"] = data.failedTrials;
        json["bestProgress"] = data.bestProgress;
        json["frontierTime"] = data.frontierTime;
        json["completed"] = data.completed;
        std::vector<matjson::Value> actions;
        actions.reserve(data.bestActions.size());
        for (auto const& event : data.bestActions) {
            matjson::Value action;
            action["time"] = event.time;
            action["channel"] = event.channel;
            action["down"] = event.down;
            actions.push_back(std::move(action));
        }
        json["actions"] = std::move(actions);
        auto source = json.dump(matjson::NO_INDENTATION);
        if (source.size() > MaxFileBytes) return false;
        temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                geode::log::warn("Cannot create temporary bot training file");
                return false;
            }
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            output.close();
            if (!output) {
                geode::log::warn("Cannot write bot training file");
                std::filesystem::remove(temporary, ec);
                return false;
            }
        }
        // Windows filesystem::rename does not replace an existing file. Use
        // MoveFileExW without deleting the previous successful save first.
        if (!replaceTrainingFile(temporary, path, ec)) {
            geode::log::warn("Cannot commit bot training file: {}", ec.message());
            std::filesystem::remove(temporary, ec);
            return false;
        }
        return true;
    }
    catch (std::exception const& error) {
        geode::log::warn("Cannot save bot training: {}", error.what());
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        return false;
    }
}

std::string ProgressStore::fingerprint(GJGameLevel* level) {
    if (!level) return {};
    StableHash hash;
    hash.add(AlgorithmKey);
    hash.add(GEODE_GD_VERSION_STRING);
    hash.add(geode::Loader::get()->getGameVersion());
    hash.addNumber(level->m_levelID.value());
    hash.addNumber(static_cast<int>(level->m_levelType));
    hash.addNumber(level->m_levelVersion);
    hash.addNumber(level->m_gameVersion);
    hash.add(std::string_view(level->m_levelString.data(), level->m_levelString.size()));
    return "cube-v1-" + hash.hex();
}

std::filesystem::path ProgressStore::pathFor(GJGameLevel* level) {
    if (!level) return {};
    auto id = level->m_levelID.value();
    auto type = static_cast<int>(level->m_levelType);
    std::string filename;
    if (id > 0) filename = fmt::format("type-{}-id-{}.json", type, id);
    else {
        StableHash hash;
        hash.add(std::string_view(level->m_levelString.data(), level->m_levelString.size()));
        filename = fmt::format("type-{}-local-{}.json", type, hash.hex());
    }
    return geode::Mod::get()->getSaveDir() / "training" / filename;
}

} // namespace autobot
