#include "PlayerCombatFeelSystem.h"

#include "CourseSpawnRuntime.h"
#include "RailRouteHazards.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float Abs(float value) {
    return std::fabs(value);
}

float EnemyDistance(const CourseEnemyActor& enemy) {
    return enemy.desc.spawnDistance + enemy.desc.distanceOffset;
}

float ObstacleDistance(const CourseObstacleActor& obstacle) {
    return obstacle.desc.spawnDistance + obstacle.desc.distanceOffset;
}
} // namespace

void PlayerCombatFeelSystem::Reset() {
    stats_ = {};
}

CourseCollisionWeaponState PlayerCombatFeelSystem::BuildWeaponState(
    const PlayerCombatFeelFrameInput& input) {
    CourseCollisionWeaponState weapon = input.baseWeapon;
    weapon.assistEnabled = false;
    weapon.assistLateralOffset = input.hasReticleAim
        ? input.reticleAimLateralOffset
        : input.playerLateralOffset;
    weapon.assistVerticalOffset = input.hasReticleAim
        ? input.reticleAimVerticalOffset
        : input.playerVerticalOffset;

    float targetLateral = weapon.assistLateralOffset;
    float targetVertical = weapon.assistVerticalOffset;
    float targetDistance = 0.0f;
    std::string targetName;
    if (input.allowAimAssist &&
        TryResolveLockOn(input, targetLateral, targetVertical, targetDistance, targetName)) {
        weapon.assistEnabled = true;
        weapon.assistLateralOffset = targetLateral;
        weapon.assistVerticalOffset = targetVertical;
        weapon.radius = (std::max)(weapon.radius, 2.7f);
        stats_.lockOnActive = true;
        stats_.lockOnTarget = std::move(targetName);
        stats_.lockOnDistance = targetDistance;
    } else {
        weapon.assistEnabled = input.hasReticleAim;
        stats_.lockOnActive = false;
        stats_.lockOnTarget.clear();
        stats_.lockOnDistance = 0.0f;
    }

    if (stats_.combo >= 8) {
        weapon.damageMultiplier *= 1.12f;
    }
    return weapon;
}

void PlayerCombatFeelSystem::ApplyCollisionStats(const CourseCollisionFrameStats& stats) {
    const uint32_t hitCount = stats.playerShotEnemyHits + stats.playerShotObstacleHits;
    if (hitCount > 0) {
        stats_.combo += hitCount;
        stats_.maxCombo = (std::max)(stats_.maxCombo, stats_.combo);
        stats_.comboTimer = 2.4f;
        stats_.score += stats.playerShotEnemyHits * (100u + stats_.combo * 8u);
        stats_.score += stats.playerShotObstacleHits * 50u;
        stats_.hitFlash = 1.0f;
        stats_.hitStopTime = (std::min)(0.08f, stats_.hitStopTime + 0.025f * static_cast<float>(hitCount));
        stats_.cameraShake = (std::max)(stats_.cameraShake, 0.25f + 0.05f * static_cast<float>(hitCount));
    }

}

void PlayerCombatFeelSystem::ApplyPlayerDamageResults(
    std::span<const PlayerDamageResult> results) {
    for (const PlayerDamageResult& result : results) {
        if (!result.accepted || result.appliedDamage <= 0.0f) continue;
        stats_.combo = 0;
        stats_.comboTimer = 0.0f;
        stats_.damageFlash = 1.0f;
        stats_.hitStopTime = (std::max)(
            stats_.hitStopTime,
            result.lethal ? 0.10f : 0.06f);
        stats_.cameraShake = (std::max)(
            stats_.cameraShake,
            result.lethal ? 1.0f : 0.75f);
    }
}

void PlayerCombatFeelSystem::ApplyGrazeScoreResults(
    std::span<const GrazeScoreResult> results) {
    for (const GrazeScoreResult& result : results) {
        if (!result.accepted) continue;
        ++stats_.nearMissCount;
        stats_.nearMissScore += result.scoreAwarded;
        stats_.score += result.scoreAwarded;
        stats_.combo = result.chainAfter;
        stats_.maxCombo = (std::max)(stats_.maxCombo, stats_.combo);
        stats_.comboTimer = (std::max)(stats_.comboTimer, 2.5f);
        stats_.cameraShake = (std::max)(
            stats_.cameraShake,
            0.06f + result.closeness * 0.08f);
    }
}

void PlayerCombatFeelSystem::ApplyLockOnRelease(
    uint32_t tokenCount,
    uint32_t hitCount,
    uint32_t maxLockCount) {
    stats_.lastLockTokenCount = tokenCount;
    stats_.lastLockHitCount = hitCount;
    stats_.lastLockScore = 0;
    stats_.lastLockWasMax = tokenCount >= maxLockCount && maxLockCount > 0;
    stats_.lastLockWasEarly = tokenCount > 0 && !stats_.lastLockWasMax;
    if (tokenCount == 0 || hitCount == 0) {
        return;
    }

    const uint32_t chainBonus = tokenCount * hitCount * 45u;
    const uint32_t maxBonus = stats_.lastLockWasMax ? 800u + tokenCount * 40u : 0u;
    const uint32_t timingBonus = stats_.lastLockWasEarly ? hitCount * 35u : 0u;
    stats_.lastLockScore = hitCount * 160u + chainBonus + maxBonus + timingBonus;
    stats_.score += stats_.lastLockScore;
    stats_.combo += tokenCount;
    stats_.maxCombo = (std::max)(stats_.maxCombo, stats_.combo);
    stats_.comboTimer = 3.2f;
    stats_.hitFlash = 1.0f;
    stats_.hitStopTime = (std::min)(0.12f, stats_.hitStopTime + 0.018f * static_cast<float>(tokenCount));
    stats_.cameraShake = (std::max)(stats_.cameraShake, stats_.lastLockWasMax ? 0.72f : 0.36f);
}

void PlayerCombatFeelSystem::Update(float deltaTime) {
    const float dt = (std::max)(0.0f, deltaTime);
    stats_.comboTimer = (std::max)(0.0f, stats_.comboTimer - dt);
    if (stats_.comboTimer <= 0.0f) {
        stats_.combo = 0;
    }
    stats_.hitFlash = (std::max)(0.0f, stats_.hitFlash - dt * 5.8f);
    stats_.damageFlash = (std::max)(0.0f, stats_.damageFlash - dt * 3.6f);
    stats_.hitStopTime = (std::max)(0.0f, stats_.hitStopTime - dt);
    stats_.cameraShake = (std::max)(0.0f, stats_.cameraShake - dt * 2.8f);
}

bool PlayerCombatFeelSystem::TryResolveLockOn(
    const PlayerCombatFeelFrameInput& input,
    float& lateralOffset,
    float& verticalOffset,
    float& targetDistance,
    std::string& targetName) const {
    if (input.spawnRuntime == nullptr) {
        return false;
    }

    constexpr float kForwardMin = 4.0f;
    const float forwardMax = input.baseWeapon.range;
    const float aimLateral = input.hasReticleAim
        ? input.reticleAimLateralOffset
        : input.playerLateralOffset;
    const float aimVertical = input.hasReticleAim
        ? input.reticleAimVerticalOffset
        : input.playerVerticalOffset;
    float bestScore = (std::numeric_limits<float>::max)();
    bool found = false;

    for (const CourseEnemyActor& enemy : input.spawnRuntime->Enemies()) {
        if (enemy.desc.hitPoints <= 0.0f ||
            (enemy.combatState.initialized &&
             !enemy.combatState.canBeTargeted)) {
            continue;
        }
        const float distance = EnemyDistance(enemy);
        const float forward = distance - input.playerDistance;
        if (forward < kForwardMin || forward > forwardMax) {
            continue;
        }
        const float lateral = Abs(enemy.desc.lateralOffset - aimLateral);
        const float vertical = Abs(enemy.desc.verticalOffset - aimVertical);
        if (lateral > 5.2f || vertical > 4.6f) {
            continue;
        }
        const float score = forward * 0.020f + lateral * 2.2f + vertical * 1.8f;
        if (score < bestScore) {
            bestScore = score;
            lateralOffset = enemy.desc.lateralOffset;
            verticalOffset = enemy.desc.verticalOffset;
            targetDistance = forward;
            targetName = enemy.desc.role.empty() ? enemy.desc.waveId : enemy.desc.role;
            found = true;
        }
    }

    for (const CourseObstacleActor& obstacle : input.spawnRuntime->Obstacles()) {
        if (!obstacle.desc.breakable || RailRouteHazards::IsSlab(obstacle)) {
            continue;
        }
        const float distance = ObstacleDistance(obstacle);
        const float forward = distance - input.playerDistance;
        if (forward < kForwardMin || forward > forwardMax) {
            continue;
        }
        const float lateral = Abs(obstacle.desc.lateralOffset - aimLateral);
        const float vertical = Abs(obstacle.desc.verticalOffset - aimVertical);
        if (lateral > obstacle.desc.halfExtents.x + 3.4f ||
            vertical > obstacle.desc.halfExtents.y + 3.0f) {
            continue;
        }
        const float score = forward * 0.035f + lateral * 1.7f + vertical * 1.45f + 2.0f;
        if (score < bestScore) {
            bestScore = score;
            lateralOffset = obstacle.desc.lateralOffset;
            verticalOffset = obstacle.desc.verticalOffset;
            targetDistance = forward;
            targetName = obstacle.desc.id;
            found = true;
        }
    }

    return found;
}
