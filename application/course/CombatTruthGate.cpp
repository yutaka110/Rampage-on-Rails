#include "CombatTruthGate.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include "CourseSpawnRuntime.h"
#include "EnemyAttackDefensePresentationBridge.h"
#include "EnemyAttackTelegraphSystem.h"
#include "EnemyProjectilePresentationBridge.h"

namespace {
std::string Utf8(std::u8string_view text) {
    return {
        reinterpret_cast<const char*>(text.data()),
        reinterpret_cast<const char*>(text.data() + text.size())};
}

bool IsLivingThreat(const CourseEnemyActor& actor) noexcept {
    if(actor.stationaryDormant) return false;
    if (actor.entranceExitState.exitComplete ||
        actor.entranceExitState.phase == EnemyEntranceExitPhase::Exited) {
        return false;
    }
    if (actor.combatState.initialized) {
        if (actor.combatState.currentHitPoints <= 0.0f ||
            actor.combatState.phase == EnemyCombatPhase::Dying ||
            actor.combatState.phase == EnemyCombatPhase::Retired) {
            return false;
        }
    } else if (actor.desc.hitPoints <= 0.0f) {
        return false;
    }
    return true;
}

bool IsActiveTelegraph(EnemyAttackTelegraphPhase phase) noexcept {
    return phase == EnemyAttackTelegraphPhase::Warming ||
        phase == EnemyAttackTelegraphPhase::Tracking ||
        phase == EnemyAttackTelegraphPhase::Imminent ||
        phase == EnemyAttackTelegraphPhase::Fired;
}
}

void CombatTruthGate::Reset() {
    frame_ = {};
    clearCandidateSeconds_ = 0.0f;
    recentDamageHoldSeconds_ = 0.0f;
    revision_ = 0;
}

void CombatTruthGate::Update(
    const CombatTruthGateInput& input,
    const CombatTruthGateSettings& settings) {
    CombatTruthGateFrame next{};
    next.revision = ++revision_;
    const float dt = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;

    if (!input.gameplayActive) {
        clearCandidateSeconds_ = 0.0f;
        recentDamageHoldSeconds_ = 0.0f;
        next.statusText = Utf8(u8"\u5f85\u6a5f");
        frame_ = std::move(next);
        return;
    }

    if (input.runtime != nullptr) {
        for (const CourseEnemyActor& actor : input.runtime->Enemies()) {
            if (IsLivingThreat(actor)) ++next.activeHostiles;
        }
        for (const CourseBulletActor& projectile : input.runtime->Bullets()) {
            if (projectile.active && !projectile.hitConsumed &&
                projectile.damage > 0.0f) {
                ++next.activeHostileProjectiles;
            }
        }
    }
    if (input.telegraph != nullptr) {
        for (const EnemyAttackTelegraphCue& cue : input.telegraph->cues) {
            if (IsActiveTelegraph(cue.phase)) ++next.activeTelegraphs;
        }
    }
    if (input.defense != nullptr) {
        for (const EnemyAttackDefensePresentationCue& cue : input.defense->cues) {
            if (!cue.actionSatisfied &&
                (cue.projectileInFlight || IsActiveTelegraph(cue.phase))) {
                ++next.unresolvedDefenseWindows;
            }
        }
    }
    for (const PlayerDamageResult& result : input.damageResults) {
        if (result.accepted && result.appliedDamage > 0.0f) {
            ++next.acceptedDamageResults;
        }
    }
    next.activeWaves = input.activeWaves;

    if (next.acceptedDamageResults > 0) {
        recentDamageHoldSeconds_ = (std::max)(
            0.0f, settings.damageClearHoldSeconds);
    } else {
        recentDamageHoldSeconds_ = (std::max)(
            0.0f, recentDamageHoldSeconds_ - dt);
    }

    CombatTruthBlocker blockers = CombatTruthBlocker::None;
    if (next.activeHostiles > 0) blockers = blockers | CombatTruthBlocker::HostileActor;
    if (next.activeTelegraphs > 0) blockers = blockers | CombatTruthBlocker::AttackTelegraph;
    if (next.activeHostileProjectiles > 0) blockers = blockers | CombatTruthBlocker::HostileProjectile;
    if (next.unresolvedDefenseWindows > 0) blockers = blockers | CombatTruthBlocker::DefenseWindow;
    if (settings.activeWaveBlocksClear && next.activeWaves > 0) {
        blockers = blockers | CombatTruthBlocker::ActiveWave;
    }
    if (recentDamageHoldSeconds_ > 0.0f) {
        blockers = blockers | CombatTruthBlocker::RecentDamage;
    }
    next.blockers = blockers;
    next.combatActive = blockers != CombatTruthBlocker::None;

    if (next.combatActive) {
        clearCandidateSeconds_ = 0.0f;
    } else {
        clearCandidateSeconds_ += dt;
    }
    next.clearCandidateSeconds = clearCandidateSeconds_;
    next.recentDamageHoldSeconds = recentDamageHoldSeconds_;
    next.safeToAnnounceClear = !next.combatActive &&
        clearCandidateSeconds_ >= (std::max)(
            0.0f, settings.clearConfirmationSeconds);
    next.safeToResolveSession = next.safeToAnnounceClear;

    bool impactObserved = false;
    if (input.projectiles != nullptr) {
        impactObserved = std::any_of(
            input.projectiles->events.begin(),
            input.projectiles->events.end(),
            [](const EnemyProjectilePresentationEvent& event) {
                return event.kind ==
                    EnemyProjectilePresentationEventKind::Impacted;
            });
    }
    next.damageWithoutKnownThreat = next.acceptedDamageResults > 0 &&
        !impactObserved && next.activeHostiles == 0 &&
        next.activeTelegraphs == 0 &&
        next.activeHostileProjectiles == 0;

    if (next.activeHostileProjectiles > 0) {
        next.statusText = Utf8(u8"\u5f3e\u63a5\u8fd1 ") +
            std::to_string(next.activeHostileProjectiles);
    } else if (next.unresolvedDefenseWindows > 0) {
        next.statusText = Utf8(u8"\u9632\u5fa1 ") +
            std::to_string(next.unresolvedDefenseWindows);
    } else if (next.activeTelegraphs > 0) {
        next.statusText = Utf8(u8"\u653b\u6483\u4e88\u544a ") +
            std::to_string(next.activeTelegraphs);
    } else if (next.activeHostiles > 0) {
        next.statusText = Utf8(u8"\u6575 ") +
            std::to_string(next.activeHostiles);
    } else if (next.activeWaves > 0) {
        next.statusText = Utf8(u8"\u6226\u95d8\u4e2d");
    } else if (recentDamageHoldSeconds_ > 0.0f) {
        next.statusText = Utf8(u8"\u8105\u5a01\u7d42\u4e86");
    } else if (!next.safeToAnnounceClear) {
        next.statusText = Utf8(u8"\u5b89\u5168\u78ba\u8a8d");
    } else {
        next.statusText = Utf8(u8"\u5b89\u5168");
    }
    frame_ = std::move(next);
}

const char* ToString(CombatTruthBlocker blocker) noexcept {
    switch (blocker) {
    case CombatTruthBlocker::None: return "None";
    case CombatTruthBlocker::HostileActor: return "HostileActor";
    case CombatTruthBlocker::AttackTelegraph: return "AttackTelegraph";
    case CombatTruthBlocker::HostileProjectile: return "HostileProjectile";
    case CombatTruthBlocker::DefenseWindow: return "DefenseWindow";
    case CombatTruthBlocker::ActiveWave: return "ActiveWave";
    case CombatTruthBlocker::RecentDamage: return "RecentDamage";
    }
    return "Mixed";
}
