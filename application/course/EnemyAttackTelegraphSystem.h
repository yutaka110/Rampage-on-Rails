#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "CourseSpawnRuntime.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"

class RailPath;
struct CourseAsset;
struct TerrainGenerationSettings;
class TerrainEditLayer;

enum class EnemyAttackTelegraphPhase : uint8_t {
    None,
    Warming,
    Tracking,
    Imminent,
    Fired,
};

enum class EnemyAttackTelegraphEventKind : uint8_t {
    Acquired,
    Imminent,
    Fired,
};

struct EnemyAttackTelegraphSettings {
    bool enabled = true;
    bool requireWorldVisibility = true;
    bool suppressOccluded = true;
    float leadSeconds = 0.90f;
    float imminentSeconds = 0.24f;
    float firedFlashSeconds = 0.18f;
    float safeAreaPixels = 48.0f;
    float offscreenPriorityBonus = 0.10f;
    uint32_t maximumVisibleCues = 3;
    uint32_t maximumVisibilityQueries = 12;
};

// One shared visual language for HUD, world-lane and VFX presentation. Phase
// remains readable without hue through the label, tier and marker scale.
struct EnemyAttackTelegraphReadabilityStyle final {
    const char* label = "";
    Vector4 primaryColor{1.0f, 0.48f, 0.04f, 1.0f};
    float glowAlpha = 0.20f;
    float markerScale = 1.0f;
    uint32_t tier = 0;
    bool showCountdown = true;
};

EnemyAttackTelegraphReadabilityStyle ResolveEnemyAttackTelegraphReadabilityStyle(
    EnemyAttackTelegraphPhase phase,
    float pulse = 0.0f) noexcept;
std::string FormatEnemyAttackCountdown(float seconds);

struct EnemyAttackTelegraphCue {
    // Launch templates use the remaining warning delay and begin at the
    // anticipated barrel position. Execution uses the same resolver at delay 0.
    // Empty for legacy cues or trajectories that require live steering.
    std::vector<EnemyProjectileRuntimeState> projectileLaunches;
    // Per-barrel time after launch, excluding the remaining warning delay.
    std::vector<float> projectileFlightSeconds;
    uint32_t actorId = 0;
    uint64_t fireSequence = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    CourseEnemyFirePattern attackPattern = CourseEnemyFirePattern::Single;
    EnemyAttackTelegraphPhase phase = EnemyAttackTelegraphPhase::None;
    Vector3 worldPosition{};
    Vector2 screenPosition{};
    Vector2 directionFromCenter{};
    float timeToFire = 0.0f;
    float targetRailDistance = 0.0f;
    float targetLateralOffset = 0.0f;
    float targetVerticalOffset = 0.0f;
    float predictedFlightSeconds = 0.0f;
    float bodyRadiusPixels = 12.0f;
    float urgency = 0.0f;
    float severity = 0.0f;
    float priority = 0.0f;
    float pulse = 0.0f;
    int projectileCount = 1;
    bool onScreen = false;
    bool behindCamera = false;
    bool occluded = false;
    bool visibilityTested = false;
    bool newlyPresented = false;
    bool hasLockedTarget = false;
    EnemyProjectileTrajectory projectileTrajectory =
        EnemyProjectileTrajectory::Direct;
};

struct EnemyAttackTelegraphEvent {
    uint32_t actorId = 0;
    uint64_t fireSequence = 0;
    EnemyAttackTelegraphEventKind kind = EnemyAttackTelegraphEventKind::Acquired;
    float severity = 0.0f;
    bool offscreen = false;
};

struct EnemyAttackTelegraphFrameStats {
    uint32_t activeEnemies = 0;
    uint32_t candidateCues = 0;
    uint32_t visibleCues = 0;
    uint32_t onScreenCues = 0;
    uint32_t offscreenCues = 0;
    uint32_t occludedCues = 0;
    uint32_t prioritySuppressed = 0;
    uint32_t visibilityQueries = 0;
    uint32_t visibilityBudgetExhausted = 0;
};

struct EnemyAttackTelegraphFrame {
    std::vector<EnemyAttackTelegraphCue> cues;
    std::vector<EnemyAttackTelegraphEvent> events;
    EnemyAttackTelegraphFrameStats stats{};
    float highestPriority = 0.0f;
    uint64_t revision = 0;
};

struct EnemyAttackTelegraphFrameInput {
    CourseSpawnRuntime* spawnRuntime = nullptr;
    const RailPath* railPath = nullptr;
    const Matrix4x4* viewProjection = nullptr;
    const CourseAsset* course = nullptr;
    const TerrainGenerationSettings* terrainSettings = nullptr;
    const TerrainEditLayer* terrainEdits = nullptr;
    const TerrainEditLayer* terrainPreview = nullptr;
    Vector3 cameraPosition{};
    float playerDistance = 0.0f;
    float deltaTime = 0.016f;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    bool cameraAllowsAttack = true;
    EnemyAttackTelegraphSettings settings{};
};

// Builds the authoritative player-facing warning frame from Behavior attack
// intents. Presentation, audio and haptics consume this ordered frame, while
// Coordinator and Behavior both require the exact presented reservation before
// the commercial execution boundary may fire.
class EnemyAttackTelegraphSystem {
public:
    void Reset();
    void CancelActor(uint32_t actorId);
    void Update(const EnemyAttackTelegraphFrameInput& input);

    const EnemyAttackTelegraphFrame& Frame() const { return frame_; }

private:
    struct TrackedActor {
        uint64_t lastFireSequence = 0;
        uint64_t lastNotifiedFireSequence = 0;
        float firedFlashRemaining = 0.0f;
        EnemyAttackTelegraphPhase lastPresentedPhase =
            EnemyAttackTelegraphPhase::None;
        bool wasPresented = false;
    };

    std::unordered_map<uint32_t, TrackedActor> trackedActors_;
    EnemyAttackTelegraphFrame frame_{};
    float elapsedTime_ = 0.0f;
    uint64_t revision_ = 0;
};

const char* ToEnemyAttackTelegraphPhaseString(EnemyAttackTelegraphPhase phase);
const char* ToEnemyAttackTelegraphEventKindString(
    EnemyAttackTelegraphEventKind kind);
