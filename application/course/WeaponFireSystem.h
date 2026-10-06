#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "WeaponDamageSystem.h"

namespace RailWeaponIds {
inline constexpr char PulseCannon[] = "rail.pulse_cannon";
inline constexpr char LockOnIce[] = "rail.lock_on_ice";
} // namespace RailWeaponIds

enum class WeaponFireMode : uint8_t {
    SemiAutomatic,
    Automatic,
    Burst,
    ChargeRelease,
    ReleaseVolley,
};

enum class WeaponFireRejectReason : uint8_t {
    None,
    Disabled,
    UnknownWeapon,
    InvalidDefinition,
    TriggerNotActivated,
    Cooldown,
    Reloading,
    OutOfAmmo,
    Overheated,
    ChargeInsufficient,
    NoProjectiles,
    InvalidInput,
};

// Authoring-time weapon data. Zero magazineCapacity means unlimited ammunition;
// zero heatCapacity disables heat. Runtime state never belongs in this object.
struct WeaponDefinition {
    std::string weaponId;
    WeaponFireMode fireMode = WeaponFireMode::SemiAutomatic;
    WeaponDamageType damageType = WeaponDamageType::Kinetic;
    float baseDamage = 1.0f;
    float range = 100.0f;
    float shotInterval = 0.12f;
    uint32_t projectilesPerShot = 1;
    uint32_t maxProjectilesPerTrigger = 1;
    uint32_t burstCount = 3;
    float burstInterval = 0.06f;
    uint32_t magazineCapacity = 0;
    uint32_t initialReserveAmmo = 0;
    float reloadDuration = 0.0f;
    bool autoReload = true;
    float heatPerProjectile = 0.0f;
    float heatCapacity = 0.0f;
    float coolingPerSecond = 0.0f;
    float overheatRecoveryFraction = 0.35f;
    float minimumChargeSeconds = 0.0f;
    float maximumChargeSeconds = 0.0f;
    float maximumChargeDamageMultiplier = 1.0f;
    bool lockOnCompatible = false;
};

// Simulation-owned mutable state. All timers use caller-supplied delta time so
// firing remains deterministic and independent of wall-clock time.
struct WeaponRuntimeState {
    std::string weaponId;
    float cooldownRemaining = 0.0f;
    float reloadRemaining = 0.0f;
    float heat = 0.0f;
    float chargeSeconds = 0.0f;
    uint32_t ammoInMagazine = 0;
    uint32_t reserveAmmo = 0;
    uint32_t burstShotsRemaining = 0;
    uint64_t totalProjectilesFired = 0;
    bool reloading = false;
    bool overheated = false;
    bool triggerWasHeld = false;
};

struct WeaponFireInput {
    std::string weaponId;
    float deltaTime = 0.0f;
    bool enabled = true;
    bool triggerHeld = false;
    bool triggerPressed = false;
    bool triggerReleased = false;
    uint32_t requestedProjectileCount = 1;
    float damageMultiplier = 1.0f;
};

// A validated authorization to create one ray/projectile. shotId is allocated
// only here and is passed unchanged into WeaponHitRequest.
struct WeaponShot {
    uint64_t shotId = 0;
    uint32_t projectileIndex = 0;
    WeaponDamageType damageType = WeaponDamageType::Kinetic;
    float damage = 0.0f;
    float range = 0.0f;
    float chargeRatio = 0.0f;
};

struct WeaponFireResult {
    std::string weaponId;
    WeaponFireRejectReason rejectReason = WeaponFireRejectReason::None;
    std::vector<WeaponShot> shots;
    float cooldownRemaining = 0.0f;
    float reloadRemaining = 0.0f;
    float heat = 0.0f;
    float chargeSeconds = 0.0f;
    uint32_t ammoInMagazine = 0;
    uint32_t reserveAmmo = 0;
    bool fired = false;
    bool reloading = false;
    bool overheated = false;
};

class WeaponFireSystem {
public:
    void Reset();

    bool RegisterDefinition(const WeaponDefinition& definition, std::string* errorMessage = nullptr);
    bool ReplaceDefinitionSet(
        const std::vector<WeaponDefinition>& definitions,
        std::string* errorMessage = nullptr);
    const WeaponDefinition* FindDefinition(const std::string& weaponId) const;
    const WeaponRuntimeState* FindRuntimeState(const std::string& weaponId) const;

    WeaponFireResult Update(const WeaponFireInput& input);
    bool BeginReload(const std::string& weaponId);

    const WeaponFireResult& LastResult() const { return lastResult_; }
    uint64_t TotalProjectilesFired() const { return totalProjectilesFired_; }

// 弾数・熱・タイマーはこのクラスだけが変更する。外部は発射/リロードを要求する。
private:
    WeaponRuntimeState* FindMutableRuntimeState(const std::string& weaponId);
    uint64_t AllocateShotId();

    std::unordered_map<std::string, WeaponDefinition> definitions_;
    std::unordered_map<std::string, WeaponRuntimeState> runtimeStates_;
    WeaponFireResult lastResult_{};
    uint64_t nextShotId_ = 1;
    uint64_t totalProjectilesFired_ = 0;
};

const char* ToWeaponFireModeString(WeaponFireMode mode);
const char* ToWeaponFireRejectReasonString(WeaponFireRejectReason reason);
bool ValidateWeaponDefinition(
    const WeaponDefinition& definition,
    std::string* errorMessage = nullptr);
