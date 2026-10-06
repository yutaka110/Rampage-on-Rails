#pragma once

#include "CourseEnemyAuthoringModel.h"
#include "CourseMapLabelLayoutSystem.h"
#include "CourseMapHybridCartographyCompositor.h"
#include "CourseMapSemanticLODSystem.h"
#include "CourseOverviewMapProjection.h"
#include "../EditorSelection.h"
#include "../scene/EditorScene.h"
#include "../../course/CourseActorAsset.h"
#include "../../course/EnemyWaveAsset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor {

enum class CourseMapSceneVisualKind : uint8_t {
    GameplayTerrain,
    HeroLandmark,
    VistaBackground,
    RockCluster,
    SceneStructure,
    AuthoredEnemy,
    EncounterEnemy,
};

struct CourseMapScenePolygon final {
    CourseMapSceneVisualKind kind = CourseMapSceneVisualKind::HeroLandmark;
    std::vector<Vector2> points;
    uint32_t fillColor = 0;
    uint32_t outlineColor = 0xffffffffu;
    float outlineThickness = 1.0f;
    std::string stableId;
    std::string label;
    bool locked = false;
};

struct CourseMapSceneActorProxy final {
    CourseMapSceneVisualKind kind = CourseMapSceneVisualKind::EncounterEnemy;
    Vector2 center{};
    Vector2 headingEnd{};
    std::vector<Vector2> silhouette;
    uint32_t fillColor = 0;
    uint32_t outlineColor = 0xffffffffu;
    float radiusPixels = 6.0f;
    std::string stableId;
    std::string actorAssetId;
    std::string displayName;
    bool selected = false;
    bool enabled = true;
    bool locked = false;
    uint32_t clusterCount = 1;
    // Retained world data lets perspective map views reuse the exact same
    // semantic actor set without reverse-projecting a 2D silhouette.
    Vector3 worldPosition{};
    Vector3 worldHeadingEnd{};
};

// Editor-only billboard layered over true projected geometry. Its radius is
// expressed in pixels, so long Course assets remain readable at Frame All.
struct CourseMapScreenSpaceProxy final {
    CourseMapSceneVisualKind kind = CourseMapSceneVisualKind::SceneStructure;
    Vector2 center{};
    Vector3 worldPosition{};
    float radiusPixels = 10.0f;
    uint32_t fillColor = 0;
    uint32_t outlineColor = 0xffffffffu;
    std::string stableId;
    std::string displayName;
    EditorDomainId domain = EditorDomainId::Unknown;
    uint64_t localIndex = 0;
    bool selected = false;
    bool locked = false;
};

using CourseMapSceneLabel = CourseMapPlacedLabel;

struct CourseMapSceneVisualizationStats final {
    uint32_t terrainPlacements = 0;
    uint32_t rockClusterEnvelopes = 0;
    uint32_t rockInstances = 0;
    uint32_t sceneStructures = 0;
    uint32_t authoredEnemies = 0;
    uint32_t encounterEnemies = 0;
    uint32_t encounterClusters = 0;
    uint32_t lodCulledPolygons = 0;
    uint32_t lodCulledActors = 0;
    uint32_t labels = 0;
    uint32_t labelsSuppressed = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
};

struct CourseMapSceneVisualizationFrame final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    std::vector<CourseMapScenePolygon> polygons;
    std::vector<CourseMapSceneActorProxy> actors;
    std::vector<CourseMapScreenSpaceProxy> screenSpaceProxies;
    std::vector<CourseMapSceneLabel> labels;
    CourseMapSemanticLODLevel semanticLod = CourseMapSemanticLODLevel::Course;
    CourseMapSceneVisualizationStats stats{};
    std::string message;
};

struct CourseMapSceneVisualizationSettings final {
    bool enabled = true;
    bool showTerrain = true;
    bool showRockClusters = true;
    bool showSceneStructures = true;
    bool showAuthoredEnemies = true;
    bool showEncounterPreview = true;
    bool showLabels = true;
    bool hologramGrid = true;
    float terrainOpacity = 0.34f;
    float structureOpacity = 0.46f;
    float enemyOpacity = 0.88f;
    uint32_t maxTerrainPrimitives = 1024;
    uint32_t maxRockInstances = 2048;
    uint32_t maxSceneStructures = 1024;
    uint32_t maxActorProxies = 2048;
    uint32_t labelBudget = 160;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapSceneVisualizationInput final {
    const CourseOverviewMapProjection* projection = nullptr;
    const CourseRailAuthoringModel* rail = nullptr;
    const CourseEnemyAuthoringModel* enemies = nullptr;
    const CourseAsset* course = nullptr;
    const EditorScene* scene = nullptr;
    const EditorSelection* selection = nullptr;
    uint64_t courseRevision = 0;
    uint32_t railGeneration = 0;
    uint32_t enemyGeneration = 0;
    CourseMapCoarseGeometryVisibility coarseGeometry{};
};

// Retained visualization layer for the large Course Map Editor. It resolves
// authored terrain, scene structures, persistent enemy placements and legacy
// encounter assets into the exact projection used by Overview picking.
class CourseMapSceneVisualizationPipeline final {
public:
    const CourseMapSceneVisualizationFrame& Build(
        const CourseMapSceneVisualizationInput& input);
    const CourseMapSceneVisualizationFrame* CurrentFrame(
        CourseOverviewMapProjectionMode mode) const noexcept;

    bool SetSettings(CourseMapSceneVisualizationSettings settings, std::string* errorMessage = nullptr);
    const CourseMapSceneVisualizationSettings& Settings() const noexcept {
        return settings_;
    }
    void SetResourceRoot(std::filesystem::path resourceRoot);
    const std::filesystem::path& ResourceRoot() const noexcept {
        return resourceRoot_;
    }
    void ReloadVisualAssets();
    void Invalidate();
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }
    const CourseMapSceneVisualizationStats& LifetimeStats() const noexcept {
        return lifetimeStats_;
    }

private:
    struct FrameKey final {
        uint64_t courseSignature = 0;
        uint64_t courseRevision = 0;
        uint64_t sceneRevision = 0;
        uint64_t settingsRevision = 0;
        uint64_t assetRevision = 0;
        uint64_t semanticLodRevision = 0;
        uint32_t selectionRevision = 0;
        uint32_t railGeneration = 0;
        uint32_t enemyGeneration = 0;
        CourseOverviewMapProjectionSettings projection{};
        CourseOverviewMapRect rect{};
        bool hasScene = false;
        bool hasEnemies = false;
        CourseMapCoarseGeometryVisibility coarseGeometry{};
    };

    struct CacheEntry final {
        bool valid = false;
        FrameKey key{};
        CourseMapSceneVisualizationFrame frame{};
    };

    CourseMapSceneVisualizationFrame BuildFrame(
        const CourseMapSceneVisualizationInput& input);
    EnemyWaveAsset* ResolveWaveAsset(std::string_view id);
    CourseActorAsset* ResolveActorAsset(std::string_view id);
    static uint64_t ComputeCourseSignature(const CourseAsset& course);
    static bool SameKey(const FrameKey& lhs, const FrameKey& rhs) noexcept;

    CourseMapSceneVisualizationSettings settings_{};
    CourseMapSemanticLODSystem semanticLod_{};
    CourseMapLabelLayoutSystem labelLayout_{};
    std::filesystem::path resourceRoot_{"Resources/courses"};
    std::array<CacheEntry, 4> caches_{};
    std::unordered_map<std::string, EnemyWaveAsset> waveCache_;
    std::unordered_map<std::string, CourseActorAsset> actorCache_;
    std::unordered_set<std::string> missingWaves_;
    std::unordered_set<std::string> missingActors_;
    uint64_t settingsRevision_ = 1;
    uint64_t assetRevision_ = 1;
    CourseMapSceneVisualizationStats lifetimeStats_{};
};

const char* ToString(CourseMapSceneVisualKind kind) noexcept;

} // namespace editor
