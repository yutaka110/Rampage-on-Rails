#pragma once

#include "CourseMapGeometryExtractionService.h"
#include "CourseMapRegionAsset.h"

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>

namespace editor {

enum class CourseMapCartographyBakeStatus : uint8_t {
    Missing,
    Loading,
    Current,
    Stale,
    Incompatible,
    Failed,
};

struct CourseMapCartographyBakeSettings final {
    // Opening a viewport must never synchronously import and compile every
    // terrain mesh. Automatic rebuilds are opt-in; persisted data still loads
    // asynchronously and an explicit rebuild remains available in the toolbar.
    bool autoBake = false;
    bool persistDerivedAsset = true;
    bool allowBoxFallback = true;
    float tileWorldSize = 256.0f;
    float footprintSimplificationTolerance = 0.35f;
    float minimumProjectedArea = 0.01f;
    uint32_t maximumRegions = 16384;
    uint32_t maximumTrianglesPerRegion = 2048;
    uint32_t maximumFootprintPoints = 256;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapCartographyBakeInput final {
    const CourseMapVisualAsset* visualAsset = nullptr;
    const EditorAssetRegistry* assets = nullptr;
    uint64_t visualRevision = 0;
    uint32_t assetRegistryRevision = 0;
};

struct CourseMapCartographyBakeStats final {
    uint32_t regions = 0;
    uint32_t exactRegions = 0;
    uint32_t fallbackRegions = 0;
    uint32_t sourceTriangles = 0;
    uint32_t bakedTriangles = 0;
    uint32_t footprintPoints = 0;
    uint32_t tiles = 0;
    uint64_t bakes = 0;
    uint64_t cacheLoads = 0;
    uint64_t currentHits = 0;
    uint64_t extractionBuilds = 0;
    uint64_t extractionCacheHits = 0;
    uint64_t asynchronousCacheLoadStarts = 0;
    uint64_t asynchronousCacheLoadCompletions = 0;
};

struct CourseMapCartographyBakeResult final {
    CourseMapCartographyBakeStatus status =
        CourseMapCartographyBakeStatus::Missing;
    bool assetAvailable = false;
    bool bakedThisCall = false;
    bool loadedFromCache = false;
    bool usedGeometryFallback = false;
    bool fallbackRequired = true;
    uint64_t expectedFingerprint = 0;
    std::filesystem::path cachePath;
    CourseMapCartographyBakeStats stats{};
    std::string message;
};

// Second-stage derived-data compiler. It converts extracted placement geometry
// into stable semantic regions while retaining source triangles for later
// cartographic rendering and a simplified footprint for overview composition.
class CourseMapCartographyBakePipeline final {
public:
    explicit CourseMapCartographyBakePipeline(
        std::filesystem::path projectRoot = std::filesystem::current_path());

    const CourseMapCartographyBakeResult& Ensure(
        const CourseMapCartographyBakeInput& input);
    CourseMapCartographyBakeResult Bake(
        const CourseMapCartographyBakeInput& input);
    CourseMapCartographyBakeStatus Assess(
        const CourseMapRegionAsset& asset,
        const CourseMapCartographyBakeInput& input,
        uint64_t* expectedFingerprint = nullptr);

    void RequestRebuild() noexcept;
    void InvalidateAsset() noexcept;
    bool SetSettings(CourseMapCartographyBakeSettings settings, std::string* errorMessage = nullptr);
    const CourseMapCartographyBakeSettings& Settings() const noexcept {
        return settings_;
    }
    void SetCacheRoot(std::filesystem::path path);
    const CourseMapRegionAsset* CurrentAsset() const noexcept;
    const CourseMapCartographyBakeResult& LastResult() const noexcept {
        return lastResult_;
    }
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }

private:
    struct AsyncCacheLoadResult final {
        bool loaded = false;
        CourseMapRegionAsset asset{};
        std::string error;
    };

    struct SourceKey final {
        const CourseMapVisualAsset* visualAsset = nullptr;
        const EditorAssetRegistry* assets = nullptr;
        uint64_t visualFingerprint = 0;
        uint64_t visualRevision = 0;
        uint64_t settingsRevision = 0;
        uint32_t assetRegistryRevision = 0;
    };

    const CourseMapGeometryExtractionResult& ResolveExtraction(
        const CourseMapCartographyBakeInput& input);
    uint64_t ComputeSettingsHash() const;
    uint64_t ResolveMeshRegistryFingerprint(
        const EditorAssetRegistry* assets) const;
    bool IsFastSourceCurrent(
        const CourseMapRegionAsset& asset,
        const CourseMapCartographyBakeInput& input,
        uint64_t meshRegistryFingerprint,
        uint64_t settingsHash) const noexcept;
    uint64_t ResolveFingerprint(
        const CourseMapCartographyBakeInput& input,
        const CourseMapGeometryExtractionResult& extraction,
        uint64_t* settingsHash = nullptr) const;
    std::filesystem::path CachePath(
        const CourseMapCartographyBakeInput& input) const;
    void BeginCacheLoad(
        const CourseMapCartographyBakeInput& input,
        uint64_t meshRegistryFingerprint,
        uint64_t settingsHash);
    bool PollCacheLoad(
        const CourseMapCartographyBakeInput& input,
        uint64_t meshRegistryFingerprint,
        uint64_t settingsHash,
        bool& stillPending);
    void ApplyLoadedAsset(CourseMapRegionAsset loaded);
    static bool SameSourceKey(const SourceKey& a, const SourceKey& b) noexcept;

    CourseMapCartographyBakeSettings settings_{};
    std::filesystem::path projectRoot_;
    CourseMapGeometryExtractionService extractionService_;
    std::filesystem::path cacheRoot_{".editor/derived-data/course-map"};
    CourseMapGeometryExtractionResult extraction_{};
    SourceKey extractionKey_{};
    bool extractionValid_ = false;
    CourseMapRegionAsset asset_{};
    CourseMapCartographyBakeResult lastResult_{};
    CourseMapCartographyBakeStats lifetimeStats_{};
    uint64_t settingsRevision_ = 1;
    bool cacheAttempted_ = false;
    bool forceRebuild_ = false;
    std::future<AsyncCacheLoadResult> cacheLoadFuture_{};
    bool cacheLoadPending_ = false;
    uint64_t cacheLoadVisualFingerprint_ = 0;
    uint64_t cacheLoadMeshRegistryFingerprint_ = 0;
    uint64_t cacheLoadSettingsHash_ = 0;
    uint64_t cacheLoadEpoch_ = 0;
    uint64_t invalidationEpoch_ = 1;
    mutable const EditorAssetRegistry* fingerprintRegistry_ = nullptr;
    mutable uint32_t fingerprintRegistryRevision_ = 0;
    mutable uint64_t meshRegistryFingerprint_ = 0;
};

const char* ToString(CourseMapCartographyBakeStatus status) noexcept;

} // namespace editor
