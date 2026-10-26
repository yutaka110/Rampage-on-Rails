#include "EncounterDirector.h"

#include "CourseSpawnRuntime.h"
#include "EnemyEncounterReadabilityDirector.h"
#include "RailWorldRaycast.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool IsRoute(const CourseEventMarker& event) {
    return event.type=="enemy_wave" && event.payload.starts_with("route:");
}
size_t RouteSlots(const CourseEventMarker& event) { return event.payload=="route:pair" ? 2u : 1u; }
size_t PrioritySlots(const CourseEventMarker& event) {
    return event.payload=="route:turret" || event.id.starts_with("route_sniper_") ||
        event.id=="route_scout_sniper_pair" ? 1u : 0u;
}
bool IsPriorityEnemy(const CourseEnemyActor& actor) {
    return actor.behaviorDefinition.archetype==EnemyBehaviorArchetype::Sniper ||
        actor.behaviorDefinition.archetype==EnemyBehaviorArchetype::Turret;
}
bool IsType(const CourseEventMarker& event, const char* type) {
    return event.type == type;
}
} // namespace

void EncounterDirector::Reset() {
    pendingEvents_.clear();
    timeSinceMajorRelease_ = 999.0f;
    timeSinceRouteRelease_ = 999.0f;
    secondsWithoutAimableEnemy_ = 0.0f;
    availabilityAge_ = 999.0f;
    availabilityActorId_ = availabilityCursor_ = 0;
    availabilityVisible_ = false;
    adaptiveSequence_ = 0;
    roleOpportunitySeconds_ = 0.0f;
    timeSinceRoleRelease_ = 999.0f;
    reinforcedWaveId_.clear();
    escortedPriorityActorId_ = 0;
    roleSequence_ = 0;
    lastStats_ = {};
    lastStats_.lastDecision = "Reset";
}

EncounterDirectorFrameOutput EncounterDirector::Update(const EncounterDirectorFrameInput& input) {
    EncounterDirectorFrameOutput output{};
    const float dt = std::isfinite(input.deltaTime) ? (std::max)(0.0f, input.deltaTime) : 0.0f;
    timeSinceMajorRelease_ += dt;
    timeSinceRouteRelease_ += dt;
    timeSinceRoleRelease_ += dt;

    lastStats_ = {};
    if (input.spawnRuntime != nullptr) {
        lastStats_.activeEnemies = input.spawnRuntime->ActiveEnemyCount();
        lastStats_.activeBullets = input.spawnRuntime->ActiveBulletCount();
        lastStats_.activeObstacles = input.spawnRuntime->ActiveObstacleCount();
        for(const auto& actor:input.spawnRuntime->Enemies()) {
            if(actor.stationaryDormant) continue;
            if(actor.desc.hitPoints<=0 || actor.entranceExitState.exitComplete) continue;
            if(actor.desc.sourcePlacementGuid.empty()) ++lastStats_.activeAuxiliaryEnemies;
            else ++lastStats_.activePrimaryEnemies;
            lastStats_.livingBoss|=actor.behaviorDefinition.archetype==EnemyBehaviorArchetype::Boss;
            lastStats_.activePriorityEnemies+=IsPriorityEnemy(actor);
        }
    }

    for (const CourseEventMarker& event : input.triggeredEvents) {
        if (pendingEvents_.size() >= settings_.maxQueuedEvents && !IsCriticalEvent(event)) {
            ++lastStats_.suppressedThisFrame;
            lastStats_.lastDecision = "Suppressed non-critical event because encounter queue is full.";
            continue;
        }
        pendingEvents_.push_back({event, 0.0f});
    }

    std::vector<PendingEvent> stillPending;
    stillPending.reserve(pendingEvents_.size());
    auto admission=lastStats_;
    for (PendingEvent& pending : pendingEvents_) {
        pending.age += dt;
        if(IsRoute(pending.event)) {
            if(pending.age>settings_.routeAdmissionWindowSeconds ||
                input.currentDistance>pending.event.distance+settings_.routeAdmissionDistance || admission.livingBoss) {
                ++lastStats_.suppressedThisFrame;
                lastStats_.lastDecision="Skipped a crowded/passed roadside encounter; never catch up in a burst.";
                continue;
            }
            if(dt<=0) { stillPending.push_back(pending);continue; }
        }
        std::string reason;
        if (CanRelease(pending.event, pending.age, admission, reason)) {
            output.dispatchEvents.push_back(pending.event);
            ++lastStats_.releasedThisFrame;
            lastStats_.lastDecision = reason;
            if(IsRoute(pending.event)) {
                admission.activeEnemies+=RouteSlots(pending.event);
                admission.activeAuxiliaryEnemies+=RouteSlots(pending.event);
                admission.activePriorityEnemies+=PrioritySlots(pending.event);
                timeSinceRouteRelease_=0;
            }
            if (IsMajorEncounter(pending.event)) {
                timeSinceMajorRelease_ = 0.0f;
            }
        } else {
            stillPending.push_back(pending);
            ++lastStats_.delayedThisFrame;
            lastStats_.lastDecision = reason;
        }
    }

    pendingEvents_ = std::move(stillPending);
    const bool adaptiveActive = input.adaptivePatrolsAllowed && input.spawnRuntime &&
        input.readability && !admission.livingBoss;
    if (!adaptiveActive) {
        // A cinematic or result screen must not accumulate a deferred patrol.
        secondsWithoutAimableEnemy_ = 0.0f;
        availabilityAge_ = 999.0f;
        availabilityVisible_ = false;
        roleOpportunitySeconds_ = 0.0f;
    } else if (dt > 0.0f) {
        const float adaptiveDt = (std::min)(dt, 0.25f);
        availabilityAge_ += adaptiveDt;
        lastStats_.hasAimableEnemy = HasAimableEnemy(input);
        secondsWithoutAimableEnemy_ = lastStats_.hasAimableEnemy ? 0.0f : secondsWithoutAimableEnemy_ + adaptiveDt;
        // Let an existing small entrance finish. A primary carrier launch is
        // longer than our aiming-gap target, so it may have one side patrol
        // under it, within the same total/auxiliary population caps.
        const bool arriving = std::any_of(input.spawnRuntime->Enemies().begin(),
            input.spawnRuntime->Enemies().end(), [](const auto& actor) {
                const bool auxiliary = actor.desc.sourcePlacementGuid.empty();
                if(auxiliary && (actor.terrainIngress.waiting || (actor.terrainIngress.initialized && !actor.terrainIngress.finished)))
                    return actor.desc.hitPoints>0;
                const bool shortPrimaryEntrance = !actor.carrierDeployment.initialized && actor.age < 0.5f;
                return (auxiliary || shortPrimaryEntrance) && actor.desc.hitPoints > 0.0f && actor.age < 4.5f &&
                    actor.desc.actorAssetId != "short_turret" &&
                    (!actor.entranceExitState.initialized ||
                     actor.entranceExitState.phase == EnemyEntranceExitPhase::Pending ||
                     actor.entranceExitState.phase == EnemyEntranceExitPhase::Entering);
            });
        const bool primaryReserved = input.primaryEncounterDue &&
            admission.activePrimaryEnemies == 0 && admission.activeAuxiliaryEnemies == 0 &&
            admission.activeBullets == 0;
        if (!lastStats_.hasAimableEnemy && !arriving && !primaryReserved &&
            secondsWithoutAimableEnemy_ >= settings_.adaptivePatrolDelaySeconds) {
            CourseEventMarker patrol;
            patrol.distance = input.currentDistance;
            patrol.type = "enemy_wave";
            patrol.id = adaptiveSequence_ % 2 == 0 ? "route_patrol_left" : "route_patrol_right";
            patrol.payload = "route:adaptive";
            uint32_t greatestActorId = 0;
            for (const auto& actor : input.spawnRuntime->Enemies()) greatestActorId = (std::max)(greatestActorId, actor.actorId);
            // A restored checkpoint may contain an earlier patrol while this
            // director's clock has reset; never merge their formation identities.
            patrol.editorGuid = "adaptive-patrol-" + std::to_string(adaptiveSequence_ + 1) + "-" + std::to_string(greatestActorId);
            std::string reason;
            if (CanRelease(patrol, 0.0f, admission, reason)) {
                output.dispatchEvents.push_back(std::move(patrol));
                timeSinceRouteRelease_ = 0.0f;
                ++adaptiveSequence_;
                ++lastStats_.releasedThisFrame;
                ++lastStats_.adaptivePatrolsThisFrame;
                ++admission.activeEnemies;
                ++admission.activeAuxiliaryEnemies;
                lastStats_.lastDecision = "Approaching side patrol fills an empty aiming window.";
            }
        }
        MixEnemyRoles(input, adaptiveDt, admission, output);
    }
    lastStats_.secondsWithoutAimableEnemy = secondsWithoutAimableEnemy_;
    lastStats_.pendingEvents = pendingEvents_.size();
    if (lastStats_.lastDecision.empty()) {
        lastStats_.lastDecision = output.dispatchEvents.empty()
            ? "No encounter events this frame."
            : "Released encounter events.";
    }
    return output;
}

void EncounterDirector::MixEnemyRoles(const EncounterDirectorFrameInput& input, float deltaTime,
    EncounterDirectorStats& admission, EncounterDirectorFrameOutput& output) {
    if (!input.roleMixingAllowed || !lastStats_.hasAimableEnemy || admission.livingBoss ||
        timeSinceRoleRelease_ < settings_.roleMixSpacingSeconds) {
        roleOpportunitySeconds_ = 0.0f;
        return;
    }
    unsigned visibleAssaults=0;
    const CourseEnemyActor* primary=nullptr;
    const CourseEnemyActor* priority=nullptr;
    uint32_t greatestActorId=0;
    for (const auto& actor : input.spawnRuntime->Enemies()) {
        greatestActorId=(std::max)(greatestActorId,actor.actorId);
        if(actor.stationaryDormant) continue;
        if (actor.desc.hitPoints<=0 || actor.entranceExitState.exitComplete) continue;
        const auto visible=std::find_if(input.readability->actors.begin(),input.readability->actors.end(),
            [&](const auto& proxy){return proxy.actorId==actor.actorId;});
        const float forward=actor.desc.spawnDistance+actor.desc.distanceOffset-input.currentDistance;
        if (visible==input.readability->actors.end() || !visible->onScreen || !visible->screenReadable ||
            !visible->targetable || forward<input.minimumTargetForwardDistance || forward>input.maximumTargetForwardDistance ||
            (actor.combatState.initialized && !actor.combatState.canBeTargeted) ||
            (actor.entranceExitState.initialized && !actor.entranceExitState.targetable)) continue;
        if (IsPriorityEnemy(actor)) priority=&actor;
        if (actor.behaviorDefinition.archetype==EnemyBehaviorArchetype::Assault) {
            ++visibleAssaults;
            if (!actor.desc.sourcePlacementGuid.empty()) primary=&actor;
        }
    }
    const bool addSniper=primary && visibleAssaults>=3 && admission.activePriorityEnemies==0 &&
        primary->desc.waveId!=reinforcedWaveId_ && input.currentDistance>=620;
    const bool addEscort=priority && visibleAssaults<2 &&
        priority->actorId!=escortedPriorityActorId_ && !input.primaryEncounterDue;
    if (!addSniper && !addEscort) {roleOpportunitySeconds_=0.0f;return;}
    roleOpportunitySeconds_+=deltaTime;
    if (roleOpportunitySeconds_<settings_.roleMixDelaySeconds) return;
    CourseEventMarker support;
    support.distance=input.currentDistance;support.type="enemy_wave";support.payload="route:support";
    // Opposite sides preserve a choice of targets instead of hiding the
    // precision hull or fixed battery behind its escort.
    const bool fromLeft=addSniper ? primary->desc.lateralOffset>=0 : priority->desc.lateralOffset>=0;
    support.id=addSniper ? (fromLeft?"route_sniper_left":"route_sniper_right") :
        (fromLeft?"route_patrol_left":"route_patrol_right");
    support.editorGuid="mixed-role-"+std::to_string(roleSequence_+1)+"-"+std::to_string(greatestActorId);
    std::string reason;
    if (!CanRelease(support,0.0f,admission,reason)) return;
    output.dispatchEvents.push_back(std::move(support));
    ++admission.activeEnemies;++admission.activeAuxiliaryEnemies;
    admission.activePriorityEnemies+=addSniper;
    if (addSniper) reinforcedWaveId_=primary->desc.waveId;
    else escortedPriorityActorId_=priority->actorId;
    ++roleSequence_;++lastStats_.releasedThisFrame;++lastStats_.mixedRoleArrivalsThisFrame;
    timeSinceRouteRelease_=timeSinceRoleRelease_=roleOpportunitySeconds_=0.0f;
    lastStats_.lastDecision=addSniper?"Precision threat joins a sustained assault group.":"Single escort joins a readable priority threat.";
}

bool EncounterDirector::HasAimableEnemy(const EncounterDirectorFrameInput& input) {
    std::vector<const EnemyEncounterActorReadability*> candidates;
    for (const auto& visible : input.readability->actors) {
        if (!visible.onScreen || !visible.screenReadable || !visible.targetable) continue;
        const auto actor = std::find_if(input.spawnRuntime->Enemies().begin(),
            input.spawnRuntime->Enemies().end(), [&](const auto& enemy) { return enemy.actorId == visible.actorId; });
        if (actor == input.spawnRuntime->Enemies().end() || actor->desc.hitPoints <= 0.0f ||
            (actor->combatState.initialized && !actor->combatState.canBeTargeted) ||
            (actor->entranceExitState.initialized && !actor->entranceExitState.targetable)) continue;
        const float forward = actor->desc.spawnDistance + actor->desc.distanceOffset - input.currentDistance;
        if (forward < input.minimumTargetForwardDistance || forward > input.maximumTargetForwardDistance) continue;
        candidates.push_back(&visible);
    }
    if (candidates.empty()) {
        availabilityVisible_ = false;
        availabilityAge_ = 999.0f;
        return false;
    }
    const auto retained = std::find_if(candidates.begin(), candidates.end(),
        [&](const auto* candidate) { return candidate->actorId == availabilityActorId_; });
    if (availabilityAge_ < settings_.availabilityQueryIntervalSeconds) {
        return availabilityVisible_ && retained != candidates.end();
    }
    // Keep a proven visible target; rotate hidden candidates. One visibility ray
    // per 0.12s at most, sharing the existing Hybrid/BVH frame cache in gameplay.
    const auto* candidate = availabilityVisible_ && retained != candidates.end() ? *retained :
        candidates[availabilityCursor_++ % candidates.size()];
    availabilityActorId_ = candidate->actorId;
    availabilityAge_ = 0.0f;
    availabilityVisible_ = true;
    if (input.visibilityQuery) {
        const auto origin = input.visibilityQuery->aim ? input.visibilityQuery->aim->worldRayOrigin : Vector3{};
        const Vector3 delta{candidate->worldPosition.x-origin.x, candidate->worldPosition.y-origin.y,
            candidate->worldPosition.z-origin.z};
        const float length = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
        availabilityVisible_ = false;
        if (length > 0.001f) {
            RailAimState aim{};
            aim.valid = true; aim.worldRayOrigin = origin;
            aim.worldRayDirection = {delta.x/length, delta.y/length, delta.z/length};
            aim.maxDistance = length + 8.0f;
            auto query = *input.visibilityQuery; query.aim = &aim; query.includeVisualColumns = true;
            const auto hit = RailWorldRaycast::Query(query);
            ++lastStats_.availabilityQueriesThisFrame;
            availabilityVisible_ = hit.hit && hit.kind == RailAimHitKind::Enemy && hit.actorId == candidate->actorId;
        }
    }
    return availabilityVisible_;
}

bool EncounterDirector::CanRelease(
    const CourseEventMarker& event,
    float age,
    const EncounterDirectorStats& stats,
    std::string& reason) const {
    if(IsRoute(event)) {
        const auto slots=RouteSlots(event);
        if(stats.activeEnemies+slots>settings_.maxActiveEnemies ||
            stats.activeAuxiliaryEnemies+slots>settings_.maxAuxiliaryEnemies ||
            (stats.activePrimaryEnemies>=6 && event.payload!="route:adaptive" &&
             event.payload!="route:support" && PrioritySlots(event)==0) ||
            (PrioritySlots(event)>0 && stats.activePriorityEnemies>0) || stats.activeBullets>18 || stats.livingBoss) {
            reason="Roadside encounter waits briefly for a readable auxiliary slot.";return false;
        }
        if(timeSinceRouteRelease_<settings_.routeSpacingSeconds) {
            reason="Roadside encounter spacing.";return false;
        }
        reason="Released a small roadside encounter within budget.";return true;
    }
    if (age >= settings_.maxQueueDelay) {
        reason = "Released event after maximum queue delay.";
        return true;
    }

    if (IsType(event, "enemy_wave")) {
        if (stats.activeEnemies >= settings_.maxActiveEnemies) {
            reason = "Delayed enemy wave because active enemy budget is full.";
            return false;
        }
        if (timeSinceMajorRelease_ < settings_.majorEncounterSpacing) {
            reason = "Delayed enemy wave to preserve encounter spacing.";
            return false;
        }
        reason = "Released enemy wave.";
        return true;
    }

    if (IsType(event, "boss")) {
        if (stats.activeEnemies > settings_.bossEntryEnemyLimit) {
            reason = "Delayed boss entry until previous enemies clear.";
            return false;
        }
        reason = "Released boss entry.";
        return true;
    }

    if (IsType(event, "boss_phase")) {
        if (timeSinceMajorRelease_ < settings_.majorEncounterSpacing * 0.75f) {
            reason = "Delayed boss phase to preserve readability.";
            return false;
        }
        reason = "Released boss phase.";
        return true;
    }

    if (IsType(event, "obstacle")) {
        if (stats.activeObstacles >= settings_.maxActiveObstacles) {
            reason = "Delayed obstacle because active obstacle budget is full.";
            return false;
        }
        reason = "Released obstacle.";
        return true;
    }

    reason = "Released non-combat event.";
    return true;
}

bool EncounterDirector::IsMajorEncounter(const CourseEventMarker& event) const {
    return (!IsRoute(event) && IsType(event, "enemy_wave")) || IsType(event, "boss") || IsType(event, "boss_phase");
}

bool EncounterDirector::IsCriticalEvent(const CourseEventMarker& event) const {
    return IsMajorEncounter(event) || IsType(event, "checkpoint");
}
