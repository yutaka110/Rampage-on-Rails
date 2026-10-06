#include "WeaponFireSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace {
constexpr float kTimerEpsilon = 0.00001f;
constexpr uint32_t kMaximumCatchUpSalvos = 4;

bool FiniteNonNegative(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

float SaturatedFinite(double value) {
    const double limit = (std::numeric_limits<float>::max)();
    return static_cast<float>((std::clamp)(value, -limit, limit));
}

bool ValidateDefinitionInternal(const WeaponDefinition& definition, std::string* errorMessage) {
    auto reject = [errorMessage](const char* message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (definition.weaponId.empty()) {
        return reject("weaponId must not be empty");
    }
    if (definition.fireMode < WeaponFireMode::SemiAutomatic || definition.fireMode > WeaponFireMode::ReleaseVolley ||
        definition.damageType < WeaponDamageType::Kinetic || definition.damageType > WeaponDamageType::Ice) {
        return reject("weapon fire mode and damage type must be recognized");
    }
    if (!FiniteNonNegative(definition.baseDamage) ||
        !std::isfinite(definition.range) || definition.range <= 0.0f ||
        !std::isfinite(definition.shotInterval) || definition.shotInterval <= 0.0f ||
        definition.projectilesPerShot == 0 || definition.projectilesPerShot > 4096 ||
        definition.maxProjectilesPerTrigger == 0 || definition.maxProjectilesPerTrigger > 4096) {
        return reject("damage, range, cadence, and projectile counts must be valid");
    }
    if (definition.fireMode == WeaponFireMode::Burst &&
        (definition.burstCount == 0 || definition.burstCount > 4096 || !std::isfinite(definition.burstInterval) ||
         definition.burstInterval <= 0.0f)) {
        return reject("burst weapons require a positive burst count and interval");
    }
    if (!FiniteNonNegative(definition.reloadDuration) ||
        !FiniteNonNegative(definition.heatPerProjectile) ||
        !FiniteNonNegative(definition.heatCapacity) ||
        !FiniteNonNegative(definition.coolingPerSecond) ||
        !std::isfinite(definition.overheatRecoveryFraction) ||
        definition.overheatRecoveryFraction < 0.0f ||
        definition.overheatRecoveryFraction > 1.0f) {
        return reject("reload and heat values must be finite and non-negative");
    }
    if (!FiniteNonNegative(definition.minimumChargeSeconds) ||
        !FiniteNonNegative(definition.maximumChargeSeconds) ||
        !std::isfinite(definition.maximumChargeDamageMultiplier) ||
        definition.maximumChargeDamageMultiplier < 1.0f ||
        definition.minimumChargeSeconds > definition.maximumChargeSeconds) {
        return reject("charge values must define a valid range and multiplier");
    }
    if (definition.fireMode == WeaponFireMode::ChargeRelease &&
        definition.maximumChargeSeconds <= 0.0f) {
        return reject("charge-release weapons require a positive maximum charge time");
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

void CompleteReload(const WeaponDefinition& definition, WeaponRuntimeState& state) {
    if (!state.reloading) {
        return;
    }
    const uint32_t missing = definition.magazineCapacity > state.ammoInMagazine
        ? definition.magazineCapacity - state.ammoInMagazine
        : 0;
    const uint32_t loaded = (std::min)(missing, state.reserveAmmo);
    state.ammoInMagazine += loaded;
    state.reserveAmmo -= loaded;
    state.reloadRemaining = 0.0f;
    state.reloading = false;
}

void TickState(
    const WeaponDefinition& definition,
    WeaponRuntimeState& state,
    float deltaTime) {
    const float dt = std::isfinite(deltaTime) ? (std::max)(0.0f, deltaTime) : 0.0f;
    state.cooldownRemaining = SaturatedFinite(static_cast<double>(state.cooldownRemaining) - dt);
    if (!state.triggerWasHeld && state.burstShotsRemaining == 0) {
        state.cooldownRemaining = (std::max)(0.0f, state.cooldownRemaining);
    }

    if (state.reloading) {
        state.reloadRemaining -= dt;
        if (state.reloadRemaining <= kTimerEpsilon) {
            CompleteReload(definition, state);
        }
    }

    state.heat = (std::max)(0.0f, state.heat - definition.coolingPerSecond * dt);
    if (state.overheated &&
        (definition.heatCapacity <= 0.0f ||
         state.heat <= definition.heatCapacity * definition.overheatRecoveryFraction)) {
        state.overheated = false;
    }
}

void CopyRuntimeToResult(const WeaponRuntimeState& state, WeaponFireResult& result) {
    result.cooldownRemaining = (std::max)(0.0f, state.cooldownRemaining);
    result.reloadRemaining = (std::max)(0.0f, state.reloadRemaining);
    result.heat = state.heat;
    result.chargeSeconds = state.chargeSeconds;
    result.ammoInMagazine = state.ammoInMagazine;
    result.reserveAmmo = state.reserveAmmo;
    result.reloading = state.reloading;
    result.overheated = state.overheated;
}
} // namespace

void WeaponFireSystem::Reset() {
    definitions_.clear();
    runtimeStates_.clear();
    lastResult_ = {};
    nextShotId_ = 1;
    totalProjectilesFired_ = 0;
}

bool WeaponFireSystem::RegisterDefinition(
    const WeaponDefinition& definition,
    std::string* errorMessage) {
    if (!ValidateWeaponDefinition(definition, errorMessage)) {
        return false;
    }

    const auto oldDefinition = definitions_.find(definition.weaponId);
    const bool existed = oldDefinition != definitions_.end();
    const uint32_t oldCapacity = existed ? oldDefinition->second.magazineCapacity : 0;
    const WeaponFireMode oldMode = existed ? oldDefinition->second.fireMode : definition.fireMode;
    definitions_[definition.weaponId] = definition;

    WeaponRuntimeState& state = runtimeStates_[definition.weaponId];
    if (state.weaponId.empty()) {
        state.weaponId = definition.weaponId;
        state.ammoInMagazine = definition.magazineCapacity;
        state.reserveAmmo = definition.initialReserveAmmo;
    } else if (definition.magazineCapacity == 0) {
        state.ammoInMagazine = 0;
        state.reserveAmmo = 0;
        state.reloading = false;
        state.reloadRemaining = 0.0f;
    } else if (oldCapacity == 0) {
        state.ammoInMagazine = definition.magazineCapacity;
        state.reserveAmmo = definition.initialReserveAmmo;
    } else {
        state.ammoInMagazine = (std::min)(state.ammoInMagazine, definition.magazineCapacity);
    }
    state.heat = definition.heatCapacity > 0.0f
        ? (std::min)(state.heat, definition.heatCapacity)
        : 0.0f;
    // 定義を再読み込みしても、実行中の状態は新しい定義の範囲を守る。
    if (definition.magazineCapacity == 0) state.ammoInMagazine = state.reserveAmmo = 0;
    if (definition.magazineCapacity == 0 || definition.reloadDuration == 0.0f) {
        state.reloading = false;
        state.reloadRemaining = 0.0f;
    } else {
        state.reloadRemaining = (std::min)(state.reloadRemaining, definition.reloadDuration);
    }
    if (oldMode != definition.fireMode) {
        state.chargeSeconds = 0.0f;
        state.burstShotsRemaining = 0;
        state.triggerWasHeld = false;
    }
    state.chargeSeconds = definition.fireMode == WeaponFireMode::ChargeRelease
        ? (std::min)(state.chargeSeconds, definition.maximumChargeSeconds) : 0.0f;
    state.burstShotsRemaining = definition.fireMode == WeaponFireMode::Burst
        ? (std::min)(state.burstShotsRemaining, definition.burstCount) : 0;
    if (definition.heatCapacity == 0.0f ||
        state.heat <= definition.heatCapacity * definition.overheatRecoveryFraction) {
        state.overheated = false;
    } else if (state.heat >= definition.heatCapacity) {
        state.overheated = true;
    }
    return true;
}

bool WeaponFireSystem::ReplaceDefinitionSet(
    const std::vector<WeaponDefinition>& definitions,
    std::string* errorMessage) {
    std::unordered_set<std::string> activeWeaponIds;
    activeWeaponIds.reserve(definitions.size());
    for (const WeaponDefinition& definition : definitions) {
        std::string validationError;
        if (!ValidateWeaponDefinition(definition, &validationError)) {
            if (errorMessage != nullptr) {
                *errorMessage = validationError;
            }
            return false;
        }
        if (!activeWeaponIds.emplace(definition.weaponId).second) {
            if (errorMessage != nullptr) {
                *errorMessage = "Duplicate weapon definition: " + definition.weaponId;
            }
            return false;
        }
    }

    // Work on a complete copy so a validation or registration failure cannot
    // partially mutate the live definitions or their runtime state.
    WeaponFireSystem staged = *this;
    for (const WeaponDefinition& definition : definitions) {
        std::string registrationError;
        if (!staged.RegisterDefinition(definition, &registrationError)) {
            if (errorMessage != nullptr) {
                *errorMessage = registrationError;
            }
            return false;
        }
    }
    for (auto iterator = staged.definitions_.begin(); iterator != staged.definitions_.end();) {
        if (!activeWeaponIds.contains(iterator->first)) {
            staged.runtimeStates_.erase(iterator->first);
            iterator = staged.definitions_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    *this = std::move(staged);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

const WeaponDefinition* WeaponFireSystem::FindDefinition(const std::string& weaponId) const {
    const auto found = definitions_.find(weaponId);
    return found != definitions_.end() ? &found->second : nullptr;
}

const WeaponRuntimeState* WeaponFireSystem::FindRuntimeState(const std::string& weaponId) const {
    const auto found = runtimeStates_.find(weaponId);
    return found != runtimeStates_.end() ? &found->second : nullptr;
}

WeaponRuntimeState* WeaponFireSystem::FindMutableRuntimeState(const std::string& weaponId) {
    const auto found = runtimeStates_.find(weaponId);
    return found != runtimeStates_.end() ? &found->second : nullptr;
}

bool WeaponFireSystem::BeginReload(const std::string& weaponId) {
    const WeaponDefinition* definition = FindDefinition(weaponId);
    WeaponRuntimeState* state = FindMutableRuntimeState(weaponId);
    if (definition == nullptr || state == nullptr || definition->magazineCapacity == 0 ||
        definition->reloadDuration <= 0.0f || state->reloading || state->reserveAmmo == 0 ||
        state->ammoInMagazine >= definition->magazineCapacity) {
        return false;
    }
    state->reloading = true;
    state->reloadRemaining = definition->reloadDuration;
    state->burstShotsRemaining = 0;
    return true;
}

uint64_t WeaponFireSystem::AllocateShotId() {
    uint64_t result = nextShotId_++;
    if (result == 0) {
        result = nextShotId_++;
    }
    if (nextShotId_ == 0) {
        nextShotId_ = 1;
    }
    return result;
}

WeaponFireResult WeaponFireSystem::Update(const WeaponFireInput& input) {
    WeaponFireResult result{};
    result.weaponId = input.weaponId;
    const auto definitionIt = definitions_.find(input.weaponId);
    const auto stateIt = runtimeStates_.find(input.weaponId);
    if (definitionIt == definitions_.end() || stateIt == runtimeStates_.end()) {
        result.rejectReason = WeaponFireRejectReason::UnknownWeapon;
        lastResult_ = result;
        return result;
    }
    const WeaponDefinition& definition = definitionIt->second;
    WeaponRuntimeState& state = stateIt->second;
    const double chargeMultiplier = definition.fireMode == WeaponFireMode::ChargeRelease
        ? definition.maximumChargeDamageMultiplier : 1.0;
    const double maximumShotDamage = static_cast<double>(definition.baseDamage) *
        input.damageMultiplier * chargeMultiplier;
    if (!FiniteNonNegative(input.deltaTime) || !FiniteNonNegative(input.damageMultiplier) ||
        !std::isfinite(maximumShotDamage) || maximumShotDamage > (std::numeric_limits<float>::max)()) {
        result.rejectReason = WeaponFireRejectReason::InvalidInput;
        CopyRuntimeToResult(state, result);
        lastResult_ = result;
        return result;
    }
    TickState(definition, state, input.deltaTime);

    const bool pressed = input.triggerPressed || (input.triggerHeld && !state.triggerWasHeld);
    const bool released = input.triggerReleased || (!input.triggerHeld && state.triggerWasHeld);
    const float previousCharge = state.chargeSeconds;
    if (definition.fireMode == WeaponFireMode::ChargeRelease && input.triggerHeld) {
        state.chargeSeconds = (std::min)(
            definition.maximumChargeSeconds,
            state.chargeSeconds + (std::max)(0.0f, input.deltaTime));
    }

    bool activated = false;
    switch (definition.fireMode) {
    case WeaponFireMode::SemiAutomatic:
        activated = pressed;
        break;
    case WeaponFireMode::Automatic:
        activated = input.triggerHeld;
        break;
    case WeaponFireMode::Burst:
        if (pressed && state.burstShotsRemaining == 0) {
            state.burstShotsRemaining = definition.burstCount;
        }
        activated = state.burstShotsRemaining > 0;
        break;
    case WeaponFireMode::ChargeRelease:
        activated = released;
        break;
    case WeaponFireMode::ReleaseVolley:
        activated = released;
        break;
    }

    auto finish = [&](WeaponFireRejectReason reason) {
        result.rejectReason = reason;
        state.triggerWasHeld = input.triggerHeld;
        CopyRuntimeToResult(state, result);
        lastResult_ = result;
        return result;
    };

    if (!input.enabled) {
        if (released && definition.fireMode == WeaponFireMode::ChargeRelease) {
            state.chargeSeconds = 0.0f;
        }
        return finish(WeaponFireRejectReason::Disabled);
    }
    if (!activated) {
        if (!input.triggerHeld && definition.fireMode == WeaponFireMode::ChargeRelease) {
            state.chargeSeconds = 0.0f;
        }
        return finish(WeaponFireRejectReason::TriggerNotActivated);
    }
    if (definition.fireMode == WeaponFireMode::ReleaseVolley &&
        input.requestedProjectileCount == 0) {
        return finish(WeaponFireRejectReason::NoProjectiles);
    }
    if (definition.fireMode == WeaponFireMode::ChargeRelease &&
        previousCharge + kTimerEpsilon < definition.minimumChargeSeconds) {
        state.chargeSeconds = 0.0f;
        return finish(WeaponFireRejectReason::ChargeInsufficient);
    }
    if (state.reloading) {
        return finish(WeaponFireRejectReason::Reloading);
    }
    if (state.overheated) {
        return finish(WeaponFireRejectReason::Overheated);
    }
    if (state.cooldownRemaining > kTimerEpsilon) {
        return finish(WeaponFireRejectReason::Cooldown);
    }

    uint32_t salvos = 1;
    float cadence = definition.shotInterval;
    if (definition.fireMode == WeaponFireMode::Automatic && state.triggerWasHeld) {
        const float overdue = (std::max)(0.0f, -state.cooldownRemaining);
        salvos = 1u + static_cast<uint32_t>((std::min)(
            static_cast<double>(overdue) / definition.shotInterval,
            static_cast<double>(kMaximumCatchUpSalvos - 1)));
    } else if (definition.fireMode == WeaponFireMode::Burst) {
        cadence = definition.burstInterval;
        const float overdue = (std::max)(0.0f, -state.cooldownRemaining);
        salvos = 1u + static_cast<uint32_t>((std::min)(
            static_cast<double>(overdue) / definition.burstInterval,
            static_cast<double>(kMaximumCatchUpSalvos - 1)));
        salvos = (std::min)(salvos, state.burstShotsRemaining);
    }

    uint32_t projectileCount = definition.fireMode == WeaponFireMode::ReleaseVolley
        ? (std::min)(input.requestedProjectileCount, definition.maxProjectilesPerTrigger)
        : definition.projectilesPerShot * salvos;
    projectileCount = (std::min)(projectileCount, definition.maxProjectilesPerTrigger * salvos);
    if (definition.magazineCapacity > 0) {
        projectileCount = (std::min)(projectileCount, state.ammoInMagazine);
        if (projectileCount == 0) {
            if (definition.autoReload) {
                BeginReload(definition.weaponId);
            }
            return finish(state.reloading
                ? WeaponFireRejectReason::Reloading
                : WeaponFireRejectReason::OutOfAmmo);
        }
    }
    if (definition.heatCapacity > 0.0f && definition.heatPerProjectile > 0.0f) {
        const float heatRoom = (std::max)(0.0f, definition.heatCapacity - state.heat);
        const uint32_t heatLimitedCount = static_cast<uint32_t>((std::min)(
            std::floor(static_cast<double>(heatRoom) / definition.heatPerProjectile + kTimerEpsilon),
            static_cast<double>(projectileCount)));
        projectileCount = (std::min)(projectileCount, heatLimitedCount);
        if (projectileCount == 0) {
            state.overheated = true;
            return finish(WeaponFireRejectReason::Overheated);
        }
    }

    float chargeRatio = 0.0f;
    float chargeDamageMultiplier = 1.0f;
    if (definition.fireMode == WeaponFireMode::ChargeRelease) {
        chargeRatio = definition.maximumChargeSeconds > 0.0f
            ? (std::clamp)(previousCharge / definition.maximumChargeSeconds, 0.0f, 1.0f)
            : 0.0f;
        chargeDamageMultiplier = 1.0f +
            (definition.maximumChargeDamageMultiplier - 1.0f) * chargeRatio;
        state.chargeSeconds = 0.0f;
    }

    result.shots.reserve(projectileCount);
    for (uint32_t projectileIndex = 0; projectileIndex < projectileCount; ++projectileIndex) {
        WeaponShot shot{};
        shot.shotId = AllocateShotId();
        shot.projectileIndex = projectileIndex;
        shot.damageType = definition.damageType;
        shot.damage = definition.baseDamage * input.damageMultiplier * chargeDamageMultiplier;
        shot.range = definition.range;
        shot.chargeRatio = chargeRatio;
        result.shots.push_back(shot);
    }

    result.fired = !result.shots.empty();
    if (definition.magazineCapacity > 0) {
        state.ammoInMagazine -= projectileCount;
    }
    if (definition.heatCapacity > 0.0f) {
        state.heat += definition.heatPerProjectile * static_cast<float>(projectileCount);
    }
    if (definition.heatCapacity > 0.0f && state.heat + kTimerEpsilon >= definition.heatCapacity) {
        state.heat = (std::min)(state.heat, definition.heatCapacity);
        state.overheated = true;
    }
    if (definition.fireMode == WeaponFireMode::Burst) {
        state.burstShotsRemaining -= (std::min)(salvos, state.burstShotsRemaining);
        state.cooldownRemaining = SaturatedFinite(static_cast<double>(state.cooldownRemaining) +
            (state.burstShotsRemaining > 0 ? static_cast<double>(cadence) * salvos : definition.shotInterval));
    } else {
        state.cooldownRemaining = SaturatedFinite(static_cast<double>(state.cooldownRemaining) +
            static_cast<double>(definition.shotInterval) * salvos);
    }
    state.totalProjectilesFired += projectileCount;
    totalProjectilesFired_ += projectileCount;
    state.triggerWasHeld = input.triggerHeld;
    CopyRuntimeToResult(state, result);
    lastResult_ = result;
    return result;
}

const char* ToWeaponFireModeString(WeaponFireMode mode) {
    switch (mode) {
    case WeaponFireMode::SemiAutomatic: return "Semi Automatic";
    case WeaponFireMode::Automatic: return "Automatic";
    case WeaponFireMode::Burst: return "Burst";
    case WeaponFireMode::ChargeRelease: return "Charge Release";
    case WeaponFireMode::ReleaseVolley: return "Release Volley";
    }
    return "Unknown";
}

const char* ToWeaponFireRejectReasonString(WeaponFireRejectReason reason) {
    switch (reason) {
    case WeaponFireRejectReason::None: return "None";
    case WeaponFireRejectReason::Disabled: return "Disabled";
    case WeaponFireRejectReason::UnknownWeapon: return "Unknown Weapon";
    case WeaponFireRejectReason::InvalidDefinition: return "Invalid Definition";
    case WeaponFireRejectReason::TriggerNotActivated: return "Trigger Not Activated";
    case WeaponFireRejectReason::Cooldown: return "Cooldown";
    case WeaponFireRejectReason::Reloading: return "Reloading";
    case WeaponFireRejectReason::OutOfAmmo: return "Out Of Ammo";
    case WeaponFireRejectReason::Overheated: return "Overheated";
    case WeaponFireRejectReason::ChargeInsufficient: return "Charge Insufficient";
    case WeaponFireRejectReason::NoProjectiles: return "No Projectiles";
    case WeaponFireRejectReason::InvalidInput: return "Invalid Input";
    }
    return "Unknown";
}

bool ValidateWeaponDefinition(
    const WeaponDefinition& definition,
    std::string* errorMessage) {
    return ValidateDefinitionInternal(definition, errorMessage);
}
