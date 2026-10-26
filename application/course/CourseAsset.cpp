#include "CourseAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
constexpr float kPi = 3.14159265358979323846f;

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> SplitPipe(const std::string& line) {
    std::vector<std::string> parts;
    std::string part;
    std::stringstream stream(line);
    while (std::getline(stream, part, '|')) {
        parts.push_back(Trim(part));
    }
    return parts;
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string part;
    std::stringstream stream(text);
    while (std::getline(stream, part, ',')) {
        part = Trim(part);
        if (!part.empty() && part != "-") {
            parts.push_back(std::move(part));
        }
    }
    return parts;
}

std::vector<std::string> SplitChar(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string part;
    std::stringstream stream(text);
    while (std::getline(stream, part, delimiter)) {
        parts.push_back(Trim(part));
    }
    return parts;
}

std::string JoinList(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "-";
    }
    std::ostringstream stream;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << values[index];
    }
    return stream.str();
}

bool ParseFloat(const std::string& text, float& out) {
    char* end = nullptr;
    out = std::strtof(text.c_str(), &end);
    return end != text.c_str();
}

float ParseFloatOr(const std::vector<std::string>& parts, size_t index, float fallback) {
    if (index >= parts.size()) {
        return fallback;
    }
    float value = fallback;
    return ParseFloat(parts[index], value) ? value : fallback;
}

float DegreesToRadians(float degrees) {
    return degrees * kPi / 180.0f;
}

float RadiansToDegrees(float radians) {
    return radians * 180.0f / kPi;
}

bool IsBlankReference(const std::string& value) {
    return value.empty() || value == "-";
}

std::vector<CourseRockCluster::InstanceTransformOverride> ParseRockInstanceOverrides(
    const std::string& text) {
    std::vector<CourseRockCluster::InstanceTransformOverride> overrides;
    const std::string trimmed = Trim(text);
    if (trimmed.empty() || trimmed == "-") {
        return overrides;
    }

    for (const std::string& entry : SplitChar(trimmed, ';')) {
        if (entry.empty() || entry == "-") {
            continue;
        }

        const size_t colon = entry.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        float indexValue = 0.0f;
        if (!ParseFloat(Trim(entry.substr(0, colon)), indexValue)) {
            continue;
        }

        const std::vector<std::string> values = SplitChar(entry.substr(colon + 1), ',');
        if (values.size() < 9) {
            continue;
        }

        CourseRockCluster::InstanceTransformOverride transformOverride{};
        transformOverride.index = static_cast<uint32_t>((std::max)(0.0f, indexValue));
        transformOverride.localOffset.x = ParseFloatOr(values, 0, 0.0f);
        transformOverride.localOffset.y = ParseFloatOr(values, 1, 0.0f);
        transformOverride.localOffset.z = ParseFloatOr(values, 2, 0.0f);
        transformOverride.scale.x = ParseFloatOr(values, 3, 1.0f);
        transformOverride.scale.y = ParseFloatOr(values, 4, 1.0f);
        transformOverride.scale.z = ParseFloatOr(values, 5, 1.0f);
        transformOverride.rotation.x = DegreesToRadians(ParseFloatOr(values, 6, 0.0f));
        transformOverride.rotation.y = DegreesToRadians(ParseFloatOr(values, 7, 0.0f));
        transformOverride.rotation.z = DegreesToRadians(ParseFloatOr(values, 8, 0.0f));
        overrides.push_back(transformOverride);
    }
    return overrides;
}

std::string JoinRockInstanceOverrides(
    const std::vector<CourseRockCluster::InstanceTransformOverride>& overrides) {
    if (overrides.empty()) {
        return "-";
    }

    std::ostringstream stream;
    for (size_t index = 0; index < overrides.size(); ++index) {
        const CourseRockCluster::InstanceTransformOverride& transformOverride = overrides[index];
        if (index > 0) {
            stream << ';';
        }
        stream << transformOverride.index << ':'
               << transformOverride.localOffset.x << ','
               << transformOverride.localOffset.y << ','
               << transformOverride.localOffset.z << ','
               << transformOverride.scale.x << ','
               << transformOverride.scale.y << ','
               << transformOverride.scale.z << ','
               << RadiansToDegrees(transformOverride.rotation.x) << ','
               << RadiansToDegrees(transformOverride.rotation.y) << ','
               << RadiansToDegrees(transformOverride.rotation.z);
    }
    return stream.str();
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStep(float t) {
    const float x = (std::clamp)(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float ApplyBlendCurve(float t, const std::string& curve) {
    const float x = (std::clamp)(t, 0.0f, 1.0f);
    if (curve == "linear") {
        return x;
    }
    if (curve == "ease_in") {
        return x * x;
    }
    if (curve == "ease_out") {
        return 1.0f - (1.0f - x) * (1.0f - x);
    }
    if (curve == "cinematic_hold") {
        return SmoothStep((std::clamp)((x - 0.08f) / 0.84f, 0.0f, 1.0f));
    }
    return SmoothStep(x);
}

Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t) {
    return {
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t),
    };
}

Vector4 LerpVector4(const Vector4& a, const Vector4& b, float t) {
    return {
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t),
        Lerp(a.w, b.w, t),
    };
}

CourseCameraKey LerpCamera(const CourseCameraKey& a, const CourseCameraKey& b, float t) {
    CourseCameraKey result{};
    result.distance = Lerp(a.distance, b.distance, t);
    result.backDistance = Lerp(a.backDistance, b.backDistance, t);
    result.verticalOffset = Lerp(a.verticalOffset, b.verticalOffset, t);
    result.lateralOffset = Lerp(a.lateralOffset, b.lateralOffset, t);
    result.lookAheadDistance = Lerp(a.lookAheadDistance, b.lookAheadDistance, t);
    result.lookUpOffset = Lerp(a.lookUpOffset, b.lookUpOffset, t);
    result.lookForwardOffset = Lerp(a.lookForwardOffset, b.lookForwardOffset, t);
    result.fovY = Lerp(a.fovY, b.fovY, t);
    result.roll = Lerp(a.roll, b.roll, t);
    return result;
}

CourseLightingPreset LerpLighting(
    const CourseLightingPreset& a,
    const CourseLightingPreset& b,
    float t) {
    CourseLightingPreset result = t < 0.5f ? a : b;
    result.distance = Lerp(a.distance, b.distance, t);
    result.blendDistance = Lerp(a.blendDistance, b.blendDistance, t);
    result.sunColor = LerpVector4(a.sunColor, b.sunColor, t);
    result.sunDirection = LerpVector3(a.sunDirection, b.sunDirection, t);
    result.sunIntensity = Lerp(a.sunIntensity, b.sunIntensity, t);
    result.clearColor = LerpVector4(a.clearColor, b.clearColor, t);
    result.fogColor = LerpVector3(a.fogColor, b.fogColor, t);
    result.fogIntensity = Lerp(a.fogIntensity, b.fogIntensity, t);
    result.fogStart = Lerp(a.fogStart, b.fogStart, t);
    result.fogEnd = Lerp(a.fogEnd, b.fogEnd, t);
    result.fogDensity = Lerp(a.fogDensity, b.fogDensity, t);
    result.backlitFogLift = Lerp(a.backlitFogLift, b.backlitFogLift, t);
    result.openingGlowStrength = Lerp(a.openingGlowStrength, b.openingGlowStrength, t);
    result.foregroundSilhouetteStrength = Lerp(a.foregroundSilhouetteStrength, b.foregroundSilhouetteStrength, t);
    result.lowFogLayerStrength = Lerp(a.lowFogLayerStrength, b.lowFogLayerStrength, t);
    result.coolFloorHazeStrength = Lerp(a.coolFloorHazeStrength, b.coolFloorHazeStrength, t);
    return result;
}

CourseTerrainMaterialPreset LerpTerrainMaterial(
    const CourseTerrainMaterialPreset& a,
    const CourseTerrainMaterialPreset& b,
    float t) {
    CourseTerrainMaterialPreset result = t < 0.5f ? a : b;
    result.distance = Lerp(a.distance, b.distance, t);
    result.blendDistance = Lerp(a.blendDistance, b.blendDistance, t);
    result.baseColor = LerpVector4(a.baseColor, b.baseColor, t);
    result.brightness = Lerp(a.brightness, b.brightness, t);
    result.noiseStrength = Lerp(a.noiseStrength, b.noiseStrength, t);
    result.strataStrength = Lerp(a.strataStrength, b.strataStrength, t);
    result.strataBreakupStrength = Lerp(a.strataBreakupStrength, b.strataBreakupStrength, t);
    result.specularStrength = Lerp(a.specularStrength, b.specularStrength, t);
    result.rimLightStrength = Lerp(a.rimLightStrength, b.rimLightStrength, t);
    result.backlightRimBoost = Lerp(a.backlightRimBoost, b.backlightRimBoost, t);
    result.floorSandShadowStrength = Lerp(a.floorSandShadowStrength, b.floorSandShadowStrength, t);
    result.detailNormalStrength = Lerp(a.detailNormalStrength, b.detailNormalStrength, t);
    result.microDetailStrength = Lerp(a.microDetailStrength, b.microDetailStrength, t);
    result.cavityAoStrength = Lerp(a.cavityAoStrength, b.cavityAoStrength, t);
    result.skyFillStrength = Lerp(a.skyFillStrength, b.skyFillStrength, t);
    return result;
}

const CourseCameraShotPreset* FindCameraShotPreset(const CourseAsset& asset, const std::string& id) {
    if (IsBlankReference(id)) {
        return nullptr;
    }
    for (const CourseCameraShotPreset& preset : asset.cameraShotPresets) {
        if (preset.id == id) {
            return &preset;
        }
    }
    return nullptr;
}

const CourseCameraBlendAsset* FindCameraBlendAsset(const CourseAsset& asset, const std::string& id) {
    if (IsBlankReference(id)) {
        return nullptr;
    }
    for (const CourseCameraBlendAsset& blend : asset.cameraBlendAssets) {
        if (blend.id == id) {
            return &blend;
        }
    }
    return nullptr;
}

CourseCinematicCameraShot ResolveCameraShot(const CourseAsset& asset, const CourseCinematicCameraShot& shot) {
    CourseCinematicCameraShot resolved = shot;
    if (const CourseCameraShotPreset* preset = FindCameraShotPreset(asset, shot.presetId)) {
        if (IsBlankReference(resolved.mode)) {
            resolved.mode = preset->mode;
        }
        resolved.backDistanceOffset += preset->backDistanceOffset;
        resolved.verticalOffset += preset->verticalOffset;
        resolved.lateralOffset += preset->lateralOffset;
        resolved.lookAheadOffset += preset->lookAheadOffset;
        resolved.lookUpOffset += preset->lookUpOffset;
        resolved.lookForwardOffset += preset->lookForwardOffset;
        resolved.fovOffset += preset->fovOffset;
        resolved.rollOffset += preset->rollOffset;
        resolved.shakeAmount = (std::max)(resolved.shakeAmount, preset->shakeAmount);
    }
    if (const CourseCameraBlendAsset* blend = FindCameraBlendAsset(asset, shot.blendAssetId)) {
        resolved.blendInDistance = blend->blendInDistance;
        resolved.blendOutDistance = blend->blendOutDistance;
        resolved.weightScale *= blend->weightScale;
    }
    return resolved;
}

float EvaluateShotWeight(
    const CourseCinematicCameraShot& shot,
    const CourseCameraBlendAsset* blendAsset,
    float distance) {
    if (shot.endDistance <= shot.startDistance ||
        distance < shot.startDistance ||
        distance > shot.endDistance) {
        return 0.0f;
    }

    float weight = 1.0f;
    if (shot.blendInDistance > 0.001f) {
        weight = (std::min)(
            weight,
            (std::clamp)((distance - shot.startDistance) / shot.blendInDistance, 0.0f, 1.0f));
    }
    if (shot.blendOutDistance > 0.001f) {
        weight = (std::min)(
            weight,
            (std::clamp)((shot.endDistance - distance) / shot.blendOutDistance, 0.0f, 1.0f));
    }
    const std::string curve = blendAsset != nullptr ? blendAsset->curve : std::string{"linear"};
    const float scale = shot.weightScale;
    return (std::clamp)(ApplyBlendCurve(weight, curve) * scale, 0.0f, 2.0f);
}

void SortCourseData(CourseAsset& asset) {
    std::sort(
        asset.cameraKeys.begin(),
        asset.cameraKeys.end(),
        [](const CourseCameraKey& a, const CourseCameraKey& b) {
            return a.distance < b.distance;
        });
    std::sort(
        asset.sections.begin(),
        asset.sections.end(),
        [](const CourseSection& a, const CourseSection& b) {
            return a.startDistance < b.startDistance;
        });
    std::sort(
        asset.rideProfiles.begin(),
        asset.rideProfiles.end(),
        [](const CourseRideProfileDefinition& a, const CourseRideProfileDefinition& b) {
            if (a.startDistance != b.startDistance) return a.startDistance < b.startDistance;
            return a.editorGuid < b.editorGuid;
        });
    std::sort(
        asset.rideSpeedBeats.begin(),
        asset.rideSpeedBeats.end(),
        [](const RailRideSpeedBeatDefinition& a, const RailRideSpeedBeatDefinition& b) {
            if (a.startDistance != b.startDistance) return a.startDistance < b.startDistance;
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.editorGuid < b.editorGuid;
        });
    std::sort(
        asset.railRideEvents.begin(),
        asset.railRideEvents.end(),
        [](const CourseRailRideEventDefinition& a,
           const CourseRailRideEventDefinition& b) {
            if (a.startDistance != b.startDistance)
                return a.startDistance < b.startDistance;
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.editorGuid < b.editorGuid;
        });
    std::sort(
        asset.events.begin(),
        asset.events.end(),
        [](const CourseEventMarker& a, const CourseEventMarker& b) {
            return a.distance < b.distance;
        });
    std::sort(
        asset.waveDefinitions.begin(),
        asset.waveDefinitions.end(),
        [](const CourseWaveDefinition& a, const CourseWaveDefinition& b) {
            if (a.triggerRailDistance != b.triggerRailDistance) {
                return a.triggerRailDistance < b.triggerRailDistance;
            }
            return a.editorGuid < b.editorGuid;
        });
    std::sort(
        asset.encounterBeats.begin(),
        asset.encounterBeats.end(),
        [](const EnemyEncounterBeatDefinition& a,
           const EnemyEncounterBeatDefinition& b) {
            if (a.triggerRailDistance != b.triggerRailDistance) {
                return a.triggerRailDistance < b.triggerRailDistance;
            }
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.editorGuid < b.editorGuid;
        });
    std::sort(
        asset.terrainPlacements.begin(),
        asset.terrainPlacements.end(),
        [](const CourseTerrainPlacement& a, const CourseTerrainPlacement& b) {
            if (a.distance != b.distance) {
                return a.distance < b.distance;
            }
            return a.renderPriority < b.renderPriority;
        });
    std::sort(
        asset.rockClusters.begin(),
        asset.rockClusters.end(),
        [](const CourseRockCluster& a, const CourseRockCluster& b) {
            if (a.distance != b.distance) {
                return a.distance < b.distance;
            }
            return a.id < b.id;
        });
    std::sort(
        asset.lightingPresets.begin(),
        asset.lightingPresets.end(),
        [](const CourseLightingPreset& a, const CourseLightingPreset& b) {
            return a.distance < b.distance;
        });
    std::sort(
        asset.cameraShotPresets.begin(),
        asset.cameraShotPresets.end(),
        [](const CourseCameraShotPreset& a, const CourseCameraShotPreset& b) {
            return a.id < b.id;
        });
    std::sort(
        asset.cameraBlendAssets.begin(),
        asset.cameraBlendAssets.end(),
        [](const CourseCameraBlendAsset& a, const CourseCameraBlendAsset& b) {
            return a.id < b.id;
        });
    std::sort(
        asset.cinematicCameraShots.begin(),
        asset.cinematicCameraShots.end(),
        [](const CourseCinematicCameraShot& a, const CourseCinematicCameraShot& b) {
            if (a.startDistance != b.startDistance) {
                return a.startDistance < b.startDistance;
            }
            return a.endDistance < b.endDistance;
        });
    std::sort(
        asset.terrainMaterialPresets.begin(),
        asset.terrainMaterialPresets.end(),
        [](const CourseTerrainMaterialPreset& a, const CourseTerrainMaterialPreset& b) {
            return a.distance < b.distance;
        });
    std::sort(
        asset.cinematicShotSets.begin(),
        asset.cinematicShotSets.end(),
        [](const CourseCinematicShotSet& a, const CourseCinematicShotSet& b) {
            if (a.startDistance != b.startDistance) {
                return a.startDistance < b.startDistance;
            }
            return a.endDistance < b.endDistance;
        });
}
} // namespace

const char* ToCourseCameraViewModeString(CourseCameraViewMode mode) {
    switch (mode) {
    case CourseCameraViewMode::Inherit: return "Follow / Existing Shot";
    case CourseCameraViewMode::FirstPerson: return "First Person";
    case CourseCameraViewMode::Rear: return "Rear View";
    case CourseCameraViewMode::CloseUp: return "Close Up";
    case CourseCameraViewMode::Custom: return "Custom Composition";
    }
    return "Invalid";
}

CourseCameraViewDefinition CourseCameraViewDefinition::Preset(CourseCameraViewMode mode) {
    CourseCameraViewDefinition view;
    view.mode = mode;
    if (mode == CourseCameraViewMode::FirstPerson) view.hideVehicle = true;
    if (mode == CourseCameraViewMode::Rear) {
        view.positionOffset = {0, 7, 14};
        view.yaw = kPi;
    }
    if (mode == CourseCameraViewMode::CloseUp || mode == CourseCameraViewMode::Custom) {
        view.positionOffset = {0, 6, -7};
        view.fovY = DegreesToRadians(50);
    }
    return view;
}

bool CourseCameraViewDefinition::Validate(std::string* errorMessage) const {
    const bool valid = mode >= CourseCameraViewMode::Inherit && mode <= CourseCameraViewMode::Custom &&
        std::isfinite(positionOffset.x) && std::abs(positionOffset.x) <= 1000 &&
        std::isfinite(positionOffset.y) && std::abs(positionOffset.y) <= 1000 &&
        std::isfinite(positionOffset.z) && std::abs(positionOffset.z) <= 1000 &&
        std::isfinite(yaw) && std::abs(yaw) <= kPi * 2.01f &&
        std::isfinite(pitch) && std::abs(pitch) <= DegreesToRadians(85) &&
        std::isfinite(fovY) && fovY >= DegreesToRadians(15) && fovY <= DegreesToRadians(110) &&
        priority >= -1000 && priority <= 1000;
    if (errorMessage) *errorMessage = valid ? "" : "Invalid camera view pose, angle, FOV or priority.";
    return valid;
}

const char* ToCourseTerrainLayerString(CourseTerrainLayer layer) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision:
        return "gameplay_collision";
    case CourseTerrainLayer::HeroLandmark:
        return "hero_landmark";
    case CourseTerrainLayer::VistaBackground:
        return "vista_background";
    }
    return "hero_landmark";
}

const char* ToCourseTerrainCollisionModeString(CourseTerrainCollisionMode mode) {
    switch (mode) {
    case CourseTerrainCollisionMode::None:
        return "none";
    case CourseTerrainCollisionMode::Proxy:
        return "proxy";
    case CourseTerrainCollisionMode::Solid:
        return "solid";
    }
    return "none";
}

const char* ToCourseRockClusterTypeString(CourseRockClusterType type) {
    switch (type) {
    case CourseRockClusterType::AttachedDebris:
        return "attached_debris";
    case CourseRockClusterType::HeroFracture:
        return "hero_fracture";
    case CourseRockClusterType::FallingDebris:
        return "falling_debris";
    case CourseRockClusterType::VistaSilhouette:
        return "vista_silhouette";
    }
    return "attached_debris";
}

const char* ToCourseRockClusterAnchorString(CourseRockClusterAnchor anchor) {
    switch (anchor) {
    case CourseRockClusterAnchor::LeftWall:
        return "left_wall";
    case CourseRockClusterAnchor::RightWall:
        return "right_wall";
    case CourseRockClusterAnchor::Floor:
        return "floor";
    case CourseRockClusterAnchor::CeilingBreak:
        return "ceiling_break";
    case CourseRockClusterAnchor::VistaWall:
        return "vista_wall";
    }
    return "left_wall";
}

CourseTerrainLayer ParseCourseTerrainLayer(const std::string& text) {
    if (text == "gameplay_collision" || text == "gameplay" || text == "collision") {
        return CourseTerrainLayer::GameplayCollision;
    }
    if (text == "vista_background" || text == "vista" || text == "background") {
        return CourseTerrainLayer::VistaBackground;
    }
    return CourseTerrainLayer::HeroLandmark;
}

CourseTerrainCollisionMode ParseCourseTerrainCollisionMode(const std::string& text) {
    if (text == "solid") {
        return CourseTerrainCollisionMode::Solid;
    }
    if (text == "proxy") {
        return CourseTerrainCollisionMode::Proxy;
    }
    return CourseTerrainCollisionMode::None;
}

CourseRockClusterType ParseCourseRockClusterType(const std::string& text) {
    if (text == "hero_fracture" || text == "hero" || text == "fracture") {
        return CourseRockClusterType::HeroFracture;
    }
    if (text == "falling_debris" || text == "falling" || text == "dynamic") {
        return CourseRockClusterType::FallingDebris;
    }
    if (text == "vista_silhouette" || text == "vista" || text == "background") {
        return CourseRockClusterType::VistaSilhouette;
    }
    return CourseRockClusterType::AttachedDebris;
}

CourseRockClusterAnchor ParseCourseRockClusterAnchor(const std::string& text) {
    if (text == "right_wall" || text == "right") {
        return CourseRockClusterAnchor::RightWall;
    }
    if (text == "floor" || text == "ground") {
        return CourseRockClusterAnchor::Floor;
    }
    if (text == "ceiling_break" || text == "ceiling" || text == "break") {
        return CourseRockClusterAnchor::CeilingBreak;
    }
    if (text == "vista_wall" || text == "vista" || text == "far_wall") {
        return CourseRockClusterAnchor::VistaWall;
    }
    return CourseRockClusterAnchor::LeftWall;
}

const char* ToCourseWaveCompletionConditionString(
    CourseWaveCompletionCondition condition) {
    switch (condition) {
    case CourseWaveCompletionCondition::AllEnemiesDefeated:
        return "all_enemies_defeated";
    case CourseWaveCompletionCondition::Timeout:
        return "timeout";
    case CourseWaveCompletionCondition::ReachRailDistance:
        return "reach_rail_distance";
    case CourseWaveCompletionCondition::ScriptedEvent:
        return "scripted_event";
    }
    return "all_enemies_defeated";
}

const char* ToCourseWaveExecutionPolicyString(CourseWaveExecutionPolicy policy) {
    switch (policy) {
    case CourseWaveExecutionPolicy::Parallel: return "parallel";
    case CourseWaveExecutionPolicy::Sequential: return "sequential";
    case CourseWaveExecutionPolicy::Exclusive: return "exclusive";
    }
    return "parallel";
}

CourseWaveCompletionCondition ParseCourseWaveCompletionCondition(
    const std::string& text) {
    if (text == "timeout" || text == "timed") {
        return CourseWaveCompletionCondition::Timeout;
    }
    if (text == "reach_rail_distance" || text == "reach_distance") {
        return CourseWaveCompletionCondition::ReachRailDistance;
    }
    if (text == "scripted_event" || text == "event") {
        return CourseWaveCompletionCondition::ScriptedEvent;
    }
    return CourseWaveCompletionCondition::AllEnemiesDefeated;
}

CourseWaveExecutionPolicy ParseCourseWaveExecutionPolicy(
    const std::string& text) {
    if (text == "sequential" || text == "sequence") {
        return CourseWaveExecutionPolicy::Sequential;
    }
    if (text == "exclusive") return CourseWaveExecutionPolicy::Exclusive;
    return CourseWaveExecutionPolicy::Parallel;
}

bool CourseAsset::LoadFromFile(const std::string& path, std::string* errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not open course file: " + path;
        }
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    if (!file.good() && !file.eof()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not read course file: " + path;
        }
        return false;
    }
    return LoadFromString(contents.str(), errorMessage);
}

bool CourseAsset::LoadFromString(const std::string& text, std::string* errorMessage) {
    std::istringstream file(text);

    CourseAsset loaded{};
    std::vector<TerrainBrushStamp> loadedTerrainStamps;
    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::vector<std::string> parts = SplitPipe(line);
        if (parts.empty()) {
            continue;
        }

        const std::string& kind = parts[0];
        if (kind == "course") {
            if (parts.size() >= 2 && !parts[1].empty()) {
                loaded.name = parts[1];
            }
        } else if (kind == "rail_track") {
            if (parts.size() != 2 || parts[1].empty()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rail_track row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            loaded.railTrackAssetId = parts[1];
        } else if (kind == "rail") {
            if (parts.size() < 6) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rail row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            RailPathControlPoint point{};
            point.position.x = ParseFloatOr(parts, 1, 0.0f);
            point.position.y = ParseFloatOr(parts, 2, 0.0f);
            point.position.z = ParseFloatOr(parts, 3, 0.0f);
            point.corridorRadius = ParseFloatOr(parts, 4, 18.0f);
            point.speed = ParseFloatOr(parts, 5, 32.0f);
            point.editorGuid = parts.size() >= 7 ? parts[6] : std::string{};
            if (parts.size() >= 14) {
                const int mode = static_cast<int>(ParseFloatOr(parts, 7, 0.0f));
                point.tangentMode = mode == 1 ? RailPathTangentMode::Mirrored
                    : mode == 2 ? RailPathTangentMode::Broken
                    : RailPathTangentMode::Auto;
                point.incomingTangent = {
                    ParseFloatOr(parts, 8, 0.0f), ParseFloatOr(parts, 9, 0.0f),
                    ParseFloatOr(parts, 10, 0.0f)};
                point.outgoingTangent = {
                    ParseFloatOr(parts, 11, 0.0f), ParseFloatOr(parts, 12, 0.0f),
                    ParseFloatOr(parts, 13, 0.0f)};
            }
            loaded.railPoints.push_back(point);
        } else if (kind == "rail_anchor") {
            if (parts.size() < 7) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rail_anchor row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseRailAnchorBinding binding{};
            binding.ownerGuid = parts[1];
            binding.anchor.segmentGuid = parts[2];
            binding.anchor.normalizedT = ParseFloatOr(parts, 3, 0.0f);
            binding.anchor.lateralOffset = ParseFloatOr(parts, 4, 0.0f);
            binding.anchor.verticalOffset = ParseFloatOr(parts, 5, 0.0f);
            binding.anchor.forwardOffset = ParseFloatOr(parts, 6, 0.0f);
            loaded.railAnchors.push_back(std::move(binding));
        } else if (kind == "camera") {
            if (parts.size() < 10) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid camera row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseCameraKey key{};
            key.distance = ParseFloatOr(parts, 1, 0.0f);
            key.backDistance = ParseFloatOr(parts, 2, key.backDistance);
            key.verticalOffset = ParseFloatOr(parts, 3, key.verticalOffset);
            key.lateralOffset = ParseFloatOr(parts, 4, key.lateralOffset);
            key.lookAheadDistance = ParseFloatOr(parts, 5, key.lookAheadDistance);
            key.lookUpOffset = ParseFloatOr(parts, 6, key.lookUpOffset);
            key.lookForwardOffset = ParseFloatOr(parts, 7, key.lookForwardOffset);
            key.fovY = DegreesToRadians(ParseFloatOr(parts, 8, key.fovY * 180.0f / kPi));
            key.roll = DegreesToRadians(ParseFloatOr(parts, 9, 0.0f));
            key.editorGuid = parts.size() >= 11 ? parts[10] : std::string{};
            key.editorVisible = parts.size() < 12 || parts[11] != "0";
            key.editorLocked = parts.size() >= 13 && parts[12] == "1";
            loaded.cameraKeys.push_back(key);
        } else if (kind == "section") {
            if (parts.size() < 5) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid section row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseSection section{};
            section.startDistance = ParseFloatOr(parts, 1, 0.0f);
            section.endDistance = ParseFloatOr(parts, 2, section.startDistance);
            section.name = parts[3];
            section.category = parts[4];
            loaded.sections.push_back(section);
        } else if (kind == "ride_profile") {
            if (parts.size() < 17) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid ride_profile row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            CourseRideProfileDefinition profile{};
            profile.editorGuid = parts[1];
            profile.startDistance = ParseFloatOr(parts, 2, 0.0f);
            profile.endDistance = ParseFloatOr(parts, 3, profile.startDistance);
            profile.displayName = parts[4];
            profile.speedMode = ParseCourseRideSpeedMode(parts[5]);
            profile.speedMultiplier = ParseFloatOr(parts, 6, 1.0f);
            profile.targetSpeedOverride = ParseFloatOr(parts, 7, -1.0f);
            // Schema v8 rows end at index 16. Schema v9 appends motion
            // envelope fields before the presentation fields.
            const bool hasMotionEnvelope = parts.size() >= 22;
            std::size_t index = 8;
            if (hasMotionEnvelope) {
                profile.accelerationScale = ParseFloatOr(parts, index++, 1.0f);
                profile.brakingScale = ParseFloatOr(parts, index++, 1.0f);
                profile.maximumJerk = ParseFloatOr(parts, index++, 120.0f);
                profile.cornerEntryLookAheadDistance = ParseFloatOr(parts, index++, 48.0f);
                profile.cornerSpeedScale = ParseFloatOr(parts, index++, 1.0f);
            }
            profile.turnAnticipationDistance = ParseFloatOr(parts, index++, 24.0f);
            profile.visualBankScale = ParseFloatOr(parts, index++, 1.0f);
            profile.maximumVisualBankDegrees = ParseFloatOr(parts, index++, 18.0f);
            profile.blendInDistance = ParseFloatOr(parts, index++, 16.0f);
            profile.blendOutDistance = ParseFloatOr(parts, index++, 16.0f);
            profile.cameraShotId = parts[index] == "-" ? std::string{} : parts[index];
            ++index;
            profile.enabled = parts[index++] != "0";
            profile.editorVisible = parts[index++] != "0";
            profile.editorLocked = parts[index] == "1";
            std::string profileError;
            if (!profile.Validate(&profileError)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid ride_profile at line " +
                        std::to_string(lineNumber) + ": " + profileError;
                }
                return false;
            }
            loaded.rideProfiles.push_back(std::move(profile));
        } else if (kind == "ride_speed_beat") {
            if (parts.size() < 17) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid ride_speed_beat row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            RailRideSpeedBeatDefinition beat{};
            beat.editorGuid = parts[1];
            beat.startDistance = ParseFloatOr(parts, 2, 0.0f);
            beat.endDistance = ParseFloatOr(parts, 3, beat.startDistance);
            beat.displayName = parts[4];
            beat.type = ParseRailRideSpeedBeatType(parts[5]);
            beat.speedMultiplier = ParseFloatOr(parts, 6, 1.0f);
            beat.targetSpeedOverride = ParseFloatOr(parts, 7, -1.0f);
            beat.accelerationScale = ParseFloatOr(parts, 8, 1.0f);
            beat.brakingScale = ParseFloatOr(parts, 9, 1.0f);
            beat.maximumJerk = ParseFloatOr(parts, 10, 120.0f);
            beat.blendInDistance = ParseFloatOr(parts, 11, 8.0f);
            beat.blendOutDistance = ParseFloatOr(parts, 12, 8.0f);
            beat.priority = static_cast<int>(ParseFloatOr(parts, 13, 0.0f));
            beat.enabled = parts[14] != "0";
            beat.editorVisible = parts[15] != "0";
            beat.editorLocked = parts[16] == "1";
            std::string beatError;
            if (!beat.Validate(&beatError)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid ride_speed_beat at line " +
                        std::to_string(lineNumber) + ": " + beatError;
                }
                return false;
            }
            loaded.rideSpeedBeats.push_back(std::move(beat));
        } else if (kind == "rail_ride_event") {
            if (parts.size() < 26) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rail_ride_event row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            CourseRailRideEventDefinition event{};
            event.editorGuid = parts[1];
            event.startDistance = ParseFloatOr(parts, 2, 0.0f);
            event.endDistance = ParseFloatOr(parts, 3, event.startDistance);
            event.displayName = parts[4];
            event.type = ParseCourseRailRideEventType(parts[5]);
            event.bankMode = ParseCourseRailRideBankMode(parts[6]);
            event.bankDegrees = ParseFloatOr(parts, 7, 0.0f);
            event.rumbleAmplitude = ParseFloatOr(parts, 8, 0.35f);
            event.rumbleFrequencyHz = ParseFloatOr(parts, 9, 12.0f);
            event.suspensionAmplitude = ParseFloatOr(parts, 10, 0.08f);
            event.cameraShake = ParseFloatOr(parts, 11, 0.055f);
            event.cameraFovKick = ParseFloatOr(parts, 12, 0.0f);
            event.cameraRollKickDegrees = ParseFloatOr(parts, 13, 0.0f);
            event.hapticLow = ParseFloatOr(parts, 14, 0.18f);
            event.hapticHigh = ParseFloatOr(parts, 15, 0.12f);
            event.speedInfluence = ParseFloatOr(parts, 16, 0.65f);
            event.blendInDistance = ParseFloatOr(parts, 17, 0.0f);
            event.blendOutDistance = ParseFloatOr(parts, 18, 0.0f);
            event.priority = static_cast<int>(ParseFloatOr(parts, 19, 0.0f));
            event.audioCueId = parts[20] == "-" ? std::string{} : parts[20];
            event.vfxCueId = parts[21] == "-" ? std::string{} : parts[21];
            event.triggerOncePerRun = parts[22] != "0";
            event.enabled = parts[23] != "0";
            event.editorVisible = parts[24] != "0";
            event.editorLocked = parts[25] == "1";
            std::string eventError;
            if (!event.Validate(&eventError)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rail_ride_event at line " +
                        std::to_string(lineNumber) + ": " + eventError;
                }
                return false;
            }
            loaded.railRideEvents.push_back(std::move(event));
        } else if (kind == "event") {
            if (parts.size() < 4) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid event row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseEventMarker event{};
            event.distance = ParseFloatOr(parts, 1, 0.0f);
            event.type = parts[2];
            event.id = parts[3];
            event.payload = parts.size() >= 5 ? parts[4] : std::string{};
            event.editorGuid = parts.size() >= 6 ? parts[5] : std::string{};
            event.editorVisible = parts.size() < 7 || parts[6] != "0";
            event.editorLocked = parts.size() >= 8 && parts[7] == "1";
            loaded.events.push_back(event);
        } else if (kind == "wave") {
            if (parts.size() < 13) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid wave row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            CourseWaveDefinition wave{};
            wave.editorGuid = parts[1];
            wave.displayName = parts[2];
            wave.triggerRailDistance = ParseFloatOr(parts, 3, 0.0f);
            wave.prewarmDistance = ParseFloatOr(parts, 4, 80.0f);
            wave.timeoutSeconds = ParseFloatOr(parts, 5, 30.0f);
            wave.completionCondition = ParseCourseWaveCompletionCondition(parts[6]);
            wave.executionPolicy = ParseCourseWaveExecutionPolicy(parts[7]);
            wave.nextWaveGuid = parts[8] == "-" ? std::string{} : parts[8];
            wave.triggerEventId = parts[9] == "-" ? std::string{} : parts[9];
            wave.enabled = parts[10] != "0";
            wave.editorVisible = parts[11] != "0";
            wave.editorLocked = parts[12] == "1";
            loaded.waveDefinitions.push_back(std::move(wave));
        } else if (kind == "encounter_beat") {
            if (parts.size() < 30) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid encounter_beat row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            EnemyEncounterBeatDefinition beat{};
            beat.editorGuid = parts[1];
            beat.encounterId = parts[2];
            beat.displayName = parts[3];
            beat.waveGuid = parts[4];
            beat.triggerRailDistance = ParseFloatOr(parts, 5, 0.0f);
            beat.endRailDistance = ParseFloatOr(parts, 6,
                beat.triggerRailDistance + 120.0f);
            beat.prewarmDistance = ParseFloatOr(parts, 7, 60.0f);
            beat.establishMinimumSeconds = ParseFloatOr(parts, 8, 0.55f);
            beat.establishMaximumSeconds = ParseFloatOr(parts, 9, 2.0f);
            beat.threatenMinimumSeconds = ParseFloatOr(parts, 10, 0.45f);
            beat.threatenMaximumSeconds = ParseFloatOr(parts, 11, 2.5f);
            beat.attackMinimumSeconds = ParseFloatOr(parts, 12, 0.8f);
            beat.attackMaximumSeconds = ParseFloatOr(parts, 13, 12.0f);
            beat.recoverySeconds = ParseFloatOr(parts, 14, 0.55f);
            beat.resolveTimeoutSeconds = ParseFloatOr(parts, 15, 5.0f);
            beat.requiredReadableRatio = ParseFloatOr(parts, 16, 0.65f);
            beat.maximumConcurrentAttackers = static_cast<uint32_t>(
                (std::max)(0.0f, ParseFloatOr(parts, 17, 2.0f)));
            beat.maximumThreatBudget = ParseFloatOr(parts, 18, 2.6f);
            beat.cameraShotId = parts[19] == "-" ? std::string{} : parts[19];
            beat.cameraWeight = ParseFloatOr(parts, 20, 0.85f);
            beat.cameraFocusWeight = ParseFloatOr(parts, 21, 0.72f);
            beat.cameraFovOffsetDegrees = ParseFloatOr(parts, 22, 2.5f);
            beat.cameraBackDistanceOffset = ParseFloatOr(parts, 23, 1.8f);
            beat.priority = static_cast<int>(ParseFloatOr(parts, 24, 0.0f));
            beat.exitSurvivorsOnResolve = parts[25] != "0";
            beat.requireCombatTruthForCompletion = parts[26] != "0";
            beat.enabled = parts[27] != "0";
            beat.editorVisible = parts[28] != "0";
            beat.editorLocked = parts[29] == "1";
            std::string beatError;
            if (!beat.Validate(&beatError)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid encounter_beat at line " +
                        std::to_string(lineNumber) + ": " + beatError;
                }
                return false;
            }
            loaded.encounterBeats.push_back(std::move(beat));
        } else if (kind == "enemy_placement") {
            if (parts.size() < 20) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid enemy_placement row at line " +
                        std::to_string(lineNumber);
                }
                return false;
            }
            CourseEnemyPlacement placement{};
            placement.editorGuid = parts[1];
            placement.actorAssetId = parts[2];
            placement.bulletPatternOverrideId = parts[3] == "-" ? std::string{} : parts[3];
            placement.waveGroupGuid = parts[4] == "-" ? std::string{} : parts[4];
            placement.railAnchor.segmentGuid = parts[5];
            placement.railAnchor.normalizedT = ParseFloatOr(parts, 6, 0.0f);
            placement.railAnchor.lateralOffset = ParseFloatOr(parts, 7, 0.0f);
            placement.railAnchor.verticalOffset = ParseFloatOr(parts, 8, 0.0f);
            placement.railAnchor.forwardOffset = ParseFloatOr(parts, 9, 0.0f);
            placement.localRotation.x = DegreesToRadians(ParseFloatOr(parts, 10, 0.0f));
            placement.localRotation.y = DegreesToRadians(ParseFloatOr(parts, 11, 0.0f));
            placement.localRotation.z = DegreesToRadians(ParseFloatOr(parts, 12, 0.0f));
            placement.localScale.x = ParseFloatOr(parts, 13, 1.0f);
            placement.localScale.y = ParseFloatOr(parts, 14, 1.0f);
            placement.localScale.z = ParseFloatOr(parts, 15, 1.0f);
            placement.activationLeadDistance = ParseFloatOr(parts, 16, 80.0f);
            placement.enabled = parts[17] != "0";
            placement.editorVisible = parts[18] != "0";
            placement.editorLocked = parts[19] == "1";
            loaded.enemyPlacements.push_back(std::move(placement));
        } else if (kind == "terrain") {
            if (parts.size() < 15) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid terrain row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseTerrainPlacement placement{};
            placement.distance = ParseFloatOr(parts, 1, 0.0f);
            placement.layer = ParseCourseTerrainLayer(parts[2]);
            placement.id = parts[3];
            placement.meshId = parts[4].empty() ? placement.meshId : parts[4];
            placement.lateralOffset = ParseFloatOr(parts, 5, 0.0f);
            placement.verticalOffset = ParseFloatOr(parts, 6, 0.0f);
            placement.forwardOffset = ParseFloatOr(parts, 7, 0.0f);
            placement.scale.x = ParseFloatOr(parts, 8, 1.0f);
            placement.scale.y = ParseFloatOr(parts, 9, 1.0f);
            placement.scale.z = ParseFloatOr(parts, 10, 1.0f);
            placement.rotation.x = DegreesToRadians(ParseFloatOr(parts, 11, 0.0f));
            placement.rotation.y = DegreesToRadians(ParseFloatOr(parts, 12, 0.0f));
            placement.rotation.z = DegreesToRadians(ParseFloatOr(parts, 13, 0.0f));
            placement.collisionMode = ParseCourseTerrainCollisionMode(parts[14]);
            placement.renderPriority = static_cast<int>(ParseFloatOr(parts, 15, 0.0f));
            placement.cullBehindDistance = ParseFloatOr(parts, 16, -1.0f);
            placement.cullAheadDistance = ParseFloatOr(parts, 17, -1.0f);
            placement.editorGuid = parts.size() >= 19 ? parts[18] : std::string{};
            placement.editorVisible = parts.size() < 20 || parts[19] != "0";
            placement.editorLocked = parts.size() >= 21 && parts[20] == "1";
            loaded.terrainPlacements.push_back(placement);
        } else if (kind == "terrain_brush") {
            if (parts.size() < 11) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid terrain_brush row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            TerrainBrushStamp stamp{};
            stamp.strokeGuid = parts[1];
            stamp.stampGuid = parts[2];
            if (!ParseTerrainEditOperation(parts[3], stamp.operation)) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Unknown terrain brush operation at line " + std::to_string(lineNumber);
                }
                return false;
            }
            stamp.distance = ParseFloatOr(parts, 4, 0.0f);
            stamp.angle = ParseFloatOr(parts, 5, 0.0f);
            stamp.radius = ParseFloatOr(parts, 6, stamp.radius);
            stamp.surfaceRadius = ParseFloatOr(parts, 7, stamp.surfaceRadius);
            stamp.strength = ParseFloatOr(parts, 8, stamp.strength);
            stamp.hardness = ParseFloatOr(parts, 9, stamp.hardness);
            stamp.materialLayer = static_cast<uint32_t>((std::max)(0.0f, ParseFloatOr(parts, 10, 0.0f)));
            loadedTerrainStamps.push_back(std::move(stamp));
        } else if (kind == "rock_cluster") {
            if (parts.size() < 15) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid rock_cluster row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseRockCluster cluster{};
            cluster.distance = ParseFloatOr(parts, 1, 0.0f);
            cluster.id = parts[2];
            cluster.meshId = parts[3].empty() ? cluster.meshId : parts[3];
            cluster.anchor = ParseCourseRockClusterAnchor(parts[4]);
            cluster.type = ParseCourseRockClusterType(parts[5]);
            cluster.count = static_cast<uint32_t>((std::max)(0.0f, ParseFloatOr(parts, 6, 0.0f)));
            cluster.minScale = ParseFloatOr(parts, 7, cluster.minScale);
            cluster.maxScale = ParseFloatOr(parts, 8, cluster.maxScale);
            cluster.spread.x = ParseFloatOr(parts, 9, cluster.spread.x);
            cluster.spread.y = ParseFloatOr(parts, 10, cluster.spread.y);
            cluster.spread.z = ParseFloatOr(parts, 11, cluster.spread.z);
            cluster.clearLaneRadius = ParseFloatOr(parts, 12, cluster.clearLaneRadius);
            cluster.cullBehindDistance = ParseFloatOr(parts, 13, cluster.cullBehindDistance);
            cluster.cullAheadDistance = ParseFloatOr(parts, 14, cluster.cullAheadDistance);
            cluster.rotation.x = DegreesToRadians(ParseFloatOr(parts, 15, 0.0f));
            cluster.rotation.y = DegreesToRadians(ParseFloatOr(parts, 16, 0.0f));
            cluster.rotation.z = DegreesToRadians(ParseFloatOr(parts, 17, 0.0f));
            if (parts.size() >= 19) {
                cluster.instanceOverrides = ParseRockInstanceOverrides(parts[18]);
            }
            cluster.editorGuid = parts.size() >= 20 ? parts[19] : std::string{};
            cluster.editorVisible = parts.size() < 21 || parts[20] != "0";
            cluster.editorLocked = parts.size() >= 22 && parts[21] == "1";
            loaded.rockClusters.push_back(cluster);
        } else if (kind == "lighting") {
            if (parts.size() < 26) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid lighting row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseLightingPreset preset{};
            preset.distance = ParseFloatOr(parts, 1, 0.0f);
            preset.id = parts[2];
            preset.blendDistance = ParseFloatOr(parts, 3, preset.blendDistance);
            preset.sunColor.x = ParseFloatOr(parts, 4, preset.sunColor.x);
            preset.sunColor.y = ParseFloatOr(parts, 5, preset.sunColor.y);
            preset.sunColor.z = ParseFloatOr(parts, 6, preset.sunColor.z);
            preset.sunIntensity = ParseFloatOr(parts, 7, preset.sunIntensity);
            preset.sunDirection.x = ParseFloatOr(parts, 8, preset.sunDirection.x);
            preset.sunDirection.y = ParseFloatOr(parts, 9, preset.sunDirection.y);
            preset.sunDirection.z = ParseFloatOr(parts, 10, preset.sunDirection.z);
            preset.clearColor.x = ParseFloatOr(parts, 11, preset.clearColor.x);
            preset.clearColor.y = ParseFloatOr(parts, 12, preset.clearColor.y);
            preset.clearColor.z = ParseFloatOr(parts, 13, preset.clearColor.z);
            preset.fogColor.x = ParseFloatOr(parts, 14, preset.fogColor.x);
            preset.fogColor.y = ParseFloatOr(parts, 15, preset.fogColor.y);
            preset.fogColor.z = ParseFloatOr(parts, 16, preset.fogColor.z);
            preset.fogIntensity = ParseFloatOr(parts, 17, preset.fogIntensity);
            preset.fogStart = ParseFloatOr(parts, 18, preset.fogStart);
            preset.fogEnd = ParseFloatOr(parts, 19, preset.fogEnd);
            preset.fogDensity = ParseFloatOr(parts, 20, preset.fogDensity);
            preset.backlitFogLift = ParseFloatOr(parts, 21, preset.backlitFogLift);
            preset.openingGlowStrength = ParseFloatOr(parts, 22, preset.openingGlowStrength);
            preset.foregroundSilhouetteStrength = ParseFloatOr(parts, 23, preset.foregroundSilhouetteStrength);
            preset.lowFogLayerStrength = ParseFloatOr(parts, 24, preset.lowFogLayerStrength);
            preset.coolFloorHazeStrength = ParseFloatOr(parts, 25, preset.coolFloorHazeStrength);
            loaded.lightingPresets.push_back(preset);
        } else if (kind == "camera_shot_preset") {
            if (parts.size() < 12) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid camera_shot_preset row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseCameraShotPreset preset{};
            preset.id = parts[1];
            preset.mode = parts[2];
            preset.backDistanceOffset = ParseFloatOr(parts, 3, 0.0f);
            preset.verticalOffset = ParseFloatOr(parts, 4, 0.0f);
            preset.lateralOffset = ParseFloatOr(parts, 5, 0.0f);
            preset.lookAheadOffset = ParseFloatOr(parts, 6, 0.0f);
            preset.lookUpOffset = ParseFloatOr(parts, 7, 0.0f);
            preset.lookForwardOffset = ParseFloatOr(parts, 8, 0.0f);
            preset.fovOffset = DegreesToRadians(ParseFloatOr(parts, 9, 0.0f));
            preset.rollOffset = DegreesToRadians(ParseFloatOr(parts, 10, 0.0f));
            preset.shakeAmount = ParseFloatOr(parts, 11, 0.0f);
            loaded.cameraShotPresets.push_back(preset);
        } else if (kind == "camera_blend_asset") {
            if (parts.size() < 6) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid camera_blend_asset row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseCameraBlendAsset blend{};
            blend.id = parts[1];
            blend.blendInDistance = ParseFloatOr(parts, 2, blend.blendInDistance);
            blend.blendOutDistance = ParseFloatOr(parts, 3, blend.blendOutDistance);
            blend.curve = parts[4].empty() ? blend.curve : parts[4];
            blend.weightScale = ParseFloatOr(parts, 5, blend.weightScale);
            loaded.cameraBlendAssets.push_back(blend);
        } else if (kind == "camera_shot") {
            if (parts.size() < 16) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid camera_shot row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseCinematicCameraShot shot{};
            shot.startDistance = ParseFloatOr(parts, 1, 0.0f);
            shot.endDistance = ParseFloatOr(parts, 2, shot.startDistance);
            shot.id = parts[3];
            shot.mode = parts[4];
            shot.blendInDistance = ParseFloatOr(parts, 5, shot.blendInDistance);
            shot.blendOutDistance = ParseFloatOr(parts, 6, shot.blendOutDistance);
            shot.backDistanceOffset = ParseFloatOr(parts, 7, 0.0f);
            shot.verticalOffset = ParseFloatOr(parts, 8, 0.0f);
            shot.lateralOffset = ParseFloatOr(parts, 9, 0.0f);
            shot.lookAheadOffset = ParseFloatOr(parts, 10, 0.0f);
            shot.lookUpOffset = ParseFloatOr(parts, 11, 0.0f);
            shot.lookForwardOffset = ParseFloatOr(parts, 12, 0.0f);
            shot.fovOffset = DegreesToRadians(ParseFloatOr(parts, 13, 0.0f));
            shot.rollOffset = DegreesToRadians(ParseFloatOr(parts, 14, 0.0f));
            shot.shakeAmount = ParseFloatOr(parts, 15, 0.0f);
            shot.presetId = parts.size() >= 17 ? parts[16] : std::string{};
            shot.blendAssetId = parts.size() >= 18 ? parts[17] : std::string{};
            shot.weightScale = ParseFloatOr(parts, 18, shot.weightScale);
            // Optional v2 suffix; old course rows keep the inherited camera.
            if (parts.size() > 19) {
                if (parts.size() != 28 ||
                    (parts[19] != "0" && parts[19] != "1" && parts[19] != "2" && parts[19] != "3" && parts[19] != "4") ||
                    (parts[26] != "0" && parts[26] != "1")) {
                    if (errorMessage) *errorMessage = "Malformed camera view suffix at line " + std::to_string(lineNumber);
                    return false;
                }
                shot.view.mode = static_cast<CourseCameraViewMode>(std::stoi(parts[19]));
                float values[7]{};
                for (size_t i=0; i<7; ++i) {
                    const size_t column = i < 6 ? 20+i : 27;
                    if (!ParseFloat(parts[column], values[i]) || !std::isfinite(values[i])) {
                        if (errorMessage) *errorMessage = "Invalid camera view number at line " + std::to_string(lineNumber);
                        return false;
                    }
                }
                shot.view.positionOffset = {values[0],values[1],values[2]};
                shot.view.yaw = DegreesToRadians(values[3]);
                shot.view.pitch = DegreesToRadians(values[4]);
                shot.view.fovY = DegreesToRadians(values[5]);
                if (values[6] < -1000 || values[6] > 1000 || std::floor(values[6]) != values[6]) {
                    if (errorMessage) *errorMessage = "Invalid camera view priority.";
                    return false;
                }
                shot.view.hideVehicle = parts[26] == "1";
                shot.view.priority = static_cast<int>(values[6]);
                if (!shot.view.Validate(errorMessage)) return false;
            }
            loaded.cinematicCameraShots.push_back(shot);
        } else if (kind == "terrain_material") {
            if (parts.size() < 19) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid terrain_material row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseTerrainMaterialPreset preset{};
            preset.distance = ParseFloatOr(parts, 1, 0.0f);
            preset.id = parts[2];
            preset.blendDistance = ParseFloatOr(parts, 3, preset.blendDistance);
            preset.baseColor.x = ParseFloatOr(parts, 4, preset.baseColor.x);
            preset.baseColor.y = ParseFloatOr(parts, 5, preset.baseColor.y);
            preset.baseColor.z = ParseFloatOr(parts, 6, preset.baseColor.z);
            preset.brightness = ParseFloatOr(parts, 7, preset.brightness);
            preset.noiseStrength = ParseFloatOr(parts, 8, preset.noiseStrength);
            preset.strataStrength = ParseFloatOr(parts, 9, preset.strataStrength);
            preset.strataBreakupStrength = ParseFloatOr(parts, 10, preset.strataBreakupStrength);
            preset.specularStrength = ParseFloatOr(parts, 11, preset.specularStrength);
            preset.rimLightStrength = ParseFloatOr(parts, 12, preset.rimLightStrength);
            preset.backlightRimBoost = ParseFloatOr(parts, 13, preset.backlightRimBoost);
            preset.floorSandShadowStrength = ParseFloatOr(parts, 14, preset.floorSandShadowStrength);
            preset.detailNormalStrength = ParseFloatOr(parts, 15, preset.detailNormalStrength);
            preset.microDetailStrength = ParseFloatOr(parts, 16, preset.microDetailStrength);
            preset.cavityAoStrength = ParseFloatOr(parts, 17, preset.cavityAoStrength);
            preset.skyFillStrength = ParseFloatOr(parts, 18, preset.skyFillStrength);
            loaded.terrainMaterialPresets.push_back(preset);
        } else if (kind == "cinematic_shot_set") {
            if (parts.size() < 12) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Invalid cinematic_shot_set row at line " + std::to_string(lineNumber);
                }
                return false;
            }
            CourseCinematicShotSet shotSet{};
            shotSet.startDistance = ParseFloatOr(parts, 1, 0.0f);
            shotSet.endDistance = ParseFloatOr(parts, 2, shotSet.startDistance);
            shotSet.id = parts[3];
            shotSet.label = parts[4];
            shotSet.heroLandmarkIds = SplitList(parts[5]);
            shotSet.vistaLandmarkIds = SplitList(parts[6]);
            shotSet.lightingPresetId = parts[7];
            shotSet.cameraShotId = parts[8];
            shotSet.terrainMaterialId = parts[9];
            shotSet.fogMood = parts[10];
            shotSet.compositionNotes = parts[11];
            loaded.cinematicShotSets.push_back(std::move(shotSet));
        } else if (errorMessage != nullptr) {
            *errorMessage = "Unknown course row kind at line " + std::to_string(lineNumber) + ": " + kind;
            return false;
        }
    }

    for (std::size_t begin = 0; begin < loadedTerrainStamps.size();) {
        std::size_t end = begin + 1;
        while (end < loadedTerrainStamps.size() &&
            loadedTerrainStamps[end].strokeGuid == loadedTerrainStamps[begin].strokeGuid) {
            ++end;
        }
        const std::vector<TerrainBrushStamp> stroke(
            loadedTerrainStamps.begin() + static_cast<std::ptrdiff_t>(begin),
            loadedTerrainStamps.begin() + static_cast<std::ptrdiff_t>(end));
        if (!loaded.terrainEditLayer.ApplyStroke(stroke, errorMessage)) return false;
        begin = end;
    }

    if (loaded.railPoints.size() < 2) {
        if (errorMessage != nullptr) {
            *errorMessage = "Course has fewer than 2 rail points.";
        }
        return false;
    }
    if (loaded.cameraKeys.empty()) {
        loaded.cameraKeys.push_back({});
    }

    SortCourseData(loaded);
    *this = std::move(loaded);
    return true;
}

bool CourseAsset::SaveToFile(const std::string& path, std::string* errorMessage) const {
    std::string text;
    if (!SaveToString(&text, errorMessage)) return false;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Could not write course file: " + path;
        }
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed while writing course file: " + path;
        }
        return false;
    }
    return true;
}

bool CourseAsset::SaveToString(std::string* text, std::string* errorMessage) const {
    if (text == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "Course output string is null.";
        return false;
    }
    CourseAsset saved = *this;
    saved.SortForRuntime();
    if (!saved.terrainEditLayer.Validate(errorMessage)) return false;
    for (const auto& shot : saved.cinematicCameraShots)
        if (!shot.view.Validate(errorMessage)) return false;
    for (const CourseRideProfileDefinition& profile : saved.rideProfiles) {
        if (!profile.Validate(errorMessage)) return false;
    }
    for (const RailRideSpeedBeatDefinition& beat : saved.rideSpeedBeats) {
        if (!beat.Validate(errorMessage)) return false;
    }
    for (const CourseRailRideEventDefinition& event : saved.railRideEvents) {
        if (!event.Validate(errorMessage)) return false;
    }
    for (const EnemyEncounterBeatDefinition& beat : saved.encounterBeats) {
        if (!beat.Validate(errorMessage)) return false;
    }

    const bool hasPersistentEditorWorldIdentity =
        std::all_of(saved.cameraKeys.begin(), saved.cameraKeys.end(),
            [](const CourseCameraKey& value) { return !value.editorGuid.empty(); }) &&
        std::all_of(saved.events.begin(), saved.events.end(),
            [](const CourseEventMarker& value) { return !value.editorGuid.empty(); }) &&
        std::all_of(saved.enemyPlacements.begin(), saved.enemyPlacements.end(),
            [](const CourseEnemyPlacement& value) { return !value.editorGuid.empty(); }) &&
        std::all_of(saved.terrainPlacements.begin(), saved.terrainPlacements.end(),
            [](const CourseTerrainPlacement& value) { return !value.editorGuid.empty(); }) &&
        std::all_of(saved.rockClusters.begin(), saved.rockClusters.end(),
            [](const CourseRockCluster& value) { return !value.editorGuid.empty(); });
    const bool hasPersistentRailIdentity =
        std::all_of(saved.railPoints.begin(), saved.railPoints.end(),
            [](const RailPathControlPoint& value) { return !value.editorGuid.empty(); });
    const bool hasPersistentWaveIdentity =
        std::all_of(saved.waveDefinitions.begin(), saved.waveDefinitions.end(),
            [](const CourseWaveDefinition& value) {
                return !value.editorGuid.empty();
            });
    const bool hasPersistentRideProfileIdentity =
        std::all_of(saved.rideProfiles.begin(), saved.rideProfiles.end(),
            [](const CourseRideProfileDefinition& value) {
                return !value.editorGuid.empty();
            });
    const bool hasPersistentSpeedBeatIdentity =
        std::all_of(saved.rideSpeedBeats.begin(), saved.rideSpeedBeats.end(),
            [](const RailRideSpeedBeatDefinition& value) {
                return !value.editorGuid.empty();
            });
    const bool hasPersistentRailRideEventIdentity =
        std::all_of(saved.railRideEvents.begin(), saved.railRideEvents.end(),
            [](const CourseRailRideEventDefinition& value) {
                return !value.editorGuid.empty();
            });
    const bool hasPersistentEncounterBeatIdentity =
        std::all_of(saved.encounterBeats.begin(), saved.encounterBeats.end(),
            [](const EnemyEncounterBeatDefinition& value) {
                return !value.editorGuid.empty();
            });

    std::ostringstream file;

    file << "# Rail shooter course DSL\n";
    file << "# editor-schema:"
         << (hasPersistentEditorWorldIdentity && hasPersistentRailIdentity &&
                hasPersistentWaveIdentity && hasPersistentRideProfileIdentity &&
                hasPersistentSpeedBeatIdentity &&
                hasPersistentRailRideEventIdentity &&
                hasPersistentEncounterBeatIdentity ? 13
             : (hasPersistentEditorWorldIdentity ? 3 : 1)) << "\n";
    file << "# row format:\n";
    file << "# course|name\n";
    file << "# rail_track|assetId\n";
    file << "# rail|x|y|z|corridorRadius|speed|editorGuid|tangentMode|inX|inY|inZ|outX|outY|outZ\n";
    file << "# rail_anchor|ownerGuid|segmentGuid|normalizedT|lateralOffset|verticalOffset|forwardOffset\n";
    file << "# camera|distance|backDistance|verticalOffset|lateralOffset|lookAheadDistance|lookUpOffset|lookForwardOffset|fovDeg|rollDeg|editorGuid|editorVisible|editorLocked\n";
    file << "# section|start|end|name|category\n";
    file << "# ride_profile|editorGuid|start|end|displayName|speedMode|speedMultiplier|targetSpeedOverride|accelerationScale|brakingScale|maximumJerk|cornerEntryLookAheadDistance|cornerSpeedScale|turnAnticipationDistance|visualBankScale|maximumVisualBankDegrees|blendIn|blendOut|cameraShotId|enabled|editorVisible|editorLocked\n";
    file << "# ride_speed_beat|editorGuid|start|end|displayName|type|speedMultiplier|targetSpeedOverride|accelerationScale|brakingScale|maximumJerk|blendIn|blendOut|priority|enabled|editorVisible|editorLocked\n";
    file << "# rail_ride_event|editorGuid|start|end|displayName|type|bankMode|bankDegrees|rumbleAmplitude|rumbleFrequencyHz|suspensionAmplitude|cameraShake|cameraFovKick|cameraRollKickDegrees|hapticLow|hapticHigh|speedInfluence|blendIn|blendOut|priority|audioCueId|vfxCueId|triggerOncePerRun|enabled|editorVisible|editorLocked\n";
    file << "# terrain|distance|layer|id|meshId|lateralOffset|verticalOffset|forwardOffset|scaleX|scaleY|scaleZ|pitchDeg|yawDeg|rollDeg|collisionMode|renderPriority|cullBehind|cullAhead|editorGuid|editorVisible|editorLocked\n";
    file << "# terrain_brush|strokeGuid|stampGuid|operation|distance|angleRad|radius|surfaceRadius|strength|hardness|materialLayer\n";
    file << "# rock_cluster|distance|id|meshId|anchor|type|count|minScale|maxScale|spreadX|spreadY|spreadZ|clearLaneRadius|cullBehind|cullAhead|rotX|rotY|rotZ|instanceOverrides|editorGuid|editorVisible|editorLocked\n";
    file << "# lighting|distance|id|blendDistance|sunR|sunG|sunB|sunIntensity|sunDirX|sunDirY|sunDirZ|clearR|clearG|clearB|fogR|fogG|fogB|fogIntensity|fogStart|fogEnd|fogDensity|backlitFogLift|openingGlow|silhouette|lowFog|coolHaze\n";
    file << "# camera_shot_preset|id|mode|backOffset|upOffset|sideOffset|lookAheadOffset|lookUpOffset|lookForwardOffset|fovOffsetDeg|rollOffsetDeg|shake\n";
    file << "# camera_blend_asset|id|blendIn|blendOut|curve|weightScale\n";
    file << "# camera_shot|start|end|id|mode|blendIn|blendOut|backOffset|upOffset|sideOffset|lookAheadOffset|lookUpOffset|lookForwardOffset|fovOffsetDeg|rollOffsetDeg|shake|presetId|blendAssetId|weightScale|viewMode|eyeX|eyeY|eyeZ|yawDeg|pitchDeg|viewFovDeg|hideVehicle|priority\n";
    file << "# terrain_material|distance|id|blendDistance|baseR|baseG|baseB|brightness|noise|strata|breakup|specular|rim|backlight|floorShadow|detailNormal|microDetail|cavityAo|skyFill\n";
    file << "# cinematic_shot_set|start|end|id|label|heroLandmarksCsv|vistaLandmarksCsv|lightingId|cameraShotId|terrainMaterialId|fogMood|compositionNotes\n";
    file << "# wave|editorGuid|displayName|triggerRailDistance|prewarmDistance|timeoutSeconds|completionCondition|executionPolicy|nextWaveGuid|triggerEventId|enabled|editorVisible|editorLocked\n";
    file << "# encounter_beat|editorGuid|encounterId|displayName|waveGuid|triggerRailDistance|endRailDistance|prewarmDistance|establishMin|establishMax|threatenMin|threatenMax|attackMin|attackMax|recoverySeconds|resolveTimeout|requiredReadableRatio|maxConcurrentAttackers|maxThreatBudget|cameraShotId|cameraWeight|cameraFocusWeight|cameraFovOffsetDeg|cameraBackOffset|priority|exitSurvivors|requireCombatTruth|enabled|editorVisible|editorLocked\n";
    file << "# enemy_placement|editorGuid|actorAssetId|bulletPatternOverrideId|waveGroupGuid|segmentGuid|normalizedT|lateralOffset|verticalOffset|forwardOffset|pitchDeg|yawDeg|rollDeg|scaleX|scaleY|scaleZ|activationLeadDistance|enabled|editorVisible|editorLocked\n";
    file << "# event|distance|type|id|payload|editorGuid|editorVisible|editorLocked\n\n";

    file << std::fixed << std::setprecision(3);
    file << "course|" << saved.name << "\n\n";
    file << "rail_track|" << saved.railTrackAssetId << "\n\n";

    file << "# Main camera/terrain rail. Distances are arc-length evaluated by RailPath.\n";
    for (const RailPathControlPoint& point : saved.railPoints) {
        file << "rail|"
             << point.position.x << '|'
             << point.position.y << '|'
             << point.position.z << '|'
             << point.corridorRadius << '|'
             << point.speed << '|'
             << point.editorGuid << '|'
             << static_cast<int>(point.tangentMode) << '|'
             << point.incomingTangent.x << '|'
             << point.incomingTangent.y << '|'
             << point.incomingTangent.z << '|'
             << point.outgoingTangent.x << '|'
             << point.outgoingTangent.y << '|'
             << point.outgoingTangent.z << "\n";
    }

    file << std::setprecision(6);
    for (const CourseRailAnchorBinding& binding : saved.railAnchors) {
        file << "rail_anchor|"
             << binding.ownerGuid << '|'
             << binding.anchor.segmentGuid << '|'
             << binding.anchor.normalizedT << '|'
             << binding.anchor.lateralOffset << '|'
             << binding.anchor.verticalOffset << '|'
             << binding.anchor.forwardOffset << "\n";
    }
    file << std::setprecision(3);

    file << "\n# Camera keys define the cinematic rail rig.\n";
    for (const CourseCameraKey& key : saved.cameraKeys) {
        file << "camera|"
             << key.distance << '|'
             << key.backDistance << '|'
             << key.verticalOffset << '|'
             << key.lateralOffset << '|'
             << key.lookAheadDistance << '|'
             << key.lookUpOffset << '|'
             << key.lookForwardOffset << '|'
             << key.fovY * 180.0f / kPi << '|'
             << key.roll * 180.0f / kPi << '|'
             << key.editorGuid << '|'
             << (key.editorVisible ? 1 : 0) << '|'
             << (key.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n";
    for (const CourseSection& section : saved.sections) {
        file << "section|"
             << section.startDistance << '|'
             << section.endDistance << '|'
             << section.name << '|'
             << section.category << "\n";
    }

    file << "\n# Authored ride intent: speed policy, turn anticipation, banking, and camera shot.\n";
    for (const CourseRideProfileDefinition& profile : saved.rideProfiles) {
        file << "ride_profile|"
             << profile.editorGuid << '|'
             << profile.startDistance << '|'
             << profile.endDistance << '|'
             << profile.displayName << '|'
             << ToCourseRideSpeedModeString(profile.speedMode) << '|'
             << profile.speedMultiplier << '|'
             << profile.targetSpeedOverride << '|'
             << profile.accelerationScale << '|'
             << profile.brakingScale << '|'
             << profile.maximumJerk << '|'
             << profile.cornerEntryLookAheadDistance << '|'
             << profile.cornerSpeedScale << '|'
             << profile.turnAnticipationDistance << '|'
             << profile.visualBankScale << '|'
             << profile.maximumVisualBankDegrees << '|'
             << profile.blendInDistance << '|'
             << profile.blendOutDistance << '|'
             << (profile.cameraShotId.empty() ? "-" : profile.cameraShotId) << '|'
             << (profile.enabled ? 1 : 0) << '|'
             << (profile.editorVisible ? 1 : 0) << '|'
             << (profile.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Distance-authored speed rhythm layered over the active Ride Profile.\n";
    for (const RailRideSpeedBeatDefinition& beat : saved.rideSpeedBeats) {
        file << "ride_speed_beat|"
             << beat.editorGuid << '|'
             << beat.startDistance << '|'
             << beat.endDistance << '|'
             << beat.displayName << '|'
             << ToRailRideSpeedBeatTypeString(beat.type) << '|'
             << beat.speedMultiplier << '|'
             << beat.targetSpeedOverride << '|'
             << beat.accelerationScale << '|'
             << beat.brakingScale << '|'
             << beat.maximumJerk << '|'
             << beat.blendInDistance << '|'
             << beat.blendOutDistance << '|'
             << beat.priority << '|'
             << (beat.enabled ? 1 : 0) << '|'
             << (beat.editorVisible ? 1 : 0) << '|'
             << (beat.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Local authored track feel layered over curve-derived Ride Profile dynamics.\n";
    for (const CourseRailRideEventDefinition& event : saved.railRideEvents) {
        file << "rail_ride_event|"
             << event.editorGuid << '|'
             << event.startDistance << '|'
             << event.endDistance << '|'
             << event.displayName << '|'
             << ToCourseRailRideEventTypeString(event.type) << '|'
             << ToCourseRailRideBankModeString(event.bankMode) << '|'
             << event.bankDegrees << '|'
             << event.rumbleAmplitude << '|'
             << event.rumbleFrequencyHz << '|'
             << event.suspensionAmplitude << '|'
             << event.cameraShake << '|'
             << event.cameraFovKick << '|'
             << event.cameraRollKickDegrees << '|'
             << event.hapticLow << '|'
             << event.hapticHigh << '|'
             << event.speedInfluence << '|'
             << event.blendInDistance << '|'
             << event.blendOutDistance << '|'
             << event.priority << '|'
             << (event.audioCueId.empty() ? "-" : event.audioCueId) << '|'
             << (event.vfxCueId.empty() ? "-" : event.vfxCueId) << '|'
             << (event.triggerOncePerRun ? 1 : 0) << '|'
             << (event.enabled ? 1 : 0) << '|'
             << (event.editorVisible ? 1 : 0) << '|'
             << (event.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Layered course terrain: gameplay collision, hero landmarks, and vista background.\n";
    for (const CourseTerrainPlacement& placement : saved.terrainPlacements) {
        file << "terrain|"
             << placement.distance << '|'
             << ToCourseTerrainLayerString(placement.layer) << '|'
             << placement.id << '|'
             << placement.meshId << '|'
             << placement.lateralOffset << '|'
             << placement.verticalOffset << '|'
             << placement.forwardOffset << '|'
             << placement.scale.x << '|'
             << placement.scale.y << '|'
             << placement.scale.z << '|'
             << RadiansToDegrees(placement.rotation.x) << '|'
             << RadiansToDegrees(placement.rotation.y) << '|'
             << RadiansToDegrees(placement.rotation.z) << '|'
             << ToCourseTerrainCollisionModeString(placement.collisionMode) << '|'
             << placement.renderPriority << '|'
             << placement.cullBehindDistance << '|'
             << placement.cullAheadDistance << '|'
             << placement.editorGuid << '|'
             << (placement.editorVisible ? 1 : 0) << '|'
             << (placement.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Rock clusters compose debris as attached, hero, falling, or vista groups instead of loose floating rocks.\n";
    for (const CourseRockCluster& cluster : saved.rockClusters) {
        file << "rock_cluster|"
             << cluster.distance << '|'
             << cluster.id << '|'
             << cluster.meshId << '|'
             << ToCourseRockClusterAnchorString(cluster.anchor) << '|'
             << ToCourseRockClusterTypeString(cluster.type) << '|'
             << cluster.count << '|'
             << cluster.minScale << '|'
             << cluster.maxScale << '|'
             << cluster.spread.x << '|'
             << cluster.spread.y << '|'
             << cluster.spread.z << '|'
             << cluster.clearLaneRadius << '|'
             << cluster.cullBehindDistance << '|'
             << cluster.cullAheadDistance << '|'
             << RadiansToDegrees(cluster.rotation.x) << '|'
             << RadiansToDegrees(cluster.rotation.y) << '|'
             << RadiansToDegrees(cluster.rotation.z) << '|'
             << JoinRockInstanceOverrides(cluster.instanceOverrides) << '|'
             << cluster.editorGuid << '|'
             << (cluster.editorVisible ? 1 : 0) << '|'
             << (cluster.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Distance-driven cinematic lighting and fog presets.\n";
    for (const CourseLightingPreset& preset : saved.lightingPresets) {
        file << "lighting|"
             << preset.distance << '|'
             << preset.id << '|'
             << preset.blendDistance << '|'
             << preset.sunColor.x << '|'
             << preset.sunColor.y << '|'
             << preset.sunColor.z << '|'
             << preset.sunIntensity << '|'
             << preset.sunDirection.x << '|'
             << preset.sunDirection.y << '|'
             << preset.sunDirection.z << '|'
             << preset.clearColor.x << '|'
             << preset.clearColor.y << '|'
             << preset.clearColor.z << '|'
             << preset.fogColor.x << '|'
             << preset.fogColor.y << '|'
             << preset.fogColor.z << '|'
             << preset.fogIntensity << '|'
             << preset.fogStart << '|'
             << preset.fogEnd << '|'
             << preset.fogDensity << '|'
             << preset.backlitFogLift << '|'
             << preset.openingGlowStrength << '|'
             << preset.foregroundSilhouetteStrength << '|'
             << preset.lowFogLayerStrength << '|'
             << preset.coolFloorHazeStrength << "\n";
    }

    file << "\n# Camera shot presets are reusable additive shot shapes.\n";
    for (const CourseCameraShotPreset& preset : saved.cameraShotPresets) {
        file << "camera_shot_preset|"
             << preset.id << '|'
             << preset.mode << '|'
             << preset.backDistanceOffset << '|'
             << preset.verticalOffset << '|'
             << preset.lateralOffset << '|'
             << preset.lookAheadOffset << '|'
             << preset.lookUpOffset << '|'
             << preset.lookForwardOffset << '|'
             << RadiansToDegrees(preset.fovOffset) << '|'
             << RadiansToDegrees(preset.rollOffset) << '|'
             << preset.shakeAmount << "\n";
    }

    file << "\n# Camera blend assets define distance blends and easing for shot presets.\n";
    for (const CourseCameraBlendAsset& blend : saved.cameraBlendAssets) {
        file << "camera_blend_asset|"
             << blend.id << '|'
             << blend.blendInDistance << '|'
             << blend.blendOutDistance << '|'
             << blend.curve << '|'
             << blend.weightScale << "\n";
    }

    file << "\n# Cinematic shot accents are additive over the base camera rail.\n";
    for (const CourseCinematicCameraShot& shot : saved.cinematicCameraShots) {
        file << "camera_shot|"
             << shot.startDistance << '|'
             << shot.endDistance << '|'
             << shot.id << '|'
             << shot.mode << '|'
             << shot.blendInDistance << '|'
             << shot.blendOutDistance << '|'
             << shot.backDistanceOffset << '|'
             << shot.verticalOffset << '|'
             << shot.lateralOffset << '|'
             << shot.lookAheadOffset << '|'
             << shot.lookUpOffset << '|'
             << shot.lookForwardOffset << '|'
             << RadiansToDegrees(shot.fovOffset) << '|'
             << RadiansToDegrees(shot.rollOffset) << '|'
             << shot.shakeAmount << '|'
             << (shot.presetId.empty() ? "-" : shot.presetId) << '|'
             << (shot.blendAssetId.empty() ? "-" : shot.blendAssetId) << '|'
             << shot.weightScale << '|'
             << static_cast<int>(shot.view.mode) << '|'
             << shot.view.positionOffset.x << '|' << shot.view.positionOffset.y << '|'
             << shot.view.positionOffset.z << '|'
             << RadiansToDegrees(shot.view.yaw) << '|'
             << RadiansToDegrees(shot.view.pitch) << '|'
             << RadiansToDegrees(shot.view.fovY) << '|'
             << (shot.view.hideVehicle ? 1 : 0) << '|'
             << shot.view.priority << "\n";
    }

    file << "\n# Terrain material presets drive authored rock color and detail by distance.\n";
    for (const CourseTerrainMaterialPreset& preset : saved.terrainMaterialPresets) {
        file << "terrain_material|"
             << preset.distance << '|'
             << preset.id << '|'
             << preset.blendDistance << '|'
             << preset.baseColor.x << '|'
             << preset.baseColor.y << '|'
             << preset.baseColor.z << '|'
             << preset.brightness << '|'
             << preset.noiseStrength << '|'
             << preset.strataStrength << '|'
             << preset.strataBreakupStrength << '|'
             << preset.specularStrength << '|'
             << preset.rimLightStrength << '|'
             << preset.backlightRimBoost << '|'
             << preset.floorSandShadowStrength << '|'
             << preset.detailNormalStrength << '|'
             << preset.microDetailStrength << '|'
             << preset.cavityAoStrength << '|'
             << preset.skyFillStrength << "\n";
    }

    file << "\n# Cinematic shot sets bind landmarks, camera, lighting, fog, and material into authored distance shots.\n";
    for (const CourseCinematicShotSet& shotSet : saved.cinematicShotSets) {
        file << "cinematic_shot_set|"
             << shotSet.startDistance << '|'
             << shotSet.endDistance << '|'
             << shotSet.id << '|'
             << shotSet.label << '|'
             << JoinList(shotSet.heroLandmarkIds) << '|'
             << JoinList(shotSet.vistaLandmarkIds) << '|'
             << shotSet.lightingPresetId << '|'
             << shotSet.cameraShotId << '|'
             << shotSet.terrainMaterialId << '|'
             << shotSet.fogMood << '|'
             << shotSet.compositionNotes << "\n";
    }

    file << "\n";
    for (const CourseEventMarker& event : saved.events) {
        file << "event|"
             << event.distance << '|'
             << event.type << '|'
             << event.id << '|'
             << event.payload << '|'
             << event.editorGuid << '|'
             << (event.editorVisible ? 1 : 0) << '|'
             << (event.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# First-class encounter waves referenced by persistent enemy placements.\n";
    file << std::setprecision(6);
    for (const CourseWaveDefinition& wave : saved.waveDefinitions) {
        file << "wave|"
             << wave.editorGuid << '|'
             << wave.displayName << '|'
             << wave.triggerRailDistance << '|'
             << wave.prewarmDistance << '|'
             << wave.timeoutSeconds << '|'
             << ToCourseWaveCompletionConditionString(wave.completionCondition) << '|'
             << ToCourseWaveExecutionPolicyString(wave.executionPolicy) << '|'
             << (wave.nextWaveGuid.empty() ? std::string("-") : wave.nextWaveGuid) << '|'
             << (wave.triggerEventId.empty() ? std::string("-") : wave.triggerEventId) << '|'
             << (wave.enabled ? 1 : 0) << '|'
             << (wave.editorVisible ? 1 : 0) << '|'
             << (wave.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Authored encounter rhythm layered over stable Wave membership.\n";
    for (const EnemyEncounterBeatDefinition& beat : saved.encounterBeats) {
        file << "encounter_beat|"
             << beat.editorGuid << '|'
             << beat.encounterId << '|'
             << beat.displayName << '|'
             << beat.waveGuid << '|'
             << beat.triggerRailDistance << '|'
             << beat.endRailDistance << '|'
             << beat.prewarmDistance << '|'
             << beat.establishMinimumSeconds << '|'
             << beat.establishMaximumSeconds << '|'
             << beat.threatenMinimumSeconds << '|'
             << beat.threatenMaximumSeconds << '|'
             << beat.attackMinimumSeconds << '|'
             << beat.attackMaximumSeconds << '|'
             << beat.recoverySeconds << '|'
             << beat.resolveTimeoutSeconds << '|'
             << beat.requiredReadableRatio << '|'
             << beat.maximumConcurrentAttackers << '|'
             << beat.maximumThreatBudget << '|'
             << (beat.cameraShotId.empty() ? "-" : beat.cameraShotId) << '|'
             << beat.cameraWeight << '|'
             << beat.cameraFocusWeight << '|'
             << beat.cameraFovOffsetDegrees << '|'
             << beat.cameraBackDistanceOffset << '|'
             << beat.priority << '|'
             << (beat.exitSurvivorsOnResolve ? 1 : 0) << '|'
             << (beat.requireCombatTruthForCompletion ? 1 : 0) << '|'
             << (beat.enabled ? 1 : 0) << '|'
             << (beat.editorVisible ? 1 : 0) << '|'
             << (beat.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Persistent enemy instances anchored to rail topology.\n";
    file << std::setprecision(6);
    for (const CourseEnemyPlacement& placement : saved.enemyPlacements) {
        file << "enemy_placement|"
             << placement.editorGuid << '|'
             << placement.actorAssetId << '|'
             << (placement.bulletPatternOverrideId.empty()
                    ? std::string("-") : placement.bulletPatternOverrideId) << '|'
             << (placement.waveGroupGuid.empty()
                    ? std::string("-") : placement.waveGroupGuid) << '|'
             << placement.railAnchor.segmentGuid << '|'
             << placement.railAnchor.normalizedT << '|'
             << placement.railAnchor.lateralOffset << '|'
             << placement.railAnchor.verticalOffset << '|'
             << placement.railAnchor.forwardOffset << '|'
             << RadiansToDegrees(placement.localRotation.x) << '|'
             << RadiansToDegrees(placement.localRotation.y) << '|'
             << RadiansToDegrees(placement.localRotation.z) << '|'
             << placement.localScale.x << '|'
             << placement.localScale.y << '|'
             << placement.localScale.z << '|'
             << placement.activationLeadDistance << '|'
             << (placement.enabled ? 1 : 0) << '|'
             << (placement.editorVisible ? 1 : 0) << '|'
             << (placement.editorLocked ? 1 : 0) << "\n";
    }

    file << "\n# Non-destructive procedural Terrain sculpt and paint strokes.\n";
    file << std::setprecision(6);
    for (const TerrainBrushStamp& stamp : saved.terrainEditLayer.Stamps()) {
        file << "terrain_brush|"
             << stamp.strokeGuid << '|'
             << stamp.stampGuid << '|'
             << ToString(stamp.operation) << '|'
             << stamp.distance << '|'
             << stamp.angle << '|'
             << stamp.radius << '|'
             << stamp.surfaceRadius << '|'
             << stamp.strength << '|'
             << stamp.hardness << '|'
             << stamp.materialLayer << "\n";
    }
    if (!file.good()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed while serializing course data.";
        }
        return false;
    }
    *text = file.str();
    return true;
}

void CourseAsset::BuildFallbackCanyon(float corridorRadius) {
    name = "Fallback Canyon Course";
    railPoints.clear();
    railAnchors.clear();
    cameraKeys.clear();
    sections.clear();
    rideProfiles.clear();
    rideSpeedBeats.clear();
    railRideEvents.clear();
    events.clear();
    waveDefinitions.clear();
    encounterBeats.clear();
    enemyPlacements.clear();
    terrainPlacements.clear();
    rockClusters.clear();
    lightingPresets.clear();
    cameraShotPresets.clear();
    cameraBlendAssets.clear();
    cinematicCameraShots.clear();
    terrainMaterialPresets.clear();
    cinematicShotSets.clear();

    RailPath path;
    path.BuildDefaultCanyonPath(corridorRadius);
    railPoints = path.ControlPoints();
    cameraKeys.push_back({0.0f, 18.0f, 6.2f, 0.0f, 54.0f, 2.2f, 8.0f, DegreesToRadians(54.0f), 0.0f});
    sections.push_back({0.0f, path.Length(), "Fallback Run", "Debug"});
    lightingPresets.push_back({});
    terrainMaterialPresets.push_back({});
}

void CourseAsset::SortForRuntime() {
    SortCourseData(*this);
}

void CourseAsset::ApplyToRailPath(RailPath& railPath) const {
    if (railPoints.size() >= 2) {
        railPath.SetControlPoints(railPoints);
    }
}

CourseCameraKey CourseAsset::EvaluateCamera(float distance) const {
    if (cameraKeys.empty()) {
        return {};
    }
    if (cameraKeys.size() == 1 || distance <= cameraKeys.front().distance) {
        return cameraKeys.front();
    }
    if (distance >= cameraKeys.back().distance) {
        return cameraKeys.back();
    }

    const auto upper = std::upper_bound(
        cameraKeys.begin(),
        cameraKeys.end(),
        distance,
        [](float value, const CourseCameraKey& key) {
            return value < key.distance;
        });
    const CourseCameraKey& b = *upper;
    const CourseCameraKey& a = *(upper - 1);
    const float span = (std::max)(0.001f, b.distance - a.distance);
    const float t = (std::clamp)((distance - a.distance) / span, 0.0f, 1.0f);
    return LerpCamera(a, b, t);
}

CourseLightingPreset CourseAsset::EvaluateLightingPreset(float distance) const {
    if (lightingPresets.empty()) {
        return {};
    }
    CourseLightingPreset current = lightingPresets.front();
    if (distance <= current.distance || lightingPresets.size() == 1) {
        return current;
    }

    for (size_t index = 1; index < lightingPresets.size(); ++index) {
        const CourseLightingPreset& next = lightingPresets[index];
        const float blendDistance = (std::max)(0.001f, next.blendDistance);
        const float transitionStart = next.distance - blendDistance;
        if (distance < transitionStart) {
            return current;
        }
        if (distance <= next.distance) {
            const float t = (std::clamp)((distance - transitionStart) / blendDistance, 0.0f, 1.0f);
            return LerpLighting(current, next, t);
        }
        current = next;
    }
    return current;
}

CourseCameraShotState CourseAsset::EvaluateCinematicCameraShot(
    float distance,
    std::string_view preferredShotId) const {
    CourseCameraShotState result{};
    for (const CourseCinematicCameraShot& shot : cinematicCameraShots) {
        if (!preferredShotId.empty() && shot.id != preferredShotId) continue;
        const CourseCameraBlendAsset* blendAsset = FindCameraBlendAsset(*this, shot.blendAssetId);
        const CourseCinematicCameraShot resolvedShot = ResolveCameraShot(*this, shot);
        const float weight = EvaluateShotWeight(resolvedShot, blendAsset, distance);
        if (weight > 0 && (result.weight <= 0 || shot.view.priority > result.shot.view.priority ||
            (shot.view.priority == result.shot.view.priority && weight > result.weight))) {
            result.weight = weight;
            result.shot = resolvedShot;
            result.presetId = shot.presetId;
            result.blendAssetId = shot.blendAssetId;
            result.blendCurve = blendAsset != nullptr ? blendAsset->curve : std::string{"linear"};
        }
    }
    return result;
}

CourseTerrainMaterialPreset CourseAsset::EvaluateTerrainMaterialPreset(float distance) const {
    if (terrainMaterialPresets.empty()) {
        return {};
    }
    CourseTerrainMaterialPreset current = terrainMaterialPresets.front();
    if (distance <= current.distance || terrainMaterialPresets.size() == 1) {
        return current;
    }

    for (size_t index = 1; index < terrainMaterialPresets.size(); ++index) {
        const CourseTerrainMaterialPreset& next = terrainMaterialPresets[index];
        const float blendDistance = (std::max)(0.001f, next.blendDistance);
        const float transitionStart = next.distance - blendDistance;
        if (distance < transitionStart) {
            return current;
        }
        if (distance <= next.distance) {
            const float t = (std::clamp)((distance - transitionStart) / blendDistance, 0.0f, 1.0f);
            return LerpTerrainMaterial(current, next, t);
        }
        current = next;
    }
    return current;
}

const CourseCinematicShotSet* CourseAsset::FindCinematicShotSet(float distance) const {
    const CourseCinematicShotSet* best = nullptr;
    for (const CourseCinematicShotSet& shotSet : cinematicShotSets) {
        if (distance < shotSet.startDistance || distance > shotSet.endDistance) {
            continue;
        }
        if (best == nullptr ||
            (shotSet.endDistance - shotSet.startDistance) < (best->endDistance - best->startDistance)) {
            best = &shotSet;
        }
    }
    return best;
}

const CourseSection* CourseAsset::FindSection(float distance) const {
    for (const CourseSection& section : sections) {
        if (distance >= section.startDistance && distance < section.endDistance) {
            return &section;
        }
    }
    return sections.empty() ? nullptr : &sections.back();
}

void CourseRuntime::Bind(const CourseAsset* asset) {
    asset_ = asset;
    traversalEndDistance_ = -1.0f;
    Reset(0.0f);
}

void CourseRuntime::SetTraversalEndDistance(float distance) {
    traversalEndDistance_ = std::isfinite(distance) && distance >= 0.0f ? distance : -1.0f;
}

float CourseRuntime::TraversalEndDistance(const RailPath& railPath) const {
    return traversalEndDistance_ >= 0.0f
        ? (std::min)(traversalEndDistance_, railPath.Length()) : railPath.Length();
}

void CourseRuntime::Reset(float distance) {
    distance_ = (std::max)(0.0f, distance);
    if (traversalEndDistance_ >= 0.0f) distance_ = (std::min)(distance_, traversalEndDistance_);
    nextEventIndex_ = 0;
    if (asset_ == nullptr) {
        return;
    }
    while (nextEventIndex_ < asset_->events.size() &&
        asset_->events[nextEventIndex_].distance < distance_) {
        ++nextEventIndex_;
    }
}

std::vector<CourseEventMarker> CourseRuntime::Advance(float deltaTime, const RailPath& railPath) {
    return AdvanceInternal(deltaTime, railPath, nullptr, true);
}

const CourseRideProfileDefinition* CourseAsset::FindRideProfile(float distance) const {
    const CourseRideProfileDefinition* best = nullptr;
    for (const CourseRideProfileDefinition& profile : rideProfiles) {
        if (!profile.enabled || distance < profile.startDistance || distance > profile.endDistance) {
            continue;
        }
        if (best == nullptr ||
            (profile.endDistance - profile.startDistance) <
                (best->endDistance - best->startDistance)) {
            best = &profile;
        }
    }
    return best;
}

const RailRideSpeedBeatDefinition* CourseAsset::FindRideSpeedBeat(float distance) const {
    const RailRideSpeedBeatDefinition* best = nullptr;
    for (const RailRideSpeedBeatDefinition& beat : rideSpeedBeats) {
        if (!beat.enabled || distance < beat.startDistance || distance > beat.endDistance) continue;
        if (best == nullptr || beat.priority > best->priority ||
            (beat.priority == best->priority &&
                (beat.endDistance-beat.startDistance) < (best->endDistance-best->startDistance))) {
            best = &beat;
        }
    }
    return best;
}

std::vector<CourseEventMarker> CourseRuntime::Advance(
    float deltaTime,
    const RailPath& railPath,
    float speedOverride) {
    return AdvanceInternal(deltaTime, railPath, &speedOverride, true);
}

std::vector<CourseEventMarker> CourseRuntime::AdvanceClamped(
    float deltaTime,
    const RailPath& railPath,
    float speedOverride) {
    return AdvanceInternal(deltaTime, railPath, &speedOverride, false);
}

std::vector<CourseEventMarker> CourseRuntime::AdvanceInternal(
    float deltaTime,
    const RailPath& railPath,
    const float* speedOverride,
    bool loopAtEnd) {
    std::vector<CourseEventMarker> triggered;
    if (asset_ == nullptr || railPath.Length() <= 0.0f) {
        return triggered;
    }

    const float previousDistance = distance_;
    const RailPathSample sample = railPath.Evaluate(distance_);
    // An explicit speed is authoritative (RailVehicleMovementSystem owns its
    // integration). The legacy minimum only applies to rail-authored playback
    // that did not provide a vehicle speed override.
    const float speed = speedOverride != nullptr
        ? (std::isfinite(*speedOverride) ? (std::max)(0.0f, *speedOverride) : 0.0f)
        : (std::max)(12.0f, sample.speed);
    distance_ += speed * (std::max)(0.0f, deltaTime);
    const float travelEnd = loopAtEnd ? railPath.Length() : TraversalEndDistance(railPath);
    if (distance_ > travelEnd) {
        if (loopAtEnd) {
            distance_ = std::fmod(distance_, railPath.Length());
            nextEventIndex_ = 0;
        } else {
            distance_ = travelEnd;
        }
    }

    while (nextEventIndex_ < asset_->events.size()) {
        const CourseEventMarker& event = asset_->events[nextEventIndex_];
        if (event.distance > distance_ || event.distance < previousDistance) {
            break;
        }
        triggered.push_back(event);
        ++nextEventIndex_;
    }
    return triggered;
}

const CourseSection* CourseRuntime::CurrentSection() const {
    return asset_ != nullptr ? asset_->FindSection(distance_) : nullptr;
}
