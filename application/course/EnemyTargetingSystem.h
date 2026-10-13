#pragma once

#include <cstdint>
#include <span>

#include "EnemyProjectileDefinitionAsset.h"
#include "TwinShieldDronePose.h"
#include "RailTravelPrediction.h"

class CourseSpawnRuntime;
struct CourseEnemyActor;
class RailPath;
struct RailPathSample;
struct EnemyCombatPresentationSettings;
class EnemyEncounterReadabilityDirector;

// A frame shared by model submission and weapon launch, including authored
// barrel anchors. Body collision and formation positions remain unchanged.
struct EnemyWeaponMountFrame final {
    TwinShieldDronePose dronePose{};
    Vector3 muzzleRail[2]{};
    bool ready = false;
    // Both barrels are forecast once per admitted attack/frame, then shared by
    // reachability checks, warning rays and scheduled launch templates.
    Vector3 scheduledMuzzleRail[2]{};
    float scheduledDelaySeconds = 0.0f;
    bool scheduledPredictionRequested = false;
    bool scheduledPredictionReady = false;
};

struct EnemyTargetingRuntimeState final {
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    uint64_t revision = 0;
    float originDistance = 0.0f;
    float originLateralOffset = 0.0f;
    float originVerticalOffset = 0.0f;
    float targetDistance = 0.0f;
    float targetLateralOffset = 0.0f;
    float targetVerticalOffset = 0.0f;
    float playerForwardVelocity = 0.0f;
    float playerLateralVelocity = 0.0f;
    float playerVerticalVelocity = 0.0f;
    float predictedFlightSeconds = 0.0f;
    float predictedLaunchDelaySeconds = 0.0f;
    float forwardTravelReferenceDistance = 0.0f;
    std::shared_ptr<const RailTravelPrediction> forwardTravelPrediction;
    // Ordinary hover shots freeze lateral/vertical aim for fair dodging, but
    // compensate mandatory rail travel through the warning and flight time.
    bool compensatesForwardTravel = false;
    bool forwardInterceptReachable = true;
    // Keep the frozen lateral/up anchor while releasing an unusable attack
    // reservation. Re-admission requires a complete new warning interval.
    bool waitingForReachableLaunch = false;
    bool initialized = false;
    bool solutionLocked = false;
    // Smoothed turret head direction in rail coordinates (lateral, up, forward).
    Vector3 turretAimDirection{0.0f, 0.0f, -1.0f};
};

// Shared by the turret model and projectile spawn. Imported barrels point -Z.
Vector3 ResolveTurretAimDirection(const CourseEnemyActor& actor);
Vector3 ResolveTurretMuzzleRailPosition(const CourseEnemyActor& actor, float barrelSide);
// Uses the actual barrel position at launch. x/y/z are lateral/up/rail distance.
Vector3 ResolveEnemyProjectileTargetRailPosition(const CourseEnemyActor& actor, const Vector3& muzzle);
enum class EnemyForwardInterceptStatus : uint8_t {
    Reachable,
    NoFutureIntersection,
    BeyondLifetime,
    InvalidInput,
};
struct EnemyForwardInterceptSolution final {
    float flightSeconds = 0.0f;
    EnemyForwardInterceptStatus status = EnemyForwardInterceptStatus::InvalidInput;
    bool Reachable() const noexcept { return status == EnemyForwardInterceptStatus::Reachable; }
};
EnemyForwardInterceptSolution SolveEnemyForwardIntercept(
    Vector3 relative, float forwardVelocity, float speed, float lifetime);
EnemyForwardInterceptSolution SolveEnemyPredictedForwardIntercept(
    const RailTravelPrediction& prediction, float launchDelaySeconds,
    Vector3 relativeAtLaunch, float speed, float lifetime);
struct EnemyProjectileAimSolution final {
    Vector3 targetRail{};
    float flightSeconds = 0.0f;
    bool reachable = true;
    Vector3 muzzleRail{};
};
EnemyProjectileAimSolution ResolveEnemyProjectileAimSolution(
    const CourseEnemyActor& actor, const Vector3& muzzle, float launchDelaySeconds = 0.0f);
bool CanEnemyProjectileVolleyReachTarget(const CourseEnemyActor& actor, float launchDelaySeconds = 0.0f);
Vector3 ResolveEnemyProjectileMuzzleRailPosition(const CourseEnemyActor& actor, int projectileIndex);
Vector3 ResolveEnemyProjectileWorldPosition(const RailPath& rail, Vector3 position);
Vector3 ResolveEnemyProjectileWorldPosition(const RailPathSample& sample, Vector3 position);

struct EnemyTargetingFrameInput final {
    const RailTravelPredictionInput* travelPredictionInput = nullptr;
    std::shared_ptr<const RailTravelPrediction> travelPrediction;
    const RailPath* railPath = nullptr;
    const EnemyCombatPresentationSettings* presentationSettings = nullptr;
    const EnemyEncounterReadabilityDirector* readability = nullptr;
    Vector3 cameraPosition{};
    bool hasCameraPosition = false;
    float deltaTime = 0.0f;
    float playerDistance = 0.0f;
    float playerLateralOffset = 0.0f;
    float playerVerticalOffset = 4.0f;
    float minimumVisibleBeforeFire = 0.0f;
};

struct EnemyTargetingFrame final {
    uint32_t travelForecastsBuilt = 0;
    uint32_t travelForecastSamples = 0;
    uint32_t muzzleForecastsBuilt = 0;
    uint32_t muzzleForecastSteps = 0;
    uint32_t activeReservations = 0;
    uint32_t solutionsLockedThisFrame = 0;
    uint32_t predictiveSolutions = 0;
    uint32_t forwardCompensatedSolutions = 0;
    uint32_t unreachableSolutions = 0;
    uint32_t homingSolutions = 0;
    uint64_t revision = 0;
};

// Captures one fair, checkpoint-safe aim solution per admitted attack token.
// Lateral/vertical aim is frozen before Telegraph presentation. Normal hover
// shots compensate automatic forward travel without following lateral dodges;
// only Homing projectiles may adjust direction after launch, at a turn cap.
class EnemyTargetingSystem final {
public:
    void Reset();
    void Update(
        CourseSpawnRuntime& runtime,
        const EnemyTargetingFrameInput& input);

    const EnemyTargetingFrame& Frame() const noexcept { return frame_; }

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const EnemyTargetingFrameInput& input);

    void LockSolution(
        CourseEnemyActor& actor,
        const EnemyTargetingFrameInput& input,
        float playerForwardVelocity,
        float playerLateralVelocity,
        float playerVerticalVelocity);
    void RefreshForwardIntercept(CourseEnemyActor& actor, const EnemyTargetingFrameInput& input,
        float playerForwardVelocity);

    EnemyTargetingFrame frame_{};
    float previousPlayerDistance_ = 0.0f;
    float previousPlayerLateralOffset_ = 0.0f;
    float previousPlayerVerticalOffset_ = 4.0f;
    bool hasPreviousPlayerSample_ = false;
    uint64_t revision_ = 0;
};
