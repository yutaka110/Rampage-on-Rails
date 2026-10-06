#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CourseOverviewMapController.h"
#include "CourseOverviewMapMultiViewCoordinator.h"
#include "CourseRailConstraintValidationSystem.h"

namespace editor {

struct CourseRailElevationProfileSettings final {
    float paddingPixels = 24.0f;
    float zoomDistance = 1.0f;
    float zoomHeight = 1.0f;
    Vector2 panPixels{};
    bool heightSnapEnabled = true;
    float heightSnapStep = 0.5f;
    uint32_t samplesPerSegment = 24;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseRailElevationProfileLine final {
    Vector2 start{};
    Vector2 end{};
    uint32_t color = 0xffffffffu;
    float thickness = 1.0f;
};

struct CourseRailElevationProfileMarker final {
    Vector2 position{};
    std::string pointGuid;
    uint32_t pointIndex = 0;
    float railDistance = 0.0f;
    float height = 0.0f;
    uint32_t color = 0xffffffffu;
    float radius = 5.0f;
    bool selected = false;
};

struct CourseRailElevationProfileFrame final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    std::vector<CourseRailElevationProfileLine> gridLines;
    std::vector<CourseRailElevationProfileLine> profileLines;
    std::vector<CourseRailElevationProfileMarker> markers;
    float railLength = 0.0f;
    float minimumHeight = 0.0f;
    float maximumHeight = 0.0f;
    std::string message;
};

struct CourseRailElevationDynamicPlayheadOverlay final {
    bool valid = false;
    bool visible = false;
    CourseOverviewMapRect rect{};
    Vector2 position{};
    float railDistance = 0.0f;
    uint64_t revision = 0;
};

struct CourseRailElevationProfileInput final {
    Vector2 mapPosition{};
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool cancelPressed = false;
};

struct CourseRailElevationProfileState final {
    bool bound = false;
    bool active = true;
    bool dragging = false;
    bool previewValid = false;
    int32_t hoveredMarker = -1;
    float focusDistance = 0.0f;
    uint64_t frameRevision = 0;
    uint64_t frameCacheHits = 0;
    uint64_t playheadOverlayRevision = 0;
    uint64_t playheadOverlayCacheHits = 0;
    uint64_t editRevision = 0;
    std::string message;
};

// Distance/height editor sharing the CourseAsset and EditorSelection used by
// the orthographic views. A drag is preview-only and release emits exactly one
// MovePoint mutation, keeping Undo deterministic.
class CourseRailElevationProfileEditor final {
public:
    bool Bind(
        CourseRailEditorController* rail,
        EditorSelection* selection,
        CourseOverviewMapController* overview = nullptr,
        CourseOverviewMapMultiViewCoordinator* multiView = nullptr,
        std::string* errorMessage = nullptr);
    void Unbind();
    void SetActive(bool active);
    void SetViewport(CourseOverviewMapRect rect);
    bool SetSettings(CourseRailElevationProfileSettings settings, std::string* errorMessage = nullptr);
    void Pan(Vector2 deltaPixels);
    void ZoomAt(Vector2 mapPosition, float distanceFactor, float heightFactor);
    void FrameAll();
    bool Rebuild(
        float playheadDistance = -1.0f,
        const CourseRailConstraintReport* constraints = nullptr,
        std::string* errorMessage = nullptr);
    void Tick(const CourseRailElevationProfileInput& input);
    void Cancel(std::string message = {});
    void SetFocusDistance(float railDistance);

    Vector2 Project(float railDistance, float height) const;
    float UnprojectDistance(float mapX) const;
    float UnprojectHeight(float mapY) const;
    const CourseRailElevationProfileFrame& Frame() const noexcept { return frame_; }
    const CourseRailElevationDynamicPlayheadOverlay& PlayheadOverlay() const noexcept {
        return playheadOverlay_;
    }
    const CourseRailElevationProfileState& State() const noexcept { return state_; }
    const CourseRailElevationProfileSettings& Settings() const noexcept { return settings_; }
    const CourseAsset* PreviewCourse() const noexcept {
        return state_.previewValid ? &previewCourse_ : nullptr;
    }

private:
    struct RetainedFrameKey final {
        uint64_t railRevision = 0;
        uint64_t previewRevision = 0;
        uint64_t constraintSignature = 0;
        uint64_t viewportRevision = 0;
        uint32_t railGeneration = 0;
        uint32_t selectionRevision = 0;
        CourseOverviewMapRect viewport{};
        CourseRailElevationProfileSettings settings{};
    };

    struct PlayheadOverlayKey final {
        uint64_t staticFrameRevision = 0;
        float playheadDistance = -1.0f;
    };

    bool Validate(std::string* errorMessage) const;
    bool CanMutate() const;
    const CourseRailAuthoringModel* DisplayRail() const;
    void BeginDrag(uint32_t markerIndex);
    void UpdateDrag(float mapY);
    void CommitDrag();
    void PublishPreview();
    void ClearPublishedPreview();
    void UpdateHover(Vector2 mapPosition);
    float PointDistance(uint32_t pointIndex, const CourseRailAuthoringModel& rail) const;
    RetainedFrameKey BuildFrameKey(
        const CourseRailConstraintReport* constraints) const;
    static bool SameFrameKey(
        const RetainedFrameKey& lhs,
        const RetainedFrameKey& rhs) noexcept;
    void InvalidateFrameCache() noexcept;
    void RefreshPlayheadOverlay(float playheadDistance);
    void InvalidatePlayheadOverlay() noexcept;

    CourseRailEditorController* rail_ = nullptr;
    EditorSelection* selection_ = nullptr;
    CourseOverviewMapController* overview_ = nullptr;
    CourseOverviewMapMultiViewCoordinator* multiView_ = nullptr;
    CourseRailElevationProfileSettings settings_{};
    CourseRailElevationProfileState state_{};
    CourseRailElevationProfileFrame frame_{};
    CourseRailElevationDynamicPlayheadOverlay playheadOverlay_{};
    std::optional<RetainedFrameKey> retainedFrameKey_;
    std::optional<PlayheadOverlayKey> playheadOverlayKey_;
    uint64_t previewRevision_ = 0;
    uint64_t viewportRevision_ = 0;
    CourseOverviewMapRect viewport_{};
    CourseAsset previewCourse_{};
    std::optional<CourseRailAuthoringModel> previewRail_;
    RailPathControlPoint dragOriginal_{};
    std::string dragPointGuid_;
    uint64_t dragExpectedRevision_ = 0;
    float fitMinimumHeight_ = 0.0f;
    float fitMaximumHeight_ = 1.0f;
};

} // namespace editor
