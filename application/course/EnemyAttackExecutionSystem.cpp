#include "EnemyAttackExecutionSystem.h"

#include "CourseSpawnRuntime.h"
#include "EnemyAttackCoordinator.h"
#include "EnemyBehaviorSystem.h"

void EnemyAttackExecutionSystem::Reset() {
    frame_ = {};
    revision_ = 0;
}

void EnemyAttackExecutionSystem::Update(CourseSpawnRuntime& runtime,
    EnemyAttackCoordinator& coordinator,
    EnemyBehaviorSystem& behaviorSystem) {
    runtime.ExecuteEnemyAttacks(*this, coordinator, behaviorSystem);
}

void EnemyAttackExecutionSystem::UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        EnemyAttackCoordinator& coordinator,
        EnemyBehaviorSystem& behaviorSystem) {
    frame_ = {};
    for (CourseEnemyActor& actor : actors) {
        if (!actor.behaviorDefinition.commercialBehavior ||
            !coordinator.CanExecute(actor)) {
            continue;
        }
        ++frame_.evaluatedAttacks;
        if (!actor.fireSafetyAllowed) {
            ++frame_.safetyBlockedAttacks;
            continue;
        }
        const uint64_t intentSequence = actor.attackState.intentSequence;
        const uint64_t tokenId = actor.attackState.tokenId;
        if (!coordinator.NotifyExecutionStarted(actor)) continue;
        frame_.events.push_back({
            EnemyAttackExecutionEventKind::Started,
            actor.actorId,
            intentSequence,
            tokenId,
            0});

        const uint32_t emitted = runtime.CommitEnemyVolley(actor.actorId, intentSequence, tokenId);
        if (emitted == 0) {
            coordinator.CancelActor(
                actor, EnemyAttackCancelReason::ActorUnavailable);
            frame_.events.push_back({
                EnemyAttackExecutionEventKind::Cancelled,
                actor.actorId,
                intentSequence,
                tokenId,
                0});
            continue;
        }
        if (!coordinator.NotifyExecutionCommitted(actor, emitted) ||
            !behaviorSystem.NotifyAttackCommitted(actor)) {
            coordinator.CancelActor(
                actor, EnemyAttackCancelReason::IntentSuperseded);
            continue;
        }
        ++frame_.committedVolleys;
        frame_.emittedProjectiles += emitted;
        frame_.events.push_back({
            EnemyAttackExecutionEventKind::VolleyEmitted,
            actor.actorId,
            intentSequence,
            tokenId,
            emitted});
    }
    frame_.revision = ++revision_;
}

const char* ToString(EnemyAttackExecutionEventKind kind) noexcept {
    switch (kind) {
    case EnemyAttackExecutionEventKind::Started: return "Started";
    case EnemyAttackExecutionEventKind::VolleyEmitted: return "VolleyEmitted";
    case EnemyAttackExecutionEventKind::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
