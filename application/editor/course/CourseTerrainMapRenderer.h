#pragma once

#include "CourseMapSemanticLODSystem.h"
#include "CourseOverviewMapProjection.h"
#include "CourseTerrainMapAsset.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

struct CourseTerrainMapTriangle final {
    Vector2 a{};
    Vector2 b{};
    Vector2 c{};
    uint32_t color = 0;
    float depth = 0.0f;
};

struct CourseTerrainMapPolyline final {
    std::vector<Vector2> points;
    uint32_t color = 0xffffffffu;
    float thickness = 1.0f;
};

struct CourseTerrainMapRenderStats final {
    uint32_t sourceTiles = 0;
    uint32_t visibleTiles = 0;
    uint32_t culledTiles = 0;
    uint32_t inspectedTriangles = 0;
    uint32_t emittedTriangles = 0;
    uint32_t budgetCulledTriangles = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
    uint64_t interactivePanDeferrals = 0;
};

struct CourseTerrainMapFrame final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    CourseOverviewMapProjectionMode projectionMode =
        CourseOverviewMapProjectionMode::Top;
    CourseMapSemanticLODLevel semanticLod = CourseMapSemanticLODLevel::Course;
    uint32_t sourceLod = 0;
    std::vector<CourseTerrainMapTriangle> triangles;
    std::vector<CourseTerrainMapPolyline> contours;
    CourseTerrainMapRenderStats stats{};
    std::string message;
};

struct CourseTerrainMapRenderSettings final {
    bool enabled = true;
    bool showSurface = true;
    bool showContours = true;
    float opacity = 0.30f;
    uint32_t courseTriangleBudget = 2600;
    uint32_t regionTriangleBudget = 6500;
    uint32_t detailTriangleBudget = 13000;
    uint32_t inspectTriangleBudget = 20000;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseTerrainMapBuildOptions final {
    bool interactivePan = false;
};

// Retained projection renderer for CourseTerrainMapAsset. Terrain is emitted
// as the map's base layer; semantic LOD selects a persisted mesh tier and pan
// gestures reuse the previous screen-space frame.
class CourseTerrainMapRenderer final {
public:
    const CourseTerrainMapFrame& Build(
        const CourseTerrainMapAsset* asset,
        const CourseOverviewMapProjection& projection,
        CourseTerrainMapBuildOptions options = {});

    bool SetSettings(CourseTerrainMapRenderSettings settings, std::string* errorMessage = nullptr);
    const CourseTerrainMapRenderSettings& Settings() const noexcept {
        return settings_;
    }
    void Invalidate() noexcept;
    const CourseTerrainMapRenderStats& LifetimeStats() const noexcept {
        return lifetimeStats_;
    }

private:
    struct FrameKey final {
        uint64_t sourceFingerprint = 0;
        uint64_t contentRevision = 0;
        uint64_t settingsRevision = 0;
        uint64_t semanticLodRevision = 0;
        CourseOverviewMapProjectionSettings projection{};
        CourseOverviewMapRect rect{};
    };

    struct CacheEntry final {
        bool valid = false;
        FrameKey key{};
        CourseTerrainMapFrame frame{};
    };

    static CourseTerrainMapFrame BuildFrame(
        const CourseTerrainMapAsset& asset,
        const CourseOverviewMapProjection& projection,
        const CourseTerrainMapRenderSettings& settings,
        const CourseMapSemanticLODPolicy& lod);
    static bool SameKey(const FrameKey& a, const FrameKey& b) noexcept;

    CourseTerrainMapRenderSettings settings_{};
    CourseMapSemanticLODSystem semanticLod_{};
    std::array<CacheEntry, 4> caches_{};
    std::array<CourseTerrainMapFrame, 4> fallbackFrames_{};
    uint64_t settingsRevision_ = 1;
    CourseTerrainMapRenderStats lifetimeStats_{};
};

} // namespace editor
