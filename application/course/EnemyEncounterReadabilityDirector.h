#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "CombatTruthGate.h"
#include "EnemyScreenPresencePolicy.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;
class RailPath;
struct EnemyAttackTelegraphFrame;
struct EnemyProjectilePresentationFrame;
struct EnemyAttackDefensePresentationFrame;

// Shared by silhouette measurement and gameplay render submission. This
// stable model scale preserves perspective and never pulses with charge.
inline constexpr float kTwinShieldGameplayModelScale = 1.30f;

struct EnemyEncounterReadabilitySettings final {
    CombatTruthGateSettings truth{};
    EnemyScreenPresenceSettings presence{};
    float openingScoutEndDistance = 180.0f;
    float openingScoutIdleDiameterPixels = 68.0f;
    float openingScoutEngagedDiameterPixels = 84.0f;
    float safeAreaPixels = 48.0f;
    float minimumAttackExposureSeconds = 0.34f;
    float exposureDecayPerSecond = 2.5f;
    size_t maximumActorProxies = 64;
};

struct EnemyEncounterActorReadability final {
    uint32_t actorId = 0;
    Vector3 worldPosition{};
    Vector2 screenPosition{};
    float projectedDiameterPixels = 0.0f;
    float presentationScale = 1.0f;
    float presentationAlpha = 1.0f;
    float colorBoost = 1.0f;
    float priority = 0.0f;
    float readableExposureSeconds = 0.0f;
    bool onScreen = false;
    bool behindCamera = false;
    bool targetable = false;
    bool attackEngaged = false;
    bool fixedSizeProxyApplied = false;
    bool screenReadable = false;
    bool warningReadable = false;
    bool attackPresentationReady = false;
    bool offscreenIndicatorRecommended = false;
};

struct EnemyEncounterReadabilityInput final {
    CourseSpawnRuntime* runtime = nullptr;
    const RailPath* railPath = nullptr;
    const Matrix4x4* viewProjection = nullptr;
    const EnemyAttackTelegraphFrame* telegraph = nullptr;
    const EnemyProjectilePresentationFrame* projectiles = nullptr;
    const EnemyAttackDefensePresentationFrame* defense = nullptr;
    std::span<const PlayerDamageResult> damageResults{};
    uint32_t activeWaves = 0;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    float playerDistance = -1.0f;
    float deltaTime = 0.016f;
    bool gameplayActive = true;
    EnemyEncounterReadabilitySettings settings{};
};

struct EnemyEncounterReadabilityFrame final {
    CombatTruthGateFrame truth{};
    std::vector<EnemyEncounterActorReadability> actors;
    uint32_t readableHostiles = 0;
    uint32_t offscreenHostiles = 0;
    uint32_t fixedSizeProxies = 0;
    uint32_t attackGatedActors = 0;
    uint32_t droppedActorProxies = 0;
    uint64_t revision = 0;
};

// Joins combat truth and screen-presence policy into one bounded frame used by
// HUD, render submission and next-frame fire safety.
class EnemyEncounterReadabilityDirector final {
public:
    void Reset();
    void Update(const EnemyEncounterReadabilityInput& input);

    const EnemyEncounterReadabilityFrame& Frame() const noexcept {
        return frame_;
    }
    const EnemyEncounterActorReadability* FindActor(
        uint32_t actorId) const noexcept;

private:
    struct TrackedActor final {
        float readableExposureSeconds = 0.0f;
        uint64_t touchedRevision = 0;
    };

    CombatTruthGate truthGate_{};
    EnemyScreenPresencePolicy presencePolicy_{};
    std::unordered_map<uint32_t, TrackedActor> tracked_;
    EnemyEncounterReadabilityFrame frame_{};
    uint64_t revision_ = 0;
};
