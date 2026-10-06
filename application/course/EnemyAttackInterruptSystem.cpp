#include "EnemyAttackInterruptSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>

bool EnemyAttackInterruptDefinition::Validate() const noexcept {
    return std::isfinite(minimumSingleHitDamage) &&
        std::isfinite(accumulatedDamageThreshold) &&
        std::isfinite(interruptedCooldownSeconds) &&
        minimumSingleHitDamage >= 0.0f && accumulatedDamageThreshold > 0.0f &&
        interruptedCooldownSeconds >= 0.0f;
}

EnemyAttackInterruptSystem::EnemyAttackInterruptSystem() {
    (void)Initialize({});
}

bool EnemyAttackInterruptSystem::Initialize(
    const EnemyAttackInterruptDefinition& definition) {
    if (!definition.Validate()) return false;
    definition_ = definition;
    Reset();
    return true;
}

void EnemyAttackInterruptSystem::Reset() {
    accumulatedDamage_.clear();
    frame_ = {};
    revision_ = 0;
}

void EnemyAttackInterruptSystem::BeginFrame() {
    frame_ = {};
    frame_.revision = revision_;
}

EnemyAttackInterruptResult EnemyAttackInterruptSystem::Submit(CourseSpawnRuntime& runtime,
    const DamageResult& damageResult) {
    return runtime.InterruptEnemyAttack(*this, damageResult);
}

EnemyAttackInterruptResult EnemyAttackInterruptSystem::SubmitActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        const DamageResult& damageResult) {
    EnemyAttackInterruptResult result{};
    result.actorId = damageResult.targetActorId;
    result.shotId = damageResult.shotId;
    if (!damageResult.requestAccepted || !damageResult.targetResolved ||
        damageResult.hitKind != RailAimHitKind::Enemy ||
        !damageResult.damageApplied || damageResult.targetActorId == 0) {
        return result;
    }
    for (CourseEnemyActor& actor : actors) {
        if (actor.actorId != damageResult.targetActorId) continue;
        const EnemyAttackRuntimePhase phase = actor.attackState.phase;
        const bool preCommit = phase == EnemyAttackRuntimePhase::Reserved ||
            phase == EnemyAttackRuntimePhase::Telegraphing ||
            phase == EnemyAttackRuntimePhase::Ready;
        if (!preCommit || !actor.attackState.tokenReserved ||
            actor.attackState.emittedVolleys > 0 ||
            !HasDefenseResponse(
                actor.desc.projectileDefinition.defenseResponses,
                EnemyAttackDefenseResponse::Interrupt)) {
            return result;
        }

        result.eligible = true;
        result.attackIntentSequence = actor.attackState.intentSequence;
        result.attackTokenId = actor.attackState.tokenId;
        Accumulator& accumulator = accumulatedDamage_[actor.actorId];
        if (accumulator.intentSequence != actor.attackState.intentSequence) {
            accumulator.intentSequence = actor.attackState.intentSequence;
            accumulator.damage = 0.0f;
        }
        accumulator.damage += damageResult.appliedDamage;
        result.accumulatedDamage = accumulator.damage;
        ++frame_.eligibleHits;
        const bool thresholdReached =
            damageResult.appliedDamage >= definition_.minimumSingleHitDamage ||
            accumulator.damage >= definition_.accumulatedDamageThreshold ||
            (definition_.weakPointAlwaysInterrupts &&
                damageResult.weakPointHit);
        if (thresholdReached && runtime.EnemyAttacks().CancelActor(
                actor, EnemyAttackCancelReason::PlayerInterrupted)) {
            runtime.EnemyBehavior().CancelAttackIntent(
                actor, definition_.interruptedCooldownSeconds);
            result.interrupted = true;
            ++frame_.interruptedAttacks;
            accumulatedDamage_.erase(actor.actorId);
        }
        frame_.results.push_back(result);
        frame_.revision = ++revision_;
        return result;
    }
    return result;
}
