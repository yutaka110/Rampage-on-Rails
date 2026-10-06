#include "EditorBuiltinPlayIsolationProviders.h"

#include "EditorPlaySnapshot.h"
#include "EditorRuntimeChangeSet.h"
#include "../../course/CourseAsset.h"
#include "../../terrain/TerrainGenerationSettings.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace editor {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashValue(uint64_t hash, const std::string& value) {
    hash = HashValue(hash, value.size());
    return HashBytes(hash, value.data(), value.size());
}

uint64_t HashValue(uint64_t hash, const Vector3& value) {
    hash = HashValue(hash, value.x);
    hash = HashValue(hash, value.y);
    return HashValue(hash, value.z);
}

uint64_t HashValue(uint64_t hash, const Vector4& value) {
    hash = HashValue(hash, value.x);
    hash = HashValue(hash, value.y);
    hash = HashValue(hash, value.z);
    return HashValue(hash, value.w);
}

#define EDITOR_HASH_FIELD(value, field) hash = HashValue(hash, (value).field)

uint64_t HashTerrainSettings(uint64_t hash, const TerrainGenerationSettings& value) {
    EDITOR_HASH_FIELD(value, seed);
    EDITOR_HASH_FIELD(value, chunkLength);
    EDITOR_HASH_FIELD(value, visibleAheadChunks);
    EDITOR_HASH_FIELD(value, visibleBehindChunks);
    EDITOR_HASH_FIELD(value, corridorRadius);
    EDITOR_HASH_FIELD(value, canyonHalfWidth);
    EDITOR_HASH_FIELD(value, wallHeight);
    EDITOR_HASH_FIELD(value, noiseStrength);
    EDITOR_HASH_FIELD(value, volumeRoughness);
    EDITOR_HASH_FIELD(value, volumeArchScale);
    EDITOR_HASH_FIELD(value, sdfCarveDensity);
    EDITOR_HASH_FIELD(value, sdfCarveStrength);
    EDITOR_HASH_FIELD(value, sdfCarveScale);
    EDITOR_HASH_FIELD(value, openingSilhouetteStrength);
    EDITOR_HASH_FIELD(value, openingSilhouetteScale);
    EDITOR_HASH_FIELD(value, openCanyonStartDistance);
    EDITOR_HASH_FIELD(value, openCanyonTransitionLength);
    EDITOR_HASH_FIELD(value, openCanyonStrength);
    EDITOR_HASH_FIELD(value, openCanyonFarWallDistance);
    EDITOR_HASH_FIELD(value, openCanyonFarWallHeight);
    EDITOR_HASH_FIELD(value, openCanyonLayerSpread);
    EDITOR_HASH_FIELD(value, surfaceLongitudinalSteps);
    EDITOR_HASH_FIELD(value, surfaceRadialSegments);
    EDITOR_HASH_FIELD(value, lodNearDistance);
    EDITOR_HASH_FIELD(value, lodFarDistance);
    EDITOR_HASH_FIELD(value, rockPillarDensity);
    EDITOR_HASH_FIELD(value, rockScatterDensity);
    EDITOR_HASH_FIELD(value, rockScatterScale);
    EDITOR_HASH_FIELD(value, rockEmbedStrength);
    EDITOR_HASH_FIELD(value, rockContactPebbleDensity);
    EDITOR_HASH_FIELD(value, floorPebbleDensity);
    EDITOR_HASH_FIELD(value, rockClusterStrength);
    EDITOR_HASH_FIELD(value, rockRootShadowStrength);
    EDITOR_HASH_FIELD(value, rockMotherBlendStrength);
    EDITOR_HASH_FIELD(value, rockMaterialVariation);
    EDITOR_HASH_FIELD(value, motherRockErosionStrength);
    EDITOR_HASH_FIELD(value, largeScaleErosionStrength);
    EDITOR_HASH_FIELD(value, surfaceBreakupDensity);
    EDITOR_HASH_FIELD(value, archDensity);
    EDITOR_HASH_FIELD(value, dustZoneDensity);
    return hash;
}

template <typename T, typename HashElement>
uint64_t HashVector(uint64_t hash, const std::vector<T>& values, HashElement hashElement) {
    hash = HashValue(hash, values.size());
    for (const T& value : values) {
        hash = hashElement(hash, value);
    }
    return hash;
}

} // namespace

uint64_t EditorCourseAuthoringFingerprint(const CourseAsset& course) {
    uint64_t hash = HashValue(kFnvOffset, course.name);
    hash = HashVector(hash, course.railPoints, [](uint64_t hash, const RailPathControlPoint& value) {
        hash = HashValue(hash, value.position);
        hash = HashValue(hash, value.corridorRadius);
        return HashValue(hash, value.speed);
    });
    hash = HashVector(hash, course.cameraKeys, [](uint64_t hash, const CourseCameraKey& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, backDistance);
        EDITOR_HASH_FIELD(value, verticalOffset); EDITOR_HASH_FIELD(value, lateralOffset);
        EDITOR_HASH_FIELD(value, lookAheadDistance); EDITOR_HASH_FIELD(value, lookUpOffset);
        EDITOR_HASH_FIELD(value, lookForwardOffset); EDITOR_HASH_FIELD(value, fovY);
        EDITOR_HASH_FIELD(value, roll); return hash;
    });
    hash = HashVector(hash, course.sections, [](uint64_t hash, const CourseSection& value) {
        EDITOR_HASH_FIELD(value, startDistance); EDITOR_HASH_FIELD(value, endDistance);
        EDITOR_HASH_FIELD(value, name); EDITOR_HASH_FIELD(value, category); return hash;
    });
    hash = HashVector(hash, course.rideProfiles, [](uint64_t hash, const CourseRideProfileDefinition& value) {
        EDITOR_HASH_FIELD(value, editorGuid); EDITOR_HASH_FIELD(value, displayName);
        EDITOR_HASH_FIELD(value, startDistance); EDITOR_HASH_FIELD(value, endDistance);
        EDITOR_HASH_FIELD(value, speedMode); EDITOR_HASH_FIELD(value, speedMultiplier);
        EDITOR_HASH_FIELD(value, targetSpeedOverride); EDITOR_HASH_FIELD(value, turnAnticipationDistance);
        EDITOR_HASH_FIELD(value, visualBankScale); EDITOR_HASH_FIELD(value, maximumVisualBankDegrees);
        EDITOR_HASH_FIELD(value, blendInDistance); EDITOR_HASH_FIELD(value, blendOutDistance);
        EDITOR_HASH_FIELD(value, cameraShotId); EDITOR_HASH_FIELD(value, enabled);
        EDITOR_HASH_FIELD(value, editorVisible); EDITOR_HASH_FIELD(value, editorLocked); return hash;
    });
    hash = HashVector(hash, course.rideSpeedBeats, [](uint64_t hash, const RailRideSpeedBeatDefinition& value) {
        EDITOR_HASH_FIELD(value, editorGuid); EDITOR_HASH_FIELD(value, displayName);
        EDITOR_HASH_FIELD(value, startDistance); EDITOR_HASH_FIELD(value, endDistance);
        EDITOR_HASH_FIELD(value, type); EDITOR_HASH_FIELD(value, speedMultiplier);
        EDITOR_HASH_FIELD(value, targetSpeedOverride); EDITOR_HASH_FIELD(value, accelerationScale);
        EDITOR_HASH_FIELD(value, brakingScale); EDITOR_HASH_FIELD(value, maximumJerk);
        EDITOR_HASH_FIELD(value, blendInDistance); EDITOR_HASH_FIELD(value, blendOutDistance);
        EDITOR_HASH_FIELD(value, priority); EDITOR_HASH_FIELD(value, enabled);
        EDITOR_HASH_FIELD(value, editorVisible); EDITOR_HASH_FIELD(value, editorLocked); return hash;
    });
    hash = HashVector(hash, course.events, [](uint64_t hash, const CourseEventMarker& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, type);
        EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, payload); return hash;
    });
    hash = HashVector(hash, course.enemyPlacements, [](uint64_t hash, const CourseEnemyPlacement& value) {
        EDITOR_HASH_FIELD(value, editorGuid); EDITOR_HASH_FIELD(value, actorAssetId);
        EDITOR_HASH_FIELD(value, bulletPatternOverrideId); EDITOR_HASH_FIELD(value, waveGroupGuid);
        hash = HashValue(hash, value.railAnchor.segmentGuid);
        hash = HashValue(hash, value.railAnchor.normalizedT);
        hash = HashValue(hash, value.railAnchor.lateralOffset);
        hash = HashValue(hash, value.railAnchor.verticalOffset);
        hash = HashValue(hash, value.railAnchor.forwardOffset);
        EDITOR_HASH_FIELD(value, localRotation); EDITOR_HASH_FIELD(value, localScale);
        EDITOR_HASH_FIELD(value, activationLeadDistance); EDITOR_HASH_FIELD(value, enabled);
        EDITOR_HASH_FIELD(value, editorVisible); EDITOR_HASH_FIELD(value, editorLocked);
        return hash;
    });
    hash = HashVector(hash, course.terrainPlacements, [](uint64_t hash, const CourseTerrainPlacement& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, layer); EDITOR_HASH_FIELD(value, id);
        EDITOR_HASH_FIELD(value, meshId); EDITOR_HASH_FIELD(value, lateralOffset);
        EDITOR_HASH_FIELD(value, verticalOffset); EDITOR_HASH_FIELD(value, forwardOffset);
        EDITOR_HASH_FIELD(value, scale); EDITOR_HASH_FIELD(value, rotation);
        EDITOR_HASH_FIELD(value, collisionMode); EDITOR_HASH_FIELD(value, renderPriority);
        EDITOR_HASH_FIELD(value, cullBehindDistance); EDITOR_HASH_FIELD(value, cullAheadDistance); return hash;
    });
    hash = HashVector(hash, course.rockClusters, [](uint64_t hash, const CourseRockCluster& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, meshId);
        EDITOR_HASH_FIELD(value, anchor); EDITOR_HASH_FIELD(value, type); EDITOR_HASH_FIELD(value, count);
        EDITOR_HASH_FIELD(value, minScale); EDITOR_HASH_FIELD(value, maxScale); EDITOR_HASH_FIELD(value, spread);
        EDITOR_HASH_FIELD(value, rotation); EDITOR_HASH_FIELD(value, clearLaneRadius);
        EDITOR_HASH_FIELD(value, cullBehindDistance); EDITOR_HASH_FIELD(value, cullAheadDistance);
        hash = HashVector(hash, value.instanceOverrides, [](uint64_t nested, const CourseRockCluster::InstanceTransformOverride& item) {
            nested = HashValue(nested, item.index); nested = HashValue(nested, item.localOffset);
            nested = HashValue(nested, item.scale); return HashValue(nested, item.rotation);
        });
        return hash;
    });
    hash = HashVector(hash, course.lightingPresets, [](uint64_t hash, const CourseLightingPreset& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, blendDistance);
        EDITOR_HASH_FIELD(value, sunColor); EDITOR_HASH_FIELD(value, sunDirection); EDITOR_HASH_FIELD(value, sunIntensity);
        EDITOR_HASH_FIELD(value, clearColor); EDITOR_HASH_FIELD(value, fogColor); EDITOR_HASH_FIELD(value, fogIntensity);
        EDITOR_HASH_FIELD(value, fogStart); EDITOR_HASH_FIELD(value, fogEnd); EDITOR_HASH_FIELD(value, fogDensity);
        EDITOR_HASH_FIELD(value, backlitFogLift); EDITOR_HASH_FIELD(value, openingGlowStrength);
        EDITOR_HASH_FIELD(value, foregroundSilhouetteStrength); EDITOR_HASH_FIELD(value, lowFogLayerStrength);
        EDITOR_HASH_FIELD(value, coolFloorHazeStrength); return hash;
    });
    hash = HashVector(hash, course.cameraShotPresets, [](uint64_t hash, const CourseCameraShotPreset& value) {
        EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, mode); EDITOR_HASH_FIELD(value, backDistanceOffset);
        EDITOR_HASH_FIELD(value, verticalOffset); EDITOR_HASH_FIELD(value, lateralOffset); EDITOR_HASH_FIELD(value, lookAheadOffset);
        EDITOR_HASH_FIELD(value, lookUpOffset); EDITOR_HASH_FIELD(value, lookForwardOffset); EDITOR_HASH_FIELD(value, fovOffset);
        EDITOR_HASH_FIELD(value, rollOffset); EDITOR_HASH_FIELD(value, shakeAmount); return hash;
    });
    hash = HashVector(hash, course.cameraBlendAssets, [](uint64_t hash, const CourseCameraBlendAsset& value) {
        EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, blendInDistance); EDITOR_HASH_FIELD(value, blendOutDistance);
        EDITOR_HASH_FIELD(value, curve); EDITOR_HASH_FIELD(value, weightScale); return hash;
    });
    hash = HashVector(hash, course.cinematicCameraShots, [](uint64_t hash, const CourseCinematicCameraShot& value) {
        EDITOR_HASH_FIELD(value, startDistance); EDITOR_HASH_FIELD(value, endDistance); EDITOR_HASH_FIELD(value, id);
        EDITOR_HASH_FIELD(value, mode); EDITOR_HASH_FIELD(value, presetId); EDITOR_HASH_FIELD(value, blendAssetId);
        EDITOR_HASH_FIELD(value, blendInDistance); EDITOR_HASH_FIELD(value, blendOutDistance); EDITOR_HASH_FIELD(value, weightScale);
        EDITOR_HASH_FIELD(value, backDistanceOffset); EDITOR_HASH_FIELD(value, verticalOffset); EDITOR_HASH_FIELD(value, lateralOffset);
        EDITOR_HASH_FIELD(value, lookAheadOffset); EDITOR_HASH_FIELD(value, lookUpOffset); EDITOR_HASH_FIELD(value, lookForwardOffset);
        EDITOR_HASH_FIELD(value, fovOffset); EDITOR_HASH_FIELD(value, rollOffset); EDITOR_HASH_FIELD(value, shakeAmount); return hash;
    });
    hash = HashVector(hash, course.terrainMaterialPresets, [](uint64_t hash, const CourseTerrainMaterialPreset& value) {
        EDITOR_HASH_FIELD(value, distance); EDITOR_HASH_FIELD(value, id); EDITOR_HASH_FIELD(value, blendDistance);
        EDITOR_HASH_FIELD(value, baseColor); EDITOR_HASH_FIELD(value, brightness); EDITOR_HASH_FIELD(value, noiseStrength);
        EDITOR_HASH_FIELD(value, strataStrength); EDITOR_HASH_FIELD(value, strataBreakupStrength);
        EDITOR_HASH_FIELD(value, specularStrength); EDITOR_HASH_FIELD(value, rimLightStrength);
        EDITOR_HASH_FIELD(value, backlightRimBoost); EDITOR_HASH_FIELD(value, floorSandShadowStrength);
        EDITOR_HASH_FIELD(value, detailNormalStrength); EDITOR_HASH_FIELD(value, microDetailStrength);
        EDITOR_HASH_FIELD(value, cavityAoStrength); EDITOR_HASH_FIELD(value, skyFillStrength); return hash;
    });
    hash = HashVector(hash, course.cinematicShotSets, [](uint64_t hash, const CourseCinematicShotSet& value) {
        EDITOR_HASH_FIELD(value, startDistance); EDITOR_HASH_FIELD(value, endDistance); EDITOR_HASH_FIELD(value, id);
        EDITOR_HASH_FIELD(value, label);
        hash = HashVector(hash, value.heroLandmarkIds, [](uint64_t nested, const std::string& item) { return HashValue(nested, item); });
        hash = HashVector(hash, value.vistaLandmarkIds, [](uint64_t nested, const std::string& item) { return HashValue(nested, item); });
        hash = HashValue(hash, value.lightingPresetId); hash = HashValue(hash, value.cameraShotId);
        hash = HashValue(hash, value.terrainMaterialId); hash = HashValue(hash, value.fogMood);
        return HashValue(hash, value.compositionNotes);
    });
    return hash;
}

uint64_t EditorTerrainAuthoringFingerprint(const TerrainAuthoringState& value) {
    uint64_t hash = kFnvOffset;
#define EDITOR_HASH_TERRAIN(field) EDITOR_HASH_FIELD(value, field)
    EDITOR_HASH_TERRAIN(enabled); EDITOR_HASH_TERRAIN(autoAdvancePreview); EDITOR_HASH_TERRAIN(showDebugDraw);
    EDITOR_HASH_TERRAIN(showRailPath); EDITOR_HASH_TERRAIN(showCorridor); EDITOR_HASH_TERRAIN(showChunks);
    EDITOR_HASH_TERRAIN(showSpawnCandidates); EDITOR_HASH_TERRAIN(showRockScatter); EDITOR_HASH_TERRAIN(showVfxZones);
    EDITOR_HASH_TERRAIN(showVolumeSlice); EDITOR_HASH_TERRAIN(showSdfSamples); EDITOR_HASH_TERRAIN(showCascadeBounds);
    EDITOR_HASH_TERRAIN(showShadowDebugView); EDITOR_HASH_TERRAIN(showHiZDebugPreview); EDITOR_HASH_TERRAIN(enableDebrisRendering);
    EDITOR_HASH_TERRAIN(showCourseObjectFrame); EDITOR_HASH_TERRAIN(enableCourseObjectViewportEditing);
    EDITOR_HASH_TERRAIN(freezeCourseRuntime); EDITOR_HASH_TERRAIN(courseObjectAuthoringInputLocked);
    EDITOR_HASH_TERRAIN(courseObjectSnapEnabled); EDITOR_HASH_TERRAIN(courseObjectFrameDepthTest);
    EDITOR_HASH_TERRAIN(courseObjectSelectionType); EDITOR_HASH_TERRAIN(selectedCourseTerrainPlacement);
    EDITOR_HASH_TERRAIN(selectedCourseRockCluster); EDITOR_HASH_TERRAIN(courseObjectGizmoMode);
    EDITOR_HASH_TERRAIN(courseObjectActiveAxis); EDITOR_HASH_TERRAIN(courseObjectGizmoSpace);
    EDITOR_HASH_TERRAIN(courseObjectPivotMode);
    hash = HashVector(hash, value.selectedCourseTerrainPlacements,
        [](uint64_t nested, int item) { return HashValue(nested, item); });
    hash = HashVector(hash, value.selectedCourseRockClusters,
        [](uint64_t nested, int item) { return HashValue(nested, item); });
    EDITOR_HASH_TERRAIN(courseObjectFramePadding);
    EDITOR_HASH_TERRAIN(courseObjectMoveSensitivity); EDITOR_HASH_TERRAIN(courseObjectScaleSensitivity);
    EDITOR_HASH_TERRAIN(courseObjectRotateSensitivity); EDITOR_HASH_TERRAIN(courseObjectMoveSnap);
    EDITOR_HASH_TERRAIN(courseObjectScaleSnap); EDITOR_HASH_TERRAIN(courseObjectRotateSnapDegrees);
    EDITOR_HASH_TERRAIN(courseObjectEditRevision); EDITOR_HASH_TERRAIN(courseObjectUndoRequested);
    EDITOR_HASH_TERRAIN(courseObjectRedoRequested); EDITOR_HASH_TERRAIN(courseObjectUndoDepth);
    EDITOR_HASH_TERRAIN(courseObjectRedoDepth); EDITOR_HASH_TERRAIN(autoReloadPreset);
    EDITOR_HASH_TERRAIN(requestSavePreset); EDITOR_HASH_TERRAIN(requestLoadPreset); EDITOR_HASH_TERRAIN(requestReloadPreset);
    EDITOR_HASH_TERRAIN(displayMode); EDITOR_HASH_TERRAIN(materialBaseColor); EDITOR_HASH_TERRAIN(materialBrightness);
    EDITOR_HASH_TERRAIN(materialNoiseStrength); EDITOR_HASH_TERRAIN(materialStrataStrength);
    EDITOR_HASH_TERRAIN(materialStrataBreakupStrength); EDITOR_HASH_TERRAIN(materialSpecularStrength);
    EDITOR_HASH_TERRAIN(materialRimLightStrength); EDITOR_HASH_TERRAIN(materialBacklightRimBoost);
    EDITOR_HASH_TERRAIN(materialFloorSandShadowStrength); EDITOR_HASH_TERRAIN(materialDetailNormalStrength);
    EDITOR_HASH_TERRAIN(materialMicroDetailStrength); EDITOR_HASH_TERRAIN(useDetailTextureCache);
    EDITOR_HASH_TERRAIN(materialDetailCacheScale); EDITOR_HASH_TERRAIN(materialDetailTileWorldSize);
    EDITOR_HASH_TERRAIN(materialDetailNearScale); EDITOR_HASH_TERRAIN(materialDetailFarScale);
    EDITOR_HASH_TERRAIN(materialDetailDistanceBlend); EDITOR_HASH_TERRAIN(useDetailNormalMap);
    EDITOR_HASH_TERRAIN(materialDetailNormalMapStrength); EDITOR_HASH_TERRAIN(materialDetailHybridBlend);
    EDITOR_HASH_TERRAIN(invertDetailNormalY); EDITOR_HASH_TERRAIN(materialCavityAoStrength);
    EDITOR_HASH_TERRAIN(materialSkyFillStrength); EDITOR_HASH_TERRAIN(useCanyonSunLighting);
    EDITOR_HASH_TERRAIN(canyonSunColor); EDITOR_HASH_TERRAIN(canyonSunDirection); EDITOR_HASH_TERRAIN(canyonSunIntensity);
    EDITOR_HASH_TERRAIN(cascadeShadowEnabled); EDITOR_HASH_TERRAIN(cascadeShadowBias);
    EDITOR_HASH_TERRAIN(cascadeShadowStrength); EDITOR_HASH_TERRAIN(cascadeShadowSplit0);
    EDITOR_HASH_TERRAIN(cascadeShadowSplit1); EDITOR_HASH_TERRAIN(cascadeShadowSplit2);
    EDITOR_HASH_TERRAIN(cascadeShadowSplit3); EDITOR_HASH_TERRAIN(shadowDebugCascade);
    EDITOR_HASH_TERRAIN(hiZDebugMip); EDITOR_HASH_TERRAIN(debrisOcclusionMip);
    EDITOR_HASH_TERRAIN(debrisOcclusionStrength); EDITOR_HASH_TERRAIN(debrisOcclusionDepthBias);
    EDITOR_HASH_TERRAIN(debrisOcclusionUpdateInterval); EDITOR_HASH_TERRAIN(previewDistance); EDITOR_HASH_TERRAIN(previewSpeed);
    hash = HashTerrainSettings(hash, value.settings);
#undef EDITOR_HASH_TERRAIN
    return hash;
}

uint64_t EditorVfxAuthoringFingerprint(const EffectRuntime& runtime) {
    uint64_t hash = kFnvOffset;
    std::vector<std::string> names;
    names.reserve(runtime.Assets().size());
    for (const auto& [name, asset] : runtime.Assets()) names.push_back(name);
    std::sort(names.begin(), names.end());
    hash = HashValue(hash, names.size());
    for (const std::string& name : names) {
        const EffectAsset& asset = runtime.Assets().at(name);
        hash = HashValue(hash, name);
        hash = HashValue(hash, asset.name);
        hash = HashValue(hash, asset.shader);
        hash = HashValue(hash, asset.texture);
        hash = HashValue(hash, asset.lifetime);
        hash = HashValue(hash, asset.defaultParticle.lifetime);
        hash = HashValue(hash, asset.defaultParticle.spawnFrequency);
        hash = HashValue(hash, asset.defaultParticle.uvScrollSpeed);
        hash = HashValue(hash, asset.color);
    }
    return hash;
}

uint64_t EditorPostProcessAuthoringFingerprint(const PostProcessStack& stack) {
    uint64_t hash = HashValue(kFnvOffset, stack.Passes().size());
    for (const PostProcessPass& pass : stack.Passes()) {
        hash = HashValue(hash, pass.name);
        hash = HashValue(hash, pass.inputResource);
        hash = HashValue(hash, pass.outputResource);
        hash = HashValue(hash, pass.pipeline);
        hash = HashValue(hash, pass.secondaryInputResource);
        hash = HashValue(hash, pass.tertiaryInputResource);
        hash = HashValue(hash, pass.enabled);
        hash = HashValue(hash, pass.intensity);
        hash = HashValue(hash, pass.resolutionScale);
        hash = HashBytes(hash, &pass.parameters, sizeof(pass.parameters));
    }
    return hash;
}

bool EditorCoursePlayIsolationProvider::Capture(EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (course_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Course is unavailable for Play isolation capture.");
        return false;
    }
    return snapshot.Store(std::string(Id()), *course_, AuthoringFingerprint(), error);
}

bool EditorCoursePlayIsolationProvider::Restore(const EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (course_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Course is unavailable for Play isolation restore.");
        return false;
    }
    const CourseAsset* captured = snapshot.Read<CourseAsset>(Id(), error);
    if (captured == nullptr) return false;
    *course_ = *captured;
    ClearEditorError(error);
    return true;
}

bool EditorCoursePlayIsolationProvider::BuildRuntimeChangeSet(
    const EditorPlaySnapshot& snapshot, EditorRuntimeChangeSet& changes, EditorError* error) const {
    const EditorPlaySnapshotEntry* entry = snapshot.Find(Id());
    if (entry == nullptr || course_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Course Play snapshot is unavailable.");
        return false;
    }
    changes.Add(EditorRuntimeChange{
        std::string(Id()), "course.authoring", "Course authoring changes",
        entry->authoringFingerprint, AuthoringFingerprint(), true});
    ClearEditorError(error);
    return true;
}

uint64_t EditorCoursePlayIsolationProvider::AuthoringFingerprint() const {
    return course_ == nullptr ? 0 : EditorCourseAuthoringFingerprint(*course_);
}

bool EditorTerrainPlayIsolationProvider::Capture(EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (terrain_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Terrain is unavailable for Play isolation capture.");
        return false;
    }
    return snapshot.Store(std::string(Id()), *terrain_, AuthoringFingerprint(), error);
}

bool EditorTerrainPlayIsolationProvider::Restore(const EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (terrain_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Terrain is unavailable for Play isolation restore.");
        return false;
    }
    const TerrainAuthoringState* captured = snapshot.Read<TerrainAuthoringState>(Id(), error);
    if (captured == nullptr) return false;
    *terrain_ = *captured;
    ClearEditorError(error);
    return true;
}

bool EditorTerrainPlayIsolationProvider::BuildRuntimeChangeSet(
    const EditorPlaySnapshot& snapshot, EditorRuntimeChangeSet& changes, EditorError* error) const {
    const EditorPlaySnapshotEntry* entry = snapshot.Find(Id());
    if (entry == nullptr || terrain_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Terrain Play snapshot is unavailable.");
        return false;
    }
    changes.Add(EditorRuntimeChange{
        std::string(Id()), "terrain.authoring", "Terrain authoring changes",
        entry->authoringFingerprint, AuthoringFingerprint(), true});
    ClearEditorError(error);
    return true;
}

uint64_t EditorTerrainPlayIsolationProvider::AuthoringFingerprint() const {
    return terrain_ == nullptr ? 0 : EditorTerrainAuthoringFingerprint(*terrain_);
}

bool EditorVfxPlayIsolationProvider::Capture(EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (runtime_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "VFX runtime is unavailable for authoring isolation.");
        return false;
    }
    return snapshot.Store(std::string(Id()), runtime_->Assets(), AuthoringFingerprint(), error);
}

bool EditorVfxPlayIsolationProvider::Restore(const EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (runtime_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "VFX runtime is unavailable for authoring restore.");
        return false;
    }
    const EditorVfxAuthoringSnapshot* captured = snapshot.Read<EditorVfxAuthoringSnapshot>(Id(), error);
    if (captured == nullptr) return false;
    std::string validationError;
    if (!runtime_->ReplaceAssets(*captured, &validationError)) {
        SetEditorError(error, EditorErrorCode::NotAvailable, validationError); return false;
    }
    runtime_->ClearInstances();
    ClearEditorError(error);
    return true;
}

bool EditorVfxPlayIsolationProvider::BuildRuntimeChangeSet(
    const EditorPlaySnapshot& snapshot, EditorRuntimeChangeSet& changes, EditorError* error) const {
    const EditorPlaySnapshotEntry* entry = snapshot.Find(Id());
    if (entry == nullptr || runtime_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "VFX authoring Play snapshot is unavailable.");
        return false;
    }
    changes.Add(EditorRuntimeChange{std::string(Id()), "vfx.assets", "VFX asset changes",
        entry->authoringFingerprint, AuthoringFingerprint(), true});
    ClearEditorError(error);
    return true;
}

uint64_t EditorVfxPlayIsolationProvider::AuthoringFingerprint() const {
    return runtime_ == nullptr ? 0 : EditorVfxAuthoringFingerprint(*runtime_);
}

bool EditorPostProcessPlayIsolationProvider::Capture(EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (stack_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Post-process stack is unavailable for isolation.");
        return false;
    }
    return snapshot.Store(std::string(Id()), stack_->Passes(), AuthoringFingerprint(), error);
}

bool EditorPostProcessPlayIsolationProvider::Restore(const EditorPlaySnapshot& snapshot, EditorError* error) const {
    if (stack_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Post-process stack is unavailable for restore.");
        return false;
    }
    const EditorPostProcessAuthoringSnapshot* captured = snapshot.Read<EditorPostProcessAuthoringSnapshot>(Id(), error);
    if (captured == nullptr) return false;
    std::string validationError;
    if (!stack_->ReplacePasses(*captured, &validationError)) {
        SetEditorError(error, EditorErrorCode::NotAvailable, validationError); return false;
    }
    ClearEditorError(error);
    return true;
}

bool EditorPostProcessPlayIsolationProvider::BuildRuntimeChangeSet(
    const EditorPlaySnapshot& snapshot, EditorRuntimeChangeSet& changes, EditorError* error) const {
    const EditorPlaySnapshotEntry* entry = snapshot.Find(Id());
    if (entry == nullptr || stack_ == nullptr) {
        SetEditorError(error, EditorErrorCode::NotAvailable, "Post-process Play snapshot is unavailable.");
        return false;
    }
    changes.Add(EditorRuntimeChange{std::string(Id()), "postProcess.stack", "Post-process changes",
        entry->authoringFingerprint, AuthoringFingerprint(), true});
    ClearEditorError(error);
    return true;
}

uint64_t EditorPostProcessPlayIsolationProvider::AuthoringFingerprint() const {
    return stack_ == nullptr ? 0 : EditorPostProcessAuthoringFingerprint(*stack_);
}

#undef EDITOR_HASH_FIELD

} // namespace editor
