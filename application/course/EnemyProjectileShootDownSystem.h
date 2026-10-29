#pragma once

#include <cstdint>
#include <vector>

#include "EnemyProjectileSystem.h"
#include "../terrain/RailPath.h"

enum class EnemyProjectileShootDownRejectReason : uint8_t {
    None,
    InvalidShot,
    NoTarget,
    ProjectileNotShootable,
};

struct EnemyProjectileShootDownRequest final {
    uint64_t shotId = 0;
    Vector3 rayOrigin{};
    Vector3 rayDirection{0.0f, 0.0f, 1.0f};
    float maximumDistance = 0.0f;
    float maximumWorldHitDistance = 0.0f;
    float damage = 0.0f;
    float primaryRadius = 0.0f;
    // Only an aimed contact on the visible stream produces a chain clash.
    // Incidental rounds crossing a shot at the gun can still be cancelled.
    bool focusedGatlingClash = true;
};

struct EnemyProjectileShootDownResult final {
    uint64_t shotId = 0;
    uint64_t projectileId = 0;
    uint32_t ownerActorId = 0;
    EnemyProjectileShootDownRejectReason rejectReason =
        EnemyProjectileShootDownRejectReason::None;
    Vector3 worldHitPoint{};
    Vector3 worldHitNormal{};
    float hitDistance = 0.0f;
    float appliedDamage = 0.0f;
    float remainingHitPoints = 0.0f;
    bool targetResolved = false;
    bool accepted = false;
    bool destroyed = false;
    // Only the first contact owns the surrounding clash. The same immutable
    // volume is forwarded to presentation on the contact frame.
    Vector3 clashCenter{};
    float clashRadius = 0.0f;
    float primaryRadius = 0.0f;
    bool secondaryClash = false;
};

struct EnemyProjectileShootDownFrame final {
    std::vector<EnemyProjectileShootDownResult> results;
    uint32_t shotsEvaluated = 0;
    uint32_t projectilesHit = 0;
    uint32_t projectilesDestroyed = 0;
    uint64_t revision = 0;
};

class EnemyProjectileShootDownSystem final {
public:
    void Reset();
    void BeginFrame();
    EnemyProjectileShootDownResult Submit(
        std::vector<EnemyProjectileRuntimeState>& projectiles,
        const RailPath& railPath,
        const EnemyProjectileShootDownRequest& request);
    // One real primary pulse may disperse two nearby, already launched rounds
    // from the same gatling burst. Every contact enters the normal result frame.
    EnemyProjectileShootDownResult SubmitPrimary(
        std::vector<EnemyProjectileRuntimeState>& projectiles,
        const RailPath& railPath,
        const EnemyProjectileShootDownRequest& request);

    const EnemyProjectileShootDownResult& LastResult() const noexcept {
        return lastResult_;
    }
    const EnemyProjectileShootDownFrame& Frame() const noexcept {
        return frame_;
    }

private:
    EnemyProjectileShootDownResult SubmitRay(
        std::vector<EnemyProjectileRuntimeState>& projectiles,
        const RailPath& railPath,
        const EnemyProjectileShootDownRequest& request,
        bool primaryPulse,
        const EnemyProjectileRuntimeState* adjacentTo = nullptr);
    EnemyProjectileShootDownResult lastResult_{};
    EnemyProjectileShootDownFrame frame_{};
    uint64_t revision_ = 0;
};

const char* ToString(EnemyProjectileShootDownRejectReason reason) noexcept;
