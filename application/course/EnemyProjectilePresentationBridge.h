#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "EnemyProjectileSystem.h"
#include "EnemyProjectileShootDownSystem.h"
#include "PlayerDamageSystem.h"
#include "../terrain/RailPath.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;

enum class EnemyProjectilePresentationEventKind : uint8_t {
    Spawned,
    Impacted,
    Intercepted,
    Expired,
};

struct EnemyProjectilePresentationSettings final {
    bool enabled = true;
    size_t maximumVisibleProjectiles = 256;
    size_t maximumEventsPerFrame = 32;
    float threatForwardDistance = 42.0f;
    float threatLateralDistance = 7.0f;
};

// Immutable, world-space render proxy derived from authoritative projectile
// state. Presentation consumers must never write these values back to gameplay.
struct EnemyProjectilePresentation final {
    uint64_t projectileId = 0;
    uint32_t ownerActorId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    EnemyProjectileTrajectory trajectory = EnemyProjectileTrajectory::Direct;
    std::string definitionId;
    std::string trailEffectId;
    std::string impactEffectId;
    EnemyAttackDefenseResponse defenseResponses =
        EnemyAttackDefenseResponse::None;
    Vector3 worldPosition{};
    Vector3 previousWorldPosition{};
    Vector3 motionDirection{0.0f, 0.0f, -1.0f};
    Vector3 railRight{1.0f, 0.0f, 0.0f};
    Vector3 railUp{0.0f, 1.0f, 0.0f};
    Vector4 color{1.0f, 0.18f, 0.08f, 1.0f};
    float collisionRadius = 0.34f;
    float normalizedAge = 0.0f;
    float distanceToPlayer = 0.0f;
    float forwardDistanceToPlayer = 0.0f;
    bool threat = false;
    bool spawnedThisFrame = false;
    // Visible solid energy body and the shoot-down target share this radius.
    // The smaller hurt radius above still belongs to player damage authority.
    float interceptionRadius = 0.0f;
};

struct EnemyProjectilePresentationEvent final {
    EnemyProjectilePresentationEventKind kind =
        EnemyProjectilePresentationEventKind::Spawned;
    uint64_t projectileId = 0;
    uint32_t ownerActorId = 0;
    EnemyProjectileTrajectory trajectory = EnemyProjectileTrajectory::Direct;
    Vector3 worldPosition{};
    Vector3 motionDirection{0.0f, 0.0f, -1.0f};
    Vector4 color{1.0f, 0.18f, 0.08f, 1.0f};
    std::string effectId;
    bool lethal = false;
    Vector3 clashCenter{};
    float clashRadius = 0.0f;
    bool secondaryClash = false;
};

struct EnemyProjectilePresentationFrame final {
    std::vector<EnemyProjectilePresentation> projectiles;
    std::vector<EnemyProjectilePresentationEvent> events;
    uint32_t activeRuntimeProjectiles = 0;
    uint32_t droppedProjectiles = 0;
    uint32_t droppedEvents = 0;
    uint64_t sourceProjectileRevision = 0;
    uint64_t revision = 0;
};

struct EnemyProjectilePresentationInput final {
    const CourseSpawnRuntime* runtime = nullptr;
    const RailPath* railPath = nullptr;
    std::span<const PlayerDamageResult> playerDamageResults{};
    std::span<const EnemyProjectileShootDownResult> shootDownResults{};
    float playerDistance = 0.0f;
    float playerLateralOffset = 0.0f;
    float playerVerticalOffset = 4.0f;
    float deltaTime = 0.0f;
    bool gameplayActive = true;
    EnemyProjectilePresentationSettings settings{};
};

// Converts projectile simulation into stable visual proxies and one-shot
// lifecycle events. Projectile ID is the sole identity across render/audio.
class EnemyProjectilePresentationBridge final {
public:
    void Reset();
    void Update(const EnemyProjectilePresentationInput& input);

    const EnemyProjectilePresentationFrame& Frame() const noexcept {
        return frame_;
    }
    const EnemyProjectilePresentation* FindProjectile(
        uint64_t projectileId) const noexcept;

private:
    void PushEvent(
        EnemyProjectilePresentationEvent event,
        size_t maximumEvents);

    EnemyProjectilePresentationFrame frame_{};
    std::unordered_map<uint64_t, EnemyProjectilePresentation> tracked_;
    uint64_t revision_ = 0;
};

const char* ToString(EnemyProjectilePresentationEventKind kind) noexcept;
