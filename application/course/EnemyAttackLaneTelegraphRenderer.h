#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "EnemyAttackTelegraphSystem.h"

class EffectRuntime;
class RailPath;
namespace ge3::debug { class DebugDrawSystem; }

enum class EnemyAttackLaneShape : unsigned char {
    Line,
    Fan,
    Homing,
    Arc,
};

struct EnemyAttackLaneTelegraphRendererSettings final {
    bool enabled = true;
    bool effectRuntimeEnabled = true;
    size_t maximumVisibleLanes = 3;
    float baseLaneWidth = 0.34f;
    float fanEndpointSpacing = 2.6f;
    float targetMarkerRadius = 0.52f;
    float sourceMarkerRadius = 0.48f;
    float imminentScale = 1.10f;
    std::string markerEffectId = "enemy_attack_lane_marker";
};

struct EnemyAttackLaneTelegraphProxy final {
    struct PathPoint final {
        Vector3 world{};
        Vector3 right{1,0,0};
        Vector3 up{0,1,0};
    };
    std::vector<std::vector<PathPoint>> projectilePaths;
    uint32_t actorId = 0;
    uint64_t attackIntentSequence = 0;
    uint64_t attackTokenId = 0;
    EnemyAttackLaneShape shape = EnemyAttackLaneShape::Line;
    EnemyAttackTelegraphPhase phase = EnemyAttackTelegraphPhase::None;
    EnemyProjectileTrajectory trajectory = EnemyProjectileTrajectory::Direct;
    Vector3 startWorld{};
    Vector3 targetWorld{};
    Vector3 railRight{1.0f, 0.0f, 0.0f};
    Vector3 railUp{0.0f, 1.0f, 0.0f};
    Vector4 color{1.0f, 0.08f, 0.72f, 0.65f};
    float laneWidth = 0.34f;
    float sourceRadius = 0.48f;
    float targetRadius = 0.72f;
    float opacity = 1.0f;
    float urgency = 0.0f;
    float pulse = 0.0f;
    float convergenceRadius = 0.72f;
    float flowPhase = 0.0f;
    uint32_t readabilityTier = 0;
    uint32_t directionMarkerCount = 3;
    int projectileCount = 1;
    bool openingCue = false;
    uint32_t sourceEffectInstanceId = 0;
    uint32_t targetEffectInstanceId = 0;
};

struct EnemyAttackLaneTelegraphRenderFrame final {
    std::vector<EnemyAttackLaneTelegraphProxy> lanes;
    uint32_t effectBackedMarkers = 0;
    uint32_t productionSubmittedLanes = 0;
    uint32_t droppedByBudget = 0;
    uint64_t sourceTelegraphRevision = 0;
    uint64_t revision = 0;
};

struct EnemyAttackLaneTelegraphRenderInput final {
    const EnemyAttackTelegraphFrame* telegraph = nullptr;
    const RailPath* railPath = nullptr;
    EffectRuntime* effectRuntime = nullptr;
    bool gameplayActive = true;
    bool openingPresentation = false;
    float elapsedTime = 0.0f;
    EnemyAttackLaneTelegraphRendererSettings settings{};
};

// Converts generic attack cues into trajectory-specific world-space warning
// lanes. Marker effects use the production VFX runtime; bounded geometry is
// retained for lane direction, spread gaps, and a Release-safe fallback.
class EnemyAttackLaneTelegraphRenderer final {
public:
    void Reset(EffectRuntime* effectRuntime = nullptr);
    void CancelActor(uint32_t actorId, EffectRuntime* effectRuntime = nullptr);
    void Update(const EnemyAttackLaneTelegraphRenderInput& input);
    void AppendProductionWorldPrimitives(
        ge3::debug::DebugDrawSystem& productionDraw) const;
    void AppendWorldPrimitives(ge3::debug::DebugDrawSystem& debugDraw) const;

    bool WasSubmitted(
        uint32_t actorId,
        uint64_t attackIntentSequence) const noexcept;

    const EnemyAttackLaneTelegraphRenderFrame& Frame() const noexcept {
        return frame_;
    }

private:
    struct LaneKey final {
        uint32_t actorId = 0;
        uint64_t attackIntentSequence = 0;
        bool operator==(const LaneKey&) const = default;
    };
    struct LaneKeyHash final {
        size_t operator()(const LaneKey& key) const noexcept;
    };
    struct ManagedMarkers final {
        uint32_t sourceInstanceId = 0;
        uint32_t targetInstanceId = 0;
        uint64_t touchedRevision = 0;
    };

    void StopMarkers(ManagedMarkers& markers, EffectRuntime* runtime);
    void StopUntouched(EffectRuntime* runtime, uint64_t revision);

    std::unordered_map<LaneKey, ManagedMarkers, LaneKeyHash> managedMarkers_;
    EnemyAttackLaneTelegraphRenderFrame frame_{};
    uint64_t revision_ = 0;
};

const char* ToString(EnemyAttackLaneShape shape) noexcept;
