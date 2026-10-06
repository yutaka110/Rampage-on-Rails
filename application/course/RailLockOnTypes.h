#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RailAimState.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"

enum class RailLockTargetKind {
    Enemy,
    Obstacle,
};

enum class RailLockRejectReason {
    None,
    BehindCamera,
    Offscreen,
    OutOfDepth,
    OutOfForwardRange,
    Occluded,
    NotSwept,
    AlreadyLocked,
    StackLimit,
    LockModeInactive,
    LockContainerFull,
};

struct RailLockTargetHandle {
    RailLockTargetKind kind = RailLockTargetKind::Enemy;
    uint32_t actorId = 0;
    uint32_t generationId = 0;
};

struct RailLockAnchor {
    RailLockTargetHandle target{};
    uint32_t anchorId = 0;
    std::string label;
    Vector3 worldPosition{};
    Vector2 screenPosition{};
    float screenRadius = 32.0f;
    float forwardDistance = 0.0f;
    float priority = 0.0f;
    float lineOfSightBlockT = 0.0f;
    uint32_t lineOfSightOccluderActorId = 0;
    int maxStack = 1;
    bool lineOfSightBlocked = false;
};

struct RailLockCandidate {
    RailLockAnchor anchor{};
    float distanceToReticle = 0.0f;
    float score = 0.0f;
    float reticlePriorityScore = 0.0f;
    float centerPriorityScore = 0.0f;
    float forwardPriorityScore = 0.0f;
    float kindPriorityScore = 0.0f;
    float anchorPriorityScore = 0.0f;
    bool lockable = false;
    RailLockRejectReason rejectReason = RailLockRejectReason::None;
};

struct RailLockToken {
    RailLockTargetHandle target{};
    uint32_t anchorId = 0;
    int stackIndex = 0;
    float acquiredTime = 0.0f;
    Vector2 acquiredScreenPosition{};
    float acquiredScreenDistance = 0.0f;
    std::string label;
};

struct RailLockRelease {
    std::vector<RailLockToken> tokens;
    float releaseTime = 0.0f;
};

struct RailLockSettings {
    int maxLocks = 8;
    float minForwardDistance = 4.0f;
    float maxForwardDistance = 120.0f;
    float enemyScreenRadius = 34.0f;
    float obstacleScreenRadius = 42.0f;
    float assistRadius = 12.0f;
    float reticleKeyboardSpeed = 820.0f;
    float reticleGamepadSpeed = 920.0f;
    float releaseDamage = 34.0f;
    float lockVfxTravelDurationMin = 0.22f;
    float lockVfxTravelDurationMax = 0.62f;
    float lockVfxTravelDistanceDivisor = 180.0f;
    float lockVfxVisualScaleMin = 2.2f;
    float lockVfxVisualScaleMax = 6.0f;
    float lockVfxVisualScalePerDistance = 0.035f;
    float lockVfxImpactScaleMin = 1.35f;
    float lockVfxImpactScaleMax = 3.4f;
    float lockVfxImpactScalePerDistance = 0.025f;
    float lockVfxReleaseShotInterval = 0.035f;
    float lockVfxMuzzleForwardOffset = 4.0f;
    int lockVfxMaxConcurrentShots = 16;
    float lockPriorityReticleWeight = 0.58f;
    float lockPriorityCenterWeight = 0.22f;
    float lockPriorityForwardThreatWeight = 0.18f;
    float lockPriorityAnchorWeight = 0.32f;
    float lockPriorityEnemyBonus = 0.18f;
    float lockPriorityObstacleBonus = 0.04f;
    float lockPriorityDistanceTieBreak = 0.004f;
    bool lockLineOfSightEnabled = true;
    float lockLineOfSightObstaclePadding = 0.65f;
    bool lockAimFeelEnabled = true;
    float lockAimMagnetRadius = 86.0f;
    float lockAimMagnetStrength = 0.34f;
    float lockAimMaxPullSpeed = 520.0f;
    float lockAimDeadZone = 6.0f;
    float lockAimTargetBlend = 0.62f;
    float lockAimReticleIntentWeight = 0.72f;
    float lockAimCenterIntentWeight = 0.16f;
    float lockAimForwardIntentWeight = 0.12f;
    bool lockHudPolishEnabled = true;
    float lockHudScale = 1.0f;
    float lockHudOpacity = 0.92f;
    float lockHudSafeArea = 34.0f;
    float lockHudTargetScoreAlpha = 0.42f;
    float lockHudReticleGlowScale = 1.0f;
    float lockHudReleaseFlash = 0.22f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct RailReticleState {
    RailAimState aim{};
    Vector2 previousScreenPosition{};
    Vector2 currentScreenPosition{};
    Vector2 velocity{};
    Vector2 aimFeelTargetScreenPosition{};
    float aimFeelStrength = 0.0f;
    float aimFeelPullPixels = 0.0f;
    float aimFeelTargetScore = 0.0f;
    float appliedAimFrictionScale = 1.0f;
    bool lockHeld = false;
    bool lockPressed = false;
    bool lockReleased = false;
    bool initialized = false;
    bool aimFeelActive = false;
};

struct RailLockDebugFrame {
    std::vector<RailLockCandidate> candidates;
    std::vector<RailLockToken> tokens;
    std::vector<RailLockToken> acquiredTokens;
    std::vector<RailLockToken> releasedTokens;
    RailReticleState reticle{};
    float elapsedTime = 0.0f;
    int anchorCount = 0;
    int acceptedThisFrame = 0;
    int releasedThisFrame = 0;
};
