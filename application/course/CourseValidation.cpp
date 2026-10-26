#include "CourseValidation.h"

#include "CourseAsset.h"
#include "EnemyWaveAsset.h"
#include "ObstacleAsset.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {
constexpr float kPi = 3.14159265358979323846f;

void AddIssue(
    CourseValidationReport& report,
    CourseValidationSeverity severity,
    std::string subject,
    std::string message,
    float distance = -1.0f) {
    report.issues.push_back({severity, std::move(message), std::move(subject), distance});
    switch (severity) {
    case CourseValidationSeverity::Error:
        ++report.errorCount;
        break;
    case CourseValidationSeverity::Warning:
        ++report.warningCount;
        break;
    case CourseValidationSeverity::Info:
        ++report.infoCount;
        break;
    }
}

bool FileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string JoinPath(const std::string& root, const char* folder, const std::string& id, const char* extension) {
    return root + "/" + folder + "/" + id + extension;
}

bool IsKnownEventType(const std::string& type) {
    static const std::unordered_set<std::string> knownTypes = {
        "enemy_wave",
        "obstacle",
        "boss",
        "boss_phase",
        "checkpoint",
        "setpiece",
        "vfx",
    };
    return knownTypes.find(type) != knownTypes.end();
}

std::string DefaultActorAssetForRole(const std::string& role) {
    if (role.find("boss") != std::string::npos || role.find("gatekeeper") != std::string::npos) {
        return "gatekeeper_boss";
    }
    if (role.find("turret") != std::string::npos || role.find("crossfire") != std::string::npos) {
        return "cliff_turret";
    }
    if (role.find("chase") != std::string::npos || role.find("pursuit") != std::string::npos) {
        return "drone_chaser";
    }
    return "drone_basic";
}

bool NearlyEqual(float a, float b) {
    return std::abs(a - b) <= 0.01f;
}

bool IsBlankReference(const std::string& value) {
    return value.empty() || value == "-";
}

bool IsKnownCameraBlendCurve(const std::string& value) {
    return value == "linear" ||
        value == "smoothstep" ||
        value == "ease_in" ||
        value == "ease_out" ||
        value == "cinematic_hold";
}

float ResolveRailLength(const CourseAsset& course, float optionLength) {
    if (optionLength > 0.0f) {
        return optionLength;
    }
    RailPath path;
    course.ApplyToRailPath(path);
    return path.Length();
}
} // namespace

const char* ToString(CourseValidationSeverity severity) {
    switch (severity) {
    case CourseValidationSeverity::Error:
        return "Error";
    case CourseValidationSeverity::Warning:
        return "Warning";
    case CourseValidationSeverity::Info:
        return "Info";
    }
    return "Info";
}

CourseValidationReport ValidateCourseAsset(
    const CourseAsset& course,
    const CourseValidationOptions& options) {
    CourseValidationReport report{};
    const float railLength = ResolveRailLength(course, options.railLength);

    if (course.name.empty()) {
        AddIssue(report, CourseValidationSeverity::Warning, "course", "Course name is empty.");
    }
    if (course.railPoints.size() < 2) {
        AddIssue(report, CourseValidationSeverity::Error, "rail", "Course needs at least two rail points.");
    }
    if (railLength <= 0.0f) {
        AddIssue(report, CourseValidationSeverity::Error, "rail", "Rail length is zero.");
    }

    for (size_t index = 0; index < course.railPoints.size(); ++index) {
        const RailPathControlPoint& point = course.railPoints[index];
        const std::string subject = "rail[" + std::to_string(index) + "]";
        if (point.corridorRadius <= 0.0f) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Corridor radius must be positive.");
        }
        if (point.speed <= 0.0f) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Rail speed must be positive.");
        }
        if (index > 0) {
            const RailPathControlPoint& previous = course.railPoints[index - 1];
            const float dx = point.position.x - previous.position.x;
            const float dy = point.position.y - previous.position.y;
            const float dz = point.position.z - previous.position.z;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (distanceSq < 1.0f) {
                AddIssue(report, CourseValidationSeverity::Warning, subject, "Rail point is almost identical to the previous point.");
            }
        }
    }

    if (course.cameraKeys.empty()) {
        AddIssue(report, CourseValidationSeverity::Warning, "camera", "No camera keys; runtime will use defaults.");
    }
    for (size_t index = 0; index < course.cameraKeys.size(); ++index) {
        const CourseCameraKey& key = course.cameraKeys[index];
        const std::string subject = "camera[" + std::to_string(index) + "]";
        if (key.distance < 0.0f || (railLength > 0.0f && key.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera key is outside rail length.", key.distance);
        }
        if (key.fovY < 15.0f * kPi / 180.0f || key.fovY > 110.0f * kPi / 180.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera FOV is outside the practical authoring range.", key.distance);
        }
        if (index > 0 && key.distance < course.cameraKeys[index - 1].distance) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Camera key will be sorted on save.", key.distance);
        }
    }

    float previousSectionEnd = 0.0f;
    for (size_t index = 0; index < course.sections.size(); ++index) {
        const CourseSection& section = course.sections[index];
        const std::string subject = "section[" + std::to_string(index) + "]";
        if (section.name.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Section name is empty.", section.startDistance);
        }
        if (section.startDistance < 0.0f || section.endDistance <= section.startDistance) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Section range is invalid.", section.startDistance);
        }
        if (railLength > 0.0f && section.endDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Section extends beyond rail length.", section.endDistance);
        }
        if (index > 0) {
            if (section.startDistance < previousSectionEnd - 0.01f) {
                AddIssue(report, CourseValidationSeverity::Warning, subject, "Section overlaps the previous section.", section.startDistance);
            } else if (section.startDistance > previousSectionEnd + 20.0f) {
                AddIssue(report, CourseValidationSeverity::Info, subject, "Large gap before this section.", section.startDistance);
            }
        }
        previousSectionEnd = (std::max)(previousSectionEnd, section.endDistance);
    }

    std::unordered_set<std::string> rideProfileGuids;
    for (size_t index = 0; index < course.rideProfiles.size(); ++index) {
        const CourseRideProfileDefinition& profile = course.rideProfiles[index];
        const std::string subject = "ride_profile[" + std::to_string(index) + "]";
        std::string profileError;
        if (!profile.Validate(&profileError)) {
            AddIssue(report, CourseValidationSeverity::Error, subject, profileError, profile.startDistance);
        }
        if (!rideProfileGuids.insert(profile.editorGuid).second) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                "Ride profile editor GUID is duplicated.", profile.startDistance);
        }
        if (railLength > 0.0f && profile.endDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Ride profile extends beyond rail length.", profile.endDistance);
        }
        if (!profile.cameraShotId.empty()) {
            const bool found = std::any_of(
                course.cinematicCameraShots.begin(), course.cinematicCameraShots.end(),
                [&profile](const CourseCinematicCameraShot& shot) {
                    return shot.id == profile.cameraShotId;
                });
            if (!found) {
                AddIssue(report, CourseValidationSeverity::Error, subject,
                    "Ride profile references a missing cinematic camera shot: " +
                        profile.cameraShotId,
                    profile.startDistance);
            }
        }
    }

    std::unordered_set<std::string> speedBeatGuids;
    for (size_t index = 0; index < course.rideSpeedBeats.size(); ++index) {
        const RailRideSpeedBeatDefinition& beat = course.rideSpeedBeats[index];
        const std::string subject = "ride_speed_beat[" + std::to_string(index) + "]";
        std::string beatError;
        if (!beat.Validate(&beatError)) {
            AddIssue(report, CourseValidationSeverity::Error, subject, beatError, beat.startDistance);
        }
        if (!speedBeatGuids.insert(beat.editorGuid).second) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                "Speed Beat editor GUID is duplicated.", beat.startDistance);
        }
        if (railLength > 0.0f && beat.endDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Speed Beat extends beyond rail length.", beat.endDistance);
        }
    }

    std::unordered_set<std::string> railRideEventGuids;
    for (size_t index = 0; index < course.railRideEvents.size(); ++index) {
        const CourseRailRideEventDefinition& event = course.railRideEvents[index];
        const std::string subject =
            "rail_ride_event[" + std::to_string(index) + "]";
        std::string eventError;
        if (!event.Validate(&eventError)) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                eventError, event.startDistance);
        }
        if (!railRideEventGuids.insert(event.editorGuid).second) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                "Rail Ride Event editor GUID is duplicated.",
                event.startDistance);
        }
        if (railLength > 0.0f && event.endDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Rail Ride Event extends beyond rail length.",
                event.endDistance);
        }
    }

    std::unordered_set<std::string> encounterBeatGuids;
    std::unordered_set<std::string> encounterIds;
    for (size_t index = 0; index < course.encounterBeats.size(); ++index) {
        const EnemyEncounterBeatDefinition& beat =
            course.encounterBeats[index];
        const std::string subject =
            "encounter_beat[" + std::to_string(index) + "]";
        std::string beatError;
        if (!beat.Validate(&beatError)) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                beatError, beat.triggerRailDistance);
        }
        if (!encounterBeatGuids.insert(beat.editorGuid).second) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                "Encounter Beat editor GUID is duplicated.",
                beat.triggerRailDistance);
        }
        if (!encounterIds.insert(beat.encounterId).second) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Encounter ID is reused; only one active Beat is paced at a time.",
                beat.triggerRailDistance);
        }
        const bool waveExists = std::any_of(
            course.waveDefinitions.begin(), course.waveDefinitions.end(),
            [&beat](const CourseWaveDefinition& wave) {
                return wave.editorGuid == beat.waveGuid;
            });
        if (!waveExists) {
            AddIssue(report, CourseValidationSeverity::Error, subject,
                "Encounter Beat references a missing Wave GUID: " +
                    beat.waveGuid,
                beat.triggerRailDistance);
        }
        if (!beat.cameraShotId.empty()) {
            const bool shotExists = std::any_of(
                course.cinematicCameraShots.begin(),
                course.cinematicCameraShots.end(),
                [&beat](const CourseCinematicCameraShot& shot) {
                    return shot.id == beat.cameraShotId;
                });
            if (!shotExists) {
                AddIssue(report, CourseValidationSeverity::Error, subject,
                    "Encounter Beat references a missing camera shot: " +
                        beat.cameraShotId,
                    beat.triggerRailDistance);
            }
        }
        if (railLength > 0.0f &&
            beat.triggerRailDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Encounter Beat trigger is beyond rail length.",
                beat.triggerRailDistance);
        }
        if (railLength > 0.0f &&
            beat.endRailDistance > railLength + 0.01f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject,
                "Encounter Beat authored range extends beyond rail length.",
                beat.endRailDistance);
        }
    }

    uint32_t gameplayTerrainCount = 0;
    uint32_t heroTerrainCount = 0;
    uint32_t vistaTerrainCount = 0;
    for (size_t index = 0; index < course.terrainPlacements.size(); ++index) {
        const CourseTerrainPlacement& placement = course.terrainPlacements[index];
        const std::string subject = "terrain[" + std::to_string(index) + "]";
        if (placement.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Terrain placement id is empty.", placement.distance);
        }
        if (placement.meshId.empty()) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Terrain placement mesh id is empty.", placement.distance);
        }
        if (placement.distance < 0.0f || (railLength > 0.0f && placement.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Terrain placement is outside rail length.", placement.distance);
        }
        if (placement.scale.x <= 0.0f || placement.scale.y <= 0.0f || placement.scale.z <= 0.0f) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Terrain placement scale must be positive.", placement.distance);
        }
        if (index > 0 && placement.distance < course.terrainPlacements[index - 1].distance) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Terrain placements will be sorted on save.", placement.distance);
        }

        if (placement.layer == CourseTerrainLayer::GameplayCollision) {
            ++gameplayTerrainCount;
            if (placement.collisionMode == CourseTerrainCollisionMode::None) {
                AddIssue(
                    report,
                    CourseValidationSeverity::Warning,
                    subject,
                    "Gameplay terrain should use proxy or solid collision.",
                    placement.distance);
            }
        } else if (placement.layer == CourseTerrainLayer::HeroLandmark) {
            ++heroTerrainCount;
        } else {
            ++vistaTerrainCount;
            if (placement.collisionMode != CourseTerrainCollisionMode::None) {
                AddIssue(
                    report,
                    CourseValidationSeverity::Warning,
                    subject,
                    "Vista background terrain should not use collision.",
                    placement.distance);
            }
        }
    }

    if (!course.terrainPlacements.empty()) {
        if (gameplayTerrainCount == 0) {
            AddIssue(report, CourseValidationSeverity::Info, "terrain", "No gameplay collision terrain placements authored.");
        }
        if (heroTerrainCount == 0) {
            AddIssue(report, CourseValidationSeverity::Info, "terrain", "No hero landmark terrain placements authored.");
        }
        if (vistaTerrainCount == 0) {
            AddIssue(report, CourseValidationSeverity::Info, "terrain", "No vista background terrain placements authored.");
        }
    }

    std::string terrainEditError;
    if (!course.terrainEditLayer.Validate(&terrainEditError)) {
        AddIssue(
            report,
            CourseValidationSeverity::Error,
            "terrain_edit_layer",
            "Terrain Edit Layer is invalid: " + terrainEditError);
    }
    for (size_t index = 0; index < course.terrainEditLayer.Stamps().size(); ++index) {
        const TerrainBrushStamp& stamp = course.terrainEditLayer.Stamps()[index];
        if (railLength > 0.0f && stamp.distance > railLength + stamp.radius) {
            AddIssue(
                report,
                CourseValidationSeverity::Warning,
                "terrain_brush[" + std::to_string(index) + "]",
                "Terrain brush stamp is outside rail length.",
                stamp.distance);
        }
    }

    for (size_t index = 0; index < course.rockClusters.size(); ++index) {
        const CourseRockCluster& cluster = course.rockClusters[index];
        const std::string subject = "rock_cluster[" + std::to_string(index) + "]";
        if (cluster.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Rock cluster id is empty.", cluster.distance);
        }
        if (cluster.meshId.empty()) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Rock cluster mesh id is empty.", cluster.distance);
        }
        if (cluster.distance < 0.0f || (railLength > 0.0f && cluster.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Rock cluster is outside rail length.", cluster.distance);
        }
        if (cluster.count == 0) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Rock cluster count is zero.", cluster.distance);
        }
        if (cluster.count > 32) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Rock cluster count will be clamped at runtime.", cluster.distance);
        }
        const uint32_t authoredLimit =
            cluster.type == CourseRockClusterType::HeroFracture ? 6u :
            cluster.type == CourseRockClusterType::FallingDebris ? 12u :
            cluster.type == CourseRockClusterType::VistaSilhouette ? 8u :
            10u;
        if (cluster.count > authoredLimit) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Rock cluster count is high for this composition type.", cluster.distance);
        }
        if (cluster.minScale <= 0.0f || cluster.maxScale <= 0.0f || cluster.maxScale < cluster.minScale) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Rock cluster scale range is invalid.", cluster.distance);
        }
        if (cluster.spread.x < 0.0f || cluster.spread.y < 0.0f || cluster.spread.z < 0.0f) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Rock cluster spread must be non-negative.", cluster.distance);
        }
        if (cluster.clearLaneRadius < 10.0f &&
            cluster.type != CourseRockClusterType::VistaSilhouette) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Rock cluster clear lane radius is small; gameplay readability may suffer.", cluster.distance);
        }
        if (cluster.type == CourseRockClusterType::AttachedDebris &&
            cluster.anchor == CourseRockClusterAnchor::CeilingBreak) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Attached debris on a ceiling break can read as floating; use falling_debris or wall/floor anchors.", cluster.distance);
        }
        if (cluster.type != CourseRockClusterType::FallingDebris &&
            cluster.anchor == CourseRockClusterAnchor::CeilingBreak &&
            cluster.spread.y > 12.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Static ceiling rock cluster has high vertical spread; it may look like floating debris.", cluster.distance);
        }
        if (cluster.type == CourseRockClusterType::VistaSilhouette &&
            cluster.anchor != CourseRockClusterAnchor::VistaWall) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Vista silhouette clusters usually work best on vista_wall anchors.", cluster.distance);
        }
        if (cluster.cullAheadDistance < 40.0f || cluster.cullBehindDistance < 20.0f) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Rock cluster cull distances are very tight.", cluster.distance);
        }
        std::unordered_set<uint32_t> overrideIndices;
        for (const CourseRockCluster::InstanceTransformOverride& transformOverride : cluster.instanceOverrides) {
            const std::string overrideSubject =
                subject + ".override[" + std::to_string(transformOverride.index) + "]";
            if (cluster.count > 0 && transformOverride.index >= cluster.count) {
                AddIssue(report, CourseValidationSeverity::Warning, overrideSubject, "Rock instance override index is outside cluster count.", cluster.distance);
            }
            if (!overrideIndices.insert(transformOverride.index).second) {
                AddIssue(report, CourseValidationSeverity::Warning, overrideSubject, "Duplicate rock instance override index; first matching override wins at runtime.", cluster.distance);
            }
            if (transformOverride.scale.x <= 0.0f ||
                transformOverride.scale.y <= 0.0f ||
                transformOverride.scale.z <= 0.0f) {
                AddIssue(report, CourseValidationSeverity::Error, overrideSubject, "Rock instance override scale must be positive.", cluster.distance);
            }
        }
    }

    std::unordered_set<std::string> terrainPlacementIds;
    for (const CourseTerrainPlacement& placement : course.terrainPlacements) {
        if (!placement.id.empty()) {
            terrainPlacementIds.insert(placement.id);
        }
    }

    std::unordered_set<std::string> lightingPresetIds;
    for (size_t index = 0; index < course.lightingPresets.size(); ++index) {
        const CourseLightingPreset& preset = course.lightingPresets[index];
        if (!preset.id.empty()) {
            lightingPresetIds.insert(preset.id);
        }
        const std::string subject = "lighting[" + std::to_string(index) + "]";
        if (preset.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Lighting preset id is empty.", preset.distance);
        }
        if (preset.distance < 0.0f || (railLength > 0.0f && preset.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Lighting preset is outside rail length.", preset.distance);
        }
        if (preset.blendDistance < 0.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Lighting blend distance should be non-negative.", preset.distance);
        }
        if (preset.fogEnd <= preset.fogStart) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Fog end should be greater than fog start.", preset.distance);
        }
    }

    std::unordered_set<std::string> cameraShotPresetIds;
    for (size_t index = 0; index < course.cameraShotPresets.size(); ++index) {
        const CourseCameraShotPreset& preset = course.cameraShotPresets[index];
        if (!preset.id.empty()) {
            cameraShotPresetIds.insert(preset.id);
        }
        const std::string subject = "camera_shot_preset[" + std::to_string(index) + "]";
        if (preset.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera shot preset id is empty.");
        }
        if (preset.mode.empty()) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Camera shot preset mode is empty.");
        }
    }

    std::unordered_set<std::string> cameraBlendAssetIds;
    for (size_t index = 0; index < course.cameraBlendAssets.size(); ++index) {
        const CourseCameraBlendAsset& blend = course.cameraBlendAssets[index];
        if (!blend.id.empty()) {
            cameraBlendAssetIds.insert(blend.id);
        }
        const std::string subject = "camera_blend_asset[" + std::to_string(index) + "]";
        if (blend.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera blend asset id is empty.");
        }
        if (blend.blendInDistance < 0.0f || blend.blendOutDistance < 0.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera blend distances should be non-negative.");
        }
        if (!IsKnownCameraBlendCurve(blend.curve)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Unknown camera blend curve: " + blend.curve);
        }
        if (blend.weightScale < 0.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera blend weight scale should be non-negative.");
        }
    }

    std::unordered_set<std::string> cameraShotIds;
    for (size_t index = 0; index < course.cinematicCameraShots.size(); ++index) {
        const CourseCinematicCameraShot& shot = course.cinematicCameraShots[index];
        if (!shot.id.empty()) {
            cameraShotIds.insert(shot.id);
        }
        const std::string subject = "camera_shot[" + std::to_string(index) + "]";
        std::string viewError;
        if (!shot.view.Validate(&viewError))
            AddIssue(report, CourseValidationSeverity::Error, subject, viewError, shot.startDistance);
        if (shot.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera shot id is empty.", shot.startDistance);
        }
        if (shot.endDistance <= shot.startDistance) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Camera shot range is invalid.", shot.startDistance);
        }
        if (shot.startDistance < 0.0f || (railLength > 0.0f && shot.endDistance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera shot extends outside rail length.", shot.startDistance);
        }
        if (!IsBlankReference(shot.presetId) &&
            cameraShotPresetIds.find(shot.presetId) == cameraShotPresetIds.end()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing camera shot preset: " + shot.presetId, shot.startDistance);
        }
        if (!IsBlankReference(shot.blendAssetId) &&
            cameraBlendAssetIds.find(shot.blendAssetId) == cameraBlendAssetIds.end()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing camera blend asset: " + shot.blendAssetId, shot.startDistance);
        }
        if (shot.weightScale < 0.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Camera shot weight scale should be non-negative.", shot.startDistance);
        }
    }

    std::unordered_set<std::string> terrainMaterialIds;
    for (size_t index = 0; index < course.terrainMaterialPresets.size(); ++index) {
        const CourseTerrainMaterialPreset& preset = course.terrainMaterialPresets[index];
        if (!preset.id.empty()) {
            terrainMaterialIds.insert(preset.id);
        }
        const std::string subject = "terrain_material[" + std::to_string(index) + "]";
        if (preset.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Terrain material preset id is empty.", preset.distance);
        }
        if (preset.distance < 0.0f || (railLength > 0.0f && preset.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Terrain material preset is outside rail length.", preset.distance);
        }
        if (preset.brightness <= 0.0f) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Terrain material brightness should be positive.", preset.distance);
        }
    }

    for (size_t index = 0; index < course.cinematicShotSets.size(); ++index) {
        const CourseCinematicShotSet& shotSet = course.cinematicShotSets[index];
        const std::string subject = "cinematic_shot_set[" + std::to_string(index) + "]";
        if (shotSet.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Cinematic shot set id is empty.", shotSet.startDistance);
        }
        if (shotSet.label.empty()) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Cinematic shot set label is empty.", shotSet.startDistance);
        }
        if (shotSet.endDistance <= shotSet.startDistance) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Cinematic shot set range is invalid.", shotSet.startDistance);
        }
        if (shotSet.startDistance < 0.0f || (railLength > 0.0f && shotSet.endDistance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Cinematic shot set extends outside rail length.", shotSet.startDistance);
        }
        if (shotSet.heroLandmarkIds.empty() && shotSet.vistaLandmarkIds.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Cinematic shot set has no landmark references.", shotSet.startDistance);
        }
        for (const std::string& id : shotSet.heroLandmarkIds) {
            if (!IsBlankReference(id) && terrainPlacementIds.find(id) == terrainPlacementIds.end()) {
                AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing hero landmark terrain placement: " + id, shotSet.startDistance);
            }
        }
        for (const std::string& id : shotSet.vistaLandmarkIds) {
            if (!IsBlankReference(id) && terrainPlacementIds.find(id) == terrainPlacementIds.end()) {
                AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing vista landmark terrain placement: " + id, shotSet.startDistance);
            }
        }
        if (!IsBlankReference(shotSet.lightingPresetId) &&
            lightingPresetIds.find(shotSet.lightingPresetId) == lightingPresetIds.end()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing lighting preset: " + shotSet.lightingPresetId, shotSet.startDistance);
        }
        if (!IsBlankReference(shotSet.cameraShotId) &&
            cameraShotIds.find(shotSet.cameraShotId) == cameraShotIds.end()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing camera shot: " + shotSet.cameraShotId, shotSet.startDistance);
        }
        if (!IsBlankReference(shotSet.terrainMaterialId) &&
            terrainMaterialIds.find(shotSet.terrainMaterialId) == terrainMaterialIds.end()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Missing terrain material preset: " + shotSet.terrainMaterialId, shotSet.startDistance);
        }
    }

    std::unordered_set<std::string> eventKeys;
    for (size_t index = 0; index < course.events.size(); ++index) {
        const CourseEventMarker& event = course.events[index];
        const std::string subject = "event[" + std::to_string(index) + "]";
        if (event.type.empty()) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Event type is empty.", event.distance);
        } else if (!IsKnownEventType(event.type)) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Unknown event type: " + event.type, event.distance);
        }
        if (event.id.empty()) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Event id is empty.", event.distance);
        }
        if (event.distance < 0.0f || (railLength > 0.0f && event.distance > railLength + 0.01f)) {
            AddIssue(report, CourseValidationSeverity::Error, subject, "Event is outside rail length.", event.distance);
        }
        if (index > 0 && event.distance < course.events[index - 1].distance) {
            AddIssue(report, CourseValidationSeverity::Info, subject, "Event will be sorted on save.", event.distance);
        }

        std::ostringstream key;
        key << event.type << '|' << event.id << '|' << static_cast<int>(event.distance * 10.0f);
        if (!eventKeys.insert(key.str()).second) {
            AddIssue(report, CourseValidationSeverity::Warning, subject, "Duplicate event at nearly the same distance.", event.distance);
        }

        if (event.type == "enemy_wave" && !event.id.empty()) {
            const std::string path = JoinPath(options.resourceRoot, "waves", event.id, ".wave");
            EnemyWaveAsset wave;
            std::string error;
            if (!wave.LoadFromFile(path, &error)) {
                AddIssue(report, CourseValidationSeverity::Error, subject, error, event.distance);
            } else {
                for (size_t unitIndex = 0; unitIndex < wave.units.size(); ++unitIndex) {
                    const EnemyWaveUnit& unit = wave.units[unitIndex];
                    const std::string unitSubject = event.id + ".unit[" + std::to_string(unitIndex) + "]";
                    const std::string actorId = unit.actorAssetId.empty()
                        ? DefaultActorAssetForRole(unit.role)
                        : unit.actorAssetId;
                    if (!actorId.empty() && !FileExists(JoinPath(options.resourceRoot, "actors", actorId, ".actor"))) {
                        AddIssue(report, CourseValidationSeverity::Error, unitSubject, "Missing actor asset: " + actorId, event.distance);
                    }
                    if (!unit.bulletPatternId.empty() &&
                        !FileExists(JoinPath(options.resourceRoot, "bullet_patterns", unit.bulletPatternId, ".pattern"))) {
                        AddIssue(report, CourseValidationSeverity::Error, unitSubject, "Missing bullet pattern asset: " + unit.bulletPatternId, event.distance);
                    }
                }
            }
        } else if (event.type == "obstacle" && !event.id.empty()) {
            const std::string path = JoinPath(options.resourceRoot, "obstacles", event.id, ".obstacle");
            ObstacleAsset obstacle;
            std::string error;
            if (!obstacle.LoadFromFile(path, &error)) {
                AddIssue(report, CourseValidationSeverity::Error, subject, error, event.distance);
            }
        } else if (event.type == "boss") {
            if (!FileExists(JoinPath(options.resourceRoot, "actors", "gatekeeper_boss", ".actor"))) {
                AddIssue(report, CourseValidationSeverity::Error, subject, "Missing default boss actor asset: gatekeeper_boss", event.distance);
            }
        }
    }

    for (size_t first = 0; first < course.events.size(); ++first) {
        size_t count = 1;
        for (size_t second = first + 1; second < course.events.size(); ++second) {
            if (std::abs(course.events[second].distance - course.events[first].distance) > options.denseEventWindow) {
                break;
            }
            ++count;
        }
        if (count > options.denseEventWarningCount) {
            AddIssue(
                report,
                CourseValidationSeverity::Warning,
                "event-density",
                "Too many course events in a short distance window.",
                course.events[first].distance);
            break;
        }
    }

    if (report.issues.empty()) {
        AddIssue(report, CourseValidationSeverity::Info, "course", "Validation passed.");
    }
    return report;
}
