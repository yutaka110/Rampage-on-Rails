#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "EnemyFormationDefinition.h"

class CourseSpawnRuntime;
class RailPath;
struct CourseEnemyActor;

struct EnemyFormationMemberRuntimeState final {
    std::string formationId;
    uint32_t slotIndex = 0;
    uint32_t slotCount = 1;
    float slotForwardOffset = 0.0f;
    float slotLateralOffset = 0.0f;
    float slotVerticalOffset = 0.0f;
    float smoothedForwardCorrection = 0.0f;
    float smoothedLateralCorrection = 0.0f;
    float smoothedVerticalCorrection = 0.0f;
    float appliedForwardOffset = 0.0f;
    float appliedLateralOffset = 0.0f;
    float appliedVerticalOffset = 0.0f;
    // Stable per-actor hover home plus bounded, smoothed crowd separation.
    // Included in applied* so BeginFrame removes it exactly once.
    float hoverForwardOffset = 0.0f;
    float hoverLateralOffset = 0.0f;
    float hoverVerticalOffset = 0.0f;
    bool hoverInitialized = false;
    uint64_t revision = 0;
    bool initialized = false;
    bool leader = false;
};

struct EnemyFormationFrame final {
    uint32_t formations = 0;
    uint32_t members = 0;
    uint32_t correctedMembers = 0;
    uint32_t singleActors = 0;
    uint64_t revision = 0;
};

// Maintains authored or procedural slots without becoming a second base-motion
// authority. BeginFrame removes the prior additive correction; Update applies
// exactly one new correction after EnemyBehaviorSystem has written its pose.
class EnemyFormationSystem final {
public:
    void Reset();
    void BeginFrame(CourseSpawnRuntime& runtime);
    void Update(CourseSpawnRuntime& runtime, float deltaTime, const RailPath* railPath = nullptr);
    bool SetDefinition(
        std::string formationId,
        EnemyFormationDefinition definition,
        std::string* errorMessage = nullptr);
    void ClearDefinition(const std::string& formationId);

    const EnemyFormationFrame& Frame() const noexcept { return frame_; }
    const EnemyFormationDefinition* FindDefinition(
        const std::string& formationId) const noexcept;

private:
    // Only the owning Runtime supplies mutable storage to these algorithms.
    friend class CourseSpawnRuntime;
    void BeginFrameActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime);
    void UpdateActors(std::span<CourseEnemyActor> actors,
        CourseSpawnRuntime& runtime,
        float deltaTime, const RailPath* railPath);
    void ApplyHoverSpacing(std::span<CourseEnemyActor> actors, float deltaTime, const RailPath* railPath);

    EnemyFormationDefinition ResolveDefinition(
        const std::string& formationId) const;

    std::unordered_map<std::string, EnemyFormationDefinition> definitions_;
    EnemyFormationFrame frame_{};
    uint64_t revision_ = 0;
};
