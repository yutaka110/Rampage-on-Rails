#include "RailVehicleBodyCollisionSystem.h"
#include "RailRouteHazards.h"

#include "../terrain/RailPath.h"
#include "../terrain/TerrainGenerationSettings.h"
#include "../terrain/TerrainVolumeField.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

struct RailLocalPoint final {
    float distance = 0.0f;
    float lateral = 0.0f;
    float vertical = 0.0f;
};

bool Finite(float value) noexcept { return std::isfinite(value); }

float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float LengthSquared(Vector3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vector3 NormalizeOr(Vector3 value, Vector3 fallback) noexcept {
    const float lengthSquared = LengthSquared(value);
    if (!Finite(lengthSquared) || lengthSquared <= 0.0000001f) return fallback;
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

Vector3 RailLocalNormalToWorld(
    const RailVehicleRuntimeState& state,
    Vector3 localNormal) noexcept {
    return NormalizeOr(
        Add(
            Add(Scale(state.right, localNormal.x), Scale(state.up, localNormal.y)),
            Scale(state.forward, localNormal.z)),
        Scale(state.forward, -1.0f));
}

bool SweepPointAgainstExpandedAabb(
    const RailLocalPoint& start,
    const RailLocalPoint& end,
    const RailLocalPoint& center,
    Vector3 obstacleHalfExtents,
    Vector3 bodyHalfExtents,
    float& enterFraction,
    bool& startedInside,
    Vector3& contactNormalRailLocal) noexcept {
    const float startValues[3] = {start.distance, start.lateral, start.vertical};
    const float endValues[3] = {end.distance, end.lateral, end.vertical};
    const float centerValues[3] = {center.distance, center.lateral, center.vertical};
    const float extents[3] = {
        (std::max)(0.01f, std::abs(obstacleHalfExtents.z)) +
            bodyHalfExtents.z,
        (std::max)(0.01f, std::abs(obstacleHalfExtents.x)) +
            bodyHalfExtents.x,
        (std::max)(0.01f, std::abs(obstacleHalfExtents.y)) +
            bodyHalfExtents.y};
    float enter = 0.0f;
    float leave = 1.0f;
    int32_t enteringAxis = -1;
    float enteringSign = 0.0f;
    startedInside = true;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const float minimum = centerValues[axis] - extents[axis];
        const float maximum = centerValues[axis] + extents[axis];
        const float origin = startValues[axis];
        const float delta = endValues[axis] - origin;
        if (origin < minimum || origin > maximum) startedInside = false;
        if (std::abs(delta) <= 0.000001f) {
            if (origin < minimum || origin > maximum) return false;
            continue;
        }
        float first = (minimum - origin) / delta;
        float second = (maximum - origin) / delta;
        float firstNormalSign = -1.0f;
        if (first > second) {
            std::swap(first, second);
            firstNormalSign = 1.0f;
        }
        if (first > enter) {
            enter = first;
            enteringAxis = static_cast<int32_t>(axis);
            enteringSign = firstNormalSign;
        }
        leave = (std::min)(leave, second);
        if (enter > leave) return false;
    }
    if (leave < 0.0f || enter > 1.0f) return false;
    enterFraction = (std::clamp)(enter, 0.0f, 1.0f);
    contactNormalRailLocal = {0.0f, 0.0f, -1.0f};
    if (startedInside) {
        const float dx = start.distance - center.distance;
        const float dy = start.lateral - center.lateral;
        const float dz = start.vertical - center.vertical;
        const float normalized[3] = {
            dx / extents[0], dy / extents[1], dz / extents[2]};
        uint32_t axis = 0;
        if (std::abs(normalized[1]) > std::abs(normalized[axis])) axis = 1;
        if (std::abs(normalized[2]) > std::abs(normalized[axis])) axis = 2;
        enteringAxis = static_cast<int32_t>(axis);
        enteringSign = normalized[axis] < 0.0f ? -1.0f : 1.0f;
    }
    if (enteringAxis == 0) contactNormalRailLocal = {0.0f, 0.0f, enteringSign};
    if (enteringAxis == 1) contactNormalRailLocal = {enteringSign, 0.0f, 0.0f};
    if (enteringAxis == 2) contactNormalRailLocal = {0.0f, enteringSign, 0.0f};
    return true;
}

bool OverlapsSweepBounds(
    const RailLocalPoint& start,
    const RailLocalPoint& end,
    const RailLocalPoint& center,
    Vector3 obstacleHalfExtents,
    Vector3 bodyHalfExtents) noexcept {
    const float minimumDistance = (std::min)(start.distance, end.distance) -
        bodyHalfExtents.z;
    const float maximumDistance = (std::max)(start.distance, end.distance) +
        bodyHalfExtents.z;
    const float minimumLateral = (std::min)(start.lateral, end.lateral) -
        bodyHalfExtents.x;
    const float maximumLateral = (std::max)(start.lateral, end.lateral) +
        bodyHalfExtents.x;
    const float minimumVertical = (std::min)(start.vertical, end.vertical) -
        bodyHalfExtents.y;
    const float maximumVertical = (std::max)(start.vertical, end.vertical) +
        bodyHalfExtents.y;
    return center.distance + std::abs(obstacleHalfExtents.z) >= minimumDistance &&
        center.distance - std::abs(obstacleHalfExtents.z) <= maximumDistance &&
        center.lateral + std::abs(obstacleHalfExtents.x) >= minimumLateral &&
        center.lateral - std::abs(obstacleHalfExtents.x) <= maximumLateral &&
        center.vertical + std::abs(obstacleHalfExtents.y) >= minimumVertical &&
        center.vertical - std::abs(obstacleHalfExtents.y) <= maximumVertical;
}

bool PointInsideProceduralTerrain(
    const TerrainVolumeField& field,
    float distance,
    float lateral,
    float vertical) {
    return field.SampleLocal(distance, lateral, vertical).sdf >= 0.0f;
}

std::string ContactKey(
    RailVehicleBodyContactKind kind,
    uint32_t actorId,
    const std::string& stableId) {
    return std::to_string(static_cast<uint32_t>(kind)) + ":" +
        std::to_string(actorId) + ":" + stableId;
}

} // namespace

RailVehicleBodyCollisionSystem::RailVehicleBodyCollisionSystem() {
    (void)Initialize(RailVehicleHitboxProfile::MineCartDefaults(), nullptr);
}

bool RailVehicleBodyCollisionSystem::Initialize(
    const RailVehicleHitboxProfile& profile,
    std::string* errorMessage) {
    if (!profile.Validate(errorMessage)) return false;
    profile_ = profile;
    initialized_ = true;
    Reset();
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void RailVehicleBodyCollisionSystem::Reset() {
    frame_ = {};
    activeContactKey_.clear();
    contactSequence_ = 0;
    frame_.revision = ++revision_;
}

const RailVehicleBodyCollisionFrame& RailVehicleBodyCollisionSystem::Update(
    const RailVehicleBodyCollisionInput& input) {
    frame_ = {};
    frame_.revision = ++revision_;
    if (!initialized_ || !input.gameplayActive ||
        input.vehicleDefinition == nullptr || input.vehicleState == nullptr ||
        !input.vehicleState->initialized ||
        !Finite(input.vehicleState->previousDistance) ||
        !Finite(input.vehicleState->distance)) {
        activeContactKey_.clear();
        return frame_;
    }

    const RailVehicleRuntimeState& state = *input.vehicleState;
    const RailLocalPoint start{
        state.previousDistance, 0.0f,
        input.vehicleDefinition->bodyVerticalOffset};
    const RailLocalPoint end{
        state.distance, 0.0f,
        input.vehicleDefinition->bodyVerticalOffset};
    frame_.valid = true;
    frame_.impactFraction = 1.0f;
    frame_.impactDistance = end.distance;
    frame_.impactVerticalOffset = end.vertical;
    frame_.impactSpeed = state.speed;
    frame_.sourceVehicleRevision = state.revision;

    float bestFraction = 1.0f;
    Vector3 bestNormalRailLocal{0.0f, 0.0f, -1.0f};
    const auto consider = [&] (
        float fraction,
        RailVehicleBodyContactKind kind,
        uint32_t actorId,
        const std::string& stableId,
        Vector3 normalRailLocal) {
        if (fraction > bestFraction ||
            (fraction == bestFraction && frame_.contact)) {
            return;
        }
        bestFraction = fraction;
        frame_.contact = true;
        frame_.blocking = true;
        frame_.kind = kind;
        frame_.hitActorId = actorId;
        frame_.hitStableId = stableId;
        bestNormalRailLocal = NormalizeOr(
            normalRailLocal, {0.0f, 0.0f, -1.0f});
    };

    if (profile_.includeDynamicObstacles && input.spawnRuntime != nullptr) {
        for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
            // Damage resolves before the next runtime cleanup; a destroyed
            // obstacle must stop blocking the vehicle in the same frame.
            if (obstacle.age >= obstacle.desc.lifetime ||
                (obstacle.desc.breakable && obstacle.desc.hitPoints <= 0.0f)) continue;
            RailLocalPoint center{
                obstacle.desc.spawnDistance + obstacle.desc.distanceOffset,
                obstacle.desc.lateralOffset,
                obstacle.desc.verticalOffset};
            auto relativeEnd=end;
            if(RailRouteHazards::IsSlab(obstacle)) {
                const auto previous=RailRouteHazards::Stone(obstacle,obstacle.age).rail;
                const auto current=RailRouteHazards::Stone(obstacle,obstacle.age+
                    (end.distance>=RailRouteHazards::SlabTrigger?input.deltaTime:0)).rail;
                center={previous.z,previous.x,previous.y};
                relativeEnd.distance+=previous.z-current.z;
                relativeEnd.lateral+=previous.x-current.x;
                relativeEnd.vertical+=previous.y-current.y;
            }
            if (!OverlapsSweepBounds(
                    start, relativeEnd, center, obstacle.desc.halfExtents,
                    profile_.bodyHalfExtents)) {
                continue;
            }
            if (frame_.obstacleCandidatesTested >=
                profile_.maximumObstacleCandidates) {
                frame_.budgetExceeded = true;
                break;
            }
            ++frame_.obstacleCandidatesTested;
            float fraction = 1.0f;
            bool startedInside = false;
            Vector3 normalRailLocal{};
            if (SweepPointAgainstExpandedAabb(
                    start, relativeEnd, center, obstacle.desc.halfExtents,
                    profile_.bodyHalfExtents, fraction, startedInside,
                    normalRailLocal)) {
                (void)startedInside;
                consider(
                    fraction,
                    RailVehicleBodyContactKind::DynamicObstacle,
                    obstacle.actorId,
                    obstacle.desc.id,
                    normalRailLocal);
            }
        }
    }

    if (profile_.includeTerrainPlacements && input.course != nullptr) {
        for (const CourseTerrainPlacement& placement :
             input.course->terrainPlacements) {
            if (placement.layer != CourseTerrainLayer::GameplayCollision ||
                placement.collisionMode == CourseTerrainCollisionMode::None) {
                continue;
            }
            const RailLocalPoint center{
                placement.distance + placement.forwardOffset,
                placement.lateralOffset,
                placement.verticalOffset};
            if (!OverlapsSweepBounds(
                    start, end, center, placement.scale,
                    profile_.bodyHalfExtents)) {
                continue;
            }
            if (frame_.terrainCandidatesTested >=
                profile_.maximumTerrainCandidates) {
                frame_.budgetExceeded = true;
                break;
            }
            ++frame_.terrainCandidatesTested;
            float fraction = 1.0f;
            bool startedInside = false;
            Vector3 normalRailLocal{};
            if (SweepPointAgainstExpandedAabb(
                    start, end, center, placement.scale,
                    profile_.bodyHalfExtents, fraction, startedInside,
                    normalRailLocal)) {
                (void)startedInside;
                consider(
                    fraction,
                    RailVehicleBodyContactKind::TerrainPlacement,
                    0u,
                    placement.editorGuid.empty()
                        ? placement.id : placement.editorGuid,
                    normalRailLocal);
            }
        }
    }

    if (profile_.includeProceduralTerrain && input.railPath != nullptr &&
        input.terrainSettings != nullptr && input.railPath->Length() > 0.0f) {
        TerrainVolumeField field(
            *input.railPath,
            *input.terrainSettings,
            input.terrainEdits,
            input.terrainPreview);
        const std::array<Vector2, 8> crossSection{{
            {-profile_.bodyHalfExtents.x, -profile_.bodyHalfExtents.y},
            {-profile_.bodyHalfExtents.x, profile_.bodyHalfExtents.y},
            {profile_.bodyHalfExtents.x, -profile_.bodyHalfExtents.y},
            {profile_.bodyHalfExtents.x, profile_.bodyHalfExtents.y},
            {-profile_.bodyHalfExtents.x, 0.0f},
            {profile_.bodyHalfExtents.x, 0.0f},
            {0.0f, -profile_.bodyHalfExtents.y},
            {0.0f, profile_.bodyHalfExtents.y}}};
        Vector2 unsafeOffset{};
        const auto unsafe = [&] (float fraction) {
            const float distance = Lerp(start.distance, end.distance, fraction);
            for (const Vector2& offset : crossSection) {
                ++frame_.proceduralSamplesTested;
                if (PointInsideProceduralTerrain(
                        field,
                        distance,
                        offset.x,
                        start.vertical + offset.y)) {
                    unsafeOffset = offset;
                    return true;
                }
            }
            return false;
        };
        if (unsafe(0.0f)) {
            consider(
                0.0f,
                RailVehicleBodyContactKind::ProceduralTerrain,
                0u,
                "procedural_terrain",
                NormalizeOr(
                    {-unsafeOffset.x, -unsafeOffset.y, 0.0f},
                    {0.0f, 1.0f, 0.0f}));
        } else {
            float previous = 0.0f;
            for (uint32_t sample = 1;
                 sample <= profile_.proceduralSweepSamples;
                 ++sample) {
                const float fraction = static_cast<float>(sample) /
                    static_cast<float>(profile_.proceduralSweepSamples);
                if (unsafe(fraction)) {
                    float low = previous;
                    float high = fraction;
                    for (uint32_t refinement = 0;
                         refinement < profile_.proceduralRefinementSteps;
                         ++refinement) {
                        const float middle = (low + high) * 0.5f;
                        if (unsafe(middle)) high = middle;
                        else low = middle;
                    }
                    consider(
                        high,
                        RailVehicleBodyContactKind::ProceduralTerrain,
                        0u,
                        "procedural_terrain",
                        NormalizeOr(
                            {-unsafeOffset.x, -unsafeOffset.y, 0.0f},
                            {0.0f, 1.0f, 0.0f}));
                    break;
                }
                previous = fraction;
            }
        }
    }

    if (frame_.budgetExceeded) {
        frame_.contact = true;
        frame_.blocking = true;
        frame_.kind = RailVehicleBodyContactKind::QueryBudgetExceeded;
        frame_.hitActorId = 0;
        frame_.hitStableId = "vehicle_body_query_budget";
        bestFraction = 0.0f;
    }

    if (!frame_.contact) {
        activeContactKey_.clear();
        return frame_;
    }
    frame_.impactFraction = (std::clamp)(bestFraction, 0.0f, 1.0f);
    frame_.impactDistance = Lerp(
        start.distance, end.distance, frame_.impactFraction);
    frame_.impactLateralOffset = 0.0f;
    frame_.impactVerticalOffset = start.vertical;
    frame_.impactNormalRailLocal = bestNormalRailLocal;
    frame_.impactNormalWorld = RailLocalNormalToWorld(state, bestNormalRailLocal);
    const float supportDistance =
        std::abs(bestNormalRailLocal.x) * profile_.bodyHalfExtents.x +
        std::abs(bestNormalRailLocal.y) * profile_.bodyHalfExtents.y +
        std::abs(bestNormalRailLocal.z) * profile_.bodyHalfExtents.z;
    Vector3 impactCenter = state.position;
    if (input.railPath != nullptr && input.railPath->Length() > 0.0f) {
        const RailPathSample impactSample =
            input.railPath->Evaluate(frame_.impactDistance);
        impactCenter = Add(
            impactSample.position,
            Scale(impactSample.up, input.vehicleDefinition->bodyVerticalOffset));
    }
    frame_.impactWorldPosition = Add(
        impactCenter,
        Scale(frame_.impactNormalWorld, -supportDistance));
    const std::string key = ContactKey(
        frame_.kind, frame_.hitActorId, frame_.hitStableId);
    frame_.beganContactThisFrame = key != activeContactKey_;
    frame_.persistentContact = !frame_.beganContactThisFrame;
    if (frame_.beganContactThisFrame) ++contactSequence_;
    frame_.contactSequence = contactSequence_;
    activeContactKey_ = key;
    return frame_;
}

std::optional<RailVehicleProjectileContact> SweepRailVehicleProjectileContact(
    const RailVehicleBodyCollisionInput& input,
    const CourseBulletActor& projectile) {
    if (!input.gameplayActive || !input.vehicleDefinition || !input.vehicleState ||
        !input.vehicleState->initialized || !input.railPath) {
        return std::nullopt;
    }
    const auto& vehicle = *input.vehicleDefinition;
    const auto& state = *input.vehicleState;
    const RailLocalPoint start{
        projectile.spawnDistance + projectile.previousDistanceOffset - state.previousDistance,
        projectile.previousLateralOffset,
        projectile.previousVerticalOffset - vehicle.bodyVerticalOffset};
    const RailLocalPoint end{
        projectile.spawnDistance + projectile.distanceOffset - state.distance,
        projectile.lateralOffset,
        projectile.verticalOffset - vehicle.bodyVerticalOffset};
    const auto half = vehicle.collisionHalfExtents;
    const Vector3 padding{projectile.radius, projectile.radius, projectile.radius};
    RailVehicleProjectileContact contact{};
    bool startedInside = false;
    Vector3 normal{};
    if (!SweepPointAgainstExpandedAabb(start, end, {}, padding, half,
            contact.fraction, startedInside, normal)) {
        return std::nullopt;
    }

    // The sweep stops the projectile centre at the expanded box. Place the
    // burst on the underlying armour face, including for a spawn in overlap.
    const auto faceCoordinate = [t = contact.fraction](
        float from, float to, float extent, float normalComponent) {
        return normalComponent != 0.0f
            ? normalComponent * extent
            : (std::clamp)(Lerp(from, to, t), -extent, extent);
    };
    const float distance = Lerp(state.previousDistance, state.distance, contact.fraction);
    const auto sample = input.railPath->Evaluate(distance);
    const float lateral = faceCoordinate(start.lateral, end.lateral, half.x, normal.x);
    const float vertical = vehicle.bodyVerticalOffset +
        faceCoordinate(start.vertical, end.vertical, half.y, normal.y);
    const float longitudinal = faceCoordinate(start.distance, end.distance, half.z, normal.z);
    contact.railPosition = {lateral, vertical, distance + longitudinal};
    contact.worldPosition = Add(sample.position, Add(Scale(sample.right, lateral),
        Add(Scale(sample.up, vertical), Scale(sample.tangent, longitudinal))));
    contact.worldNormal = Add(Scale(sample.right, normal.x),
        Add(Scale(sample.up, normal.y), Scale(sample.tangent, normal.z)));
    // Carry the swept point into the current vehicle pose for the VFX query.
    // Damage and collision feedback continue to use the original world point.
    contact.currentVehicleProbe = Add(state.position, Add(Scale(state.right, lateral),
        Add(Scale(state.up, vertical - vehicle.bodyVerticalOffset),
            Scale(state.forward, longitudinal))));
    return contact;
}

const char* ToString(RailVehicleBodyContactKind kind) noexcept {
    switch (kind) {
    case RailVehicleBodyContactKind::None: return "None";
    case RailVehicleBodyContactKind::DynamicObstacle: return "DynamicObstacle";
    case RailVehicleBodyContactKind::TerrainPlacement: return "TerrainPlacement";
    case RailVehicleBodyContactKind::ProceduralTerrain: return "ProceduralTerrain";
    case RailVehicleBodyContactKind::QueryBudgetExceeded:
        return "QueryBudgetExceeded";
    }
    return "Unknown";
}
