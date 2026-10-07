#pragma once

#include "RailAimState.h"

class RailPath;
class CourseSpawnRuntime;
struct CourseAsset;
struct TerrainGenerationSettings;
class TerrainEditLayer;

// Read-only scene inputs used to resolve the authoritative aim ray. All hit
// candidates are compared in world-ray distance, so the result also provides
// deterministic weapon occlusion.
struct RailWorldRaycastInput {
    const RailAimState* aim = nullptr;
    const RailPath* railPath = nullptr;
    const CourseSpawnRuntime* spawnRuntime = nullptr;
    const CourseAsset* course = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    const TerrainEditLayer* terrainPreview = nullptr;
    float playerDistance = 0.0f;
    float collisionPadding = 0.0f;
    bool includeProceduralTerrain = true;
    // Visibility-only query: solid decorative columns can hide a warning,
    // without becoming weapon/projectile collision surfaces.
    bool includeVisualColumns = false;
};

class RailWorldRaycast {
public:
    // Main-thread gameplay update lifetime. Nested scopes share the current
    // frame; callers without a scope keep the uncached reference behavior.
    // Only procedural terrain is memoized. Moving actors/placements are always
    // queried live, and rail/settings/edit changes invalidate terrain results.
    class FrameCacheScope final {
    public:
        FrameCacheScope();
        ~FrameCacheScope();
        FrameCacheScope(const FrameCacheScope&) = delete;
        FrameCacheScope& operator=(const FrameCacheScope&) = delete;
    };
    struct FrameCacheStats {
        uint64_t terrainResultHits = 0;
        uint64_t railSampleHits = 0;
        uint64_t railEvaluations = 0;
    };
    static FrameCacheStats CacheStats();
    static RailAimHit Query(const RailWorldRaycastInput& input);
};
