#pragma once

#include "CourseMapCartographyBakePipeline.h"
#include "CourseMapSemanticLODSystem.h"
#include "CourseOverviewMapProjection.h"

#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor {

struct CourseMapCartographyTriangle final {
    Vector2 a{};
    Vector2 b{};
    Vector2 c{};
    uint32_t fillColor = 0;
    float depth = 0.0f;
    std::string stableId;
    CourseMapVisualLayer layer = CourseMapVisualLayer::HeroLandmark;
    bool exactSourceGeometry = false;
};

struct CourseMapCartographyOutline final {
    std::vector<Vector2> points;
    uint32_t color = 0xffffffffu;
    uint32_t glowColor = 0;
    float thickness = 1.0f;
    float glowThickness = 3.0f;
    bool closed = true;
    bool locked = false;
    bool exactSourceGeometry = false;
    std::string stableId;
    CourseMapVisualLayer layer = CourseMapVisualLayer::HeroLandmark;
};

struct CourseMapCartographyRenderStats final {
    uint32_t sourceRegions = 0;
    uint32_t visibleRegions = 0;
    uint32_t visibleExactRegions = 0;
    uint32_t visibleFallbackRegions = 0;
    uint32_t visibleTiles = 0;
    uint32_t culledTiles = 0;
    uint32_t culledRegions = 0;
    uint32_t inspectedTriangles = 0;
    uint32_t emittedTriangles = 0;
    uint32_t budgetCulledTriangles = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
    uint64_t asynchronousBuildStarts = 0;
    uint64_t asynchronousBuildCompletions = 0;
    uint64_t retainedFramesWhileBuilding = 0;
    uint64_t supersededBuildCompletions = 0;
    uint64_t interactivePanDeferrals = 0;
    uint64_t screenTranslatedFrames = 0;
};

struct CourseMapCartographyFrame final {
    bool valid = false;
    uint64_t generation = 0;
    bool fallbackRequested = true;
    CourseOverviewMapRect rect{};
    CourseMapCartographyBakeStatus sourceStatus =
        CourseMapCartographyBakeStatus::Missing;
    CourseOverviewMapProjectionMode projectionMode =
        CourseOverviewMapProjectionMode::Top;
    CourseMapSemanticLODLevel semanticLod = CourseMapSemanticLODLevel::Course;
    // A pan-only projection change does not alter cartographic geometry. The
    // front frame remains immutable and is translated at presentation time
    // until the latest requested projection has been rebuilt.
    Vector2 presentationOffset{};
    std::vector<CourseMapCartographyTriangle> triangles;
    std::vector<CourseMapCartographyOutline> outlines;
    CourseMapCartographyRenderStats stats{};
    std::string message;
};

struct CourseMapCartographyBuildOptions final {
    // While true, no replacement build is started for a valid retained frame.
    // The caller still submits the latest projection every frame so completed
    // obsolete work can be rejected and the display offset stays current.
    bool interactivePan = false;
};

struct CourseMapCartographyRenderSettings final {
    bool enabled = true;
    bool showSurfaceTriangles = true;
    bool showRegionOutlines = true;
    bool showFallbackGeometry = true;
    bool glow = true;
    float terrainOpacity = 0.34f;
    float structureOpacity = 0.48f;
    float vistaOpacity = 0.16f;
    // These are screen-space cartography budgets, not source-mesh budgets.
    // Higher detail remains available while the default course view stays
    // responsive even in unoptimized editor builds.
    uint32_t courseTriangleBudget = 2500;
    uint32_t regionTriangleBudget = 6000;
    uint32_t detailTriangleBudget = 12000;
    uint32_t inspectTriangleBudget = 24000;
    uint32_t maximumOutlines = 16384;

    bool Validate(std::string* errorMessage = nullptr) const;
};

// Retained cartographic renderer for CourseMapRegionAsset. It consumes the
// same projection as picking, performs tile/region visibility filtering, emits
// depth-sorted real mesh triangles, and keeps simplified footprints as stable
// semantic outlines. Non-current data explicitly requests the old hologram.
class CourseMapCartographyRenderer final {
public:
    const CourseMapCartographyFrame& Build(
        const CourseMapRegionAsset* asset,
        CourseMapCartographyBakeStatus sourceStatus,
        const CourseOverviewMapProjection& projection,
        CourseMapCartographyBuildOptions options = {});

    bool SetSettings(CourseMapCartographyRenderSettings settings, std::string* errorMessage = nullptr);
    const CourseMapCartographyRenderSettings& Settings() const noexcept {
        return settings_;
    }
    void Invalidate() noexcept;
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }
    const CourseMapCartographyRenderStats& LifetimeStats() const noexcept {
        return lifetimeStats_;
    }

private:
    struct FrameKey final {
        uint64_t sourceFingerprint = 0;
        uint64_t contentRevision = 0;
        uint64_t settingsRevision = 0;
        uint64_t semanticLodRevision = 0;
        CourseMapCartographyBakeStatus sourceStatus =
            CourseMapCartographyBakeStatus::Missing;
        CourseOverviewMapProjectionSettings projection{};
        CourseOverviewMapRect rect{};
    };

    struct CacheEntry final {
        bool valid = false;
        FrameKey key{};
        CourseMapCartographyFrame frame{};
        std::future<CourseMapCartographyFrame> buildFuture{};
        std::optional<FrameKey> pendingKey{};
        uint64_t pendingEpoch = 0;
    };

    static CourseMapCartographyFrame BuildFrame(
        const CourseMapRegionAsset& asset,
        const CourseOverviewMapProjection& projection,
        const CourseMapCartographyRenderSettings& settings,
        const CourseMapSemanticLODPolicy& lod);
    bool PollBuild(CacheEntry& cache, const FrameKey& latestRequestedKey);
    bool StartBuild(
        CacheEntry& cache,
        FrameKey key,
        std::shared_ptr<const CourseMapRegionAsset> asset,
        const CourseOverviewMapProjection& projection);
    std::shared_ptr<const CourseMapRegionAsset> AcquireSourceSnapshot(
        const CourseMapRegionAsset& asset);
    static bool SameKey(const FrameKey& lhs, const FrameKey& rhs) noexcept;
    static bool CanScreenTranslate(
        const FrameKey& retained,
        const FrameKey& requested) noexcept;
    static Vector2 ScreenTranslation(
        const FrameKey& retained,
        const FrameKey& requested) noexcept;

    CourseMapCartographyRenderSettings settings_{};
    CourseMapSemanticLODSystem semanticLod_{};
    std::array<CacheEntry, 4> caches_{};
    std::array<CourseMapCartographyFrame, 4> fallbackFrames_{};
    uint64_t settingsRevision_ = 1;
    uint64_t invalidationEpoch_ = 1;
    uint64_t sourceSnapshotFingerprint_ = 0;
    uint64_t sourceSnapshotContentRevision_ = 0;
    std::shared_ptr<const CourseMapRegionAsset> sourceSnapshot_{};
    CourseMapCartographyRenderStats lifetimeStats_{};
};

} // namespace editor
