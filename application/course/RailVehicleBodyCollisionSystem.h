#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"
#include "RailVehicleHitboxProfile.h"

class RailPath;
struct TerrainGenerationSettings;
class TerrainEditLayer;

enum class RailVehicleBodyContactKind : uint8_t {
    None,
    DynamicObstacle,
    TerrainPlacement,
    ProceduralTerrain,
    QueryBudgetExceeded,
};

struct RailVehicleBodyCollisionInput final {
    const RailVehicleDefinition* vehicleDefinition = nullptr;
    const RailVehicleRuntimeState* vehicleState = nullptr;
    const CourseSpawnRuntime* spawnRuntime = nullptr;
    const CourseAsset* course = nullptr;
    const RailPath* railPath = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    const TerrainEditLayer* terrainPreview = nullptr;
    bool gameplayActive = true;
};

struct RailVehicleBodyCollisionFrame final {
    bool valid = false;
    bool contact = false;
    bool blocking = false;
    bool beganContactThisFrame = false;
    bool persistentContact = false;
    bool budgetExceeded = false;
    RailVehicleBodyContactKind kind = RailVehicleBodyContactKind::None;
    uint32_t hitActorId = 0;
    std::string hitStableId;
    float impactFraction = 1.0f;
    float impactDistance = 0.0f;
    float impactLateralOffset = 0.0f;
    float impactVerticalOffset = 0.0f;
    float impactSpeed = 0.0f;
    Vector3 impactWorldPosition{};
    Vector3 impactNormalWorld{0.0f, 1.0f, 0.0f};
    Vector3 impactNormalRailLocal{0.0f, 0.0f, -1.0f};
    uint32_t obstacleCandidatesTested = 0;
    uint32_t terrainCandidatesTested = 0;
    uint32_t proceduralSamplesTested = 0;
    uint64_t contactSequence = 0;
    uint64_t sourceVehicleRevision = 0;
    uint64_t revision = 0;
};

struct RailVehicleProjectileContact final {
    float fraction = 0.0f;
    Vector3 railPosition{}; // lateral, vertical, distance
    Vector3 worldPosition{};
    Vector3 worldNormal{};
    Vector3 currentVehicleProbe{};
};

std::optional<RailVehicleProjectileContact> SweepRailVehicleProjectileContact(
    const RailVehicleBodyCollisionInput& input,
    const CourseBulletActor& projectile);

// Sweeps the authoritative rail-local vehicle box from previousDistance to
// distance. It queries gameplay collision proxies only; render meshes and LOD
// never participate in damage authority.
class RailVehicleBodyCollisionSystem final {
public:
    RailVehicleBodyCollisionSystem();

    bool Initialize(
        const RailVehicleHitboxProfile& profile,
        std::string* errorMessage = nullptr);
    void Reset();
    const RailVehicleBodyCollisionFrame& Update(
        const RailVehicleBodyCollisionInput& input);

    const RailVehicleHitboxProfile& Profile() const noexcept {
        return profile_;
    }
    const RailVehicleBodyCollisionFrame& Frame() const noexcept {
        return frame_;
    }

private:
    RailVehicleHitboxProfile profile_{};
    RailVehicleBodyCollisionFrame frame_{};
    std::string activeContactKey_;
    uint64_t contactSequence_ = 0;
    uint64_t revision_ = 0;
    bool initialized_ = false;
};

const char* ToString(RailVehicleBodyContactKind kind) noexcept;
