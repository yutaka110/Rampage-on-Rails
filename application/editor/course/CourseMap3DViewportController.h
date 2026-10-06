#pragma once

#include "CourseEnemyEditorController.h"
#include "CourseMap3DPickingService.h"
#include "CourseRailEditorController.h"
#include "CourseWaveEditorController.h"

#include <optional>
#include <string>

namespace editor {

struct CourseMap3DViewportBinding final {
    const CourseRailEditorController* rail = nullptr;
    const CourseEnemyEditorController* enemies = nullptr;
    const CourseWaveEditorController* waves = nullptr;
    EditorSelection* selection = nullptr;
};

struct CourseMap3DViewportState final {
    bool active = false;
    bool bound = false;
    bool valid = false;
    std::string message;
    CourseMap3DPickResult hovered{};
    uint64_t frameRevision = 0;
    uint64_t frameCacheHits = 0;
    uint64_t cameraRevision = 0;
};

// Document-scoped perspective navigation, retained rendering and canonical
// selection coordinator for the Course Map 3D view.
class CourseMap3DViewportController final {
public:
    bool Bind(CourseMap3DViewportBinding binding,
        std::string* errorMessage = nullptr);
    void Unbind();
    void SetActive(bool active) noexcept;
    void SetViewport(CourseOverviewMapRect viewport);

    void Orbit(Vector2 deltaPixels);
    void Pan(Vector2 deltaPixels);
    void Dolly(float wheelSteps);
    void FrameAll(const CourseTerrainMapAsset* terrain = nullptr,
        const CourseMapSceneVisualizationFrame* sceneVisualization = nullptr);
    bool FrameSelected();

    bool Rebuild(const CourseTerrainMapAsset* terrain = nullptr,
        const CourseMapSceneVisualizationFrame* sceneVisualization = nullptr,
        std::string* errorMessage = nullptr);
    void UpdatePlayhead(float railDistance);
    CourseMap3DPickResult HoverAt(Vector2 screenPosition);
    CourseMap3DPickResult SelectAt(Vector2 screenPosition,
        bool additive = false, bool toggle = false, bool cycleOverlaps = false);

    const CourseMap3DFrame& Frame() const noexcept { return frame_; }
    const CourseMap3DDynamicOverlay& DynamicOverlay() const noexcept {
        return dynamicOverlay_;
    }
    const CourseMap3DCamera& Camera() const noexcept { return camera_; }
    bool ConfigureCamera(const CourseMap3DCamera& camera, std::string* errorMessage = nullptr);
    CourseMap3DViewportRenderer& Renderer() noexcept { return renderer_; }
    const CourseMap3DViewportState& State() const noexcept { return state_; }

private:
    struct FrameKey final {
        uint64_t railRevision = 0;
        uint64_t enemyRevision = 0;
        uint64_t waveRevision = 0;
        uint64_t terrainRevision = 0;
        uint64_t sceneVisualizationRevision = 0;
        uint64_t cameraRevision = 0;
        uint64_t rendererRevision = 0;
        uint32_t selectionRevision = 0;
        CourseOverviewMapRect viewport{};
    };

    bool ValidateBinding(std::string* errorMessage) const;
    std::optional<Vector3> SelectedWorldPosition() const;
    static bool SameKey(const FrameKey& a, const FrameKey& b) noexcept;
    void TouchCamera() noexcept;

    CourseMap3DViewportBinding binding_{};
    CourseMap3DViewportState state_{};
    CourseOverviewMapRect viewport_{};
    CourseMap3DCamera camera_{};
    CourseMap3DViewportRenderer renderer_{};
    CourseMap3DPickingService picking_{};
    CourseMap3DFrame frame_{};
    CourseMap3DDynamicOverlay dynamicOverlay_{};
    std::optional<FrameKey> frameKey_{};
    uint64_t overlaySourceFrameRevision_ = 0;
    float overlayRailDistance_ = -1.0f;
    Vector2 lastPickPosition_{};
    std::string lastPickStableId_;
    std::size_t overlapCycle_ = 0;
};

} // namespace editor
