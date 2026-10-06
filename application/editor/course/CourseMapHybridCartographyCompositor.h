#pragma once

#include "CourseMapCartographyRenderer.h"

#include <array>
#include <cstdint>
#include <string>

namespace editor {

enum class CourseMapHybridGeometryLayer : uint8_t {
    Terrain = 0,
    Rocks,
    Structures,
    Count,
};

struct CourseMapCoarseGeometryVisibility final {
    bool terrain = true;
    bool rocks = true;
    bool structures = true;
};

struct CourseMapHybridLayerCoverage final {
    uint32_t exactRegions = 0;
    float occupiedScreenRatio = 0.0f;
    float largestRegionScreenRatio = 0.0f;
    bool drawCoarseGeometry = true;
    bool drawExactGeometry = false;
};

struct CourseMapHybridCartographyFrame final {
    bool valid = false;
    bool drawCartography = false;
    bool requestHologramFallback = true;
    CourseMapSemanticLODLevel semanticLod = CourseMapSemanticLODLevel::Course;
    CourseMapCoarseGeometryVisibility coarseGeometry{};
    std::array<CourseMapHybridLayerCoverage,
        static_cast<std::size_t>(CourseMapHybridGeometryLayer::Count)> layers{};
    uint64_t sourceGeneration = 0;
    std::string message;
};

struct CourseMapHybridCartographySettings final {
    bool enabled = true;
    bool alwaysKeepCoarseAtCourseLod = true;
    bool alwaysKeepCoarseAtRegionLod = true;
    uint32_t coverageGridResolution = 32;
    float terrainCoverageThreshold = 0.055f;
    float rockCoverageThreshold = 0.025f;
    float structureCoverageThreshold = 0.015f;
    float largestRegionThreshold = 0.006f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

// Decides how retained exact cartography and cheap semantic proxies are
// composited. Coverage is evaluated independently per visual layer, preventing
// one valid mesh from hiding every terrain/rock/structure proxy in a course.
class CourseMapHybridCartographyCompositor final {
public:
    const CourseMapHybridCartographyFrame& Compose(
        const CourseMapCartographyFrame* cartography);

    bool SetSettings(CourseMapHybridCartographySettings settings, std::string* errorMessage = nullptr);
    const CourseMapHybridCartographySettings& Settings() const noexcept {
        return settings_;
    }
    void Invalidate() noexcept;
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }

private:
    struct CacheEntry final {
        CourseMapHybridCartographyFrame frame{};
        const CourseMapCartographyFrame* source = nullptr;
        uint64_t generation = 0;
        uint64_t settingsRevision = 0;
    };

    static std::size_t LayerIndex(CourseMapVisualLayer layer) noexcept;
    static bool SameSettings(
        const CourseMapHybridCartographySettings& lhs,
        const CourseMapHybridCartographySettings& rhs) noexcept;
    CourseMapHybridCartographyFrame BuildFrame(
        const CourseMapCartographyFrame* cartography) const;

    CourseMapHybridCartographySettings settings_{};
    std::array<CacheEntry, 4> caches_{};
    uint64_t settingsRevision_ = 1;
};

} // namespace editor
