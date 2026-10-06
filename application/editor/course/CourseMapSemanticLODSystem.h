#pragma once

#include <string>

#include "CourseOverviewMapProjection.h"

#include <cstdint>

namespace editor {

enum class CourseMapSemanticLODLevel : uint8_t {
    Course,
    Region,
    Detail,
    Inspect,
};

struct CourseMapSemanticLODSettings final {
    float regionZoom = 1.8f;
    float detailZoom = 4.5f;
    float inspectZoom = 10.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapSemanticLODPolicy final {
    CourseMapSemanticLODLevel level = CourseMapSemanticLODLevel::Course;
    bool showVistaTerrain = true;
    bool showRockInstances = false;
    bool showSceneStructures = true;
    bool showIndividualEncounterEnemies = false;
    bool clusterEncounterEnemies = true;
    bool showTerrainLabels = false;
    bool showStructureLabels = false;
    bool showAuthoredEnemyLabels = false;
    bool showEncounterEnemyLabels = false;
    float encounterClusterCellPixels = 72.0f;
    float minimumPolygonAreaPixels = 3.0f;
    float actorScale = 0.82f;
    float minimumActorRadiusPixels = 9.0f;
    float selectedActorRadiusPixels = 14.0f;
    float structureProxyRadiusPixels = 10.0f;
    uint32_t labelBudget = 18;
};

// Converts continuous map zoom into a stable semantic tier. The policy is
// consumed by visualization generation, not just rendering, so hidden detail
// does not create draw commands or labels.
class CourseMapSemanticLODSystem final {
public:
    CourseMapSemanticLODPolicy Evaluate(
        const CourseOverviewMapProjection& projection) const noexcept;
    bool SetSettings(CourseMapSemanticLODSettings settings, std::string* errorMessage = nullptr);
    const CourseMapSemanticLODSettings& Settings() const noexcept { return settings_; }
    uint64_t SettingsRevision() const noexcept { return settingsRevision_; }

private:
    CourseMapSemanticLODSettings settings_{};
    uint64_t settingsRevision_ = 1;
};

const char* ToString(CourseMapSemanticLODLevel level) noexcept;

} // namespace editor
