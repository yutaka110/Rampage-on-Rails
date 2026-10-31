#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "CourseAsset.h"

class CourseSpawnRuntime;
struct EnemyEncounterReadabilityFrame;
struct RailWorldRaycastInput;

struct EncounterDirectorSettings {
    size_t maxActiveEnemies = 10;
    size_t maxActiveObstacles = 5;
    size_t bossEntryEnemyLimit = 1;
    size_t maxQueuedEvents = 32;
    float majorEncounterSpacing = 1.15f;
    float maxQueueDelay = 4.0f;
    size_t maxAuxiliaryEnemies = 2;
    float routeSpacingSeconds = 2.0f;
    float routeAdmissionWindowSeconds = 0.8f;
    float routeAdmissionDistance = 24.0f;
    // Begin the slower exterior approach early; never rush the visible flight
    // just to fill an empty aiming window.
    float adaptivePatrolDelaySeconds = 0.35f;
    float availabilityQueryIntervalSeconds = 0.12f;
    float roleMixDelaySeconds = 0.65f;
    float roleMixSpacingSeconds = 8.0f;
};

struct EncounterDirectorFrameInput {
    float deltaTime = 0.016f;
    float currentDistance = 0.0f;
    std::vector<CourseEventMarker> triggeredEvents;
    const CourseSpawnRuntime* spawnRuntime = nullptr;
    // Previous completed presentation; independent of RMB/reticle position.
    const EnemyEncounterReadabilityFrame* readability = nullptr;
    const RailWorldRaycastInput* visibilityQuery = nullptr;
    float minimumTargetForwardDistance = 4.0f;
    float maximumTargetForwardDistance = 120.0f;
    bool adaptivePatrolsAllowed = false;
    bool primaryEncounterDue = false;
    bool roleMixingAllowed = false;
    bool suppressRouteEncounters = false;
};

struct EncounterDirectorFrameOutput {
    std::vector<CourseEventMarker> dispatchEvents;
};

struct EncounterDirectorStats {
    size_t pendingEvents = 0;
    size_t activeEnemies = 0;
    size_t activeBullets = 0;
    size_t activeObstacles = 0;
    size_t activeAuxiliaryEnemies = 0;
    size_t activePrimaryEnemies = 0;
    bool livingBoss = false;
    size_t activePriorityEnemies = 0;
    bool hasAimableEnemy = false;
    float secondsWithoutAimableEnemy = 0.0f;
    uint32_t availabilityQueriesThisFrame = 0;
    uint32_t adaptivePatrolsThisFrame = 0;
    uint32_t mixedRoleArrivalsThisFrame = 0;
    uint32_t releasedThisFrame = 0;
    uint32_t delayedThisFrame = 0;
    uint32_t suppressedThisFrame = 0;
    std::string lastDecision;
};

class EncounterDirector {
public:
    void Reset();
    EncounterDirectorFrameOutput Update(const EncounterDirectorFrameInput& input);
    const EncounterDirectorStats& LastStats() const { return lastStats_; }
    EncounterDirectorSettings& Settings() { return settings_; }
    const EncounterDirectorSettings& Settings() const { return settings_; }

private:
    struct PendingEvent {
        CourseEventMarker event;
        float age = 0.0f;
    };

    bool CanRelease(
        const CourseEventMarker& event,
        float age,
        const EncounterDirectorStats& stats,
        std::string& reason) const;
    bool IsMajorEncounter(const CourseEventMarker& event) const;
    bool IsCriticalEvent(const CourseEventMarker& event) const;
    bool HasAimableEnemy(const EncounterDirectorFrameInput& input);
    void MixEnemyRoles(const EncounterDirectorFrameInput& input, float deltaTime,
        EncounterDirectorStats& admission, EncounterDirectorFrameOutput& output);

    EncounterDirectorSettings settings_{};
    EncounterDirectorStats lastStats_{};
    std::vector<PendingEvent> pendingEvents_;
    float timeSinceMajorRelease_ = 999.0f;
    float timeSinceRouteRelease_ = 999.0f;
    float secondsWithoutAimableEnemy_ = 0.0f;
    float availabilityAge_ = 999.0f;
    uint32_t availabilityActorId_ = 0;
    uint32_t availabilityCursor_ = 0;
    bool availabilityVisible_ = false;
    uint64_t adaptiveSequence_ = 0;
    float roleOpportunitySeconds_ = 0.0f;
    float timeSinceRoleRelease_ = 999.0f;
    std::string reinforcedWaveId_;
    uint32_t escortedPriorityActorId_ = 0;
    uint64_t roleSequence_ = 0;
};
