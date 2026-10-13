#pragma once

#include "utils/math/Vector.h"
#include <cstdint>
#include <memory>

class RailPath;
class TerrainEditLayer;
struct TerrainGenerationSettings;

// CPU-only, immutable collision chunks. Their resolution is independent of
// render LOD. Worker jobs own their source snapshots; no GPU/readback is used.
class TerrainCollisionWorld final {
public:
    struct Hit {
        bool available = false;
        bool hit = false;
        bool nearSurface = false;
        float distance = 0.0f;
        Vector3 normal{};
        uint32_t nodesVisited = 0;
        uint32_t trianglesTested = 0;
    };
    struct Stats {
        uint32_t residentChunks = 0;
        uint32_t pendingBuilds = 0;
        uint64_t triangles = 0;
        uint64_t bytes = 0;
        uint64_t generation = 0;
    };

    TerrainCollisionWorld();
    ~TerrainCollisionWorld();
    TerrainCollisionWorld(const TerrainCollisionWorld&) = delete;
    TerrainCollisionWorld& operator=(const TerrainCollisionWorld&) = delete;

    void Update(const RailPath& rail, const TerrainGenerationSettings& settings,
        const TerrainEditLayer* edits, const TerrainEditLayer* preview, float focusDistance);
    bool Matches(const RailPath& rail, const TerrainGenerationSettings& settings,
        const TerrainEditLayer* edits, const TerrainEditLayer* preview) const;
    // Full rail-search coverage is required even for misses. A partially
    // resident mesh must never be interpreted as an unobstructed line of sight.
    Hit Query(const Vector3& origin, const Vector3& unitDirection, float maxDistance,
        float searchStart, float searchEnd) const;
    Stats GetStats() const;

    // Deterministic headless validation/benchmark path; gameplay uses Update.
    void BuildSynchronously(const RailPath& rail, const TerrainGenerationSettings& settings,
        const TerrainEditLayer* edits, const TerrainEditLayer* preview,
        float startDistance, float endDistance);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
