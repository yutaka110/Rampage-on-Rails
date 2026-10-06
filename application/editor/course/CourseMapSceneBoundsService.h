#pragma once

#include <string>

#include "CourseEnemyAuthoringModel.h"
#include "CourseRailAuthoringModel.h"
#include "../scene/EditorScene.h"

#include <cstdint>
#include <vector>

namespace editor {

struct CourseMapSceneBoundsSettings final {
    bool includeGameplayTerrain = true;
    bool includeHeroLandmarks = true;
    bool includeVistaBackground = false;
    bool includeRockClusters = true;
    bool includeSceneStructures = true;
    bool includeEnemies = true;
    float worldPadding = 8.0f;
    uint32_t maximumFitPoints = 8192;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapSceneBoundsStats final {
    uint32_t terrainPlacements = 0;
    uint32_t rockClusters = 0;
    uint32_t sceneStructures = 0;
    uint32_t enemies = 0;
    uint32_t fitPoints = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
};

struct CourseMapSceneBoundsInput final {
    const CourseRailAuthoringModel* rail = nullptr;
    const CourseEnemyAuthoringModel* enemies = nullptr;
    const CourseAsset* course = nullptr;
    const EditorScene* scene = nullptr;
    uint64_t courseRevision = 0;
    uint64_t enemyRevision = 0;
    uint32_t railGeneration = 0;
    uint32_t enemyGeneration = 0;
};

struct CourseMapSceneBoundsFrame final {
    bool valid = false;
    Vector3 worldMinimum{};
    Vector3 worldMaximum{};
    std::vector<Vector3> fitPoints;
    CourseMapSceneBoundsStats stats{};
    uint64_t revision = 0;
};

// Builds the world-space fitting set shared by Top, Side, Free and Rail views.
// Distant vista decoration is opt-in so composition art cannot collapse the
// editable gameplay route to a few pixels.
class CourseMapSceneBoundsService final {
public:
    const CourseMapSceneBoundsFrame& Build(const CourseMapSceneBoundsInput& input);
    bool SetSettings(CourseMapSceneBoundsSettings settings, std::string* errorMessage = nullptr);
    const CourseMapSceneBoundsSettings& Settings() const noexcept { return settings_; }
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }
    void Invalidate() noexcept;

private:
    struct CacheKey final {
        uint64_t courseSignature = 0;
        uint64_t courseRevision = 0;
        uint64_t sceneRevision = 0;
        uint64_t enemyRevision = 0;
        uint64_t settingsRevision = 0;
        uint32_t railGeneration = 0;
        uint32_t enemyGeneration = 0;
        bool hasScene = false;
        bool hasEnemies = false;
    };

    static uint64_t CourseSignature(const CourseAsset& course);
    static bool SameKey(const CacheKey& a, const CacheKey& b) noexcept;

    CourseMapSceneBoundsSettings settings_{};
    CourseMapSceneBoundsFrame frame_{};
    CacheKey cachedKey_{};
    bool cacheValid_ = false;
    uint64_t settingsRevision_ = 1;
    CourseMapSceneBoundsStats lifetimeStats_{};
};

} // namespace editor
