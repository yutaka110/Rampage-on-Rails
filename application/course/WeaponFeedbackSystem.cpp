#include "WeaponFeedbackSystem.h"

#include "CourseSpawnRuntime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool Finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float LengthSquared(const Vector3& value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

bool PayloadMatchesDamage(
    const WeaponHitRequest& request,
    const DamageResult& damage) {
    if (request.shotId == 0 || request.shotId != damage.shotId ||
        request.targetActorId != damage.targetActorId ||
        request.hitKind != damage.hitKind || request.damageType != damage.damageType ||
        !Finite(request.hitPoint) || !Finite(request.hitNormal) ||
        LengthSquared(request.hitNormal) <= 0.000001f ||
        !std::isfinite(request.baseDamage) || request.baseDamage < 0.0f ||
        !std::isfinite(damage.requestedDamage) || damage.requestedDamage < 0.0f ||
        !std::isfinite(damage.appliedDamage) || damage.appliedDamage < 0.0f ||
        !std::isfinite(damage.remainingHitPoints) || damage.remainingHitPoints < 0.0f) {
        return false;
    }
    const float damageTolerance = (std::max)(0.001f, request.baseDamage * 0.001f);
    if (std::fabs(request.baseDamage - damage.requestedDamage) > damageTolerance ||
        damage.appliedDamage > damage.requestedDamage + damageTolerance ||
        (damage.damageApplied != (damage.appliedDamage > 0.0f)) ||
        (damage.destroyed && !damage.damageApplied) ||
        (damage.weakPointHit && !damage.damageApplied) ||
        (damage.blocked && damage.damageApplied)) {
        return false;
    }
    return true;
}

HitFeedbackKind ResolveFeedbackKind(const DamageResult& damage) {
    if (damage.destroyed) {
        return HitFeedbackKind::Destroyed;
    }
    if (damage.blocked || damage.rejectReason == DamageRejectReason::Indestructible) {
        return HitFeedbackKind::Blocked;
    }
    if (damage.weakPointHit && damage.damageApplied) {
        return HitFeedbackKind::WeakPointHit;
    }
    if (damage.armorReduced && damage.damageApplied) {
        return HitFeedbackKind::ArmorHit;
    }
    if (damage.damageApplied) {
        return HitFeedbackKind::NormalHit;
    }
    if (damage.requestAccepted && damage.targetResolved &&
        damage.rejectReason == DamageRejectReason::None) {
        return HitFeedbackKind::ArmorHit;
    }
    return HitFeedbackKind::None;
}

Vector4 DamageTypeColor(WeaponDamageType damageType) {
    switch (damageType) {
    case WeaponDamageType::Kinetic: return {1.0f, 0.70f, 0.26f, 0.90f};
    case WeaponDamageType::Energy: return {0.56f, 0.92f, 1.0f, 0.90f};
    case WeaponDamageType::Explosive: return {1.0f, 0.42f, 0.12f, 0.94f};
    case WeaponDamageType::Ice: return {0.48f, 0.86f, 1.0f, 0.92f};
    }
    return {1.0f, 1.0f, 1.0f, 0.90f};
}

void ConfigurePresentation(WeaponFeedbackEvent& event) {
    event.impactColor = DamageTypeColor(event.damageType);
    const float damageIntensity = (std::clamp)(event.appliedDamage / 40.0f, 0.0f, 1.0f);
    switch (event.feedbackKind) {
    case HitFeedbackKind::Blocked:
        event.intensity = 0.34f;
        event.hudDuration = 0.12f;
        event.cameraShake = 0.14f;
        event.hitStopSeconds = 0.0f;
        event.controllerLowFrequency = 0.18f;
        event.controllerHighFrequency = 0.34f;
        event.impactCueId = "weapon_feedback_blocked";
        event.impactEffectName = "hit_plane_burst";
        event.audioCueId = "weapon_impact_blocked";
        event.impactColor = {1.0f, 0.62f, 0.18f, 0.88f};
        event.impactRadius = 0.78f;
        event.impactLifetime = 0.32f;
        event.showHitMarker = true;
        break;
    case HitFeedbackKind::NormalHit:
        event.intensity = 0.45f + damageIntensity * 0.25f;
        event.hudDuration = 0.18f;
        event.cameraShake = 0.20f + damageIntensity * 0.10f;
        event.hitStopSeconds = 0.015f + damageIntensity * 0.015f;
        event.controllerLowFrequency = 0.22f + damageIntensity * 0.16f;
        event.controllerHighFrequency = 0.48f + damageIntensity * 0.18f;
        event.impactCueId = "weapon_feedback_hit";
        event.impactEffectName = event.damageType == WeaponDamageType::Ice
            ? "ice_impact"
            : "hit_ring";
        event.audioCueId = "weapon_impact_confirmed";
        event.impactRadius = 0.62f + damageIntensity * 0.18f;
        event.impactLifetime = 0.44f;
        event.showHitMarker = true;
        break;
    case HitFeedbackKind::WeakPointHit:
        event.intensity = 0.82f;
        event.hudDuration = 0.20f;
        event.cameraShake = 0.36f;
        event.hitStopSeconds = 0.045f;
        event.controllerLowFrequency = 0.52f;
        event.controllerHighFrequency = 0.82f;
        event.impactCueId = "weapon_feedback_weak_point";
        event.impactEffectName = "hit_ring";
        event.audioCueId = "weapon_impact_weak_point";
        event.impactColor = {1.0f, 0.90f, 0.24f, 0.96f};
        event.impactRadius = 0.92f;
        event.impactLifetime = 0.52f;
        event.showHitMarker = true;
        break;
    case HitFeedbackKind::ArmorHit:
        event.intensity = 0.28f;
        event.hudDuration = 0.13f;
        event.cameraShake = 0.12f;
        event.controllerHighFrequency = 0.28f;
        event.impactCueId = "weapon_feedback_armor";
        event.impactEffectName = "hit_plane_burst";
        event.audioCueId = "weapon_impact_armor";
        event.impactColor = {0.72f, 0.76f, 0.82f, 0.86f};
        event.impactRadius = 0.70f;
        event.impactLifetime = 0.30f;
        event.showHitMarker = true;
        break;
    case HitFeedbackKind::Destroyed:
        event.intensity = 1.0f;
        event.hudDuration = 0.40f;
        event.cameraShake = 0.48f + damageIntensity * 0.16f;
        event.hitStopSeconds = 0.055f + damageIntensity * 0.025f;
        event.controllerLowFrequency = 0.82f;
        event.controllerHighFrequency = 0.74f;
        event.impactCueId = "weapon_feedback_destroyed";
        event.impactEffectName = "hit_ring";
        event.audioCueId = "weapon_impact_destroyed";
        event.impactColor = {1.0f, 0.82f, 0.30f, 0.98f};
        event.impactRadius = 1.18f;
        event.impactLifetime = 0.70f;
        event.showHitMarker = true;
        break;
    case HitFeedbackKind::None:
        break;
    }
}

void SpawnImpactVfx(CourseSpawnRuntime& runtime, const WeaponFeedbackEvent& event) {
    CourseVfxCueDesc cue{};
    cue.id = event.impactCueId;
    cue.effectName = event.impactEffectName;
    cue.payload = "shotId=" + std::to_string(event.shotId) +
        ";feedback=" + ToHitFeedbackKindString(event.feedbackKind);
    cue.radius = event.impactRadius;
    cue.lifetime = event.impactLifetime;
    cue.color = event.impactColor;
    cue.worldPosition = event.worldPosition;
    cue.hasWorldPosition = true;
    runtime.SpawnVfxCue(std::move(cue));
}
} // namespace

void WeaponFeedbackSystem::Reset() {
    processedShotOrder_.clear();
    processedShotIds_.clear();
    recentEvents_.clear();
    lastResult_ = {};
    lastAcceptedEvent_ = {};
    hitMarkerTimeRemaining_ = 0.0f;
    nextSequence_ = 1;
    acceptedEventCount_ = 0;
    duplicateEventCount_ = 0;
}

void WeaponFeedbackSystem::Update(float deltaTime) {
    const float dt = std::isfinite(deltaTime) ? (std::max)(0.0f, deltaTime) : 0.0f;
    hitMarkerTimeRemaining_ = (std::max)(0.0f, hitMarkerTimeRemaining_ - dt);
}

bool WeaponFeedbackSystem::RememberShot(uint64_t shotId) {
    if (processedShotIds_.contains(shotId)) {
        return false;
    }
    processedShotIds_.insert(shotId);
    processedShotOrder_.push_back(shotId);
    while (processedShotOrder_.size() > kShotHistoryCapacity) {
        processedShotIds_.erase(processedShotOrder_.front());
        processedShotOrder_.pop_front();
    }
    return true;
}

WeaponFeedbackDispatchResult WeaponFeedbackSystem::Submit(
    CourseSpawnRuntime& runtime,
    const WeaponHitRequest& request,
    const DamageResult& damageResult) {
    WeaponFeedbackDispatchResult result{};
    if (!PayloadMatchesDamage(request, damageResult)) {
        result.rejectReason = WeaponFeedbackRejectReason::InvalidPayload;
        lastResult_ = result;
        return result;
    }
    if (damageResult.duplicate ||
        damageResult.rejectReason == DamageRejectReason::DuplicateShot ||
        processedShotIds_.contains(damageResult.shotId)) {
        ++duplicateEventCount_;
        result.rejectReason = WeaponFeedbackRejectReason::DuplicateShot;
        result.duplicate = true;
        lastResult_ = result;
        return result;
    }
    if (!damageResult.requestAccepted) {
        result.rejectReason = WeaponFeedbackRejectReason::DamageRejected;
        lastResult_ = result;
        return result;
    }
    if (!damageResult.targetResolved ||
        damageResult.rejectReason == DamageRejectReason::TargetNotFound ||
        damageResult.rejectReason == DamageRejectReason::TargetAlreadyDestroyed) {
        result.rejectReason = WeaponFeedbackRejectReason::UnresolvedTarget;
        lastResult_ = result;
        return result;
    }

    const HitFeedbackKind feedbackKind = ResolveFeedbackKind(damageResult);
    if (feedbackKind == HitFeedbackKind::None) {
        result.rejectReason = WeaponFeedbackRejectReason::NoFeedback;
        lastResult_ = result;
        return result;
    }
    if (!RememberShot(damageResult.shotId)) {
        ++duplicateEventCount_;
        result.rejectReason = WeaponFeedbackRejectReason::DuplicateShot;
        result.duplicate = true;
        lastResult_ = result;
        return result;
    }

    WeaponFeedbackEvent& event = result.event;
    event.sequence = nextSequence_++;
    if (nextSequence_ == 0) {
        nextSequence_ = 1;
    }
    event.shotId = damageResult.shotId;
    event.targetActorId = damageResult.targetActorId;
    event.hitKind = damageResult.hitKind;
    event.damageType = damageResult.damageType;
    event.feedbackKind = feedbackKind;
    event.worldPosition = request.hitPoint;
    event.worldNormal = request.hitNormal;
    event.incomingDirection = request.rayDirection;
    event.requestedDamage = damageResult.requestedDamage;
    event.appliedDamage = damageResult.appliedDamage;
    event.remainingHitPoints = damageResult.remainingHitPoints;
    event.blocked = damageResult.blocked;
    event.weakPoint = damageResult.weakPointHit;
    event.destroyed = damageResult.destroyed;
    event.lockOnContact = request.lockOnContact && damageResult.damageApplied;
    ConfigurePresentation(event);

    const bool droneCoreBreak=event.destroyed && event.hitKind==RailAimHitKind::Enemy &&
        std::any_of(runtime.Enemies().begin(),runtime.Enemies().end(),[&](const auto& actor) {
            return actor.actorId==event.targetActorId && actor.desc.meshId=="twin_shield_hull";
        });
    if(droneCoreBreak || event.lockOnContact) {
        // Lock arrival owns the short contact; Defeated owns the core cut.
        // Neither should be covered by the old long yellow contact ring.
        event.impactCueId=droneCoreBreak ? "weapon_feedback_drone_core_break" : "weapon_feedback_lock_contact";
        event.impactEffectName.clear();
        event.hudDuration=0.18f;
    } else SpawnImpactVfx(runtime, event);
    result.accepted = true;
    result.vfxSpawned = !droneCoreBreak && !event.lockOnContact;
    ++acceptedEventCount_;
    lastAcceptedEvent_ = event;
    recentEvents_.push_back(event);
    while (recentEvents_.size() > kEventHistoryCapacity) {
        recentEvents_.pop_front();
    }
    hitMarkerTimeRemaining_ = event.showHitMarker ? event.hudDuration : 0.0f;
    lastResult_ = result;
    return result;
}

float WeaponFeedbackSystem::HitMarkerNormalizedTime() const {
    if (lastAcceptedEvent_.hudDuration <= 0.0f) {
        return 0.0f;
    }
    return (std::clamp)(
        hitMarkerTimeRemaining_ / lastAcceptedEvent_.hudDuration,
        0.0f,
        1.0f);
}

const char* ToHitFeedbackKindString(HitFeedbackKind kind) {
    switch (kind) {
    case HitFeedbackKind::None: return "None";
    case HitFeedbackKind::Blocked: return "Blocked";
    case HitFeedbackKind::NormalHit: return "Normal Hit";
    case HitFeedbackKind::WeakPointHit: return "Weak Point Hit";
    case HitFeedbackKind::ArmorHit: return "Armor Hit";
    case HitFeedbackKind::Destroyed: return "Destroyed";
    }
    return "Unknown";
}

const char* ToWeaponFeedbackRejectReasonString(WeaponFeedbackRejectReason reason) {
    switch (reason) {
    case WeaponFeedbackRejectReason::None: return "None";
    case WeaponFeedbackRejectReason::InvalidPayload: return "Invalid Payload";
    case WeaponFeedbackRejectReason::DamageRejected: return "Damage Rejected";
    case WeaponFeedbackRejectReason::UnresolvedTarget: return "Unresolved Target";
    case WeaponFeedbackRejectReason::DuplicateShot: return "Duplicate Shot";
    case WeaponFeedbackRejectReason::NoFeedback: return "No Feedback";
    }
    return "Unknown";
}
