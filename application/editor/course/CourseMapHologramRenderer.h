#pragma once

#include "CourseMapSemanticLODSystem.h"
#include "CourseMapVisualAsset.h"
#include "CourseMapVisualBakePipeline.h"
#include "CourseOverviewMapProjection.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {

struct CourseMapHologramPolygon final {
    CourseMapVisualLayer layer = CourseMapVisualLayer::HeroLandmark;
    std::vector<Vector2> points;
    uint32_t fillColor = 0;
    uint32_t outlineColor = 0xffffffffu;
    uint32_t glowColor = 0;
    float outlineThickness = 1.0f;
    float glowThickness = 3.0f;
    std::string stableId;
    bool locked = false;
};

struct CourseMapHologramLineBatch final {
    std::vector<Vector2> points;
    uint32_t color = 0xffffffffu;
    uint32_t glowColor = 0;
    float thickness = 1.0f;
    float glowThickness = 3.0f;
    bool major = false;
};

struct CourseMapHologramStats final {
    uint32_t sourcePrimitives = 0;
    uint32_t visiblePolygons = 0;
    uint32_t culledPolygons = 0;
    uint32_t visibleContours = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
};

struct CourseMapHologramFrame final {
    bool valid = false;
    bool fallbackRequested = true;
    CourseOverviewMapRect rect{};
    CourseMapVisualBakeStatus sourceStatus = CourseMapVisualBakeStatus::Missing;
    CourseMapSemanticLODLevel semanticLod = CourseMapSemanticLODLevel::Course;
    std::vector<CourseMapHologramPolygon> polygons;
    std::vector<CourseMapHologramLineBatch> contours;
    CourseMapHologramStats stats{};
    std::string message;
};

struct CourseMapHologramSettings final {
    bool enabled = true;
    bool showContours = true;
    bool glow = true;
    float terrainOpacity = 0.30f;
    float structureOpacity = 0.42f;
    float contourOpacity = 0.48f;
    uint32_t maximumPolygons = 16384;
    uint32_t maximumContours = 32768;

    bool Validate(std::string* errorMessage = nullptr) const;
};

// Converts the baked, world-space visual asset into retained ImGui-friendly
// draw data. A non-current source never produces partial geometry: callers are
// explicitly told to use the live scene fallback instead.
class CourseMapHologramRenderer final {
public:
    const CourseMapHologramFrame& Build(
        const CourseMapVisualAsset* asset,
        CourseMapVisualBakeStatus sourceStatus,
        const CourseOverviewMapProjection& projection);

    bool SetSettings(CourseMapHologramSettings settings, std::string* errorMessage = nullptr);
    const CourseMapHologramSettings& Settings() const noexcept { return settings_; }
    void Invalidate() noexcept;
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }
    const CourseMapHologramStats& LifetimeStats() const noexcept {
        return lifetimeStats_;
    }

private:
    struct FrameKey final {
        uint64_t sourceFingerprint = 0;
        uint64_t contentRevision = 0;
        uint64_t settingsRevision = 0;
        uint64_t semanticLodRevision = 0;
        CourseMapVisualBakeStatus sourceStatus = CourseMapVisualBakeStatus::Missing;
        CourseOverviewMapProjectionSettings projection{};
        CourseOverviewMapRect rect{};
    };

    struct CacheEntry final {
        bool valid = false;
        FrameKey key{};
        CourseMapHologramFrame frame{};
    };

    CourseMapHologramFrame BuildFrame(
        const CourseMapVisualAsset& asset,
        const CourseOverviewMapProjection& projection);
    static bool SameKey(const FrameKey& lhs, const FrameKey& rhs) noexcept;

    CourseMapHologramSettings settings_{};
    CourseMapSemanticLODSystem semanticLod_{};
    std::array<CacheEntry, 4> caches_{};
    uint64_t settingsRevision_ = 1;
    CourseMapHologramStats lifetimeStats_{};
};

} // namespace editor
