#include "CourseMapSemanticLODSystem.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>

namespace editor {

CourseMapSemanticLODPolicy CourseMapSemanticLODSystem::Evaluate(
    const CourseOverviewMapProjection& projection) const noexcept {
    CourseMapSemanticLODPolicy policy{};
    const float zoom = projection.State().valid
        ? projection.Settings().zoom : 1.0f;
    if (zoom < settings_.regionZoom) {
        policy.level = CourseMapSemanticLODLevel::Course;
        return policy;
    }
    if (zoom < settings_.detailZoom) {
        policy.level = CourseMapSemanticLODLevel::Region;
        policy.showRockInstances = true;
        policy.showStructureLabels = true;
        policy.showAuthoredEnemyLabels = true;
        policy.encounterClusterCellPixels = 50.0f;
        policy.minimumPolygonAreaPixels = 2.0f;
        policy.actorScale = 0.92f;
        policy.minimumActorRadiusPixels = 8.0f;
        policy.structureProxyRadiusPixels = 9.0f;
        policy.labelBudget = 36;
        return policy;
    }
    if (zoom < settings_.inspectZoom) {
        policy.level = CourseMapSemanticLODLevel::Detail;
        policy.showRockInstances = true;
        policy.showIndividualEncounterEnemies = true;
        policy.clusterEncounterEnemies = false;
        policy.showTerrainLabels = true;
        policy.showStructureLabels = true;
        policy.showAuthoredEnemyLabels = true;
        policy.minimumPolygonAreaPixels = 1.0f;
        policy.actorScale = 1.0f;
        policy.minimumActorRadiusPixels = 7.0f;
        policy.structureProxyRadiusPixels = 8.0f;
        policy.labelBudget = 72;
        return policy;
    }
    policy.level = CourseMapSemanticLODLevel::Inspect;
    policy.showRockInstances = true;
    policy.showIndividualEncounterEnemies = true;
    policy.clusterEncounterEnemies = false;
    policy.showTerrainLabels = true;
    policy.showStructureLabels = true;
    policy.showAuthoredEnemyLabels = true;
    policy.showEncounterEnemyLabels = true;
    policy.minimumPolygonAreaPixels = 0.0f;
    policy.actorScale = 1.08f;
    policy.minimumActorRadiusPixels = 6.0f;
    policy.structureProxyRadiusPixels = 7.0f;
    policy.labelBudget = 160;
    return policy;
}

bool CourseMapSemanticLODSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(regionZoom, 1.05f, 16.0f) &&
        settings::InRange(detailZoom, regionZoom + 0.1f, 32.0f) &&
        settings::InRange(inspectZoom, detailZoom + 0.1f, 64.0f);
    return settings::Result(valid, errorMessage,
        "CourseMapSemanticLODSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapSemanticLODSystem::SetSettings(
    CourseMapSemanticLODSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (settings_.regionZoom == settings.regionZoom &&
        settings_.detailZoom == settings.detailZoom &&
        settings_.inspectZoom == settings.inspectZoom) return true;
    settings_ = settings;
    ++settingsRevision_;
    return true;
}

const char* ToString(CourseMapSemanticLODLevel level) noexcept {
    switch (level) {
    case CourseMapSemanticLODLevel::Course: return "Course";
    case CourseMapSemanticLODLevel::Region: return "Region";
    case CourseMapSemanticLODLevel::Detail: return "Detail";
    case CourseMapSemanticLODLevel::Inspect: return "Inspect";
    }
    return "Unknown";
}

} // namespace editor
