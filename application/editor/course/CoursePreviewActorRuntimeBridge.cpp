#include "CoursePreviewActorRuntimeBridge.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace editor {

bool CoursePreviewActorRuntimeSettings::Validate(std::string* errorMessage) const {
    const bool valid = std::isfinite(prewarmOpacity) && prewarmOpacity >= 0.0f && prewarmOpacity <= 1.0f;
    if (errorMessage != nullptr) *errorMessage = valid ? "" : "Preview actor opacity must be finite and within 0..1.";
    return valid;
}

bool CoursePreviewActorRuntimeBridge::Configure(const CoursePreviewActorRuntimeSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

CoursePreviewActorRuntimeBridge::CoursePreviewActorRuntimeBridge() {
    compileOptions_.allowFallbackActorAssets = true;
}

void CoursePreviewActorRuntimeBridge::SetCompileOptions(
    CourseWaveRuntimeCompileOptions options) {
    Reset();
    compileOptions_ = std::move(options);
}

bool CoursePreviewActorRuntimeBridge::Synchronize(
    const CoursePreviewSimulationSystem& simulation,
    float deltaTime,
    float playerDistance,
    std::string* errorMessage) {
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f || !std::isfinite(playerDistance)) {
        if (errorMessage != nullptr) *errorMessage = "Preview actor synchronization requires finite time and distance.";
        return false;
    }
    stats_.spawnedThisFrame = 0;
    stats_.removedThisFrame = 0;
    stats_.prewarmedActors = 0;
    stats_.activeActors = 0;

    if (!simulation.IsActive() || simulation.Snapshot() == nullptr) {
        Reset();
        return true;
    }
    if (!programValid_ || synchronizedSnapshot_ != simulation.Snapshot()) {
        if (!CompileSnapshot(simulation, errorMessage)) return false;
    }

    std::vector<uint32_t> retiredActorIds;
    for (const CourseEnemyActor& actor : runtime_.Enemies()) {
        const CoursePreviewEnemyState* state = FindSimulationEnemy(simulation, actor.desc.sourcePlacementGuid);
        if (state == nullptr || !ShouldMaterialize(state->phase) ||
            compileResult_.program.FindActor(actor.desc.sourcePlacementGuid) == nullptr) retiredActorIds.push_back(actor.actorId);
    }
    runtime_.RetireEnemies(retiredActorIds, false);
    stats_.removedThisFrame = static_cast<uint32_t>(retiredActorIds.size());
    const auto& runtimeActors = runtime_.Enemies();

    for (const CompiledCourseWaveActor& compiled : compileResult_.program.actors) {
        const CoursePreviewEnemyState* state = FindSimulationEnemy(
            simulation, compiled.placementGuid);
        if (state == nullptr || !ShouldMaterialize(state->phase)) continue;
        auto existing = std::find_if(
            runtimeActors.begin(), runtimeActors.end(), [&](const auto& actor) {
                return actor.desc.sourcePlacementGuid == compiled.placementGuid;
            });
        bool newlySpawned = false;
        if (existing == runtimeActors.end()) {
            CourseEnemyActorDesc desc = compiled.actor;
            desc.previewOnly = true;
            desc.suppressFire = !settings_.simulateEnemyFire;
            if (!settings_.simulateMovement) desc.forwardSpeed = 0.0f;
            desc.lifetime = (std::max)(desc.lifetime, 3600.0f);
            if (!runtime_.SpawnEnemyActor(std::move(desc), errorMessage)) return false;
            ++stats_.spawnedThisFrame;
            newlySpawned = true;
            existing = std::find_if(
                runtimeActors.begin(), runtimeActors.end(), [&](const auto& actor) {
                    return actor.desc.sourcePlacementGuid == compiled.placementGuid;
                });
        }
        if (existing != runtimeActors.end()) {
            if (newlySpawned || settings_.preserveActorsAtAuthoredTransform ||
                !settings_.simulateMovement) {
                ApplyCompiledState(existing->actorId, compiled, state->phase);
            } else {
                CourseEnemyActorDesc desc = existing->desc;
                desc.suppressFire = !settings_.simulateEnemyFire || state->phase == CoursePreviewEnemyPhase::Prewarmed;
                desc.forwardSpeed = compiled.actor.forwardSpeed;
                runtime_.SynchronizePreviewEnemy(existing->actorId, std::move(desc));
            }
            if (state->phase == CoursePreviewEnemyPhase::Prewarmed) {
                ++stats_.prewarmedActors;
            } else if (state->phase == CoursePreviewEnemyPhase::Active) {
                ++stats_.activeActors;
            }
        }
    }

    CourseEnemyFireSafetyFrameInput safety{};
    safety.deltaTime = (std::max)(0.0f, deltaTime);
    safety.playerDistance = playerDistance;
    safety.cameraAllowsEnemyFire = settings_.simulateEnemyFire;
    safety.cameraStableForAiming = settings_.simulateEnemyFire;
    safety.cameraHardTransition = false;
    safety.cameraReason = settings_.simulateEnemyFire
        ? "preview actor bridge"
        : "preview fire disabled";
    runtime_.Update(deltaTime, safety);
    if (!settings_.simulateEnemyFire) runtime_.ClearProjectiles();

    // Movement can be simulated, but the default editor contract keeps the
    // authored placement exact so scrubbing is stable and repeatable.
    if (settings_.preserveActorsAtAuthoredTransform || !settings_.simulateMovement) {
        for (const CourseEnemyActor& actor : runtime_.Enemies()) {
            const CompiledCourseWaveActor* compiled =
                compileResult_.program.FindActor(actor.desc.sourcePlacementGuid);
            const CoursePreviewEnemyState* state = FindSimulationEnemy(
                simulation, actor.desc.sourcePlacementGuid);
            if (compiled != nullptr && state != nullptr) {
                ApplyCompiledState(actor.actorId, *compiled, state->phase);
            }
        }
    }

    stats_.active = true;
    stats_.runtimeActors = static_cast<uint32_t>(runtime_.ActiveEnemyCount());
    stats_.synchronizedSimulationRevision = simulation.Frame().simulationRevision;
    stats_.message = "Preview Actor runtime synchronized from compiled Wave program.";
    return true;
}

void CoursePreviewActorRuntimeBridge::Reset() {
    runtime_.Reset();
    compileResult_ = {};
    stats_ = {};
    synchronizedSnapshot_ = nullptr;
    programValid_ = false;
}

bool CoursePreviewActorRuntimeBridge::CompileSnapshot(
    const CoursePreviewSimulationSystem& simulation,
    std::string* errorMessage) {
    runtime_.Reset();
    compileResult_ = compiler_.Compile(*simulation.Snapshot(), compileOptions_);
    programValid_ = compileResult_.succeeded;
    synchronizedSnapshot_ = simulation.Snapshot();
    stats_.programValid = programValid_;
    stats_.compiledWaves = static_cast<uint32_t>(compileResult_.program.waves.size());
    stats_.compiledActors = static_cast<uint32_t>(compileResult_.program.actors.size());
    stats_.programFingerprint = compileResult_.program.sourceFingerprint;
    stats_.fallbackActors = static_cast<uint32_t>(std::count_if(
        compileResult_.program.actors.begin(), compileResult_.program.actors.end(),
        [](const CompiledCourseWaveActor& actor) {
            return !actor.actorAssetResolved;
        }));
    stats_.message = compileResult_.message;
    if (!programValid_) {
        if (errorMessage != nullptr) *errorMessage = compileResult_.message;
        return false;
    }
    return true;
}

const CoursePreviewEnemyState* CoursePreviewActorRuntimeBridge::FindSimulationEnemy(
    const CoursePreviewSimulationSystem& simulation,
    std::string_view placementGuid) const {
    const auto found = std::find_if(
        simulation.Enemies().begin(), simulation.Enemies().end(),
        [&](const CoursePreviewEnemyState& enemy) {
            return enemy.placementGuid == placementGuid;
        });
    return found != simulation.Enemies().end() ? &*found : nullptr;
}

bool CoursePreviewActorRuntimeBridge::ShouldMaterialize(
    CoursePreviewEnemyPhase phase) const {
    return phase == CoursePreviewEnemyPhase::Active ||
        (settings_.showPrewarmedActors &&
            phase == CoursePreviewEnemyPhase::Prewarmed);
}

void CoursePreviewActorRuntimeBridge::ApplyCompiledState(
    uint32_t actorId, const CompiledCourseWaveActor& compiled, CoursePreviewEnemyPhase phase) {
    CourseEnemyActorDesc desc = compiled.actor;
    desc.previewOnly = true;
    desc.suppressFire = !settings_.simulateEnemyFire || phase == CoursePreviewEnemyPhase::Prewarmed;
    if (!settings_.simulateMovement || settings_.preserveActorsAtAuthoredTransform) desc.forwardSpeed = 0.0f;
    desc.lifetime = (std::max)(desc.lifetime, 3600.0f);
    if (phase == CoursePreviewEnemyPhase::Prewarmed) desc.color.w *= (std::clamp)(settings_.prewarmOpacity, 0.05f, 1.0f);
    runtime_.SynchronizePreviewEnemy(actorId, std::move(desc));
}

} // namespace editor
