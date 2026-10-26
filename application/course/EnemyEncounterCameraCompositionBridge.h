#pragma once

#include <cstdint>
#include <string>

#include "EnemyEncounterPacingDirector.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;
class RailPath;

struct EnemyEncounterCameraCompositionInput final {
    const EnemyEncounterPacingFrame* pacing = nullptr;
    const CourseSpawnRuntime* runtime = nullptr;
    const RailPath* railPath = nullptr;
    float playerDistance = 0.0f;
    float deltaTime = 0.016f;
    bool gameplayActive = true;
};

struct EnemyEncounterCameraCompositionFrame final {
    bool active = false;
    bool hasFocusWorld = false;
    EnemyEncounterBeatPhase phase = EnemyEncounterBeatPhase::Dormant;
    std::string beatGuid;
    std::string preferredCameraShotId;
    Vector3 focusWorld{};
    float blend = 0.0f;
    float preferredCameraShotWeight = 0.0f;
    float focusWeight = 0.0f;
    float fovOffsetRadians = 0.0f;
    float backDistanceOffset = 0.0f;
    uint32_t focusActorCount = 0;
    uint64_t revision = 0;
};

// Converts authored pacing intent into a bounded request consumed by the
// existing RailCameraDirector. It never writes camera transforms directly.
class EnemyEncounterCameraCompositionBridge final {
public:
    void Reset();
    const EnemyEncounterCameraCompositionFrame& Update(
        const EnemyEncounterCameraCompositionInput& input);
    const EnemyEncounterCameraCompositionFrame& Frame() const noexcept {
        return frame_;
    }

private:
    EnemyEncounterCameraCompositionFrame frame_{};
    float blend_ = 0.0f;
    Vector3 focusOffset_{0,2,42};
    float previousDistance_ = 0.0f;
    bool hasRailHistory_ = false;
    uint64_t revision_ = 0;
};

