#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../terrain/RailPath.h"
#include "../terrain/TerrainEditLayer.h"
#include "RailAnchor.h"
#include "CourseRideProfileDefinition.h"
#include "RailRideSpeedBeatDefinition.h"
#include "CourseRailRideEventDefinition.h"
#include "EnemyEncounterBeatDefinition.h"
#include "utils/math/Vector.h"

struct CourseCameraKey {
    float distance = 0.0f;
    float backDistance = 18.0f;
    float verticalOffset = 6.0f;
    float lateralOffset = 0.0f;
    float lookAheadDistance = 54.0f;
    float lookUpOffset = 2.2f;
    float lookForwardOffset = 8.0f;
    float fovY = 0.30f * 3.14159265358979323846f;
    float roll = 0.0f;
    std::string editorGuid;
    bool editorVisible = true;
    bool editorLocked = false;
};

struct CourseSection {
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    std::string name;
    std::string category;
};

struct CourseEventMarker {
    float distance = 0.0f;
    std::string type;
    std::string id;
    std::string payload;
    std::string editorGuid;
    bool editorVisible = true;
    bool editorLocked = false;
};

// Persistent, editor-authored enemy instance. Runtime actor IDs and mutable
// combat state deliberately do not live here: this record is the CourseAsset
// source of truth and remains stable across play sessions and rail edits.
struct CourseEnemyPlacement {
    std::string editorGuid;
    std::string actorAssetId = "drone";
    std::string bulletPatternOverrideId;
    std::string waveGroupGuid;
    RailAnchor railAnchor{};
    Vector3 localRotation{};
    Vector3 localScale{1.0f, 1.0f, 1.0f};
    float activationLeadDistance = 80.0f;
    bool enabled = true;
    bool editorVisible = true;
    bool editorLocked = false;
};

enum class CourseWaveCompletionCondition {
    AllEnemiesDefeated,
    Timeout,
    ReachRailDistance,
    ScriptedEvent,
};

enum class CourseWaveExecutionPolicy {
    Parallel,
    Sequential,
    Exclusive,
};

// First-class encounter authoring record. Enemy placements reference
// editorGuid; displayName is presentation-only and may be renamed safely.
struct CourseWaveDefinition {
    std::string editorGuid;
    std::string displayName = "Wave";
    float triggerRailDistance = 0.0f;
    float prewarmDistance = 80.0f;
    float timeoutSeconds = 30.0f;
    CourseWaveCompletionCondition completionCondition =
        CourseWaveCompletionCondition::AllEnemiesDefeated;
    CourseWaveExecutionPolicy executionPolicy =
        CourseWaveExecutionPolicy::Parallel;
    std::string nextWaveGuid;
    std::string triggerEventId;
    bool enabled = true;
    bool editorVisible = true;
    bool editorLocked = false;
};

enum class CourseTerrainLayer {
    GameplayCollision,
    HeroLandmark,
    VistaBackground,
};

enum class CourseTerrainCollisionMode {
    None,
    Proxy,
    Solid,
};

enum class CourseRockClusterType {
    AttachedDebris,
    HeroFracture,
    FallingDebris,
    VistaSilhouette,
};

enum class CourseRockClusterAnchor {
    LeftWall,
    RightWall,
    Floor,
    CeilingBreak,
    VistaWall,
};

struct CourseTerrainPlacement {
    float distance = 0.0f;
    CourseTerrainLayer layer = CourseTerrainLayer::HeroLandmark;
    std::string id;
    std::string meshId = "animated_cube";
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float forwardOffset = 0.0f;
    Vector3 scale = {1.0f, 1.0f, 1.0f};
    Vector3 rotation = {0.0f, 0.0f, 0.0f};
    CourseTerrainCollisionMode collisionMode = CourseTerrainCollisionMode::None;
    int renderPriority = 0;
    float cullBehindDistance = -1.0f;
    float cullAheadDistance = -1.0f;
    std::string editorGuid;
    bool editorVisible = true;
    bool editorLocked = false;
};

struct CourseRockCluster {
    float distance = 0.0f;
    std::string id;
    std::string meshId = "curved_canyon_wall";
    CourseRockClusterAnchor anchor = CourseRockClusterAnchor::LeftWall;
    CourseRockClusterType type = CourseRockClusterType::AttachedDebris;
    uint32_t count = 0;
    float minScale = 0.5f;
    float maxScale = 2.0f;
    Vector3 spread = {10.0f, 4.0f, 8.0f};
    Vector3 rotation = {0.0f, 0.0f, 0.0f};
    float clearLaneRadius = 18.0f;
    float cullBehindDistance = 120.0f;
    float cullAheadDistance = 260.0f;
    struct InstanceTransformOverride {
        uint32_t index = 0;
        Vector3 localOffset = {0.0f, 0.0f, 0.0f};
        Vector3 scale = {1.0f, 1.0f, 1.0f};
        Vector3 rotation = {0.0f, 0.0f, 0.0f};
    };
    std::vector<InstanceTransformOverride> instanceOverrides;
    std::string editorGuid;
    bool editorVisible = true;
    bool editorLocked = false;
};

struct CourseLightingPreset {
    float distance = 0.0f;
    std::string id;
    float blendDistance = 80.0f;
    Vector4 sunColor = {1.0f, 0.74f, 0.46f, 1.0f};
    Vector3 sunDirection = {-0.38f, -0.52f, 0.76f};
    float sunIntensity = 2.4f;
    Vector4 clearColor = {0.32f, 0.29f, 0.28f, 1.0f};
    Vector3 fogColor = {0.42f, 0.38f, 0.36f};
    float fogIntensity = 0.48f;
    float fogStart = 135.0f;
    float fogEnd = 1450.0f;
    float fogDensity = 0.26f;
    float backlitFogLift = 0.34f;
    float openingGlowStrength = 0.62f;
    float foregroundSilhouetteStrength = 0.58f;
    float lowFogLayerStrength = 0.30f;
    float coolFloorHazeStrength = 0.24f;
};

enum class CourseCameraViewMode { Inherit, FirstPerson, Rear, CloseUp, Custom };

// Explicit poses are relative to the rail at the player's current distance.
// Zero yaw looks along travel; pi looks behind while the vehicle keeps moving forward.
struct CourseCameraViewDefinition {
    CourseCameraViewMode mode = CourseCameraViewMode::Inherit;
    Vector3 positionOffset{0.0f, 5.8f, 0.8f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovY = 1.0471975512f;
    bool hideVehicle = false;
    int priority = 0;

    static CourseCameraViewDefinition Preset(CourseCameraViewMode mode);
    bool Validate(std::string* errorMessage = nullptr) const;
};
const char* ToCourseCameraViewModeString(CourseCameraViewMode mode);

struct CourseCinematicCameraShot {
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    std::string id;
    std::string mode;
    std::string presetId;
    std::string blendAssetId;
    float blendInDistance = 24.0f;
    float blendOutDistance = 24.0f;
    float weightScale = 1.0f;
    float backDistanceOffset = 0.0f;
    float verticalOffset = 0.0f;
    float lateralOffset = 0.0f;
    float lookAheadOffset = 0.0f;
    float lookUpOffset = 0.0f;
    float lookForwardOffset = 0.0f;
    float fovOffset = 0.0f;
    float rollOffset = 0.0f;
    float shakeAmount = 0.0f;
    CourseCameraViewDefinition view{};
};

struct CourseCameraShotPreset {
    std::string id;
    std::string mode;
    float backDistanceOffset = 0.0f;
    float verticalOffset = 0.0f;
    float lateralOffset = 0.0f;
    float lookAheadOffset = 0.0f;
    float lookUpOffset = 0.0f;
    float lookForwardOffset = 0.0f;
    float fovOffset = 0.0f;
    float rollOffset = 0.0f;
    float shakeAmount = 0.0f;
};

struct CourseCameraBlendAsset {
    std::string id;
    float blendInDistance = 24.0f;
    float blendOutDistance = 24.0f;
    std::string curve = "smoothstep";
    float weightScale = 1.0f;
};

struct CourseCameraShotState {
    float weight = 0.0f;
    CourseCinematicCameraShot shot{};
    std::string presetId;
    std::string blendAssetId;
    std::string blendCurve = "linear";
};

struct CourseTerrainMaterialPreset {
    float distance = 0.0f;
    std::string id;
    float blendDistance = 80.0f;
    Vector4 baseColor = {1.02f, 0.96f, 0.90f, 1.0f};
    float brightness = 1.0f;
    float noiseStrength = 1.0f;
    float strataStrength = 1.0f;
    float strataBreakupStrength = 0.92f;
    float specularStrength = 0.135f;
    float rimLightStrength = 0.84f;
    float backlightRimBoost = 0.72f;
    float floorSandShadowStrength = 0.38f;
    float detailNormalStrength = 1.18f;
    float microDetailStrength = 1.16f;
    float cavityAoStrength = 0.82f;
    float skyFillStrength = 0.30f;
};

struct CourseCinematicShotSet {
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    std::string id;
    std::string label;
    std::vector<std::string> heroLandmarkIds;
    std::vector<std::string> vistaLandmarkIds;
    std::string lightingPresetId;
    std::string cameraShotId;
    std::string terrainMaterialId;
    std::string fogMood;
    std::string compositionNotes;
};

struct CourseAsset {
    std::string name = "Untitled Course";
    // Course-level reference to the immutable rail presentation asset. RailPath
    // control points remain the geometry source of truth.
    std::string railTrackAssetId = "mine_cart_standard";
    std::vector<RailPathControlPoint> railPoints;
    std::vector<CourseRailAnchorBinding> railAnchors;
    std::vector<CourseCameraKey> cameraKeys;
    std::vector<CourseSection> sections;
    std::vector<CourseRideProfileDefinition> rideProfiles;
    std::vector<RailRideSpeedBeatDefinition> rideSpeedBeats;
    std::vector<CourseRailRideEventDefinition> railRideEvents;
    std::vector<CourseEventMarker> events;
    std::vector<CourseWaveDefinition> waveDefinitions;
    std::vector<EnemyEncounterBeatDefinition> encounterBeats;
    std::vector<CourseEnemyPlacement> enemyPlacements;
    std::vector<CourseTerrainPlacement> terrainPlacements;
    TerrainEditLayer terrainEditLayer{};
    std::vector<CourseRockCluster> rockClusters;
    std::vector<CourseLightingPreset> lightingPresets;
    std::vector<CourseCameraShotPreset> cameraShotPresets;
    std::vector<CourseCameraBlendAsset> cameraBlendAssets;
    std::vector<CourseCinematicCameraShot> cinematicCameraShots;
    std::vector<CourseTerrainMaterialPreset> terrainMaterialPresets;
    std::vector<CourseCinematicShotSet> cinematicShotSets;

    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    bool LoadFromString(const std::string& text, std::string* errorMessage = nullptr);
    bool SaveToString(std::string* text, std::string* errorMessage = nullptr) const;
    void BuildFallbackCanyon(float corridorRadius);
    void SortForRuntime();
    void ApplyToRailPath(RailPath& railPath) const;
    CourseCameraKey EvaluateCamera(float distance) const;
    CourseLightingPreset EvaluateLightingPreset(float distance) const;
    CourseCameraShotState EvaluateCinematicCameraShot(
        float distance,
        std::string_view preferredShotId = {}) const;
    CourseTerrainMaterialPreset EvaluateTerrainMaterialPreset(float distance) const;
    const CourseCinematicShotSet* FindCinematicShotSet(float distance) const;
    const CourseSection* FindSection(float distance) const;
    const CourseRideProfileDefinition* FindRideProfile(float distance) const;
    const RailRideSpeedBeatDefinition* FindRideSpeedBeat(float distance) const;
    bool IsValid() const { return railPoints.size() >= 2; }
};

const char* ToCourseTerrainLayerString(CourseTerrainLayer layer);
const char* ToCourseTerrainCollisionModeString(CourseTerrainCollisionMode mode);
const char* ToCourseRockClusterTypeString(CourseRockClusterType type);
const char* ToCourseRockClusterAnchorString(CourseRockClusterAnchor anchor);
CourseTerrainLayer ParseCourseTerrainLayer(const std::string& text);
CourseTerrainCollisionMode ParseCourseTerrainCollisionMode(const std::string& text);
CourseRockClusterType ParseCourseRockClusterType(const std::string& text);
CourseRockClusterAnchor ParseCourseRockClusterAnchor(const std::string& text);
const char* ToCourseWaveCompletionConditionString(
    CourseWaveCompletionCondition condition);
const char* ToCourseWaveExecutionPolicyString(CourseWaveExecutionPolicy policy);
CourseWaveCompletionCondition ParseCourseWaveCompletionCondition(
    const std::string& text);
CourseWaveExecutionPolicy ParseCourseWaveExecutionPolicy(
    const std::string& text);

class CourseRuntime {
public:
    void Bind(const CourseAsset* asset);
    void Reset(float distance = 0.0f);
    std::vector<CourseEventMarker> Advance(float deltaTime, const RailPath& railPath);
    std::vector<CourseEventMarker> Advance(float deltaTime, const RailPath& railPath, float speedOverride);
    std::vector<CourseEventMarker> AdvanceClamped(
        float deltaTime,
        const RailPath& railPath,
        float speedOverride);

    // A gameplay finish can precede the scenery tail. Copies used by enemy
    // aim prediction retain this same authoritative stop.
    void SetTraversalEndDistance(float distance);
    float TraversalEndDistance(const RailPath& railPath) const;
    float Distance() const { return distance_; }
    const CourseSection* CurrentSection() const;

private:
    std::vector<CourseEventMarker> AdvanceInternal(
        float deltaTime,
        const RailPath& railPath,
        const float* speedOverride,
        bool loopAtEnd);

    const CourseAsset* asset_ = nullptr;
    float distance_ = 0.0f;
    size_t nextEventIndex_ = 0;
    float traversalEndDistance_ = -1.0f;
};
