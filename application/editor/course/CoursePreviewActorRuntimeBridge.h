#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CoursePreviewSimulationSystem.h"
#include "CourseWaveRuntimeCompiler.h"

namespace editor {

struct CoursePreviewActorRuntimeSettings final {
    bool showPrewarmedActors = true;
    bool simulateMovement = false;
    bool simulateEnemyFire = false;
    bool preserveActorsAtAuthoredTransform = true;
    float prewarmOpacity = 0.42f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CoursePreviewActorRuntimeStats final {
    bool active = false;
    bool programValid = false;
    uint64_t programFingerprint = 0;
    uint64_t synchronizedSimulationRevision = 0;
    uint32_t compiledWaves = 0;
    uint32_t compiledActors = 0;
    uint32_t runtimeActors = 0;
    uint32_t prewarmedActors = 0;
    uint32_t activeActors = 0;
    uint32_t spawnedThisFrame = 0;
    uint32_t removedThisFrame = 0;
    uint32_t fallbackActors = 0;
    std::string message;
};

// Materializes compiled preview placements into a dedicated CourseSpawnRuntime.
// The bridge never touches gameplay CourseSpawnRuntime, authored CourseAsset or Undo.
class CoursePreviewActorRuntimeBridge final {
public:
    CoursePreviewActorRuntimeBridge();
    void SetCompileOptions(CourseWaveRuntimeCompileOptions options);
    bool Synchronize(
        const CoursePreviewSimulationSystem& simulation,
        float deltaTime,
        float playerDistance,
        std::string* errorMessage = nullptr);
    void Reset();

    const CourseSpawnRuntime& Runtime() const noexcept { return runtime_; }
    const CompiledCourseWaveProgram* Program() const noexcept {
        return programValid_ ? &compileResult_.program : nullptr;
    }
    const CourseWaveRuntimeCompileResult& CompileResult() const noexcept {
        return compileResult_;
    }
    const CoursePreviewActorRuntimeStats& Stats() const noexcept { return stats_; }
    bool Configure(const CoursePreviewActorRuntimeSettings& settings, std::string* errorMessage = nullptr);
    const CoursePreviewActorRuntimeSettings& Settings() const noexcept { return settings_; }
    bool Active() const noexcept { return stats_.active && programValid_; }

private:
    bool CompileSnapshot(
        const CoursePreviewSimulationSystem& simulation,
        std::string* errorMessage);
    const CoursePreviewEnemyState* FindSimulationEnemy(
        const CoursePreviewSimulationSystem& simulation,
        std::string_view placementGuid) const;
    bool ShouldMaterialize(CoursePreviewEnemyPhase phase) const;
    void ApplyCompiledState(
        uint32_t actorId,
        const CompiledCourseWaveActor& compiled,
        CoursePreviewEnemyPhase phase);

    CourseWaveRuntimeCompiler compiler_{};
    CourseWaveRuntimeCompileOptions compileOptions_{};
    CourseWaveRuntimeCompileResult compileResult_{};
    CourseSpawnRuntime runtime_{};
    CoursePreviewActorRuntimeSettings settings_{};
    CoursePreviewActorRuntimeStats stats_{};
    const CourseAsset* synchronizedSnapshot_ = nullptr;
    bool programValid_ = false;
};

} // namespace editor
