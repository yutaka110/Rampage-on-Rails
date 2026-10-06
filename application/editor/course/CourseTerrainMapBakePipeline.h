#pragma once

#include "CourseRailAuthoringModel.h"
#include "CourseTerrainMapAsset.h"
#include "../../terrain/TerrainGenerationSettings.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>

namespace editor {

enum class CourseTerrainMapBakeStatus : uint8_t {
    Missing,
    Building,
    Current,
    Stale,
    Failed,
};

struct CourseTerrainMapBakeSettings final {
    bool autoBake = true;
    bool persistDerivedAsset = true;
    float tileLength = 240.0f;
    std::array<uint32_t, 3> longitudinalSegments{96u, 192u, 384u};
    std::array<uint32_t, 3> radialSegments{16u, 24u, 32u};

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseTerrainMapBakeInput final {
    const CourseRailAuthoringModel* rail = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    std::string courseName;
    uint64_t courseRevision = 0;
};

struct CourseTerrainMapBakeStats final {
    uint64_t buildsStarted = 0;
    uint64_t buildsCompleted = 0;
    uint64_t staleBuildsDiscarded = 0;
    uint64_t cacheLoads = 0;
    uint64_t cacheWrites = 0;
    uint64_t currentHits = 0;
    uint32_t lods = 0;
    uint32_t tiles = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
};

struct CourseTerrainMapBakeResult final {
    CourseTerrainMapBakeStatus status = CourseTerrainMapBakeStatus::Missing;
    bool assetAvailable = false;
    bool builtThisCall = false;
    bool loadedFromCache = false;
    uint64_t expectedFingerprint = 0;
    std::filesystem::path cachePath;
    CourseTerrainMapBakeStats stats{};
    std::string message;
};

// Latest-request-wins derived-data builder for the full procedural terrain
// shell. Source data is snapshotted before entering the worker so authoring can
// continue safely while an older map remains visible.
class CourseTerrainMapBakePipeline final {
public:
    explicit CourseTerrainMapBakePipeline(
        std::filesystem::path projectRoot = std::filesystem::current_path());

    const CourseTerrainMapBakeResult& Ensure(
        const CourseTerrainMapBakeInput& input);
    CourseTerrainMapBakeResult Bake(
        const CourseTerrainMapBakeInput& input);
    CourseTerrainMapBakeStatus Assess(
        const CourseTerrainMapAsset& asset,
        const CourseTerrainMapBakeInput& input,
        uint64_t* expectedFingerprint = nullptr) const;

    void RequestRebuild() noexcept;
    void InvalidateAsset() noexcept;
    bool SetSettings(CourseTerrainMapBakeSettings settings, std::string* errorMessage = nullptr);
    const CourseTerrainMapBakeSettings& Settings() const noexcept {
        return settings_;
    }
    void SetCacheRoot(std::filesystem::path path);
    const CourseTerrainMapAsset* CurrentAsset() const noexcept;
    const CourseTerrainMapBakeResult& LastResult() const noexcept {
        return lastResult_;
    }
    const CourseTerrainMapBakeStats& LifetimeStats() const noexcept {
        return lifetimeStats_;
    }

private:
    struct Snapshot final {
        RailPath rail{};
        TerrainGenerationSettings terrainSettings{};
        TerrainEditLayer terrainEdits{};
        CourseTerrainMapBakeSettings bakeSettings{};
        std::string courseName;
        uint64_t sourceRailHash = 0;
        uint64_t sourceTerrainSettingsHash = 0;
        uint64_t sourceTerrainEditHash = 0;
        uint64_t bakeSettingsHash = 0;
        uint64_t sourceFingerprint = 0;
        std::filesystem::path cachePath;
        bool forceRebuild = false;
    };

    struct WorkerResult final {
        bool succeeded = false;
        bool loadedFromCache = false;
        bool wroteCache = false;
        uint64_t sourceFingerprint = 0;
        CourseTerrainMapAsset asset{};
        std::string message;
    };

    bool MakeSnapshot(
        const CourseTerrainMapBakeInput& input,
        bool forceRebuild,
        Snapshot& snapshot,
        std::string* errorMessage = nullptr) const;
    static WorkerResult RunWorker(Snapshot snapshot);
    static CourseTerrainMapAsset BuildAsset(const Snapshot& snapshot);
    void StartWorker(Snapshot snapshot);
    bool PollWorker(uint64_t expectedFingerprint);
    void RefreshAssetStats();
    uint64_t ResolveFingerprint(
        const CourseTerrainMapBakeInput& input,
        uint64_t* railHash = nullptr,
        uint64_t* terrainSettingsHash = nullptr,
        uint64_t* terrainEditHash = nullptr,
        uint64_t* bakeSettingsHash = nullptr) const;
    std::filesystem::path CachePath(std::string_view courseName) const;

    CourseTerrainMapBakeSettings settings_{};
    std::filesystem::path projectRoot_;
    std::filesystem::path cacheRoot_{".editor/derived-data/course-map"};
    CourseTerrainMapAsset asset_{};
    CourseTerrainMapBakeResult lastResult_{};
    CourseTerrainMapBakeStats lifetimeStats_{};
    std::future<WorkerResult> worker_{};
    std::optional<uint64_t> pendingFingerprint_{};
    uint64_t latestRequestedFingerprint_ = 0;
    bool forceRebuild_ = false;
};

const char* ToString(CourseTerrainMapBakeStatus status) noexcept;

} // namespace editor
