#pragma once

#include <cstdint>
#include <span>
#include <deque>
#include <unordered_set>

#include "RailAimState.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;
struct CourseAsset;
struct CourseObstacleActor;

enum class WeaponDamageType : uint8_t {
    Kinetic,
    Energy,
    Explosive,
    Ice,
};

enum class DamageRejectReason : uint8_t {
    None,
    InvalidRequest,
    DuplicateShot,
    UnsupportedHitKind,
    TargetNotFound,
    TargetAlreadyDestroyed,
    TargetNotDamageable,
    Indestructible,
};

// Immutable description of a single confirmed weapon contact. Consumers must
// use shotId for idempotency and targetActorId for actor resolution; spatial
// fallback targeting is intentionally forbidden.
struct WeaponHitRequest {
    uint64_t shotId = 0;
    uint32_t attackerActorId = 0; // 0 is the local player/system attacker.
    uint32_t targetActorId = 0;
    uint32_t sourceIndex = UINT32_MAX;
    RailAimHitKind hitKind = RailAimHitKind::None;
    WeaponDamageType damageType = WeaponDamageType::Kinetic;
    Vector3 rayOrigin{};
    Vector3 rayDirection{0.0f, 0.0f, 1.0f};
    Vector3 hitPoint{};
    Vector3 hitNormal{};
    float hitDistance = 0.0f;
    float baseDamage = 0.0f;
};

struct DamageResult {
    uint64_t shotId = 0;
    uint32_t targetActorId = 0;
    RailAimHitKind hitKind = RailAimHitKind::None;
    WeaponDamageType damageType = WeaponDamageType::Kinetic;
    DamageRejectReason rejectReason = DamageRejectReason::None;
    float requestedDamage = 0.0f;
    float appliedDamage = 0.0f;
    float hitPointsBefore = 0.0f;
    float remainingHitPoints = 0.0f;
    bool requestAccepted = false;
    bool targetResolved = false;
    bool damageApplied = false;
    bool blocked = false;
    bool weakPointHit = false;
    bool destroyed = false;
    bool duplicate = false;
};

// Actor-ID based damage reception boundary for runtime Course actors and
// indestructible world surfaces. Keeps a bounded shot-id history so retries do
// not apply damage twice.
class CourseActorDamageReceiver {
public:
    void Reset();
    DamageResult Apply(
        CourseSpawnRuntime& runtime,
        const CourseAsset* course,
        const WeaponHitRequest& request);

    uint64_t ProcessedRequestCount() const { return processedRequestCount_; }
    uint64_t DuplicateRequestCount() const { return duplicateRequestCount_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    DamageResult ApplyActors(std::span<CourseEnemyActor> actors, std::span<CourseObstacleActor> obstacles,
        CourseSpawnRuntime& runtime,
        const CourseAsset* course,
        const WeaponHitRequest& request);

    bool RememberShot(uint64_t shotId);

    static constexpr size_t kShotHistoryCapacity = 4096;
    std::deque<uint64_t> processedShotOrder_;
    std::unordered_set<uint64_t> processedShotIds_;
    uint64_t processedRequestCount_ = 0;
    uint64_t duplicateRequestCount_ = 0;
};

const char* ToDamageRejectReasonString(DamageRejectReason reason);
