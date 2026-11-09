#include "WeaponDefinitionRegistry.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace {
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

WeaponDefinitionAsset PulseFallback() {
    WeaponDefinitionAsset asset{};
    asset.displayName = "Pulse Cannon (Fallback)";
    asset.definition.weaponId = RailWeaponIds::PulseCannon;
    asset.definition.fireMode = WeaponFireMode::Automatic;
    asset.definition.damageType = WeaponDamageType::Energy;
    asset.definition.baseDamage = 12.0f;
    asset.definition.range = 150.0f;
    asset.definition.shotInterval = 0.085f;
    asset.definition.projectilesPerShot = 1;
    asset.definition.maxProjectilesPerTrigger = 1;
    asset.muzzleVfxId = "pulse_muzzle";
    asset.tracerVfxId = "pulse_tracer";
    asset.fireAudioId = "pulse_fire";
    asset.feedbackPresetId = "energy_standard";
    asset.aimAssistPresetId = "gamepad_standard";
    return asset;
}

WeaponDefinitionAsset LockOnFallback() {
    WeaponDefinitionAsset asset{};
    asset.displayName = "Lock-On Ice (Fallback)";
    asset.definition.weaponId = RailWeaponIds::LockOnIce;
    asset.definition.fireMode = WeaponFireMode::ReleaseVolley;
    asset.definition.damageType = WeaponDamageType::Ice;
    asset.definition.baseDamage = 34.0f;
    asset.definition.range = 120.0f;
    asset.definition.shotInterval = 0.12f;
    asset.definition.projectilesPerShot = 1;
    asset.definition.maxProjectilesPerTrigger = 8;
    asset.definition.lockOnCompatible = true;
    asset.muzzleVfxId = "lock_ice_muzzle";
    asset.tracerVfxId = "lock_ice_projectile";
    asset.fireAudioId = "lock_ice_release";
    asset.feedbackPresetId = "ice_standard";
    asset.aimAssistPresetId = "lock_on_standard";
    asset.muzzleForwardOffset = 4.0f;
    return asset;
}

uint32_t ActiveFallbackCount(
    const std::unordered_map<std::string, WeaponDefinitionAsset>& fallbackAssets,
    const std::unordered_map<std::string, WeaponDefinitionAsset>& loadedAssets) {
    uint32_t count = 0;
    for (const auto& [weaponId, asset] : fallbackAssets) {
        (void)asset;
        if (loadedAssets.find(weaponId) == loadedAssets.end()) {
            ++count;
        }
    }
    return count;
}
} // namespace

WeaponDefinitionRegistry::WeaponDefinitionRegistry() {
    Reset();
}

void WeaponDefinitionRegistry::Reset() {
    fallbackAssets_ = BuildFallbackAssets();
    loadedAssets_.clear();
    directory_.clear();
    loadedFingerprint_ = 0;
    stats_ = {};
    stats_.fallbackAssetCount = static_cast<uint32_t>(fallbackAssets_.size());
    stats_.usingFallback = true;
    stats_.status = "Fallback definitions active";
}

WeaponDefinitionRegistry::AssetMap WeaponDefinitionRegistry::BuildFallbackAssets() {
    AssetMap result;
    WeaponDefinitionAsset pulse = PulseFallback();
    WeaponDefinitionAsset lockOn = LockOnFallback();
    result.emplace(pulse.definition.weaponId, std::move(pulse));
    result.emplace(lockOn.definition.weaponId, std::move(lockOn));
    return result;
}

bool WeaponDefinitionRegistry::StageDirectory(
    const std::filesystem::path& directory,
    AssetMap& stagedAssets,
    uint64_t& fingerprint,
    std::string& errorMessage) const {
    stagedAssets.clear();
    fingerprint = kFnvOffset;
    std::error_code error;
    if (!std::filesystem::exists(directory, error) ||
        !std::filesystem::is_directory(directory, error)) {
        errorMessage = "Weapon definition directory is unavailable: " + directory.generic_string();
        return false;
    }

    std::vector<std::filesystem::path> paths;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        if (!iterator->is_regular_file(error) || iterator->path().extension() != ".weapon") {
            continue;
        }
        paths.push_back(iterator->path());
    }
    if (error) {
        errorMessage = "Could not enumerate weapon definition directory: " + error.message();
        return false;
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    if (paths.empty()) {
        errorMessage = "Weapon definition directory contains no .weapon assets";
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

        WeaponDefinitionAsset asset{};
        std::string loadError;
        if (!asset.LoadFromFile(path.generic_string(), &loadError)) {
            errorMessage = std::move(loadError);
            return false;
        }
        const std::string weaponId = asset.definition.weaponId;
        if (!stagedAssets.emplace(weaponId, std::move(asset)).second) {
            errorMessage = "Duplicate weaponId '" + weaponId + "' in " + directory.generic_string();
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

bool WeaponDefinitionRegistry::LoadDirectory(
    const std::filesystem::path& directory,
    WeaponFireSystem* fireSystem,
    std::string* errorMessage) {
    AssetMap stagedAssets;
    uint64_t fingerprint = 0;
    std::string stagingError;
    if (!StageDirectory(directory, stagedAssets, fingerprint, stagingError)) {
        ++stats_.failedReloads;
        stats_.status = "Load failed; last-known-good definitions retained: " + stagingError;
        if (errorMessage != nullptr) {
            *errorMessage = stagingError;
        }
        return false;
    }

    if (fireSystem != nullptr) {
        std::string synchronizationError;
        if (!SynchronizeAssetSet(stagedAssets, *fireSystem, &synchronizationError)) {
            ++stats_.failedReloads;
            stats_.status = "Registration failed; last-known-good definitions retained: " +
                synchronizationError;
            if (errorMessage != nullptr) {
                *errorMessage = synchronizationError;
            }
            return false;
        }
    }

    loadedAssets_ = std::move(stagedAssets);
    directory_ = directory;
    loadedFingerprint_ = fingerprint;
    ++stats_.revision;
    stats_.loadedAssetCount = static_cast<uint32_t>(loadedAssets_.size());
    stats_.fallbackAssetCount = ActiveFallbackCount(fallbackAssets_, loadedAssets_);
    stats_.usingFallback = stats_.fallbackAssetCount > 0;
    stats_.status = "Loaded " + std::to_string(loadedAssets_.size()) +
        " weapon definitions from " + directory.generic_string();
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

WeaponDefinitionReloadReport WeaponDefinitionRegistry::ReloadChangedAssets(
    WeaponFireSystem* fireSystem) {
    WeaponDefinitionReloadReport report{};
    report.previousRevision = stats_.revision;
    report.currentRevision = stats_.revision;
    report.loadedAssetCount = stats_.loadedAssetCount;
    report.fallbackAssetCount = stats_.fallbackAssetCount;
    if (directory_.empty()) {
        report.status = WeaponDefinitionReloadStatus::Failed;
        report.message = "Weapon definition directory has not been loaded";
        ++stats_.failedReloads;
        stats_.status = report.message;
        return report;
    }

    AssetMap stagedAssets;
    uint64_t fingerprint = 0;
    std::string stagingError;
    if (!StageDirectory(directory_, stagedAssets, fingerprint, stagingError)) {
        report.status = WeaponDefinitionReloadStatus::Failed;
        report.message = "Reload failed; last-known-good definitions retained: " + stagingError;
        ++stats_.failedReloads;
        stats_.status = report.message;
        return report;
    }
    if (fingerprint == loadedFingerprint_) {
        report.status = WeaponDefinitionReloadStatus::NoChange;
        report.message = "No weapon definition changes detected";
        return report;
    }

    if (fireSystem != nullptr) {
        std::string synchronizationError;
        if (!SynchronizeAssetSet(stagedAssets, *fireSystem, &synchronizationError)) {
            report.status = WeaponDefinitionReloadStatus::Failed;
            report.message = "Reload registration failed; runtime retained: " +
                synchronizationError;
            ++stats_.failedReloads;
            stats_.status = report.message;
            return report;
        }
    }

    loadedAssets_ = std::move(stagedAssets);
    loadedFingerprint_ = fingerprint;
    ++stats_.revision;
    ++stats_.successfulReloads;
    stats_.loadedAssetCount = static_cast<uint32_t>(loadedAssets_.size());
    stats_.fallbackAssetCount = ActiveFallbackCount(fallbackAssets_, loadedAssets_);
    stats_.usingFallback = stats_.fallbackAssetCount > 0;
    stats_.status = "Hot reloaded " + std::to_string(loadedAssets_.size()) + " weapon definitions";
    report.status = WeaponDefinitionReloadStatus::Reloaded;
    report.currentRevision = stats_.revision;
    report.loadedAssetCount = stats_.loadedAssetCount;
    report.message = stats_.status;
    return report;
}

bool WeaponDefinitionRegistry::SynchronizeFireSystem(
    WeaponFireSystem& fireSystem,
    std::string* errorMessage) const {
    return SynchronizeAssetSet(loadedAssets_, fireSystem, errorMessage);
}

bool WeaponDefinitionRegistry::SynchronizeAssetSet(
    const AssetMap& loadedAssets,
    WeaponFireSystem& fireSystem,
    std::string* errorMessage) const {
    std::vector<std::string> weaponIds;
    weaponIds.reserve(fallbackAssets_.size() + loadedAssets.size());
    for (const auto& [weaponId, asset] : fallbackAssets_) {
        (void)asset;
        weaponIds.push_back(weaponId);
    }
    for (const auto& [weaponId, asset] : loadedAssets) {
        (void)asset;
        if (fallbackAssets_.find(weaponId) == fallbackAssets_.end()) {
            weaponIds.push_back(weaponId);
        }
    }
    std::sort(weaponIds.begin(), weaponIds.end());
    std::vector<WeaponDefinition> definitions;
    definitions.reserve(weaponIds.size());
    for (const std::string& weaponId : weaponIds) {
        const auto loaded = loadedAssets.find(weaponId);
        if (loaded != loadedAssets.end()) {
            definitions.push_back(loaded->second.definition);
            continue;
        }
        const auto fallback = fallbackAssets_.find(weaponId);
        if (fallback == fallbackAssets_.end()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Could not resolve weapon definition: " + weaponId;
            }
            return false;
        }
        definitions.push_back(fallback->second.definition);
    }
    return fireSystem.ReplaceDefinitionSet(definitions, errorMessage);
}

const WeaponDefinitionAsset* WeaponDefinitionRegistry::Find(
    const std::string& weaponId) const {
    const auto loaded = loadedAssets_.find(weaponId);
    if (loaded != loadedAssets_.end()) {
        return &loaded->second;
    }
    const auto fallback = fallbackAssets_.find(weaponId);
    return fallback != fallbackAssets_.end() ? &fallback->second : nullptr;
}

bool WeaponDefinitionRegistry::IsUsingFallback(const std::string& weaponId) const {
    return loadedAssets_.find(weaponId) == loadedAssets_.end() &&
        fallbackAssets_.find(weaponId) != fallbackAssets_.end();
}

std::filesystem::path WeaponDefinitionRegistry::DefaultDirectory() {
    return std::filesystem::path{"Resources"} / "weapons";
}

const char* ToWeaponDefinitionReloadStatusString(WeaponDefinitionReloadStatus status) {
    switch (status) {
    case WeaponDefinitionReloadStatus::NoChange: return "No Change";
    case WeaponDefinitionReloadStatus::Reloaded: return "Reloaded";
    case WeaponDefinitionReloadStatus::Failed: return "Failed";
    }
    return "Unknown";
}
