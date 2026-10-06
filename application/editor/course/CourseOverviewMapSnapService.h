#pragma once

#include <string>

#include <cstdint>
#include <string_view>

#include "CourseOverviewMapProjection.h"

namespace editor {

enum class CourseOverviewMapSnapFlags : uint32_t {
    None = 0,
    WorldGrid = 1u << 0,
    RailDistance = 1u << 1,
    LateralOffset = 1u << 2,
    ControlPoint = 1u << 3,
};

inline CourseOverviewMapSnapFlags operator|(
    CourseOverviewMapSnapFlags a,
    CourseOverviewMapSnapFlags b) {
    return static_cast<CourseOverviewMapSnapFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct CourseOverviewMapSnapSettings final {
    bool worldGridEnabled = false;
    float worldGridSize = 1.0f;
    bool railDistanceEnabled = true;
    float railDistanceStep = 1.0f;
    bool lateralOffsetEnabled = true;
    float lateralOffsetStep = 0.5f;
    bool controlPointMagnetEnabled = true;
    float controlPointMagnetPixels = 10.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseOverviewMapSnapResult final {
    bool valid = false;
    Vector2 mapPosition{};
    Vector3 worldPosition{};
    RailAnchor railAnchor{};
    float railDistance = 0.0f;
    float depth = 0.0f;
    CourseOverviewMapSnapFlags flags = CourseOverviewMapSnapFlags::None;
    std::string snappedPointGuid;
};

// The single coordinate constraint service used by point drags, RailAnchor
// drags, Wave triggers and ActorAsset drops in every overview projection.
class CourseOverviewMapSnapService final {
public:
    bool SetSettings(CourseOverviewMapSnapSettings settings, std::string* errorMessage = nullptr);
    const CourseOverviewMapSnapSettings& Settings() const noexcept { return settings_; }

    CourseOverviewMapSnapResult SnapControlPoint(
        Vector2 mapPosition,
        float preservedDepth,
        const CourseOverviewMapProjection& projection,
        const CourseRailAuthoringModel& rail,
        std::string_view ignoredPointGuid = {}) const;
    CourseOverviewMapSnapResult SnapRailAnchor(
        Vector2 mapPosition,
        float preservedDepth,
        const CourseOverviewMapProjection& projection,
        const CourseRailAuthoringModel& rail) const;
    CourseOverviewMapSnapResult SnapRailDistance(
        Vector2 mapPosition,
        const CourseOverviewMapProjection& projection,
        const CourseRailAuthoringModel& rail) const;

private:
    float SnapScalar(float value, float step) const;
    RailAnchor AnchorAtDistance(
        float distance,
        const CourseRailAuthoringModel& rail) const;

    CourseOverviewMapSnapSettings settings_{};
};

} // namespace editor
