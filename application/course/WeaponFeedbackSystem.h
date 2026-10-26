#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

#include "WeaponDamageSystem.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;

enum class HitFeedbackKind : uint8_t {
    None,
    Blocked,
    NormalHit,
    WeakPointHit,
    ArmorHit,
    Destroyed,
};

enum class WeaponFeedbackRejectReason : uint8_t {
    None,
    InvalidPayload,
    DamageRejected,
    UnresolvedTarget,
    DuplicateShot,
    NoFeedback,
};

// Presentation-ready event derived exclusively from DamageResult. Spatial data
// comes from the matching WeaponHitRequest but may not override damage outcome.
struct WeaponFeedbackEvent {
    uint64_t sequence = 0;
    uint64_t shotId = 0;
    uint32_t targetActorId = 0;
    RailAimHitKind hitKind = RailAimHitKind::None;
    WeaponDamageType damageType = WeaponDamageType::Kinetic;
    HitFeedbackKind feedbackKind = HitFeedbackKind::None;
    Vector3 worldPosition{};
    Vector3 worldNormal{};
    Vector3 incomingDirection{};
    float requestedDamage = 0.0f;
    float appliedDamage = 0.0f;
    float remainingHitPoints = 0.0f;
    float intensity = 0.0f;
    float hudDuration = 0.0f;
    float cameraShake = 0.0f;
    float hitStopSeconds = 0.0f;
    float controllerLowFrequency = 0.0f;
    float controllerHighFrequency = 0.0f;
    std::string impactCueId;
    std::string impactEffectName;
    std::string audioCueId;
    Vector4 impactColor{};
    float impactRadius = 0.0f;
    float impactLifetime = 0.0f;
    bool showHitMarker = false;
    bool blocked = false;
    bool weakPoint = false;
    bool destroyed = false;
    bool lockOnContact = false;
};

struct WeaponFeedbackDispatchResult {
    WeaponFeedbackRejectReason rejectReason = WeaponFeedbackRejectReason::None;
    WeaponFeedbackEvent event{};
    bool accepted = false;
    bool duplicate = false;
    bool vfxSpawned = false;
};

class WeaponFeedbackSystem {
public:
    void Reset();
    void Update(float deltaTime);
    WeaponFeedbackDispatchResult Submit(
        CourseSpawnRuntime& runtime,
        const WeaponHitRequest& request,
        const DamageResult& damageResult);

    const WeaponFeedbackDispatchResult& LastResult() const { return lastResult_; }
    const WeaponFeedbackEvent& LastAcceptedEvent() const { return lastAcceptedEvent_; }
    const std::deque<WeaponFeedbackEvent>& RecentEvents() const { return recentEvents_; }
    bool HitMarkerActive() const { return hitMarkerTimeRemaining_ > 0.0f; }
    float HitMarkerTimeRemaining() const { return hitMarkerTimeRemaining_; }
    float HitMarkerNormalizedTime() const;
    uint64_t AcceptedEventCount() const { return acceptedEventCount_; }
    uint64_t DuplicateEventCount() const { return duplicateEventCount_; }

private:
    bool RememberShot(uint64_t shotId);

    static constexpr size_t kShotHistoryCapacity = 4096;
    static constexpr size_t kEventHistoryCapacity = 256;
    std::deque<uint64_t> processedShotOrder_;
    std::unordered_set<uint64_t> processedShotIds_;
    std::deque<WeaponFeedbackEvent> recentEvents_;
    WeaponFeedbackDispatchResult lastResult_{};
    WeaponFeedbackEvent lastAcceptedEvent_{};
    float hitMarkerTimeRemaining_ = 0.0f;
    uint64_t nextSequence_ = 1;
    uint64_t acceptedEventCount_ = 0;
    uint64_t duplicateEventCount_ = 0;
};

const char* ToHitFeedbackKindString(HitFeedbackKind kind);
const char* ToWeaponFeedbackRejectReasonString(WeaponFeedbackRejectReason reason);
