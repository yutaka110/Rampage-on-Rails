#pragma once

#include "CourseEnemyAuthoringModel.h"
#include "CourseMapVisualAsset.h"
#include "CourseRailAuthoringModel.h"
#include "../scene/EditorScene.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace editor {

enum class CourseMapVisualBakeStatus : uint8_t {
    Missing,
    Current,
    Stale,
    Incompatible,
    Failed,
};

struct CourseMapVisualBakeSettings final {
    bool includeVistaBackground = true;
    bool includeRockMasses = true;
    bool includeSceneStructures = true;
    bool autoBake = true;
    bool persistDerivedAsset = true;
    float contourInterval = 12.0f;
    float tileWorldSize = 256.0f;
    float minimumPrimitiveExtent = 0.75f;
    uint32_t maximumPrimitives = 16384;
    uint32_t maximumContours = 32768;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapVisualBakeInput final {
    const CourseRailAuthoringModel* rail = nullptr;
    const CourseEnemyAuthoringModel* enemies = nullptr;
    const CourseAsset* course = nullptr;
    const EditorScene* scene = nullptr;
    uint64_t courseRevision = 0;
    uint64_t enemyRevision = 0;
    uint32_t railGeneration = 0;
    uint32_t enemyGeneration = 0;
};

struct CourseMapVisualBakeStats final {
    uint32_t terrainPrimitives = 0;
    uint32_t rockMasses = 0;
    uint32_t sceneStructures = 0;
    uint32_t contours = 0;
    uint32_t landmarks = 0;
    uint32_t tiles = 0;
    uint64_t bakes = 0;
    uint64_t cacheLoads = 0;
    uint64_t currentHits = 0;
};

struct CourseMapVisualBakeResult final {
    CourseMapVisualBakeStatus status = CourseMapVisualBakeStatus::Missing;
    bool assetAvailable = false;
    bool bakedThisCall = false;
    bool loadedFromCache = false;
    bool fallbackRequired = true;
    uint64_t expectedFingerprint = 0;
    std::filesystem::path cachePath;
    CourseMapVisualBakeStats stats{};
    std::string message;
};

// Deterministic editor derived-data compiler. Source revisions gate expensive
// hashing; the serialized asset is validated before it can become Current.
class CourseMapVisualBakePipeline final {
public:
    const CourseMapVisualBakeResult& Ensure(const CourseMapVisualBakeInput& input);
    CourseMapVisualBakeResult Bake(const CourseMapVisualBakeInput& input);
    CourseMapVisualBakeStatus Assess(
        const CourseMapVisualAsset& asset,
        const CourseMapVisualBakeInput& input,
        uint64_t* expectedFingerprint = nullptr) const;

    void RequestRebuild() noexcept;
    void InvalidateAsset() noexcept;
    bool SetSettings(CourseMapVisualBakeSettings settings, std::string* errorMessage = nullptr);
    const CourseMapVisualBakeSettings& Settings() const noexcept { return settings_; }
    void SetCacheRoot(std::filesystem::path path);
    const CourseMapVisualAsset* CurrentAsset() const noexcept;
    const CourseMapVisualBakeResult& LastResult() const noexcept { return lastResult_; }
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }

    static uint64_t ComputeSourceFingerprint(
        const CourseMapVisualBakeInput& input,
        const CourseMapVisualBakeSettings& settings,
        uint64_t* courseHash = nullptr,
        uint64_t* sceneHash = nullptr,
        uint64_t* settingsHash = nullptr);

private:
    struct SourceKey final {
        const CourseAsset* course = nullptr;
        const EditorScene* scene = nullptr;
        uint64_t courseRevision = 0;
        uint64_t enemyRevision = 0;
        uint64_t sceneRevision = 0;
        uint64_t settingsRevision = 0;
        uint32_t railGeneration = 0;
        uint32_t enemyGeneration = 0;
    };

    uint64_t ResolveFingerprint(const CourseMapVisualBakeInput& input) const;
    std::filesystem::path CachePath(const CourseMapVisualBakeInput& input) const;
    bool TryLoadCache(const CourseMapVisualBakeInput& input, uint64_t fingerprint);
    static bool SameSourceKey(const SourceKey& a, const SourceKey& b) noexcept;

    CourseMapVisualBakeSettings settings_{};
    std::filesystem::path cacheRoot_{".editor/derived-data/course-map"};
    CourseMapVisualAsset asset_{};
    CourseMapVisualBakeResult lastResult_{};
    CourseMapVisualBakeStats lifetimeStats_{};
    uint64_t settingsRevision_ = 1;
    mutable SourceKey fingerprintKey_{};
    mutable uint64_t cachedFingerprint_ = 0;
    bool cacheAttempted_ = false;
    bool forceRebuild_ = false;
};

const char* ToString(CourseMapVisualBakeStatus status) noexcept;

} // namespace editor
