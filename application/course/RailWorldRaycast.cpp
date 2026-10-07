#include "RailWorldRaycast.h"
#include "../diagnostics/DronePerformanceProfile.h"

#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"
#include "../terrain/RailPath.h"
#include "../terrain/TerrainGenerationSettings.h"
#include "../terrain/TerrainVolumeField.h"
#include "utils/math/MathUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr float kEpsilon = 0.00001f;
constexpr float kTieEpsilon = 0.0001f;

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float LengthSquared(const Vector3& value) {
    return Dot(value, value);
}

bool Finite(float value) {
    return std::isfinite(value);
}

bool Finite(const Vector3& value) {
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float lengthSquared = LengthSquared(value);
    if (!Finite(lengthSquared) || lengthSquared <= kEpsilon * kEpsilon) {
        return fallback;
    }
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

Vector3 FaceAgainstRay(const Vector3& normal, const Vector3& rayDirection) {
    Vector3 result = NormalizeOr(normal, Scale(rayDirection, -1.0f));
    if (Dot(result, rayDirection) > 0.0f) {
        result = Scale(result, -1.0f);
    }
    return result;
}

Vector3 ResolveRailLocal(
    const RailPath& railPath,
    float distance,
    float lateral,
    float vertical) {
    const RailPathSample sample = railPath.Evaluate(distance);
    return Add(sample.position, Add(Scale(sample.right, lateral), Scale(sample.up, vertical)));
}

Vector3 RotationFromRailTangent(const Vector3& tangent) {
    const float yaw = std::atan2(tangent.x, tangent.z);
    const float pitch = std::asin((std::clamp)(-tangent.y, -1.0f, 1.0f));
    return {pitch, yaw, 0.0f};
}

std::array<Vector3, 3> AxesFromEuler(const Vector3& rotation) {
    const Matrix4x4 matrix = MakeAffineMatrix(
        {1.0f, 1.0f, 1.0f}, rotation, {0.0f, 0.0f, 0.0f});
    return {
        NormalizeOr({matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]}, {1.0f, 0.0f, 0.0f}),
        NormalizeOr({matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]}, {0.0f, 1.0f, 0.0f}),
        NormalizeOr({matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]}, {0.0f, 0.0f, 1.0f})};
}

bool RaySphere(
    const Vector3& origin,
    const Vector3& direction,
    float maxDistance,
    const Vector3& center,
    float radius,
    float& outDistance,
    Vector3& outNormal) {
    const Vector3 offset = Subtract(origin, center);
    const float b = Dot(offset, direction);
    const float c = Dot(offset, offset) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    const float root = std::sqrt((std::max)(discriminant, 0.0f));
    float distance = -b - root;
    if (distance < 0.0f) {
        distance = -b + root;
    }
    if (distance < 0.0f || distance > maxDistance) {
        return false;
    }
    const Vector3 point = Add(origin, Scale(direction, distance));
    outDistance = distance;
    outNormal = FaceAgainstRay(Subtract(point, center), direction);
    return true;
}

bool RayObb(
    const Vector3& origin,
    const Vector3& direction,
    float maxDistance,
    const Vector3& center,
    const std::array<Vector3, 3>& axes,
    const Vector3& halfExtents,
    float& outDistance,
    Vector3& outNormal) {
    const Vector3 relativeOrigin = Subtract(origin, center);
    const float extents[3] = {
        (std::max)(std::abs(halfExtents.x), 0.01f),
        (std::max)(std::abs(halfExtents.y), 0.01f),
        (std::max)(std::abs(halfExtents.z), 0.01f)};
    float nearDistance = 0.0f;
    float farDistance = maxDistance;
    Vector3 nearNormal = Scale(direction, -1.0f);

    for (uint32_t axisIndex = 0; axisIndex < 3; ++axisIndex) {
        const float originOnAxis = Dot(relativeOrigin, axes[axisIndex]);
        const float directionOnAxis = Dot(direction, axes[axisIndex]);
        if (std::abs(directionOnAxis) <= kEpsilon) {
            if (originOnAxis < -extents[axisIndex] ||
                originOnAxis > extents[axisIndex]) {
                return false;
            }
            continue;
        }

        float first = (-extents[axisIndex] - originOnAxis) / directionOnAxis;
        float second = (extents[axisIndex] - originOnAxis) / directionOnAxis;
        Vector3 firstNormal = Scale(axes[axisIndex], -1.0f);
        Vector3 secondNormal = axes[axisIndex];
        if (first > second) {
            std::swap(first, second);
            std::swap(firstNormal, secondNormal);
        }
        if (first > nearDistance) {
            nearDistance = first;
            nearNormal = firstNormal;
        }
        farDistance = (std::min)(farDistance, second);
        if (nearDistance > farDistance) {
            return false;
        }
    }

    if (farDistance < 0.0f || nearDistance > maxDistance) {
        return false;
    }
    outDistance = (std::max)(nearDistance, 0.0f);
    outNormal = FaceAgainstRay(nearNormal, direction);
    return true;
}

struct TerrainLocalPoint {
    float distance = 0.0f;
    float lateral = 0.0f;
    float vertical = 0.0f;
};

bool SameVector(const Vector3& a, const Vector3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

std::array<float, 12> RailGeometryKey(const RailPathControlPoint& point) {
    return {point.position.x, point.position.y, point.position.z,
        point.corridorRadius, point.speed, static_cast<float>(point.tangentMode),
        point.incomingTangent.x, point.incomingTangent.y, point.incomingTangent.z,
        point.outgoingTangent.x, point.outgoingTangent.y, point.outgoingTangent.z};
}

struct EditGeometryKey {
    TerrainEditOperation operation{};
    uint32_t materialLayer = 0;
    std::array<float, 6> values{};
    std::string strokeGuid;
    bool operator==(const EditGeometryKey&) const = default;
};

EditGeometryKey EditKey(const TerrainBrushStamp& stamp) {
    return {stamp.operation, stamp.materialLayer,
        {stamp.distance, stamp.angle, stamp.radius, stamp.surfaceRadius,
         stamp.strength, stamp.hardness}, stamp.strokeGuid};
}

bool MatchesEditKey(const EditGeometryKey& key, const TerrainBrushStamp& stamp) {
    // No GUID/string allocation on the per-query validation path.
    return key.operation == stamp.operation && key.materialLayer == stamp.materialLayer &&
        key.strokeGuid == stamp.strokeGuid && key.values == std::array<float, 6>{
            stamp.distance, stamp.angle, stamp.radius, stamp.surfaceRadius,
            stamp.strength, stamp.hardness};
}

struct EditSnapshot {
    const TerrainEditLayer* layer = nullptr;
    uint32_t revision = 0;
    std::vector<EditGeometryKey> stamps;

    bool Matches(const TerrainEditLayer* candidate) const {
        if (layer != candidate) return false;
        if (candidate == nullptr) return true;
        if (revision != candidate->Revision() ||
            stamps.size() != candidate->Stamps().size()) return false;
        for (size_t i = 0; i < stamps.size(); ++i) {
            if (!MatchesEditKey(stamps[i], candidate->Stamps()[i])) return false;
        }
        return true;
    }
    void Capture(const TerrainEditLayer* candidate) {
        layer = candidate;
        revision = candidate != nullptr ? candidate->Revision() : 0;
        stamps.clear();
        if (candidate != nullptr) {
            for (const auto& stamp : candidate->Stamps()) stamps.push_back(EditKey(stamp));
        }
    }
};

struct TerrainRayResult {
    Vector3 origin{}, direction{}, normal{};
    float maxDistance = 0.0f, playerDistance = 0.0f, hitDistance = 0.0f;
    bool hit = false;
};

struct RayFrameCache {
    struct RailSampleEntry {
        RailPathSample sample{};
        uint32_t key = 0, generation = 0;
    };
    static constexpr uint32_t kRailSampleSlots = 16384;
    uint32_t scopeDepth = 0;
    const RailPath* rail = nullptr;
    float railLength = 0.0f;
    std::vector<std::array<float, 12>> railGeometry;
    std::vector<RailSampleEntry> railSamples;
    uint32_t railSampleGeneration = 0;
    std::array<unsigned char, sizeof(TerrainGenerationSettings)> settingsBytes{};
    bool terrainConfigured = false;
    EditSnapshot edits, preview;
    std::vector<TerrainRayResult> terrainResults;
    RailWorldRaycast::FrameCacheStats stats{};

    void InvalidateRailSamples() {
        if (railSamples.empty()) railSamples.resize(kRailSampleSlots);
        if (++railSampleGeneration == 0) {
            for (auto& entry : railSamples) entry.generation = 0;
            railSampleGeneration = 1;
        }
    }

    void BeginFrame() {
        rail = nullptr;
        railGeometry.clear();
        InvalidateRailSamples();
        terrainConfigured = false;
        terrainResults.clear();
        stats = {};
    }
    void Prepare(const RailWorldRaycastInput& input) {
        const auto& points = input.railPath->ControlPoints();
        bool sameRail = rail == input.railPath && railLength == input.railPath->Length() &&
            railGeometry.size() == points.size();
        for (size_t i = 0; sameRail && i < points.size(); ++i) {
            sameRail = railGeometry[i] == RailGeometryKey(points[i]);
        }
        if (!sameRail) {
            rail = input.railPath;
            railLength = rail->Length();
            railGeometry.clear();
            for (const auto& point : points) railGeometry.push_back(RailGeometryKey(point));
            InvalidateRailSamples();
            terrainConfigured = false;
        }
        // Snapshot actual bytes so padding cannot be lost through assignment.
        // A mismatch merely invalidates a cache; no approximate float matching.
        std::array<unsigned char, sizeof(TerrainGenerationSettings)> incomingSettings{};
        std::memcpy(incomingSettings.data(), input.terrainSettings, incomingSettings.size());
        if (!terrainConfigured || settingsBytes != incomingSettings ||
            !edits.Matches(input.terrainEdits) || !preview.Matches(input.terrainPreview)) {
            settingsBytes = incomingSettings;
            edits.Capture(input.terrainEdits);
            preview.Capture(input.terrainPreview);
            terrainResults.clear();
            terrainConfigured = true;
        }
    }
};

thread_local RayFrameCache gRayCache;

RailPathSample EvaluateTerrainRail(const RailPath& rail, float distance) {
    const uint32_t key = std::bit_cast<uint32_t>(distance);
    // Direct-mapped exact-key cache: collision replaces an entry, never returns
    // another distance's sample. Generations avoid per-frame clearing/allocating.
    uint32_t hash = key;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    RayFrameCache::RailSampleEntry* entry = nullptr;
    if (gRayCache.scopeDepth > 0) {
        entry = &gRayCache.railSamples[hash & (RayFrameCache::kRailSampleSlots - 1)];
        if (entry->generation == gRayCache.railSampleGeneration && entry->key == key) {
            ++gRayCache.stats.railSampleHits;
            if (drone_perf::Enabled()) ++drone_perf::frame.railSampleCacheHits;
            return entry->sample;
        }
    }
    if (drone_perf::Enabled()) ++drone_perf::frame.nearestRailEvaluations;
    const RailPathSample sample = rail.Evaluate(distance);
    if (gRayCache.scopeDepth > 0) {
        ++gRayCache.stats.railEvaluations;
        *entry = {sample, key, gRayCache.railSampleGeneration};
    }
    return sample;
}

// The original coarse grid and refinement order are retained exactly. Build
// its world positions once per ray, rather than for every marching/SDF sample.
struct TerrainRailProjection {
    const RailPath& rail;
    float start, end, span;
    uint32_t samples;
    std::array<float, 65> distances{};
    std::array<Vector3, 65> positions{};

    TerrainRailProjection(const RailPath& path, float searchStart, float searchEnd)
        : rail(path), start(searchStart), end(searchEnd),
          span((std::max)(searchEnd - searchStart, 0.001f)),
          samples((std::clamp)(static_cast<uint32_t>(std::ceil(span / 6.0f)), 16u, 64u)) {
        if (gRayCache.scopeDepth > 0) {
            for (uint32_t i = 0; i <= samples; ++i) {
                distances[i] = start + span * static_cast<float>(i) / static_cast<float>(samples);
                positions[i] = EvaluateTerrainRail(rail, distances[i]).position;
            }
        }
    }

    TerrainLocalPoint FindNearest(const Vector3& world) const {
        float bestDistance = start;
        float bestDistanceSquared = (std::numeric_limits<float>::max)();
        for (uint32_t i = 0; i <= samples; ++i) {
            const float distance = gRayCache.scopeDepth > 0 ? distances[i] :
                start + span * static_cast<float>(i) / static_cast<float>(samples);
            const Vector3 position = gRayCache.scopeDepth > 0 ? positions[i] :
                EvaluateTerrainRail(rail, distance).position;
            const float candidate = LengthSquared(Subtract(world, position));
            if (candidate < bestDistanceSquared) {
                bestDistanceSquared = candidate;
                bestDistance = distance;
            }
        }
        float window = span / static_cast<float>(samples);
        for (uint32_t iteration = 0; iteration < 5; ++iteration) {
            const float left = (std::max)(start, bestDistance - window);
            const float right = (std::min)(end, bestDistance + window);
            const float candidates[] = {left, (left + bestDistance) * 0.5f, bestDistance,
                (bestDistance + right) * 0.5f, right};
            for (float distance : candidates) {
                const float candidate = LengthSquared(
                    Subtract(world, EvaluateTerrainRail(rail, distance).position));
                if (candidate < bestDistanceSquared) {
                    bestDistanceSquared = candidate;
                    bestDistance = distance;
                }
            }
            window *= 0.5f;
        }
        const RailPathSample sample = EvaluateTerrainRail(rail, bestDistance);
        const Vector3 delta = Subtract(world, sample.position);
        return {bestDistance, Dot(delta, sample.right), Dot(delta, sample.up)};
    }
};

bool ComputeProceduralTerrain(
    const RailWorldRaycastInput& input,
    const Vector3& origin,
    const Vector3& direction,
    float maxDistance,
    float& outDistance,
    Vector3& outNormal) {
    if (!input.includeProceduralTerrain || input.terrainSettings == nullptr ||
        input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        return false;
    }
    const drone_perf::Scope profile(drone_perf::Stage::ProceduralRay);

    const float railEnd = (std::max)(input.railPath->Length() - 0.001f, 0.0f);
    const float searchMargin = maxDistance + 32.0f;
    const float searchStart = (std::clamp)(
        input.playerDistance - searchMargin, 0.0f, railEnd);
    const float searchEnd = (std::clamp)(
        input.playerDistance + searchMargin, searchStart, railEnd);
    const TerrainRailProjection projection(*input.railPath, searchStart, searchEnd);
    TerrainVolumeField field(
        *input.railPath,
        *input.terrainSettings,
        input.terrainEdits,
        input.terrainPreview);

    const uint32_t steps = (std::clamp)(
        static_cast<uint32_t>(std::ceil(maxDistance / 2.0f)), 24u, 128u);
    float previousDistance = 0.0f;
    TerrainLocalPoint previousLocal = projection.FindNearest(origin);
    float previousSdf = field.SampleLocal(
        previousLocal.distance, previousLocal.lateral, previousLocal.vertical).sdf;
    if (previousSdf >= 0.0f) {
        outDistance = 0.0f;
        outNormal = FaceAgainstRay(
            field.EstimateNormal(
                previousLocal.distance,
                previousLocal.lateral,
                previousLocal.vertical),
            direction);
        return true;
    }

    for (uint32_t index = 1; index <= steps; ++index) {
        const float distance = maxDistance * static_cast<float>(index) /
            static_cast<float>(steps);
        const Vector3 point = Add(origin, Scale(direction, distance));
        const TerrainLocalPoint local = projection.FindNearest(point);
        const float sdf = field.SampleLocal(
            local.distance, local.lateral, local.vertical).sdf;
        if (previousSdf < 0.0f && sdf >= 0.0f) {
            float low = previousDistance;
            float high = distance;
            TerrainLocalPoint hitLocal = local;
            for (uint32_t refinement = 0; refinement < 10; ++refinement) {
                const float middle = (low + high) * 0.5f;
                const Vector3 middlePoint = Add(origin, Scale(direction, middle));
                const TerrainLocalPoint middleLocal = projection.FindNearest(middlePoint);
                const float middleSdf = field.SampleLocal(
                    middleLocal.distance,
                    middleLocal.lateral,
                    middleLocal.vertical).sdf;
                if (middleSdf < 0.0f) {
                    low = middle;
                } else {
                    high = middle;
                    hitLocal = middleLocal;
                }
            }
            outDistance = (low + high) * 0.5f;
            outNormal = FaceAgainstRay(
                field.EstimateNormal(
                    hitLocal.distance,
                    hitLocal.lateral,
                    hitLocal.vertical),
                direction);
            return true;
        }
        previousDistance = distance;
        previousSdf = sdf;
    }
    return false;
}

bool RayProceduralTerrain(
    const RailWorldRaycastInput& input,
    const Vector3& origin, const Vector3& direction, float maxDistance,
    float& outDistance, Vector3& outNormal) {
    if (!input.includeProceduralTerrain || input.terrainSettings == nullptr ||
        input.railPath == nullptr || input.railPath->Length() <= 0.0f) return false;
    if (gRayCache.scopeDepth == 0) {
        return ComputeProceduralTerrain(input, origin, direction, maxDistance, outDistance, outNormal);
    }
    gRayCache.Prepare(input);
    for (const auto& result : gRayCache.terrainResults) {
        if (SameVector(origin, result.origin) && SameVector(direction, result.direction) &&
            maxDistance == result.maxDistance && input.playerDistance == result.playerDistance) {
            ++gRayCache.stats.terrainResultHits;
            if (drone_perf::Enabled()) ++drone_perf::frame.terrainRayCacheHits;
            outDistance = result.hitDistance;
            outNormal = result.normal;
            return result.hit;
        }
    }
    TerrainRayResult result{};
    result.origin = origin;
    result.direction = direction;
    result.maxDistance = maxDistance;
    result.playerDistance = input.playerDistance;
    result.hit = ComputeProceduralTerrain(input, origin, direction, maxDistance,
        result.hitDistance, result.normal);
    outDistance = result.hitDistance;
    outNormal = result.normal;
    if (gRayCache.terrainResults.size() >= 512) gRayCache.terrainResults.clear();
    gRayCache.terrainResults.push_back(result);
    return result.hit;
}

int HitPriority(RailAimHitKind kind) {
    switch (kind) {
    case RailAimHitKind::ProceduralTerrain: return 5;
    case RailAimHitKind::TerrainPlacement: return 4;
    case RailAimHitKind::Obstacle: return 3;
    case RailAimHitKind::Enemy: return 2;
    case RailAimHitKind::None: return 0;
    }
    return 0;
}

void ConsiderHit(
    RailAimHit& best,
    RailAimHitKind kind,
    float distance,
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& normal,
    uint32_t actorId,
    uint32_t sourceIndex) {
    if (!Finite(distance) || distance < 0.0f ||
        (best.hit && distance > best.distance + kTieEpsilon)) {
        return;
    }
    if (best.hit && std::abs(distance - best.distance) <= kTieEpsilon &&
        HitPriority(kind) <= HitPriority(best.kind)) {
        return;
    }
    best.hit = true;
    best.kind = kind;
    best.distance = distance;
    best.position = Add(origin, Scale(direction, distance));
    best.normal = FaceAgainstRay(normal, direction);
    best.actorId = actorId;
    best.sourceIndex = sourceIndex;
}
} // namespace

RailWorldRaycast::FrameCacheScope::FrameCacheScope() {
    if (gRayCache.scopeDepth == 0) gRayCache.BeginFrame();
    ++gRayCache.scopeDepth;
}

RailWorldRaycast::FrameCacheScope::~FrameCacheScope() {
    --gRayCache.scopeDepth;
}

RailWorldRaycast::FrameCacheStats RailWorldRaycast::CacheStats() {
    return gRayCache.stats;
}

RailAimHit RailWorldRaycast::Query(const RailWorldRaycastInput& input) {
    const drone_perf::Scope profile(drone_perf::Stage::WorldRay);
    RailAimHit best{};
    if (input.aim == nullptr || !input.aim->valid || input.railPath == nullptr ||
        input.railPath->Length() <= 0.0f || !Finite(input.aim->worldRayOrigin) ||
        !Finite(input.aim->worldRayDirection) || !Finite(input.aim->maxDistance) ||
        input.aim->maxDistance <= 0.0f) {
        return best;
    }

    const Vector3 origin = input.aim->worldRayOrigin;
    const Vector3 direction = NormalizeOr(input.aim->worldRayDirection, {});
    if (LengthSquared(direction) <= kEpsilon * kEpsilon) {
        return best;
    }
    const float maxDistance = input.aim->maxDistance;
    const float padding = (std::max)(input.collisionPadding, 0.0f);

    if (input.spawnRuntime != nullptr) {
        for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
            if (enemy.desc.hitPoints <= 0.0f ||
                (enemy.combatState.initialized &&
                 !enemy.combatState.canBeTargeted) ||
                (enemy.entranceExitState.initialized &&
                 !enemy.entranceExitState.targetable)) {
                continue;
            }
            const float railDistance = enemy.desc.spawnDistance + enemy.desc.distanceOffset;
            const Vector3 center = ResolveRailLocal(
                *input.railPath,
                railDistance,
                enemy.desc.lateralOffset,
                enemy.desc.verticalOffset);
            float distance = 0.0f;
            Vector3 normal{};
            if (RaySphere(
                    origin,
                    direction,
                    maxDistance,
                    center,
                    (std::max)(enemy.desc.radius + padding, 0.01f),
                    distance,
                    normal)) {
                ConsiderHit(
                    best,
                    RailAimHitKind::Enemy,
                    distance,
                    origin,
                    direction,
                    normal,
                    enemy.actorId,
                    UINT32_MAX);
            }
        }

        for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
            if (obstacle.desc.hitPoints <= 0.0f) {
                continue;
            }
            const float railDistance =
                obstacle.desc.spawnDistance + obstacle.desc.distanceOffset;
            const RailPathSample sample = input.railPath->Evaluate(railDistance);
            const Vector3 center = ResolveRailLocal(
                *input.railPath,
                railDistance,
                obstacle.desc.lateralOffset,
                obstacle.desc.verticalOffset);
            const std::array<Vector3, 3> axes = AxesFromEuler(
                RotationFromRailTangent(sample.tangent));
            const Vector3 halfExtents{
                obstacle.desc.halfExtents.x + padding,
                obstacle.desc.halfExtents.y + padding,
                obstacle.desc.halfExtents.z + padding};
            float distance = 0.0f;
            Vector3 normal{};
            if (RayObb(
                    origin,
                    direction,
                    maxDistance,
                    center,
                    axes,
                    halfExtents,
                    distance,
                    normal)) {
                ConsiderHit(
                    best,
                    RailAimHitKind::Obstacle,
                    distance,
                    origin,
                    direction,
                    normal,
                    obstacle.actorId,
                    UINT32_MAX);
            }
        }
    }

    if (input.course != nullptr) {
        for (uint32_t index = 0;
             index < static_cast<uint32_t>(input.course->terrainPlacements.size());
             ++index) {
            const CourseTerrainPlacement& placement =
                input.course->terrainPlacements[index];
            const float defaultBehind = placement.layer == CourseTerrainLayer::GameplayCollision
                ? 70.0f : (placement.layer == CourseTerrainLayer::VistaBackground ? 420.0f : 180.0f);
            const float defaultAhead = placement.layer == CourseTerrainLayer::GameplayCollision
                ? 220.0f : (placement.layer == CourseTerrainLayer::VistaBackground ? 760.0f : 360.0f);
            const float behind = placement.cullBehindDistance >= 0.0f
                ? placement.cullBehindDistance : defaultBehind;
            const float ahead = placement.cullAheadDistance >= 0.0f
                ? placement.cullAheadDistance : defaultAhead;
            const float presentationDelta = placement.distance - input.playerDistance;
            const bool visualColumn = input.includeVisualColumns &&
                placement.meshId == "root_spire_column" &&
                presentationDelta >= -behind && presentationDelta <= ahead;
            if (!visualColumn &&
                (placement.layer != CourseTerrainLayer::GameplayCollision ||
                 placement.collisionMode == CourseTerrainCollisionMode::None)) {
                continue;
            }
            const float railDistance = placement.distance + placement.forwardOffset;
            const RailPathSample sample = input.railPath->Evaluate(railDistance);
            const Vector3 center = ResolveRailLocal(
                *input.railPath,
                railDistance,
                placement.lateralOffset,
                placement.verticalOffset);
            const Vector3 railRotation = RotationFromRailTangent(sample.tangent);
            const std::array<Vector3, 3> axes = AxesFromEuler({
                railRotation.x + placement.rotation.x,
                railRotation.y + placement.rotation.y,
                railRotation.z + placement.rotation.z});
            const Vector3 halfExtents{
                std::abs(placement.scale.x) + padding,
                std::abs(placement.scale.y) + padding,
                std::abs(placement.scale.z) + padding};
            float distance = 0.0f;
            Vector3 normal{};
            if (RayObb(
                    origin,
                    direction,
                    maxDistance,
                    center,
                    axes,
                    halfExtents,
                    distance,
                    normal)) {
                ConsiderHit(
                    best,
                    RailAimHitKind::TerrainPlacement,
                    distance,
                    origin,
                    direction,
                    normal,
                    0,
                    index);
            }
        }
    }

    float terrainDistance = 0.0f;
    Vector3 terrainNormal{};
    if (RayProceduralTerrain(
            input,
            origin,
            direction,
            maxDistance,
            terrainDistance,
            terrainNormal)) {
        ConsiderHit(
            best,
            RailAimHitKind::ProceduralTerrain,
            terrainDistance,
            origin,
            direction,
            terrainNormal,
            0,
            UINT32_MAX);
    }
    return best;
}
