#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "CourseRailAuthoringModel.h"

namespace editor {

enum class CourseOverviewMapProjectionMode : uint8_t {
    Top,
    Side,
    RailUnwrapped,
    Free,
};

struct CourseOverviewMapRect final {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool Valid() const noexcept { return width > 1.0f && height > 1.0f; }
    bool Contains(Vector2 point) const noexcept {
        return point.x >= x && point.x <= x + width &&
            point.y >= y && point.y <= y + height;
    }
};

struct CourseOverviewMapProjectionSettings final {
    CourseOverviewMapProjectionMode mode = CourseOverviewMapProjectionMode::Top;
    float zoom = 1.0f;
    Vector2 panPixels{};
    float paddingPixels = 28.0f;
    float freeYawRadians = 0.65f;
    float freePitchRadians = -0.65f;
    uint32_t fitSamplesPerSegment = 24;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseOverviewMapProjectedPoint final {
    bool valid = false;
    Vector2 mapPosition{};
    Vector2 rawPosition{};
    Vector3 worldPosition{};
    float railDistance = 0.0f;
    float depth = 0.0f;
};

struct CourseOverviewMapProjectionState final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    Vector2 rawMinimum{};
    Vector2 rawMaximum{};
    Vector2 rawCenter{};
    float baseScale = 1.0f;
    float effectiveScale = 1.0f;
    Vector3 freeRight{1.0f, 0.0f, 0.0f};
    Vector3 freeUp{0.0f, 1.0f, 0.0f};
    Vector3 freeNormal{0.0f, 0.0f, 1.0f};
};

// Deterministic world/rail-to-map transform shared by renderer, picking and
// future edit tools. Pan is expressed in pixels; raw bounds remain stable.
class CourseOverviewMapProjection final {
public:
    bool Configure(
        const CourseRailAuthoringModel* rail,
        CourseOverviewMapRect rect,
        CourseOverviewMapProjectionSettings settings,
        std::string* errorMessage = nullptr,
        const CourseRailAuthoringModel* boundsRail = nullptr,
        const std::vector<Vector3>* additionalFitPoints = nullptr);

    CourseOverviewMapProjectedPoint ProjectWorld(const Vector3& world) const;
    // Lightweight render-only projection. Top, Side and Free are pure
    // world-to-screen transforms and deliberately skip the O(rail segments x
    // subdivisions) nearest-rail query. Rail Unwrapped still resolves the
    // nearest rail anchor because its screen coordinates require it.
    CourseOverviewMapProjectedPoint ProjectWorldScreenOnly(
        const Vector3& world) const;
    CourseOverviewMapProjectedPoint ProjectRail(
        float railDistance,
        float lateralOffset = 0.0f,
        float verticalOffset = 0.0f) const;
    Vector3 Unproject(Vector2 mapPosition, float depth = 0.0f) const;
    Vector2 RawToMap(Vector2 rawPosition) const;
    Vector2 MapToRaw(Vector2 mapPosition) const;

    const CourseOverviewMapProjectionSettings& Settings() const noexcept {
        return settings_;
    }
    const CourseOverviewMapProjectionState& State() const noexcept { return state_; }
    const CourseRailAuthoringModel* Rail() const noexcept { return rail_; }

    // Produces an immutable, self-contained projection that can safely be
    // consumed by a background render job while the authoring model changes on
    // the editor thread.
    CourseOverviewMapProjection MakeBackgroundSnapshot() const;

private:
    Vector2 ProjectRaw(const Vector3& world, float* depth) const;
    void BuildFreeBasis();
    void BuildRawBounds();

    const CourseRailAuthoringModel* rail_ = nullptr;
    const CourseRailAuthoringModel* boundsRail_ = nullptr;
    const std::vector<Vector3>* additionalFitPoints_ = nullptr;
    std::shared_ptr<const CourseRailAuthoringModel> ownedRailSnapshot_{};
    CourseOverviewMapProjectionSettings settings_{};
    CourseOverviewMapProjectionState state_{};
};

const char* ToString(CourseOverviewMapProjectionMode mode);

} // namespace editor
