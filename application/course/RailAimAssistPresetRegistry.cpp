#include "RailAimAssistPresetRegistry.h"

#include "CourseAssetParsing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr uintmax_t kMaximumPresetBytes = 64u * 1024u;
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& hash, const char* data, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        hash ^= static_cast<uint8_t>(data[index]);
        hash *= kFnvPrime;
    }
}

void HashString(uint64_t& hash, const std::string& value) {
    HashBytes(hash, value.data(), value.size());
    constexpr char separator = '\0';
    HashBytes(hash, &separator, 1);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ParseUInt(std::string_view text, uint32_t& output) {
    if (text.empty()) {
        return false;
    }
    uint32_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseFloat(std::string_view text, float& output) {
    if (text.empty()) {
        return false;
    }
    std::string owned{text};
    char* end = nullptr;
    const float parsed = std::strtof(owned.c_str(), &end);
    if (end == owned.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

bool ParseBool(std::string value, bool& output) {
    value = Lower(course_asset_parsing::Trim(std::move(value)));
    if (value == "1" || value == "true" || value == "yes") {
        output = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        output = false;
        return true;
    }
    return false;
}

bool ValidId(const std::string& value) {
    return !value.empty() && value.size() <= 96 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/';
        });
}


std::string ReadAllBytes(const std::filesystem::path& path, std::string& errorMessage) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        errorMessage = "could not read " + path.generic_string();
        return {};
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        errorMessage = "failed while reading " + path.generic_string();
        return {};
    }
    return stream.str();
}

uint32_t ActiveFallbackCount(
    const std::unordered_map<std::string, RailAimAssistPreset>& fallbackPresets,
    const std::unordered_map<std::string, RailAimAssistPreset>& loadedPresets) {
    uint32_t count = 0;
    for (const auto& [presetId, preset] : fallbackPresets) {
        (void)preset;
        if (!loadedPresets.contains(presetId)) {
            ++count;
        }
    }
    return count;
}
} // namespace

bool RailAimAssistPreset::LoadFromFile(
    const std::filesystem::path& path,
    std::string* errorMessage) {
    auto reject = [errorMessage, &path](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = path.generic_string() + ": " + message;
        }
        return false;
    };

    std::error_code fileError;
    const uintmax_t fileBytes = std::filesystem::file_size(path, fileError);
    if (fileError || fileBytes == 0 || fileBytes > kMaximumPresetBytes) {
        return reject("file is missing, empty, or exceeds the 64 KiB limit");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return reject("could not open aim-assist preset");
    }

    RailAimAssistPreset loaded{};
    std::unordered_map<std::string, std::string> values;
    std::string line;
    uint32_t lineNumber = 0;
    bool headerRead = false;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = course_asset_parsing::Trim(std::move(line));
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (!headerRead) {
            const std::vector<std::string> header = course_asset_parsing::SplitPipe(line);
            uint32_t schema = 0;
            if (header.size() != 2 || header[0] != "RAIL_AIM_ASSIST_PRESET" ||
                !ParseUInt(header[1], schema) ||
                schema != kRailAimAssistPresetSchemaVersion) {
                return reject("unsupported or missing RAIL_AIM_ASSIST_PRESET schema header");
            }
            loaded.schemaVersion = schema;
            headerRead = true;
            continue;
        }
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return reject("expected key=value at line " + std::to_string(lineNumber));
        }
        std::string key = course_asset_parsing::Trim(line.substr(0, separator));
        std::string value = course_asset_parsing::Trim(line.substr(separator + 1));
        if (key.empty() || !values.emplace(key, value).second) {
            return reject("empty or duplicate key at line " + std::to_string(lineNumber));
        }
    }
    if (!headerRead) {
        return reject("aim-assist preset header was not found");
    }

    const std::unordered_set<std::string> allowedKeys{
        "presetId", "displayName", "enabled", "mouseKeyboardEnabled",
        "gamepadEnabled", "requireWorldVisibility", "minimumDistance",
        "maximumDistance", "mouseAcquireAngleDegrees", "gamepadAcquireAngleDegrees",
        "retentionAngleMultiplier", "mouseMagnetismStrength",
        "gamepadMagnetismStrength", "mouseMaximumCorrectionDegrees",
        "gamepadMaximumCorrectionDegrees", "maximumCorrectionSpeedDegrees",
        "highIntentReticleSpeed", "minimumHighIntentStrength",
        "targetSwitchAdvantage", "targetRetentionSeconds", "angleWeight",
        "forwardWeight", "anchorPriorityWeight", "enemyPriorityBonus",
        "retainedTargetBonus", "maximumVisibilityQueries"};
    for (const auto& [key, value] : values) {
        (void)value;
        if (!allowedKeys.contains(key)) {
            return reject("unknown key: " + key);
        }
    }

    const auto find = [&](const char* key) -> const std::string* {
        const auto value = values.find(key);
        return value != values.end() ? &value->second : nullptr;
    };
    const std::string* presetId = find("presetId");
    if (presetId == nullptr || presetId->empty()) {
        return reject("presetId is required");
    }
    loaded.presetId = *presetId;
    loaded.displayName = find("displayName") != nullptr
        ? *find("displayName")
        : loaded.presetId;

    auto parseFloat = [&](const char* key, float& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseFloat(*value, target);
    };
    auto parseBool = [&](const char* key, bool& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseBool(*value, target);
    };
    auto parseUInt = [&](const char* key, uint32_t& target) {
        const std::string* value = find(key);
        return value == nullptr || ParseUInt(*value, target);
    };
    RailAimAssistSettings& settings = loaded.settings;
    if (!parseBool("enabled", settings.enabled) ||
        !parseBool("mouseKeyboardEnabled", settings.mouseKeyboardEnabled) ||
        !parseBool("gamepadEnabled", settings.gamepadEnabled) ||
        !parseBool("requireWorldVisibility", settings.requireWorldVisibility) ||
        !parseFloat("minimumDistance", settings.minimumDistance) ||
        !parseFloat("maximumDistance", settings.maximumDistance) ||
        !parseFloat("mouseAcquireAngleDegrees", settings.mouseAcquireAngleDegrees) ||
        !parseFloat("gamepadAcquireAngleDegrees", settings.gamepadAcquireAngleDegrees) ||
        !parseFloat("retentionAngleMultiplier", settings.retentionAngleMultiplier) ||
        !parseFloat("mouseMagnetismStrength", settings.mouseMagnetismStrength) ||
        !parseFloat("gamepadMagnetismStrength", settings.gamepadMagnetismStrength) ||
        !parseFloat("mouseMaximumCorrectionDegrees", settings.mouseMaximumCorrectionDegrees) ||
        !parseFloat("gamepadMaximumCorrectionDegrees", settings.gamepadMaximumCorrectionDegrees) ||
        !parseFloat("maximumCorrectionSpeedDegrees", settings.maximumCorrectionSpeedDegrees) ||
        !parseFloat("highIntentReticleSpeed", settings.highIntentReticleSpeed) ||
        !parseFloat("minimumHighIntentStrength", settings.minimumHighIntentStrength) ||
        !parseFloat("targetSwitchAdvantage", settings.targetSwitchAdvantage) ||
        !parseFloat("targetRetentionSeconds", settings.targetRetentionSeconds) ||
        !parseFloat("angleWeight", settings.angleWeight) ||
        !parseFloat("forwardWeight", settings.forwardWeight) ||
        !parseFloat("anchorPriorityWeight", settings.anchorPriorityWeight) ||
        !parseFloat("enemyPriorityBonus", settings.enemyPriorityBonus) ||
        !parseFloat("retainedTargetBonus", settings.retainedTargetBonus) ||
        !parseUInt("maximumVisibilityQueries", settings.maximumVisibilityQueries)) {
        return reject("one or more preset values are malformed");
    }

    std::string validationError;
    if (!loaded.Validate(&validationError)) {
        return reject(validationError);
    }
    *this = std::move(loaded);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

bool RailAimAssistPreset::Validate(std::string* errorMessage) const {
    auto reject = [errorMessage](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (schemaVersion != kRailAimAssistPresetSchemaVersion) {
        return reject("unsupported aim-assist preset schema version");
    }
    if (!ValidId(presetId)) {
        return reject("presetId contains unsupported characters or exceeds its limit");
    }
    if (displayName.empty() || displayName.size() > 128) {
        return reject("displayName must contain 1-128 characters");
    }
    return settings.Validate(errorMessage);
}

RailAimAssistPresetRegistry::RailAimAssistPresetRegistry() {
    Reset();
}

void RailAimAssistPresetRegistry::Reset() {
    fallbackPresets_ = BuildFallbackPresets();
    loadedPresets_.clear();
    directory_.clear();
    loadedFingerprint_ = 0;
    stats_ = {};
    stats_.fallbackPresetCount = static_cast<uint32_t>(fallbackPresets_.size());
    stats_.usingFallback = true;
    stats_.status = "Fallback aim-assist presets active";
}

RailAimAssistPresetRegistry::PresetMap
RailAimAssistPresetRegistry::BuildFallbackPresets() {
    PresetMap presets;
    RailAimAssistPreset gamepad{};
    gamepad.presetId = "gamepad_standard";
    gamepad.displayName = "Gamepad Standard (Fallback)";
    presets.emplace(gamepad.presetId, gamepad);

    RailAimAssistPreset lockOn{};
    lockOn.presetId = "lock_on_standard";
    lockOn.displayName = "Lock-On Standard (Fallback)";
    lockOn.settings.mouseAcquireAngleDegrees = 3.5f;
    lockOn.settings.gamepadAcquireAngleDegrees = 7.0f;
    lockOn.settings.mouseMagnetismStrength = 0.10f;
    lockOn.settings.gamepadMagnetismStrength = 0.42f;
    lockOn.settings.mouseMaximumCorrectionDegrees = 1.5f;
    lockOn.settings.gamepadMaximumCorrectionDegrees = 5.0f;
    presets.emplace(lockOn.presetId, lockOn);
    return presets;
}

bool RailAimAssistPresetRegistry::StageDirectory(
    const std::filesystem::path& directory,
    PresetMap& stagedPresets,
    uint64_t& fingerprint,
    std::string& errorMessage) const {
    stagedPresets.clear();
    fingerprint = kFnvOffset;
    std::error_code error;
    if (!std::filesystem::exists(directory, error) ||
        !std::filesystem::is_directory(directory, error)) {
        errorMessage = "Aim-assist preset directory is unavailable: " +
            directory.generic_string();
        return false;
    }

    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (iterator->is_regular_file(error) &&
            iterator->path().extension() == ".aimassist") {
            paths.push_back(iterator->path());
        }
    }
    if (error) {
        errorMessage = "Could not enumerate aim-assist preset directory: " +
            error.message();
        return false;
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    if (paths.empty()) {
        errorMessage = "Aim-assist preset directory contains no .aimassist assets";
        return false;
    }

    for (const std::filesystem::path& path : paths) {
        std::string readError;
        const std::string bytes = ReadAllBytes(path, readError);
        if (!readError.empty()) {
            errorMessage = std::move(readError);
            return false;
        }
        HashString(fingerprint, path.filename().generic_string());
        HashBytes(fingerprint, bytes.data(), bytes.size());

        RailAimAssistPreset preset{};
        std::string loadError;
        if (!preset.LoadFromFile(path, &loadError)) {
            errorMessage = std::move(loadError);
            return false;
        }
        const std::string presetId = preset.presetId;
        if (!stagedPresets.emplace(presetId, std::move(preset)).second) {
            errorMessage = "Duplicate presetId '" + presetId + "' in " +
                directory.generic_string();
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

bool RailAimAssistPresetRegistry::LoadDirectory(
    const std::filesystem::path& directory,
    std::string* errorMessage) {
    PresetMap stagedPresets;
    uint64_t fingerprint = 0;
    std::string stagingError;
    if (!StageDirectory(directory, stagedPresets, fingerprint, stagingError)) {
        ++stats_.failedReloads;
        stats_.status = "Load failed; last-known-good presets retained: " + stagingError;
        if (errorMessage != nullptr) {
            *errorMessage = stagingError;
        }
        return false;
    }

    loadedPresets_ = std::move(stagedPresets);
    directory_ = directory;
    loadedFingerprint_ = fingerprint;
    ++stats_.revision;
    stats_.loadedPresetCount = static_cast<uint32_t>(loadedPresets_.size());
    stats_.fallbackPresetCount = ActiveFallbackCount(fallbackPresets_, loadedPresets_);
    stats_.usingFallback = stats_.fallbackPresetCount > 0;
    stats_.status = "Loaded " + std::to_string(loadedPresets_.size()) +
        " aim-assist presets from " + directory.generic_string();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

RailAimAssistPresetReloadReport
RailAimAssistPresetRegistry::ReloadChangedPresets() {
    RailAimAssistPresetReloadReport report{};
    report.previousRevision = stats_.revision;
    report.currentRevision = stats_.revision;
    report.loadedPresetCount = stats_.loadedPresetCount;
    report.fallbackPresetCount = stats_.fallbackPresetCount;
    if (directory_.empty()) {
        report.status = RailAimAssistPresetReloadStatus::Failed;
        report.message = "Aim-assist preset directory has not been loaded";
        ++stats_.failedReloads;
        stats_.status = report.message;
        return report;
    }

    PresetMap stagedPresets;
    uint64_t fingerprint = 0;
    std::string stagingError;
    if (!StageDirectory(directory_, stagedPresets, fingerprint, stagingError)) {
        report.status = RailAimAssistPresetReloadStatus::Failed;
        report.message = "Reload failed; last-known-good presets retained: " + stagingError;
        ++stats_.failedReloads;
        stats_.status = report.message;
        return report;
    }
    if (fingerprint == loadedFingerprint_) {
        report.message = "No aim-assist preset changes detected";
        return report;
    }

    loadedPresets_ = std::move(stagedPresets);
    loadedFingerprint_ = fingerprint;
    ++stats_.revision;
    ++stats_.successfulReloads;
    stats_.loadedPresetCount = static_cast<uint32_t>(loadedPresets_.size());
    stats_.fallbackPresetCount = ActiveFallbackCount(fallbackPresets_, loadedPresets_);
    stats_.usingFallback = stats_.fallbackPresetCount > 0;
    stats_.status = "Hot reloaded " + std::to_string(loadedPresets_.size()) +
        " aim-assist presets";
    report.status = RailAimAssistPresetReloadStatus::Reloaded;
    report.currentRevision = stats_.revision;
    report.loadedPresetCount = stats_.loadedPresetCount;
    report.fallbackPresetCount = stats_.fallbackPresetCount;
    report.message = stats_.status;
    return report;
}

const RailAimAssistPreset* RailAimAssistPresetRegistry::Find(
    const std::string& presetId) const {
    const auto loaded = loadedPresets_.find(presetId);
    if (loaded != loadedPresets_.end()) {
        return &loaded->second;
    }
    const auto fallback = fallbackPresets_.find(presetId);
    return fallback != fallbackPresets_.end() ? &fallback->second : nullptr;
}

bool RailAimAssistPresetRegistry::IsUsingFallback(
    const std::string& presetId) const {
    return !loadedPresets_.contains(presetId) && fallbackPresets_.contains(presetId);
}

std::filesystem::path RailAimAssistPresetRegistry::DefaultDirectory() {
    return std::filesystem::path{"Resources"} / "aim_assist";
}

const char* ToRailAimAssistPresetReloadStatusString(
    RailAimAssistPresetReloadStatus status) {
    switch (status) {
    case RailAimAssistPresetReloadStatus::NoChange: return "No Change";
    case RailAimAssistPresetReloadStatus::Reloaded: return "Reloaded";
    case RailAimAssistPresetReloadStatus::Failed: return "Failed";
    }
    return "Unknown";
}
