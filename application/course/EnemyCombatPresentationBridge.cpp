#include "EnemyCombatPresentationBridge.h"

#include "CourseSpawnRuntime.h"
#include "../diagnostics/DebugDrawSystem.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

float Saturate(float value) noexcept {
    return (std::clamp)(value, 0.0f, 1.0f);
}

float SmoothStep(float value) noexcept {
    const float t = Saturate(value);
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutBack(float value) noexcept {
    const float t = Saturate(value) - 1.0f;
    constexpr float kOvershoot = 1.70158f;
    return 1.0f + (kOvershoot + 1.0f) * t * t * t +
        kOvershoot * t * t;
}

float PhaseDuration(
    EnemyCombatPhase phase,
    const EnemyCombatDefinition& definition) noexcept {
    switch (phase) {
    case EnemyCombatPhase::Spawning: return definition.spawnDurationSeconds;
    case EnemyCombatPhase::Engaging: return definition.engageDurationSeconds;
    case EnemyCombatPhase::Telegraphing: return definition.telegraphLeadSeconds;
    case EnemyCombatPhase::Attacking: return definition.attackCommitSeconds;
    case EnemyCombatPhase::Recovering: return definition.recoverySeconds;
    case EnemyCombatPhase::HitReact: return definition.hitReactSeconds;
    case EnemyCombatPhase::Dying: return definition.deathDurationSeconds;
    case EnemyCombatPhase::Retired: return 0.0f;
    }
    return 0.0f;
}

EnemyCombatAnimationState ResolveAnimation(EnemyCombatPhase phase) noexcept {
    switch (phase) {
    case EnemyCombatPhase::Spawning: return EnemyCombatAnimationState::Spawn;
    case EnemyCombatPhase::Engaging: return EnemyCombatAnimationState::Engage;
    case EnemyCombatPhase::Telegraphing: return EnemyCombatAnimationState::Telegraph;
    case EnemyCombatPhase::Attacking: return EnemyCombatAnimationState::Attack;
    case EnemyCombatPhase::Recovering: return EnemyCombatAnimationState::Recover;
    case EnemyCombatPhase::HitReact: return EnemyCombatAnimationState::HitReact;
    case EnemyCombatPhase::Dying: return EnemyCombatAnimationState::Death;
    case EnemyCombatPhase::Retired: return EnemyCombatAnimationState::Hidden;
    }
    return EnemyCombatAnimationState::Hidden;
}

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(Vector3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

const CourseEnemyActor* FindEnemy(
    const CourseSpawnRuntime& runtime,
    uint32_t actorId) noexcept {
    for (const CourseEnemyActor& actor : runtime.Enemies()) {
        if (actor.actorId == actorId) return &actor;
    }
    return nullptr;
}

Vector3 ResolveWorldPosition(
    const CourseEnemyActor& actor,
    const RailPath& railPath) noexcept {
    const RailPathSample sample = railPath.Evaluate(
        actor.desc.spawnDistance + actor.desc.distanceOffset);
    return Add(
        Add(sample.position, Scale(sample.right, actor.desc.lateralOffset)),
        Scale(sample.up, actor.desc.verticalOffset));
}

void ApplyProceduralAnimation(
    EnemyCombatActorPresentation& output,
    const CourseEnemyActor& actor,
    const EnemyCombatPresentationSettings& settings) noexcept {
    const EnemyCombatRuntimeState& state = actor.combatState;
    const float duration = PhaseDuration(state.phase, actor.combatDefinition);
    const float progress = duration > 0.0f
        ? Saturate(state.phaseElapsedSeconds / duration)
        : 0.0f;
    const float pulse = std::sin(progress * kPi);
    const bool individualHover = actor.desc.meshId == "twin_shield_hull" &&
        actor.behaviorDefinition.maintainForwardEngagementBand &&
        !actor.behaviorDefinition.choreographedAttackPass;
    const float ambientPhase = individualHover
        ? actor.behaviorState.hoverMotionSeconds * (0.8f +
            0.22f * std::sin(actor.behaviorState.deterministicPhase)) +
            actor.behaviorState.deterministicPhase
        : actor.age * 3.2f + static_cast<float>(actor.actorId % 11u) * 0.43f;

    output.actorId = actor.actorId;
    output.animation = ResolveAnimation(state.phase);
    output.animationNormalizedTime = progress;
    output.visible = state.phase != EnemyCombatPhase::Retired &&
        state.presentationAlpha > 0.001f;
    output.scaleMultiplier = 1.0f;
    output.bodyScale = {1.0f, 1.0f, 1.0f};
    output.materialColor = {1.0f, 1.0f, 1.0f, state.presentationAlpha};
    output.coreColor = {0.30f, 0.92f, 1.0f, state.presentationAlpha};
    output.flashStrength = Saturate(state.hitFlash);
    output.emissiveStrength = 0.35f;
    output.silhouetteSpread = settings.dronePodSpread;
    output.contrastBackdropStrength = 0.28f;
    output.commercialSilhouette =
        actor.combatDefinition.commercialStateMachine;
    output.sourceCombatRevision = state.revision;
    output.sourceBehaviorRevision = actor.behaviorState.revision;
    output.sourceAttackRevision = actor.attackState.revision;
    output.attackTokenReserved = actor.attackState.tokenReserved;
    output.attackCommittedThisFrame = actor.attackState.committedThisFrame;

    switch (state.phase) {
    case EnemyCombatPhase::Spawning: {
        const float reveal = SmoothStep(progress);
        const float arrival = EaseOutBack(progress);
        const float spawnAlpha = (std::max)(
            settings.minimumSpawnAlpha,
            settings.minimumSpawnAlpha +
                (1.0f - settings.minimumSpawnAlpha) * reveal);
        output.visible = true;
        output.verticalOffset = -(1.0f - reveal) * 0.82f;
        output.rotationOffset.y = (1.0f - reveal) * 0.56f;
        output.rotationOffset.z = (1.0f - reveal) * -0.18f;
        output.scaleMultiplier = 0.82f + arrival * 0.18f;
        output.bodyScale = {
            0.68f + reveal * 0.32f,
            1.42f - reveal * 0.42f,
            0.74f + reveal * 0.26f};
        output.silhouetteSpread = settings.dronePodSpread *
            (0.56f + reveal * 0.44f);
        output.materialColor = {0.34f, 0.82f, 1.0f, spawnAlpha};
        output.coreColor = {0.82f, 0.98f, 1.0f, spawnAlpha};
        output.emissiveStrength = 3.4f - reveal * 2.1f;
        output.contrastBackdropStrength = 0.34f + reveal * 0.12f;
        break;
    }
    case EnemyCombatPhase::Engaging:
        output.verticalOffset = std::sin(ambientPhase) *
            settings.idleBobAmplitude * progress;
        output.rotationOffset.z = std::sin(progress * kPi * 2.0f) * 0.055f;
        output.bodyScale = {
            1.0f + pulse * 0.06f,
            1.0f - pulse * 0.08f,
            1.0f + pulse * 0.04f};
        output.coreColor = {0.34f, 0.88f, 1.0f, state.presentationAlpha};
        output.emissiveStrength = 0.75f + pulse * 0.55f;
        output.contrastBackdropStrength = 0.42f;
        break;
    case EnemyCombatPhase::Telegraphing: {
        const float heartbeat = 0.5f + 0.5f * std::sin(ambientPhase * 5.6f);
        output.weaponCharge = Saturate(0.18f + progress * 0.70f + heartbeat * 0.12f);
        output.verticalOffset = std::sin(ambientPhase) * settings.idleBobAmplitude;
        output.forwardOffset = -output.weaponCharge * 0.12f;
        output.rotationOffset.x = -output.weaponCharge * 0.085f;
        output.scaleMultiplier = 1.0f +
            output.weaponCharge * settings.telegraphPulseStrength;
        output.bodyScale = {
            1.0f + output.weaponCharge * 0.12f,
            1.0f - output.weaponCharge * 0.07f,
            1.0f + output.weaponCharge * 0.09f};
        output.materialColor = {
            1.0f,
            0.68f + 0.18f * heartbeat,
            0.30f + 0.18f * heartbeat,
            state.presentationAlpha};
        output.coreColor = {1.0f, 0.90f, 0.42f, state.presentationAlpha};
        output.emissiveStrength = 1.2f + output.weaponCharge * 2.8f;
        output.contrastBackdropStrength =
            0.54f + output.weaponCharge * 0.30f;
        break;
    }
    case EnemyCombatPhase::Attacking: {
        // The muzzle frame must carry the strongest silhouette change.  A pure
        // sin(progress * pi) envelope starts at zero and made the exact shot
        // frame look neutral even though the projectile and flash had fired.
        const float shotKick = 1.0f - progress;
        output.forwardOffset =
            -(shotKick * 0.72f + pulse * 0.28f) * settings.attackRecoilDistance;
        output.rotationOffset.x =
            -(shotKick * 0.08f + pulse * 0.04f);
        output.scaleMultiplier = 1.0f + shotKick * 0.05f + pulse * 0.03f;
        output.bodyScale = {
            1.0f - shotKick * 0.08f - pulse * 0.02f,
            1.0f - shotKick * 0.06f - pulse * 0.02f,
            1.0f + shotKick * 0.24f + pulse * 0.08f};
        output.materialColor = {1.0f, 0.58f, 0.30f, state.presentationAlpha};
        output.coreColor = {1.0f, 1.0f, 0.86f, state.presentationAlpha};
        output.weaponCharge = 1.0f - progress;
        output.emissiveStrength = 4.8f * (1.0f - progress) + 0.8f;
        output.contrastBackdropStrength = 0.90f;
        break;
    }
    case EnemyCombatPhase::Recovering:
        output.verticalOffset = std::sin(ambientPhase) * settings.idleBobAmplitude;
        output.rotationOffset.x = std::sin(progress * kPi) * 0.045f;
        output.bodyScale = {
            1.0f + pulse * 0.04f,
            1.0f + pulse * 0.07f,
            1.0f - pulse * 0.10f};
        output.weaponCharge = (1.0f - progress) * 0.28f;
        output.coreColor = {0.74f, 0.80f, 1.0f, state.presentationAlpha};
        output.contrastBackdropStrength = 0.48f;
        break;
    case EnemyCombatPhase::HitReact: {
        const float decay = 1.0f - progress;
        output.lateralOffset = std::sin(progress * kPi * 7.0f) *
            settings.hitShakeAmplitude * decay;
        output.rotationOffset.z = output.lateralOffset * 0.32f;
        output.scaleMultiplier = 1.0f - pulse * 0.08f;
        output.bodyScale = {
            0.82f + progress * 0.18f,
            1.14f - progress * 0.14f,
            0.90f + progress * 0.10f};
        output.materialColor = {
            1.0f,
            1.0f - output.flashStrength * 0.76f,
            1.0f - output.flashStrength * 0.88f,
            state.presentationAlpha};
        output.coreColor = {1.0f, 1.0f, 1.0f, state.presentationAlpha};
        output.emissiveStrength = 4.5f * decay;
        output.contrastBackdropStrength = 0.78f * decay + 0.42f;
        break;
    }
    case EnemyCombatPhase::Dying:
        output.verticalOffset = -state.deathProgress * settings.deathDropDistance;
        output.rotationOffset.z = state.deathProgress * 2.2f;
        output.rotationOffset.x = state.deathProgress * 0.55f;
        output.scaleMultiplier = (1.0f + pulse * 0.12f) *
            (1.0f - SmoothStep(state.deathProgress) * 0.88f);
        output.bodyScale = {
            1.0f + pulse * 0.32f,
            1.0f - state.deathProgress * 0.42f,
            1.0f + pulse * 0.18f};
        output.silhouetteSpread = settings.dronePodSpread *
            (1.0f + state.deathProgress * 0.46f);
        output.materialColor = {
            1.0f,
            0.48f * (1.0f - state.deathProgress),
            0.14f,
            state.presentationAlpha};
        output.coreColor = {1.0f, 0.82f, 0.22f, state.presentationAlpha};
        output.emissiveStrength = 2.0f + (1.0f - state.deathProgress) * 3.0f;
        output.contrastBackdropStrength = 0.94f;
        break;
    case EnemyCombatPhase::Retired:
        output.visible = false;
        output.scaleMultiplier = 0.0f;
        output.bodyScale = {};
        output.materialColor.w = 0.0f;
        output.coreColor.w = 0.0f;
        output.contrastBackdropStrength = 0.0f;
        break;
    }
    if (actor.behaviorDefinition.choreographedAttackPass &&
        state.phase != EnemyCombatPhase::Spawning &&
        state.phase != EnemyCombatPhase::HitReact &&
        state.phase != EnemyCombatPhase::Dying &&
        state.phase != EnemyCombatPhase::Retired) {
        // Combat may remain in Telegraphing while waiting for an admitted
        // Behavior intent. Waiting escorts must not look like charging guns.
        const EnemyBehaviorRuntimeState& behavior = actor.behaviorState;
        output.animation = EnemyCombatAnimationState::Engage;
        output.forwardOffset = 0.0f;
        output.lateralOffset = 0.0f;
        output.verticalOffset = 0.0f;
        output.rotationOffset = {};
        output.bodyScale = {1.0f, 1.0f, 1.0f};
        output.scaleMultiplier = 1.0f;
        output.materialColor = {0.60f, 0.78f, 0.94f, state.presentationAlpha};
        output.coreColor = {1.0f, 0.48f, 0.12f, state.presentationAlpha};
        output.weaponCharge = 0.0f;
        output.emissiveStrength = 0.50f;
        output.silhouetteSpread = settings.dronePodSpread;
        output.contrastBackdropStrength = 0.48f;
        if (behavior.state == EnemyBehaviorState::Aiming ||
            behavior.state == EnemyBehaviorState::RequestingAttack) {
            const float pose = behavior.state == EnemyBehaviorState::Aiming
                ? SmoothStep(behavior.stateElapsedSeconds /
                    actor.behaviorDefinition.aimingDurationSeconds) : 1.0f;
            const float charge = behavior.state == EnemyBehaviorState::RequestingAttack &&
                actor.attackState.tokenReserved
                ? Saturate(1.0f - behavior.attackTimeRemaining /
                    (std::max)(0.05f, actor.behaviorDefinition.attackLeadSeconds))
                : 0.0f;
            output.animation = EnemyCombatAnimationState::Telegraph;
            output.animationNormalizedTime = charge;
            output.rotationOffset.x = -0.12f * pose;
            output.bodyScale = {1.0f + 0.12f * pose,
                1.0f - 0.14f * pose, 1.0f + 0.10f * pose};
            output.silhouetteSpread *= 1.0f + 0.18f * pose;
            output.weaponCharge = 0.08f * pose + 0.92f * charge;
            output.coreColor = {1.0f, 0.38f + 0.60f * charge,
                0.06f + 0.76f * charge, state.presentationAlpha};
            output.emissiveStrength = 0.8f + charge * 3.6f;
            output.contrastBackdropStrength = 0.62f + charge * 0.20f;
        } else if (behavior.state == EnemyBehaviorState::Evading) {
            const float recoil = 1.0f - Saturate(behavior.stateElapsedSeconds /
                actor.behaviorDefinition.attackPassRecoilSeconds);
            output.animation = EnemyCombatAnimationState::Recover;
            output.forwardOffset = settings.attackRecoilDistance * recoil;
            output.rotationOffset.x = -0.10f * recoil;
            output.weaponCharge = recoil;
            output.coreColor = {1.0f, 0.98f, 0.82f, state.presentationAlpha};
            output.emissiveStrength = 0.6f + recoil * 4.2f;
        } else if (behavior.state == EnemyBehaviorState::Retreating) {
            output.animation = EnemyCombatAnimationState::Recover;
            output.coreColor = {0.08f, 0.26f, 0.44f, state.presentationAlpha};
        }
    }
    if (actor.behaviorState.initialized &&
        actor.behaviorDefinition.commercialBehavior) {
        output.rotationOffset.x +=
            actor.behaviorState.presentationPitchRadians;
        output.rotationOffset.y +=
            actor.behaviorState.presentationYawRadians;
        output.rotationOffset.z +=
            actor.behaviorState.presentationBankRadians;
        if (actor.behaviorState.state == EnemyBehaviorState::Aiming ||
            actor.behaviorState.state == EnemyBehaviorState::RequestingAttack) {
            output.scaleMultiplier *= 1.025f;
        }
        if (actor.attackState.tokenReserved &&
            actor.attackState.phase != EnemyAttackRuntimePhase::Recovery) {
            output.materialColor.x = (std::max)(output.materialColor.x, 0.92f);
            output.materialColor.y *= 0.92f;
            output.materialColor.z *= 0.82f;
            output.weaponCharge = (std::max)(output.weaponCharge, 0.24f);
        }
        if (actor.attackState.committedThisFrame) {
            output.animation = EnemyCombatAnimationState::Attack;
            output.forwardOffset -= settings.attackRecoilDistance;
            output.rotationOffset.x -= 0.12f;
            output.scaleMultiplier *= 1.08f;
            output.bodyScale.z *= 1.18f;
            output.coreColor = {1.0f, 1.0f, 0.88f, output.materialColor.w};
            output.emissiveStrength = (std::max)(
                output.emissiveStrength, 4.8f);
        }
    }
    if (actor.behaviorDefinition.commercialBehavior &&
        state.phase != EnemyCombatPhase::Spawning &&
        state.phase != EnemyCombatPhase::Attacking &&
        state.phase != EnemyCombatPhase::HitReact &&
        state.phase != EnemyCombatPhase::Dying && state.phase != EnemyCombatPhase::Retired &&
        !actor.attackState.committedThisFrame &&
        actor.behaviorState.state != EnemyBehaviorState::Evading) {
        const bool charging = actor.fireEnvironmentReady &&
            actor.behaviorState.attackIntentActive && actor.attackState.tokenReserved &&
            actor.behaviorState.telegraphPresented;
        const float charge = charging ? Saturate(1.0f - actor.behaviorState.attackTimeRemaining /
            (std::max)(0.05f, actor.behaviorDefinition.attackLeadSeconds)) : 0.0f;
        output.weaponCharge = charging ? 0.12f + 0.88f * charge : 0.0f;
        output.emissiveStrength = charging ? 0.85f + 3.8f * charge : 0.5f;
        output.coreColor = charging
            ? Vector4{1.0f, 0.40f + 0.58f * charge, 0.06f + 0.72f * charge, output.materialColor.w}
            : Vector4{0.10f, 0.26f, 0.36f, output.materialColor.w};
        output.contrastBackdropStrength = charging ? 0.62f + 0.18f * charge : 0.38f;
    }
    if (actor.behaviorDefinition.maintainForwardEngagementBand &&
        (actor.behaviorState.engagementBandExitRequested ||
         actor.entranceExitState.exitRequested) &&
        state.phase != EnemyCombatPhase::Dying && state.phase != EnemyCombatPhase::Retired) {
        // Departure is not another charge-up. Combat's pending Telegraphing
        // phase must not leave an apparently armed muzzle on a canceled threat.
        output.animation = EnemyCombatAnimationState::Recover;
        output.weaponCharge = 0.0f;
        output.emissiveStrength = 0.5f;
        output.coreColor = {0.08f, 0.30f, 0.42f, output.materialColor.w};
        output.contrastBackdropStrength = 0.22f;
    }
    if (actor.desc.meshId == "twin_shield_hull" &&
        state.phase != EnemyCombatPhase::Spawning &&
        state.phase != EnemyCombatPhase::Dying && state.phase != EnemyCombatPhase::Retired) {
        // Rigid floating machinery: charge/recoil move the pose and shields,
        // not the hull's size. Keep banking gentle enough to track and aim.
        output.scaleMultiplier = 1.0f;
        output.bodyScale = {1,1,1};
        output.rotationOffset.z = (std::clamp)(output.rotationOffset.z,-0.16f,0.16f);
    }
    if (actor.entranceExitState.initialized) {
        output.scaleMultiplier *= actor.entranceExitState.presentationScale;
        output.materialColor.w *= actor.entranceExitState.presentationAlpha;
        output.coreColor.w *= actor.entranceExitState.presentationAlpha;
        output.visible = output.visible &&
            actor.entranceExitState.presentationAlpha > 0.001f &&
            !actor.entranceExitState.exitComplete;
    }
    if (state.phase == EnemyCombatPhase::Spawning &&
        !actor.entranceExitState.exitComplete) {
        output.materialColor.w = (std::max)(
            output.materialColor.w, settings.minimumSpawnAlpha);
        output.coreColor.w = (std::max)(
            output.coreColor.w, settings.minimumSpawnAlpha);
        output.visible = true;
    }
}

struct SpatialMix final {
    Vector3 position{};
    float attenuation = 1.0f;
    float pan = 0.0f;
};

SpatialMix BuildSpatialMix(
    const CourseEnemyActor& actor,
    const EnemyCombatPresentationInput& input) noexcept {
    SpatialMix result{};
    result.position = ResolveWorldPosition(actor, *input.railPath);
    const Vector3 offset = Subtract(result.position, input.listenerPosition);
    const float reference = (std::max)(1.0f, input.settings.audioReferenceDistance);
    const float distanceRatio = Length(offset) / reference;
    result.attenuation = 1.0f / (1.0f + distanceRatio * distanceRatio);
    result.pan = (std::clamp)(
        Dot(offset, input.listenerRight) /
            (std::max)(1.0f, input.settings.audioPanWidth),
        -1.0f,
        1.0f);
    return result;
}

int EventPriority(EnemyCombatEventKind kind) noexcept {
    switch (kind) {
    case EnemyCombatEventKind::Defeated: return 0;
    case EnemyCombatEventKind::HitReacted: return 1;
    case EnemyCombatEventKind::AttackCommitted: return 2;
    case EnemyCombatEventKind::Engaged: return 3;
    case EnemyCombatEventKind::Spawned: return 4;
    case EnemyCombatEventKind::TelegraphStarted: return 5;
    case EnemyCombatEventKind::Retired: return 6;
    }
    return 7;
}
} // namespace

void EnemyCombatPresentationBridge::Reset() {
    frame_ = {};
    revision_ = 0;
}

void EnemyCombatPresentationBridge::Update(
    const EnemyCombatPresentationInput& input) {
    frame_.actors.clear();
    frame_.audioCues.clear();
    frame_.vfxCommands.clear();
    frame_.droppedAudioCues = 0;
    frame_.droppedVfxCommands = 0;
    frame_.cameraShakeImpulse = 0.0f;
    frame_.cameraFovImpulseRadians = 0.0f;
    frame_.cameraRollImpulseRadians = 0.0f;

    if (!input.settings.enabled || input.runtime == nullptr ||
        input.railPath == nullptr || input.railPath->Length() <= 0.0f) {
        frame_.destructionBursts.clear();
        frame_.revision = ++revision_;
        return;
    }

    if (input.gameplayActive) {
        for (auto& burst : frame_.destructionBursts)
            burst.age += (std::max)(0.0f, input.deltaTime);
        std::erase_if(frame_.destructionBursts,
            [](const EnemyDestructionBurst& burst) { return burst.age >= 0.65f; });
    }
    frame_.actors.reserve(input.runtime->Enemies().size());
    for (const CourseEnemyActor& actor : input.runtime->Enemies()) {
        if (!actor.combatState.initialized) continue;
        EnemyCombatActorPresentation output{};
        ApplyProceduralAnimation(output, actor, input.settings);
        if (actor.desc.meshId == "combat_turret") {
            output.turret = true;
            const auto sample = input.railPath->Evaluate(actor.desc.spawnDistance+actor.desc.distanceOffset);
            const Vector3 local = actor.targetingState.turretAimDirection;
            const Vector3 forward = Add(Add(Scale(sample.right,local.x),Scale(sample.up,local.y)),Scale(sample.tangent,local.z));
            output.turretWorldRotation = {std::asin((std::clamp)(forward.y,-1.0f,1.0f)),
                std::atan2(-forward.x,-forward.z),0};
            const bool alive = actor.desc.hitPoints > 0 && actor.combatState.phase != EnemyCombatPhase::Dying &&
                actor.combatState.phase != EnemyCombatPhase::Retired;
            const bool warning = alive && actor.fireEnvironmentReady && actor.behaviorState.attackIntentActive &&
                actor.attackState.tokenReserved && actor.behaviorState.telegraphPresented;
            const bool firing = alive && (actor.attackState.committedThisFrame ||
                (actor.attackState.phase == EnemyAttackRuntimePhase::Recovery &&
                 actor.behaviorState.stateElapsedSeconds < 0.14f));
            output.turretMuzzleActive = warning || firing;
            output.weaponCharge = warning ? 0.18f + 0.82f*Saturate(1.0f-actor.behaviorState.attackTimeRemaining /
                (std::max)(0.05f,actor.behaviorDefinition.attackLeadSeconds)) : (firing ? 1.0f : 0.0f);
            output.coreColor = firing ? Vector4{1.0f,1.0f,0.90f,output.materialColor.w} :
                Vector4{1.0f,0.32f+0.60f*output.weaponCharge,0.06f,output.materialColor.w};
            if (alive) {
                // A planted pedestal and rotating head, not a hovering drone.
                output.forwardOffset = output.lateralOffset = output.verticalOffset = 0;
                output.rotationOffset = {};
                output.bodyScale = {1,1,1};
                output.scaleMultiplier = 1;
                output.materialColor = {0.85f,0.91f,1.0f,output.materialColor.w};
            }
        }
        frame_.actors.push_back(output);
    }

    if (input.gameplayActive) {
        std::vector<const EnemyCombatEvent*> prioritized;
        prioritized.reserve(input.events.size());
        for (const EnemyCombatEvent& event : input.events) {
            prioritized.push_back(&event);
        }
        std::stable_sort(
            prioritized.begin(),
            prioritized.end(),
            [](const EnemyCombatEvent* left, const EnemyCombatEvent* right) {
                return EventPriority(left->kind) < EventPriority(right->kind);
            });

        for (const EnemyCombatEvent* event : prioritized) {
            const CourseEnemyActor* actor = FindEnemy(*input.runtime, event->actorId);
            if (actor == nullptr) continue;
            if (actor->desc.hitPoints <= 0 &&
                (event->kind == EnemyCombatEventKind::AttackCommitted ||
                 event->kind == EnemyCombatEventKind::TelegraphStarted)) continue;
            if (event->kind == EnemyCombatEventKind::TelegraphStarted &&
                actor->behaviorDefinition.commercialBehavior &&
                !(actor->fireEnvironmentReady && actor->behaviorState.attackIntentActive &&
                  actor->behaviorState.telegraphPresented && actor->attackState.tokenReserved)) {
                continue;
            }
            const SpatialMix mix = BuildSpatialMix(*actor, input);
            const float rollSign = (actor->actorId & 1u) != 0u ? 1.0f : -1.0f;
            switch (event->kind) {
            case EnemyCombatEventKind::Spawned:
                frame_.cameraShakeImpulse = (std::max)(
                    frame_.cameraShakeImpulse, 0.10f);
                frame_.cameraFovImpulseRadians -= 0.006f;
                frame_.cameraRollImpulseRadians += rollSign * 0.003f;
                break;
            case EnemyCombatEventKind::TelegraphStarted:
                frame_.cameraShakeImpulse = (std::max)(
                    frame_.cameraShakeImpulse, 0.06f);
                frame_.cameraFovImpulseRadians -= 0.004f;
                break;
            case EnemyCombatEventKind::AttackCommitted:
                frame_.cameraShakeImpulse = (std::max)(
                    frame_.cameraShakeImpulse, 0.22f);
                frame_.cameraFovImpulseRadians += 0.008f;
                frame_.cameraRollImpulseRadians += rollSign * 0.006f;
                break;
            case EnemyCombatEventKind::HitReacted:
                frame_.cameraShakeImpulse = (std::max)(
                    frame_.cameraShakeImpulse, 0.16f);
                frame_.cameraFovImpulseRadians -= 0.004f;
                frame_.cameraRollImpulseRadians -= rollSign * 0.005f;
                break;
            case EnemyCombatEventKind::Defeated:
                frame_.cameraShakeImpulse = (std::max)(
                    frame_.cameraShakeImpulse, 0.42f);
                frame_.cameraFovImpulseRadians -= 0.014f;
                frame_.cameraRollImpulseRadians += rollSign * 0.012f;
                break;
            case EnemyCombatEventKind::Engaged:
            case EnemyCombatEventKind::Retired:
                break;
            }
            EnemyCombatPresentationAudioCue cue{};
            cue.actorId = actor->actorId;
            cue.worldPosition = mix.position;
            cue.pan = mix.pan;
            bool emitAudio = true;
            switch (event->kind) {
            case EnemyCombatEventKind::Spawned:
                cue.kind = EnemyCombatPresentationAudioCueKind::Spawn;
                cue.volume = 0.22f;
                cue.pitch = 1.04f;
                break;
            case EnemyCombatEventKind::Engaged:
                cue.kind = EnemyCombatPresentationAudioCueKind::Engage;
                cue.volume = 0.20f;
                cue.pitch = 0.96f;
                break;
            case EnemyCombatEventKind::AttackCommitted:
                cue.kind = EnemyCombatPresentationAudioCueKind::Attack;
                cue.volume = 0.30f;
                cue.pitch = 1.0f;
                emitAudio = input.settings.emitAttackAudio;
                break;
            case EnemyCombatEventKind::HitReacted:
                cue.kind = EnemyCombatPresentationAudioCueKind::HitReact;
                cue.volume = 0.36f;
                cue.pitch = event->feedbackKind == HitFeedbackKind::WeakPointHit
                    ? 1.22f : 1.0f;
                break;
            case EnemyCombatEventKind::Defeated:
                cue.kind = EnemyCombatPresentationAudioCueKind::Death;
                cue.volume = 0.62f;
                cue.pitch = 0.88f + static_cast<float>(actor->actorId % 5u) * 0.035f;
                break;
            case EnemyCombatEventKind::TelegraphStarted:
            case EnemyCombatEventKind::Retired:
                emitAudio = false;
                break;
            }
            if (emitAudio) {
                cue.volume *= input.settings.masterAudioVolume * mix.attenuation;
                if (frame_.audioCues.size() <
                    input.settings.maximumAudioCuesPerFrame) {
                    frame_.audioCues.push_back(cue);
                } else {
                    ++frame_.droppedAudioCues;
                }
            }

            const bool spawnVfx = event->kind == EnemyCombatEventKind::Spawned &&
                actor->combatDefinition.commercialStateMachine;
            const bool chargeVfx =
                event->kind == EnemyCombatEventKind::TelegraphStarted &&
                actor->combatDefinition.commercialStateMachine &&
                actor->fireEnvironmentReady && actor->behaviorState.attackIntentActive &&
                actor->behaviorState.telegraphPresented && actor->attackState.tokenReserved;
            const bool attackVfx =
                event->kind == EnemyCombatEventKind::AttackCommitted &&
                actor->combatDefinition.commercialStateMachine;
            const bool deathVfx = event->kind == EnemyCombatEventKind::Defeated;
            if (spawnVfx || chargeVfx || attackVfx || deathVfx) {
                EnemyCombatPresentationVfxCommand vfx{};
                vfx.actorId = actor->actorId;
                vfx.worldPosition = mix.position;
                if (deathVfx) {
                    // Bounded moving debris survives actor retirement and does
                    // not depend on optional particle textures/effect assets.
                    const bool alreadyVisible = std::any_of(
                        frame_.destructionBursts.begin(), frame_.destructionBursts.end(),
                        [&](const EnemyDestructionBurst& burst) { return burst.actorId == actor->actorId; });
                    if (!alreadyVisible && frame_.destructionBursts.size() < 16) {
                        frame_.destructionBursts.push_back({actor->actorId, mix.position,
                            (std::clamp)(actor->desc.radius, 0.6f, 4.0f), 0.0f});
                    }
                    // WeaponFeedback owns the contact impact. This is a larger,
                    // actor-centered destruction burst and is emitted once.
                    vfx.cueId = "enemy_combat_death";
                    vfx.effectName = "enemy_death_burst";
                    vfx.color = {1.0f, 0.34f, 0.08f, 0.96f};
                    vfx.radius = (std::max)(1.25f, actor->desc.radius * 1.72f);
                    vfx.lifetime = 0.90f;
                } else if (attackVfx) {
                    const RailPathSample sample = input.railPath->Evaluate(
                        actor->desc.spawnDistance + actor->desc.distanceOffset);
                    vfx.worldPosition = Add(
                        mix.position,
                        Scale(sample.tangent, -actor->desc.radius * 0.88f));
                    if (actor->desc.meshId == "combat_turret") {
                        const Vector3 muzzle = ResolveTurretMuzzleRailPosition(*actor,0);
                        const auto muzzleSample = input.railPath->Evaluate(muzzle.z);
                        vfx.worldPosition = Add(muzzleSample.position,
                            Add(Scale(muzzleSample.right,muzzle.x),Scale(muzzleSample.up,muzzle.y)));
                    }
                    vfx.cueId = "enemy_combat_muzzle";
                    vfx.effectName = "enemy_muzzle_burst";
                    vfx.color = {1.0f, 0.72f, 0.20f, 0.96f};
                    vfx.radius = (std::max)(0.72f, actor->desc.radius * 0.78f);
                    vfx.lifetime = 0.30f;
                } else if (chargeVfx) {
                    // Turrets use a continuous, cancellable muzzle glow. A
                    // detached one-shot charging particle could outlive defeat.
                    if (actor->desc.meshId == "combat_turret" ||
                        actor->behaviorDefinition.choreographedAttackPass) continue;
                    vfx.cueId = "enemy_combat_charge";
                    vfx.effectName = "enemy_charge_pulse";
                    vfx.color = {1.0f, 0.48f, 0.12f, 0.82f};
                    vfx.radius = (std::max)(0.82f, actor->desc.radius * 1.08f);
                    vfx.lifetime = 0.48f;
                } else {
                    vfx.cueId = "enemy_combat_spawn";
                    vfx.effectName = "enemy_spawn_reveal";
                    vfx.color = {0.24f, 0.82f, 1.0f, 0.76f};
                    vfx.radius = (std::max)(0.78f, actor->desc.radius * 1.12f);
                    vfx.lifetime = 0.58f;
                }
                if (frame_.vfxCommands.size() <
                    input.settings.maximumVfxCommandsPerFrame) {
                    frame_.vfxCommands.push_back(vfx);
                } else {
                    ++frame_.droppedVfxCommands;
                }
            }
        }
    }
    frame_.revision = ++revision_;
}

void EnemyCombatPresentationBridge::AppendDestructionPrimitives(
    ge3::debug::DebugDrawSystem& draw) const {
    for (const auto& burst : frame_.destructionBursts) {
        const float t = burst.age / 0.65f;
        const float alpha = (1.0f - t) * (1.0f - t);
        for (int i = 0; i < 10; ++i) {
            const float angle = i * 2.399963f + static_cast<float>(burst.actorId % 13u);
            const float y = -0.75f + 1.5f * static_cast<float>(i) / 9.0f;
            const float radial = std::sqrt(1.0f - y * y);
            const Vector3 direction{std::cos(angle) * radial, y, std::sin(angle) * radial};
            Vector3 position = Add(burst.worldPosition,
                Scale(direction, burst.radius * (0.25f + 3.5f * t)));
            position.y -= burst.radius * t * t;
            const Vector3 tail = Subtract(position, Scale(direction, burst.radius * 0.35f));
            const Vector4 color{1.0f, 0.64f - t * 0.36f, 0.12f, alpha};
            draw.AddLine(tail, position, {0.65f, 0.14f, 0.02f, alpha * 0.35f}, color);
            draw.AddPoint(position, burst.radius * 0.055f * (1.0f - t), color);
        }
    }
}

const EnemyCombatActorPresentation*
EnemyCombatPresentationBridge::FindActor(uint32_t actorId) const noexcept {
    for (const EnemyCombatActorPresentation& actor : frame_.actors) {
        if (actor.actorId == actorId) return &actor;
    }
    return nullptr;
}

const char* ToString(EnemyCombatAnimationState state) noexcept {
    switch (state) {
    case EnemyCombatAnimationState::Spawn: return "Spawn";
    case EnemyCombatAnimationState::Engage: return "Engage";
    case EnemyCombatAnimationState::Telegraph: return "Telegraph";
    case EnemyCombatAnimationState::Attack: return "Attack";
    case EnemyCombatAnimationState::Recover: return "Recover";
    case EnemyCombatAnimationState::HitReact: return "HitReact";
    case EnemyCombatAnimationState::Death: return "Death";
    case EnemyCombatAnimationState::Hidden: return "Hidden";
    }
    return "Unknown";
}

const char* ToString(EnemyCombatPresentationAudioCueKind kind) noexcept {
    switch (kind) {
    case EnemyCombatPresentationAudioCueKind::Spawn: return "Spawn";
    case EnemyCombatPresentationAudioCueKind::Engage: return "Engage";
    case EnemyCombatPresentationAudioCueKind::Attack: return "Attack";
    case EnemyCombatPresentationAudioCueKind::HitReact: return "HitReact";
    case EnemyCombatPresentationAudioCueKind::Death: return "Death";
    }
    return "Unknown";
}
