#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "utils/math/Vector.h"

enum class PlayerHitKind : uint8_t {
    EnemyProjectile,
    ObstacleContact,
    TerrainContact,
    ScriptedHazard,
};

enum class PlayerDamageRejectReason : uint8_t {
    None,
    InvalidRequest,
    NotInitialized,
    PlayerDefeated,
    Invulnerable,
    DuplicateProjectile,
};

struct PlayerHitRequest final {
    uint64_t requestId = 0;
    PlayerHitKind kind = PlayerHitKind::EnemyProjectile;
    uint32_t sourceActorId = 0;
    uint64_t sourceProjectileId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    std::string sourceId;
    std::string impactEffectId = "ice_impact";
    float rawDamage = 0.0f;
    float postHitInvulnerabilitySeconds = 0.65f;
    float railDistance = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 4.0f;
    Vector3 impactWorldPosition{};
    Vector3 impactNormalWorld{0.0f, 1.0f, 0.0f};
    bool hasWorldImpact = false;
};

struct PlayerDamageResult final {
    uint64_t sequence = 0;
    PlayerHitRequest request{};
    PlayerDamageRejectReason rejectReason = PlayerDamageRejectReason::None;
    float hitPointsBefore = 0.0f;
    float hitPointsAfter = 0.0f;
    float appliedDamage = 0.0f;
    float invulnerabilityGrantedSeconds = 0.0f;
    bool accepted = false;
    bool lethal = false;
    bool projectileConsumed = false;
};

struct PlayerDamageRuntimeState final {
    float hitPoints = 100.0f;
    float maximumHitPoints = 100.0f;
    float invulnerabilityRemainingSeconds = 0.0f;
    uint64_t revision = 0;
    uint64_t nextResultSequence = 1;
    uint64_t lastAcceptedSequence = 0;
    uint32_t acceptedHits = 0;
    std::vector<uint64_t> consumedProjectileIds;
    bool initialized = false;
};

struct PlayerDamageSystemSettings final {
    uint32_t projectileHistoryCapacity = 128;
    float maximumDamagePerHit = 10000.0f;
    float maximumInvulnerabilitySeconds = 5.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

// Authoritative acceptance boundary for every hostile hit against the player.
// GameSession consumes accepted result damage; presentation consumes the same
// result and never infers a hit from collision counters.
class PlayerDamageSystem final {
public:
    bool Initialize(
        float maximumHitPoints,
        float hitPoints,
        std::string* errorMessage = nullptr);
    void Reset(float maximumHitPoints = 100.0f, float hitPoints = 100.0f);
    void SynchronizeHealth(float hitPoints, float maximumHitPoints = 100.0f);
    void Update(float deltaTime, float externalInvulnerabilitySeconds = 0.0f);
    PlayerDamageResult Submit(const PlayerHitRequest& request);

    PlayerDamageRuntimeState CaptureCheckpoint() const { return state_; }
    bool RestoreCheckpoint(
        const PlayerDamageRuntimeState& checkpoint,
        std::string* errorMessage = nullptr);

    const PlayerDamageRuntimeState& State() const noexcept { return state_; }
    const std::vector<PlayerDamageResult>& ResultsThisFrame() const noexcept {
        return resultsThisFrame_;
    }
    const PlayerDamageResult& LastResult() const noexcept { return lastResult_; }
    // 設定はコピーを編集し、検証に成功した場合だけ反映する。
    // 履歴容量・無敵時間の変更に伴う既存状態の調整も、このクラスが担当する。
    // Invalid settings leave both configuration and gameplay state unchanged.
    bool Configure(const PlayerDamageSystemSettings& settings,
        std::string* errorMessage = nullptr);
    const PlayerDamageSystemSettings& Settings() const noexcept { return settings_; }

private:
    bool HasConsumedProjectile(uint64_t projectileId) const;
    void RememberProjectile(uint64_t projectileId);
    PlayerDamageResult Reject(
        const PlayerHitRequest& request,
        PlayerDamageRejectReason reason,
        bool consumeProjectile);

    PlayerDamageSystemSettings settings_{};
    PlayerDamageRuntimeState state_{};
    std::vector<PlayerDamageResult> resultsThisFrame_;
    PlayerDamageResult lastResult_{};
};

const char* ToString(PlayerHitKind kind) noexcept;
const char* ToString(PlayerDamageRejectReason reason) noexcept;
