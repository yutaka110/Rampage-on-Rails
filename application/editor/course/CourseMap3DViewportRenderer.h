#pragma once

#include "CourseEnemyAuthoringModel.h"
#include "CourseMapSceneVisualizationPipeline.h"
#include "CourseOverviewMapProjection.h"
#include "CourseOverviewMapRenderer.h"
#include "CourseTerrainMapAsset.h"
#include "CourseWaveAuthoringModel.h"
#include "../EditorSelection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace editor {

struct CourseMap3DCamera final {
    Vector3 target{};
    float yawRadians = 0.65f;
    float pitchRadians = -0.45f;
    float distance = 600.0f;
    float verticalFovRadians = 0.785398163f;
    float nearPlane = 0.5f;
    float farPlane = 200000.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMap3DCameraBasis final {
    Vector3 position{};
    Vector3 forward{0.0f, 0.0f, 1.0f};
    Vector3 right{1.0f, 0.0f, 0.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
};

struct CourseMap3DProjectedPoint final {
    bool valid = false;
    Vector2 screen{};
    float depth = 0.0f;
};

struct CourseMap3DRay final {
    bool valid = false;
    Vector3 origin{};
    Vector3 direction{0.0f, 0.0f, 1.0f};
};

CourseMap3DCameraBasis BuildCourseMap3DCameraBasis(
    const CourseMap3DCamera& camera) noexcept;
CourseMap3DProjectedPoint ProjectCourseMap3DPoint(
    Vector3 world,
    const CourseMap3DCamera& camera,
    CourseOverviewMapRect viewport) noexcept;
CourseMap3DRay BuildCourseMap3DScreenRay(
    Vector2 screen,
    const CourseMap3DCamera& camera,
    CourseOverviewMapRect viewport) noexcept;

struct CourseMap3DLine final {
    CourseOverviewMapItemKind kind = CourseOverviewMapItemKind::None;
    Vector2 start{};
    Vector2 end{};
    Vector3 worldStart{};
    Vector3 worldEnd{};
    float startDepth = 0.0f;
    float endDepth = 0.0f;
    uint32_t color = 0xffffffffu;
    float thickness = 1.0f;
    float pickRadiusPixels = 6.0f;
    EditorObjectHandle handle{};
    std::string guid;
    bool selectable = false;
    bool selected = false;
};

struct CourseMap3DMarker final {
    CourseOverviewMapItemKind kind = CourseOverviewMapItemKind::None;
    Vector2 screen{};
    Vector3 world{};
    float depth = 0.0f;
    float radiusPixels = 5.0f;
    float worldRadius = 1.0f;
    float railDistance = 0.0f;
    uint32_t color = 0xffffffffu;
    EditorObjectHandle handle{};
    std::string guid;
    bool selectable = false;
    bool selected = false;
    bool enabled = true;
    bool locked = false;
    CourseMapSceneVisualKind visualKind = CourseMapSceneVisualKind::AuthoredEnemy;
};

struct CourseMap3DTriangle final {
    Vector2 a{};
    Vector2 b{};
    Vector2 c{};
    float depth = 0.0f;
    uint32_t color = 0;
};

struct CourseMap3DLabel final {
    Vector2 screen{};
    float depth = 0.0f;
    uint32_t color = 0xffffffffu;
    std::string text;
};

struct CourseMap3DRenderStats final {
    uint32_t railLines = 0;
    uint32_t controlPoints = 0;
    uint32_t enemies = 0;
    uint32_t waves = 0;
    uint32_t encounterEnemies = 0;
    uint32_t sceneProxies = 0;
    uint32_t terrainTrianglesInspected = 0;
    uint32_t terrainTrianglesDrawn = 0;
};

struct CourseMap3DFrame final {
    bool valid = false;
    CourseOverviewMapRect viewport{};
    CourseMap3DCamera camera{};
    CourseMap3DCameraBasis basis{};
    std::vector<CourseMap3DLine> gridLines;
    std::vector<CourseMap3DTriangle> terrainTriangles;
    std::vector<CourseMap3DLine> lines;
    std::vector<CourseMap3DMarker> markers;
    std::vector<CourseMap3DLabel> labels;
    CourseMap3DRenderStats stats{};
    std::string message;
};

struct CourseMap3DRenderSettings final {
    uint32_t samplesPerRailSegment = 16;
    uint32_t terrainTriangleBudget = 7000;
    float gridExtent = 1000.0f;
    float gridStep = 100.0f;
    bool showTerrain = true;
    bool showGrid = true;
    bool showLabels = true;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMap3DRenderInput final {
    CourseOverviewMapRect viewport{};
    CourseMap3DCamera camera{};
    const CourseRailAuthoringModel* rail = nullptr;
    const CourseEnemyAuthoringModel* enemies = nullptr;
    const CourseWaveAuthoringModel* waves = nullptr;
    const CourseTerrainMapAsset* terrain = nullptr;
    const CourseMapSceneVisualizationFrame* sceneVisualization = nullptr;
    const EditorSelection* selection = nullptr;
    uint32_t railGeneration = 0;
    uint32_t enemyGeneration = 0;
    uint32_t waveGeneration = 0;
};

// Playhead/player presentation is intentionally separate from the retained
// static frame so simulation movement never rebuilds terrain or Course actors.
struct CourseMap3DDynamicOverlay final {
    bool valid = false;
    CourseMap3DMarker player{};
    CourseMap3DLine heading{};
    CourseMap3DLabel label{};
    float railDistance = 0.0f;
    uint64_t revision = 0;
};

// Builds a retained perspective command frame. The frame owns the exact
// world-space proxies consumed by CourseMap3DPickingService.
class CourseMap3DViewportRenderer final {
public:
    CourseMap3DFrame Build(const CourseMap3DRenderInput& input) const;
    bool SetSettings(CourseMap3DRenderSettings settings, std::string* errorMessage = nullptr);
    const CourseMap3DRenderSettings& Settings() const noexcept { return settings_; }
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }

private:
    CourseMap3DRenderSettings settings_{};
    uint64_t settingsRevision_ = 1;
};

} // namespace editor
