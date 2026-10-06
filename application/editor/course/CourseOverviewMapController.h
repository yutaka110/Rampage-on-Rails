#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "CourseEnemyEditorController.h"
#include "CourseOverviewMapPickingService.h"
#include "CourseOverviewMapVisibilityService.h"
#include "CoursePreviewSimulationSystem.h"
#include "CourseRailEditorController.h"
#include "CourseWaveEditorController.h"

namespace editor {

struct CourseOverviewMapControllerBinding final {
    const CourseRailEditorController* rail = nullptr;
    const CourseEnemyEditorController* enemies = nullptr;
    const CourseWaveEditorController* waves = nullptr;
    EditorSelection* selection = nullptr;
    const CoursePreviewSimulationSystem* preview = nullptr;
};

struct CourseOverviewMapControllerState final {
    bool bound = false;
    bool valid = false;
    std::string message;
    CourseOverviewMapProjectionMode mode = CourseOverviewMapProjectionMode::Top;
    CourseOverviewMapPickResult hovered{};
    uint64_t frameRevision = 0;
    uint64_t frameCacheHits = 0;
    uint64_t playheadOverlayRevision = 0;
    uint64_t playheadOverlayCacheHits = 0;
};

// Orchestrates projection, retained rendering, hit testing and canonical
// EditorSelection updates for one active CourseAsset document.
class CourseOverviewMapController final {
public:
    bool Bind(CourseOverviewMapControllerBinding binding, std::string* errorMessage = nullptr);
    void Unbind();
    bool Synchronize(std::string* errorMessage = nullptr);

    void SetViewport(CourseOverviewMapRect rect);
    void SetSceneFitPoints(
        const std::vector<Vector3>* fitPoints,
        uint64_t revision);
    void SetMode(CourseOverviewMapProjectionMode mode);
    void FrameAll();
    bool FrameMapPoints(
        const std::vector<Vector2>& mapPoints,
        float minimumZoom = 5.5f,
        float paddingPixels = 96.0f);
    // Interactive pan is a presentation transaction: canonical projection
    // settings accumulate immediately, while the retained frame is translated
    // in screen space until EndInteractivePan commits one rebuild.
    void BeginInteractivePan() noexcept;
    void PanPixels(Vector2 delta);
    bool EndInteractivePan() noexcept;
    void ZoomAt(Vector2 mapPosition, float factor);
    bool Rebuild(float fallbackPlayheadDistance = -1.0f, std::string* errorMessage = nullptr);
    void SetPreviewCourse(const CourseAsset* previewCourse);
    void ClearPreviewCourse();
    bool HasPreviewCourse() const noexcept { return previewRail_.has_value(); }

    CourseOverviewMapPickResult HoverAt(Vector2 mapPosition);
    CourseOverviewMapPickResult SelectAt(
        Vector2 mapPosition,
        bool additive = false,
        bool toggle = false,
        bool cycleOverlaps = false);

    const CourseOverviewMapFrame& Frame() const noexcept { return frame_; }
    const CourseOverviewMapVisibleFrame& VisibleFrame() const noexcept {
        return visibility_.Frame();
    }
    const CourseOverviewMapDynamicPlayheadOverlay& PlayheadOverlay() const noexcept {
        return playheadOverlay_;
    }
    const CourseOverviewMapProjection& Projection() const noexcept { return projection_; }
    bool InteractivePanActive() const noexcept { return interactivePanActive_; }
    Vector2 PresentationOffset() const noexcept { return presentationOffset_; }
    const CourseOverviewMapControllerState& State() const noexcept { return state_; }
    const CourseOverviewMapProjectionSettings& ProjectionSettings() const noexcept {
        return projectionSettings_;
    }
    bool ConfigureProjection(const CourseOverviewMapProjectionSettings& settings,
        std::string* errorMessage = nullptr);
    CourseOverviewMapRenderer& Renderer() noexcept { return renderer_; }
    CourseOverviewMapVisibilityService& Visibility() noexcept { return visibility_; }

private:
    struct RetainedFrameKey final {
        uint64_t railRevision = 0;
        uint64_t enemyRevision = 0;
        uint64_t waveRevision = 0;
        uint64_t previewRevision = 0;
        uint64_t rendererSignature = 0;
        uint64_t viewportRevision = 0;
        uint64_t visibilitySettingsRevision = 0;
        uint64_t sceneBoundsRevision = 0;
        uint32_t railGeneration = 0;
        uint32_t enemyGeneration = 0;
        uint32_t waveGeneration = 0;
        uint32_t selectionRevision = 0;
        CourseOverviewMapRect viewport{};
        CourseOverviewMapProjectionSettings projection{};
        bool hasEnemies = false;
        bool hasWaves = false;
    };

    struct PlayheadOverlayKey final {
        uint64_t staticFrameRevision = 0;
        uint64_t rendererSignature = 0;
        float playheadDistance = -1.0f;
    };

    bool ValidateBinding(std::string* errorMessage) const;
    float ResolvePlayheadDistance(float fallback) const;
    RetainedFrameKey BuildFrameKey() const;
    static bool SameFrameKey(
        const RetainedFrameKey& lhs,
        const RetainedFrameKey& rhs) noexcept;
    void InvalidateFrameCache() noexcept;
    void RefreshPlayheadOverlay(float playheadDistance);
    void InvalidatePlayheadOverlay() noexcept;

    CourseOverviewMapControllerBinding binding_{};
    CourseOverviewMapControllerState state_{};
    CourseOverviewMapRect viewport_{};
    CourseOverviewMapProjectionSettings projectionSettings_{};
    const std::vector<Vector3>* sceneFitPoints_ = nullptr;
    uint64_t sceneBoundsRevision_ = 0;
    CourseOverviewMapProjection projection_{};
    CourseOverviewMapRenderer renderer_{};
    CourseOverviewMapPickingService picking_{};
    CourseOverviewMapVisibilityService visibility_{};
    CourseOverviewMapFrame frame_{};
    CourseOverviewMapDynamicPlayheadOverlay playheadOverlay_{};
    std::optional<RetainedFrameKey> retainedFrameKey_;
    std::optional<PlayheadOverlayKey> playheadOverlayKey_;
    uint64_t previewRevision_ = 0;
    uint64_t viewportRevision_ = 0;
    bool interactivePanActive_ = false;
    Vector2 presentationOffset_{};
    std::optional<CourseRailAuthoringModel> previewRail_;
    std::optional<CourseEnemyAuthoringModel> previewEnemies_;
    std::optional<CourseWaveAuthoringModel> previewWaves_;
    Vector2 lastPickPosition_{};
    std::string lastPickStableId_;
    std::size_t overlapCycle_ = 0;
};

} // namespace editor
