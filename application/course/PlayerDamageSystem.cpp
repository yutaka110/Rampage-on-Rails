#include "PlayerDamageSystem.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
void SetError(std::string* errorMessage, std::string message) {
    if (errorMessage != nullptr) *errorMessage = std::move(message);
}

bool ValidState(const PlayerDamageRuntimeState& state) {
    return state.initialized && std::isfinite(state.hitPoints) &&
        std::isfinite(state.maximumHitPoints) &&
        std::isfinite(state.invulnerabilityRemainingSeconds) &&
        state.maximumHitPoints > 0.0f && state.hitPoints >= 0.0f &&
        state.hitPoints <= state.maximumHitPoints + 0.001f &&
        state.invulnerabilityRemainingSeconds >= 0.0f &&
        state.nextResultSequence != 0;
}
} // namespace

bool PlayerDamageSystemSettings::Validate(std::string* errorMessage) const {
    if (projectileHistoryCapacity == 0 || projectileHistoryCapacity > 65536 ||
        !std::isfinite(maximumDamagePerHit) || maximumDamagePerHit <= 0.0f ||
        !std::isfinite(maximumInvulnerabilitySeconds) || maximumInvulnerabilitySeconds < 0.0f) {
        SetError(errorMessage, "Player damage settings require a bounded nonzero history, positive damage cap and nonnegative finite invulnerability cap.");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool PlayerDamageSystem::Configure(
    const PlayerDamageSystemSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    // Keep the newest projectile IDs when reducing the duplicate-hit window.
    if (state_.consumedProjectileIds.size() > settings.projectileHistoryCapacity) {
        state_.consumedProjectileIds.erase(state_.consumedProjectileIds.begin(),
            state_.consumedProjectileIds.end() - settings.projectileHistoryCapacity);
    }
    const float remaining = (std::min)(state_.invulnerabilityRemainingSeconds,
        settings.maximumInvulnerabilitySeconds);
    if (remaining != state_.invulnerabilityRemainingSeconds ||
        settings.projectileHistoryCapacity != settings_.projectileHistoryCapacity) {
        ++state_.revision;
    }
    state_.invulnerabilityRemainingSeconds = remaining;
    settings_ = settings;
    return true;
}

bool PlayerDamageSystem::Initialize(
    float maximumHitPoints,
    float hitPoints,
    std::string* errorMessage) {
    if (!std::isfinite(maximumHitPoints) ||
        !std::isfinite(hitPoints) || maximumHitPoints <= 0.0f ||
        hitPoints < 0.0f || hitPoints > maximumHitPoints) {
        SetError(errorMessage, "Player damage health values are invalid.");
        return false;
    }
    state_ = {};
    state_.maximumHitPoints = maximumHitPoints;
    state_.hitPoints = hitPoints;
    state_.initialized = true;
    state_.revision = 1;
    resultsThisFrame_.clear();
    lastResult_ = {};
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void PlayerDamageSystem::Reset(float maximumHitPoints, float hitPoints) {
    if (!Initialize(maximumHitPoints, hitPoints, nullptr)) {
        (void)Initialize(100.0f, 100.0f, nullptr);
    }
}

void PlayerDamageSystem::SynchronizeHealth(
    float hitPoints,
    float maximumHitPoints) {
    if (!std::isfinite(hitPoints) || !std::isfinite(maximumHitPoints) ||
        maximumHitPoints <= 0.0f) {
        return;
    }
    const float clamped = (std::clamp)(hitPoints, 0.0f, maximumHitPoints);
    if (!state_.initialized) {
        (void)Initialize(maximumHitPoints, clamped, nullptr);
        return;
    }
    if (std::abs(state_.hitPoints - clamped) > 0.0001f ||
        std::abs(state_.maximumHitPoints - maximumHitPoints) > 0.0001f) {
        state_.hitPoints = clamped;
        state_.maximumHitPoints = maximumHitPoints;
        ++state_.revision;
    }
}

void PlayerDamageSystem::Update(
    float deltaTime,
    float externalInvulnerabilitySeconds) {
    resultsThisFrame_.clear();
    const float dt = std::isfinite(deltaTime)
        ? (std::max)(0.0f, deltaTime)
        : 0.0f;
    const float external = std::isfinite(externalInvulnerabilitySeconds)
        ? (std::clamp)(
            externalInvulnerabilitySeconds,
            0.0f,
            settings_.maximumInvulnerabilitySeconds)
        : 0.0f;
    const float previous = state_.invulnerabilityRemainingSeconds;
    state_.invulnerabilityRemainingSeconds = (std::max)(
        external,
        (std::max)(0.0f, previous - dt));
    if (std::abs(previous - state_.invulnerabilityRemainingSeconds) > 0.0001f) {
        ++state_.revision;
    }
}

PlayerDamageResult PlayerDamageSystem::Submit(
    const PlayerHitRequest& request) {
    if (!state_.initialized) {
        return Reject(
            request,
            PlayerDamageRejectReason::NotInitialized,
            request.sourceProjectileId != 0);
    }
    const bool validRequest = std::isfinite(request.rawDamage) &&
        std::isfinite(request.postHitInvulnerabilitySeconds) &&
        std::isfinite(request.railDistance) &&
        std::isfinite(request.lateralOffset) &&
        std::isfinite(request.verticalOffset) &&
        (!request.hasWorldImpact ||
         (std::isfinite(request.impactWorldPosition.x) &&
          std::isfinite(request.impactWorldPosition.y) &&
          std::isfinite(request.impactWorldPosition.z) &&
          std::isfinite(request.impactNormalWorld.x) &&
          std::isfinite(request.impactNormalWorld.y) &&
          std::isfinite(request.impactNormalWorld.z) &&
          request.impactNormalWorld.x * request.impactNormalWorld.x +
                  request.impactNormalWorld.y * request.impactNormalWorld.y +
                  request.impactNormalWorld.z * request.impactNormalWorld.z >
              0.000001f)) &&
        request.rawDamage > 0.0f &&
        request.postHitInvulnerabilitySeconds >= 0.0f;
    if (!validRequest) {
        return Reject(
            request,
            PlayerDamageRejectReason::InvalidRequest,
            request.sourceProjectileId != 0);
    }
    if (request.sourceProjectileId != 0 &&
        HasConsumedProjectile(request.sourceProjectileId)) {
        return Reject(
            request,
            PlayerDamageRejectReason::DuplicateProjectile,
            true);
    }
    if (state_.hitPoints <= 0.0f) {
        return Reject(
            request,
            PlayerDamageRejectReason::PlayerDefeated,
            request.sourceProjectileId != 0);
    }
    if (state_.invulnerabilityRemainingSeconds > 0.0f) {
        return Reject(
            request,
            PlayerDamageRejectReason::Invulnerable,
            false);
    }

    PlayerDamageResult result{};
    result.sequence = state_.nextResultSequence++;
    if (state_.nextResultSequence == 0) state_.nextResultSequence = 1;
    result.request = request;
    result.hitPointsBefore = state_.hitPoints;
    const float boundedDamage = (std::clamp)(
        request.rawDamage,
        0.0f,
        settings_.maximumDamagePerHit);
    result.appliedDamage = (std::min)(state_.hitPoints, boundedDamage);
    state_.hitPoints -= result.appliedDamage;
    result.hitPointsAfter = state_.hitPoints;
    result.invulnerabilityGrantedSeconds = (std::clamp)(
        request.postHitInvulnerabilitySeconds,
        0.0f,
        settings_.maximumInvulnerabilitySeconds);
    state_.invulnerabilityRemainingSeconds = (std::max)(
        state_.invulnerabilityRemainingSeconds,
        result.invulnerabilityGrantedSeconds);
    result.accepted = result.appliedDamage > 0.0f;
    result.lethal = result.accepted && state_.hitPoints <= 0.0f;
    result.projectileConsumed =
        request.kind == PlayerHitKind::EnemyProjectile;
    if (result.projectileConsumed && request.sourceProjectileId != 0) {
        RememberProjectile(request.sourceProjectileId);
    }
    if (result.accepted) {
        state_.lastAcceptedSequence = result.sequence;
        ++state_.acceptedHits;
    }
    ++state_.revision;
    lastResult_ = result;
    resultsThisFrame_.push_back(result);
    return result;
}

bool PlayerDamageSystem::RestoreCheckpoint(
    const PlayerDamageRuntimeState& checkpoint,
    std::string* errorMessage) {
    if (!ValidState(checkpoint) ||
        checkpoint.invulnerabilityRemainingSeconds >
            settings_.maximumInvulnerabilitySeconds + 0.001f ||
        checkpoint.consumedProjectileIds.size() >
            settings_.projectileHistoryCapacity) {
        SetError(errorMessage, "Player damage checkpoint is invalid.");
        return false;
    }
    state_ = checkpoint;
    resultsThisFrame_.clear();
    lastResult_ = {};
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

bool PlayerDamageSystem::HasConsumedProjectile(uint64_t projectileId) const {
    return projectileId != 0 && std::find(
        state_.consumedProjectileIds.begin(),
        state_.consumedProjectileIds.end(),
        projectileId) != state_.consumedProjectileIds.end();
}

void PlayerDamageSystem::RememberProjectile(uint64_t projectileId) {
    if (projectileId == 0 || settings_.projectileHistoryCapacity == 0 ||
        HasConsumedProjectile(projectileId)) {
        return;
    }
    state_.consumedProjectileIds.push_back(projectileId);
    if (state_.consumedProjectileIds.size() >
        settings_.projectileHistoryCapacity) {
        state_.consumedProjectileIds.erase(
            state_.consumedProjectileIds.begin(),
            state_.consumedProjectileIds.begin() +
                static_cast<std::ptrdiff_t>(
                    state_.consumedProjectileIds.size() -
                    settings_.projectileHistoryCapacity));
    }
}

PlayerDamageResult PlayerDamageSystem::Reject(
    const PlayerHitRequest& request,
    PlayerDamageRejectReason reason,
    bool consumeProjectile) {
    PlayerDamageResult result{};
    result.sequence = state_.nextResultSequence++;
    if (state_.nextResultSequence == 0) state_.nextResultSequence = 1;
    result.request = request;
    result.rejectReason = reason;
    result.hitPointsBefore = state_.hitPoints;
    result.hitPointsAfter = state_.hitPoints;
    result.projectileConsumed = consumeProjectile;
    if (consumeProjectile && request.sourceProjectileId != 0) {
        RememberProjectile(request.sourceProjectileId);
    }
    ++state_.revision;
    lastResult_ = result;
    resultsThisFrame_.push_back(result);
    return result;
}

const char* ToString(PlayerHitKind kind) noexcept {
    switch (kind) {
    case PlayerHitKind::EnemyProjectile: return "EnemyProjectile";
    case PlayerHitKind::ObstacleContact: return "ObstacleContact";
    case PlayerHitKind::TerrainContact: return "TerrainContact";
    case PlayerHitKind::ScriptedHazard: return "ScriptedHazard";
    }
    return "Unknown";
}

const char* ToString(PlayerDamageRejectReason reason) noexcept {
    switch (reason) {
    case PlayerDamageRejectReason::None: return "None";
    case PlayerDamageRejectReason::InvalidRequest: return "InvalidRequest";
    case PlayerDamageRejectReason::NotInitialized: return "NotInitialized";
    case PlayerDamageRejectReason::PlayerDefeated: return "PlayerDefeated";
    case PlayerDamageRejectReason::Invulnerable: return "Invulnerable";
    case PlayerDamageRejectReason::DuplicateProjectile: return "DuplicateProjectile";
    }
    return "Unknown";
}
