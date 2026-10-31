#pragma once

#include <cstdint>
#include <vector>

#include "utils/math/Vector.h"
#include "TerrainEditLayer.h"

enum class TerrainDisplayMode : uint32_t {
    Lit = 0,
    Unlit = 1,
    Wireframe = 2,
    Debug = 3,
    DetailNormal = 4,
};

struct TerrainGenerationSettings {
    uint32_t seed = 1337;
    float chunkLength = 80.0f;
    uint32_t visibleAheadChunks = 8;
    uint32_t visibleBehindChunks = 2;
    // Transient render budget: surface-only chunks beyond the detailed ring.
    // Collision resolution and authored terrain parameters are unchanged.
    uint32_t renderHorizonAheadChunks = 0;
    float corridorRadius = 18.0f;
    float canyonHalfWidth = 42.0f;
    float wallHeight = 35.0f;
    float noiseStrength = 14.0f;
    float volumeRoughness = 0.42f;
    float volumeArchScale = 0.55f;
    float sdfCarveDensity = 0.48f;
    float sdfCarveStrength = 0.42f;
    float sdfCarveScale = 1.0f;
    float openingSilhouetteStrength = 0.72f;
    float openingSilhouetteScale = 1.0f;
    float openCanyonStartDistance = 360.0f;
    float openCanyonTransitionLength = 240.0f;
    float openCanyonStrength = 0.92f;
    float openCanyonFarWallDistance = 400.0f;
    float openCanyonFarWallHeight = 420.0f;
    float openCanyonLayerSpread = 1.18f;
    uint32_t surfaceLongitudinalSteps = 36;
    uint32_t surfaceRadialSegments = 56;
    float lodNearDistance = 180.0f;
    float lodFarDistance = 460.0f;
    float rockPillarDensity = 0.35f;
    float rockScatterDensity = 0.16f;
    float rockScatterScale = 1.0f;
    float rockEmbedStrength = 0.78f;
    float rockContactPebbleDensity = 0.34f;
    float floorPebbleDensity = 0.24f;
    float rockClusterStrength = 0.38f;
    float rockRootShadowStrength = 0.78f;
    float rockMotherBlendStrength = 0.68f;
    float rockMaterialVariation = 0.55f;
    float motherRockErosionStrength = 0.68f;
    float largeScaleErosionStrength = 0.72f;
    float surfaceBreakupDensity = 0.58f;
    float archDensity = 0.25f;
    float dustZoneDensity = 0.45f;
};

struct TerrainAuthoringState {
    TerrainEditLayer previewEditLayer{};
    TerrainEditDirtyRegion lastEditDirtyRegion{};
    bool enabled = true;
    bool autoAdvancePreview = false;
    bool showDebugDraw = false;
    bool showRailPath = true;
    bool showCorridor = false;
    bool showChunks = false;
    bool showSpawnCandidates = false;
    bool showRockScatter = false;
    bool showVfxZones = false;
    bool showVolumeSlice = false;
    bool showSdfSamples = false;
    bool showCascadeBounds = false;
    bool showShadowDebugView = true;
    bool showHiZDebugPreview = false;
    bool enableDebrisRendering = true;
    bool showCourseObjectFrame = true;
    bool enableCourseObjectViewportEditing = true;
    bool freezeCourseRuntime = false;
    bool courseObjectAuthoringInputLocked = false;
    bool courseObjectSnapEnabled = false;
    bool courseObjectFrameDepthTest = false;
    int courseObjectSelectionType = 0;
    int selectedCourseTerrainPlacement = -1;
    int selectedCourseRockCluster = -1;
    int courseObjectGizmoMode = 0;
    int courseObjectActiveAxis = -1;
    int courseObjectGizmoSpace = 1;
    int courseObjectPivotMode = 0;
    std::vector<int> selectedCourseTerrainPlacements{};
    std::vector<int> selectedCourseRockClusters{};
    float courseObjectFramePadding = 1.12f;
    float courseObjectMoveSensitivity = 0.08f;
    float courseObjectScaleSensitivity = 0.012f;
    float courseObjectRotateSensitivity = 0.006f;
    float courseObjectMoveSnap = 0.5f;
    float courseObjectScaleSnap = 0.05f;
    float courseObjectRotateSnapDegrees = 5.0f;
    uint32_t courseObjectEditRevision = 0;
    bool courseObjectUndoRequested = false;
    bool courseObjectRedoRequested = false;
    uint32_t courseObjectUndoDepth = 0;
    uint32_t courseObjectRedoDepth = 0;
    bool autoReloadPreset = false;
    bool requestSavePreset = false;
    bool requestLoadPreset = false;
    bool requestReloadPreset = false;
    TerrainDisplayMode displayMode = TerrainDisplayMode::Lit;
    Vector4 materialBaseColor = {1.02f, 0.96f, 0.90f, 1.0f};
    float materialBrightness = 1.0f;
    float materialNoiseStrength = 1.0f;
    float materialStrataStrength = 1.0f;
    float materialStrataBreakupStrength = 0.92f;
    float materialSpecularStrength = 0.135f;
    float materialRimLightStrength = 0.84f;
    float materialBacklightRimBoost = 0.72f;
    float materialFloorSandShadowStrength = 0.38f;
    float materialDetailNormalStrength = 1.18f;
    float materialMicroDetailStrength = 1.16f;
    bool useDetailTextureCache = true;
    float materialDetailCacheScale = 1.35f;
    float materialDetailTileWorldSize = 72.0f;
    float materialDetailNearScale = 1.75f;
    float materialDetailFarScale = 0.72f;
    float materialDetailDistanceBlend = 180.0f;
    bool useDetailNormalMap = true;
    float materialDetailNormalMapStrength = 0.82f;
    float materialDetailHybridBlend = 0.66f;
    bool invertDetailNormalY = false;
    float materialCavityAoStrength = 0.82f;
    float materialSkyFillStrength = 0.30f;
    bool useCanyonSunLighting = true;
    Vector4 canyonSunColor = {1.0f, 0.74f, 0.46f, 1.0f};
    Vector3 canyonSunDirection = {-0.38f, -0.52f, 0.76f};
    float canyonSunIntensity = 2.4f;
    bool cascadeShadowEnabled = true;
    float cascadeShadowBias = 0.0018f;
    float cascadeShadowStrength = 0.68f;
    float cascadeShadowSplit0 = 120.0f;
    float cascadeShadowSplit1 = 260.0f;
    float cascadeShadowSplit2 = 520.0f;
    float cascadeShadowSplit3 = 960.0f;
    int shadowDebugCascade = 0;
    int hiZDebugMip = 3;
    int debrisOcclusionMip = 2;
    float debrisOcclusionStrength = 0.75f;
    float debrisOcclusionDepthBias = 0.012f;
    uint32_t debrisOcclusionUpdateInterval = 4;
    float previewDistance = 0.0f;
    float previewSpeed = 32.0f;
    TerrainGenerationSettings settings{};
};
