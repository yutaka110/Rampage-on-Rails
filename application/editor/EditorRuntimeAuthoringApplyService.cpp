#include "EditorRuntimeAuthoringApplyService.h"

#include "EditorNotificationCenter.h"
#include "play/EditorRuntimeApplyUndoCommand.h"

#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace editor {
namespace {

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t HashString(uint64_t hash, const std::string& value) {
    return HashBytes(hash, value.data(), value.size());
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(T));
}

uint64_t CourseSignature(const CourseAsset& course) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashString(hash, course.name);
    hash = HashValue(hash, course.railPoints.size());
    for (const RailPathControlPoint& point : course.railPoints) {
        hash = HashValue(hash, point.position.x);
        hash = HashValue(hash, point.position.y);
        hash = HashValue(hash, point.position.z);
        hash = HashValue(hash, point.corridorRadius);
        hash = HashValue(hash, point.speed);
    }
    hash = HashValue(hash, course.terrainPlacements.size());
    for (const CourseTerrainPlacement& placement : course.terrainPlacements) {
        hash = HashString(hash, placement.id);
        hash = HashString(hash, placement.meshId);
        hash = HashValue(hash, placement.distance);
        hash = HashValue(hash, placement.lateralOffset);
        hash = HashValue(hash, placement.verticalOffset);
        hash = HashValue(hash, placement.forwardOffset);
        hash = HashValue(hash, placement.scale.x);
        hash = HashValue(hash, placement.scale.y);
        hash = HashValue(hash, placement.scale.z);
    }
    hash = HashValue(hash, course.rockClusters.size());
    for (const CourseRockCluster& cluster : course.rockClusters) {
        hash = HashString(hash, cluster.id);
        hash = HashString(hash, cluster.meshId);
        hash = HashValue(hash, cluster.distance);
        hash = HashValue(hash, cluster.count);
        hash = HashValue(hash, cluster.minScale);
        hash = HashValue(hash, cluster.maxScale);
        hash = HashValue(hash, cluster.spread.x);
        hash = HashValue(hash, cluster.spread.y);
        hash = HashValue(hash, cluster.spread.z);
    }
    hash = HashValue(hash, course.cameraKeys.size());
    for (const CourseCameraKey& key : course.cameraKeys) {
        hash = HashValue(hash, key.distance);
        hash = HashValue(hash, key.backDistance);
        hash = HashValue(hash, key.verticalOffset);
        hash = HashValue(hash, key.fovY);
        hash = HashValue(hash, key.roll);
    }
    hash = HashValue(hash, course.sections.size());
    for (const CourseSection& section : course.sections) {
        hash = HashValue(hash, section.startDistance);
        hash = HashValue(hash, section.endDistance);
        hash = HashString(hash, section.name);
        hash = HashString(hash, section.category);
    }
    hash = HashValue(hash, course.rideProfiles.size());
    for (const CourseRideProfileDefinition& profile : course.rideProfiles) {
        hash = HashString(hash, profile.editorGuid);
        hash = HashString(hash, profile.displayName);
        hash = HashValue(hash, profile.startDistance);
        hash = HashValue(hash, profile.endDistance);
        hash = HashValue(hash, profile.speedMode);
        hash = HashValue(hash, profile.speedMultiplier);
        hash = HashValue(hash, profile.targetSpeedOverride);
        hash = HashValue(hash, profile.turnAnticipationDistance);
        hash = HashValue(hash, profile.visualBankScale);
        hash = HashValue(hash, profile.maximumVisualBankDegrees);
        hash = HashValue(hash, profile.blendInDistance);
        hash = HashValue(hash, profile.blendOutDistance);
        hash = HashString(hash, profile.cameraShotId);
        hash = HashValue(hash, profile.enabled);
        hash = HashValue(hash, profile.editorVisible);
        hash = HashValue(hash, profile.editorLocked);
    }
    hash = HashValue(hash, course.rideSpeedBeats.size());
    for (const RailRideSpeedBeatDefinition& beat : course.rideSpeedBeats) {
        hash = HashString(hash, beat.editorGuid);
        hash = HashString(hash, beat.displayName);
        hash = HashValue(hash, beat.startDistance);
        hash = HashValue(hash, beat.endDistance);
        hash = HashValue(hash, beat.type);
        hash = HashValue(hash, beat.speedMultiplier);
        hash = HashValue(hash, beat.targetSpeedOverride);
        hash = HashValue(hash, beat.accelerationScale);
        hash = HashValue(hash, beat.brakingScale);
        hash = HashValue(hash, beat.maximumJerk);
        hash = HashValue(hash, beat.blendInDistance);
        hash = HashValue(hash, beat.blendOutDistance);
        hash = HashValue(hash, beat.priority);
        hash = HashValue(hash, beat.enabled);
    }
    hash = HashValue(hash, course.events.size());
    for (const CourseEventMarker& event : course.events) {
        hash = HashValue(hash, event.distance);
        hash = HashString(hash, event.type);
        hash = HashString(hash, event.id);
        hash = HashString(hash, event.payload);
    }
    hash = HashValue(hash, course.lightingPresets.size());
    for (const CourseLightingPreset& preset : course.lightingPresets) {
        hash = HashValue(hash, preset.distance);
        hash = HashString(hash, preset.id);
        hash = HashValue(hash, preset.sunIntensity);
        hash = HashValue(hash, preset.fogIntensity);
        hash = HashValue(hash, preset.fogDensity);
    }
    hash = HashValue(hash, course.cameraShotPresets.size());
    for (const CourseCameraShotPreset& preset : course.cameraShotPresets) {
        hash = HashString(hash, preset.id);
        hash = HashString(hash, preset.mode);
        hash = HashValue(hash, preset.fovOffset);
        hash = HashValue(hash, preset.shakeAmount);
    }
    hash = HashValue(hash, course.cameraBlendAssets.size());
    for (const CourseCameraBlendAsset& blend : course.cameraBlendAssets) {
        hash = HashString(hash, blend.id);
        hash = HashString(hash, blend.curve);
        hash = HashValue(hash, blend.blendInDistance);
        hash = HashValue(hash, blend.blendOutDistance);
    }
    hash = HashValue(hash, course.cinematicCameraShots.size());
    for (const CourseCinematicCameraShot& shot : course.cinematicCameraShots) {
        hash = HashValue(hash, shot.startDistance);
        hash = HashValue(hash, shot.endDistance);
        hash = HashString(hash, shot.id);
        hash = HashString(hash, shot.mode);
        hash = HashString(hash, shot.presetId);
        hash = HashString(hash, shot.blendAssetId);
        hash = HashValue(hash, shot.view.mode);
        hash = HashValue(hash, shot.view.positionOffset.x);
        hash = HashValue(hash, shot.view.positionOffset.y);
        hash = HashValue(hash, shot.view.positionOffset.z);
        hash = HashValue(hash, shot.view.yaw);
        hash = HashValue(hash, shot.view.pitch);
        hash = HashValue(hash, shot.view.fovY);
        hash = HashValue(hash, shot.view.hideVehicle);
        hash = HashValue(hash, shot.view.priority);
    }
    hash = HashValue(hash, course.terrainMaterialPresets.size());
    for (const CourseTerrainMaterialPreset& material : course.terrainMaterialPresets) {
        hash = HashValue(hash, material.distance);
        hash = HashString(hash, material.id);
        hash = HashValue(hash, material.brightness);
        hash = HashValue(hash, material.noiseStrength);
        hash = HashValue(hash, material.specularStrength);
    }
    hash = HashValue(hash, course.cinematicShotSets.size());
    for (const CourseCinematicShotSet& shotSet : course.cinematicShotSets) {
        hash = HashValue(hash, shotSet.startDistance);
        hash = HashValue(hash, shotSet.endDistance);
        hash = HashString(hash, shotSet.id);
        hash = HashString(hash, shotSet.label);
        hash = HashString(hash, shotSet.lightingPresetId);
        hash = HashString(hash, shotSet.cameraShotId);
        hash = HashString(hash, shotSet.terrainMaterialId);
        hash = HashString(hash, shotSet.fogMood);
        hash = HashString(hash, shotSet.compositionNotes);
    }
    return hash;
}

uint64_t TerrainSignature(const TerrainAuthoringState& terrain) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, terrain.courseObjectEditRevision);
    hash = HashValue(hash, terrain.previewSpeed);
    hash = HashValue(hash, terrain.previewDistance);
    hash = HashValue(hash, terrain.selectedCourseTerrainPlacement);
    hash = HashValue(hash, terrain.selectedCourseRockCluster);
    hash = HashValue(hash, terrain.courseObjectGizmoSpace);
    hash = HashValue(hash, terrain.courseObjectPivotMode);
    for (const int index : terrain.selectedCourseTerrainPlacements) hash = HashValue(hash, index);
    for (const int index : terrain.selectedCourseRockClusters) hash = HashValue(hash, index);
    hash = HashValue(hash, terrain.settings.seed);
    hash = HashValue(hash, terrain.settings.chunkLength);
    hash = HashValue(hash, terrain.settings.visibleAheadChunks);
    hash = HashValue(hash, terrain.settings.volumeRoughness);
    hash = HashValue(hash, terrain.settings.wallHeight);
    return hash;
}

std::string BuildSummary(const CourseAsset& course, const TerrainAuthoringState& terrain) {
    std::ostringstream stream;
    stream << "courseHash=0x" << std::hex << CourseSignature(course)
           << " terrainHash=0x" << TerrainSignature(terrain)
           << std::dec
           << " events=" << course.events.size()
           << " cameras=" << course.cameraKeys.size()
           << " terrain=" << course.terrainPlacements.size()
           << " rocks=" << course.rockClusters.size()
           << " revision=" << terrain.courseObjectEditRevision
           << " previewSpeed=" << std::fixed << std::setprecision(3) << terrain.previewSpeed;
    return stream.str();
}

EditorObjectHandle RuntimeApplyTarget(uint64_t sessionSerial) {
    EditorObjectHandle target{};
    target.domain = EditorDomainId::Unknown;
    target.stableId = "runtime-authoring-apply:" + std::to_string(sessionSerial);
    target.displayName = "Runtime Authoring Apply";
    target.generation = static_cast<uint32_t>(sessionSerial);
    return target;
}

EditorRuntimeAuthoringApplyResult Fail(
    const EditorRuntimeAuthoringApplyRequest& request,
    std::string message) {
    if (request.notifications != nullptr) {
        request.notifications->Push(
            EditorNotificationSeverity::Error,
            request.source,
            message);
    }
    EditorRuntimeAuthoringApplyResult result{};
    result.message = std::move(message);
    return result;
}

void NotifySuccess(
    const EditorRuntimeAuthoringApplyRequest& request,
    const std::string& message) {
    if (request.notifications != nullptr) {
        request.notifications->Push(
            EditorNotificationSeverity::Info,
            request.source,
            message);
    }
}

bool ValidateRequest(
    const EditorRuntimeAuthoringApplyRequest& request,
    std::string& outMessage) {
    if (request.playSession == nullptr) {
        outMessage = "Play session state is unavailable.";
        return false;
    }
    if (!request.playSession->IsActive()) {
        outMessage = "Runtime changes can only be applied during Play/Sim.";
        return false;
    }
    if (request.snapshot == nullptr || !request.snapshot->Captured()) {
        outMessage = "Play/Sim snapshot has not been captured.";
        return false;
    }
    if (request.snapshot->SessionSerial() != 0 &&
        request.snapshot->SessionSerial() != request.playSession->SessionSerial()) {
        outMessage = "Play/Sim snapshot belongs to a different session.";
        return false;
    }
    if (request.course == nullptr) {
        outMessage = "Course asset is unavailable for runtime apply.";
        return false;
    }
    if (request.runtimeState == nullptr) {
        outMessage = "Runtime state is unavailable for runtime apply.";
        return false;
    }
    if (request.transactions == nullptr) {
        outMessage = "Transaction stack is unavailable for runtime apply.";
        return false;
    }
    if (request.validationErrorCount > 0) {
        outMessage = "Runtime apply blocked: validation errors.";
        return false;
    }
    return true;
}

void MarkRuntimeApplyDirty(const EditorRuntimeAuthoringApplyRequest& request) {
    if (request.dirtyState == nullptr || request.runtimeState == nullptr) {
        return;
    }
    request.dirtyState->MarkDirty(
        EditorDirtyDomain::CourseAuthoring,
        "runtime-authoring-apply",
        "Runtime Apply",
        "Runtime changes explicitly applied to authoring.",
        request.runtimeState->terrain.courseObjectEditRevision);
}

} // namespace

EditorRuntimeAuthoringApplyResult EditorRuntimeAuthoringApplyService::Apply(
    const EditorRuntimeAuthoringApplyRequest& request) const {
    std::string validationMessage;
    if (!ValidateRequest(request, validationMessage)) {
        return Fail(request, validationMessage);
    }

    const CourseAsset* beforeCourse = request.snapshot->CapturedCourse();
    const TerrainAuthoringState* beforeTerrain = request.snapshot->CapturedTerrain();
    if (beforeCourse == nullptr || beforeTerrain == nullptr) {
        return Fail(request, "Play/Sim snapshot is missing authoring state.");
    }

    std::string changeSetError;
    if (!request.snapshot->RefreshRuntimeChangeSet(
            EditorPlaySessionIsolationSnapshotTarget{
                request.course, request.runtimeState, request.effectRuntime, request.postProcessStack},
            &changeSetError)) {
        return Fail(
            request,
            changeSetError.empty()
                ? std::string("Failed to inspect runtime authoring changes.")
                : changeSetError);
    }
    const EditorRuntimeChangeSet& runtimeChanges = request.snapshot->RuntimeChanges();
    if (!runtimeChanges.HasChanges()) {
        return Fail(request, "No runtime authoring changes to apply.");
    }
    if (!runtimeChanges.HasSelectedChanges()) {
        return Fail(request, "No runtime authoring changes are selected for Keep Changes.");
    }

    const bool keepCourse = runtimeChanges.ProviderSelected(kCoursePlayIsolationProviderId);
    const bool keepTerrain = runtimeChanges.ProviderSelected(kTerrainPlayIsolationProviderId);
    const bool keepVfx = runtimeChanges.ProviderSelected(kVfxPlayIsolationProviderId);
    const bool keepPostProcess = runtimeChanges.ProviderSelected(kPostProcessPlayIsolationProviderId);
    const std::size_t selectedChangeCount = runtimeChanges.SelectedCount();
    const uint32_t previousTerrainRevision = request.runtimeState->terrain.courseObjectEditRevision;
    if (keepTerrain) {
        ++request.runtimeState->terrain.courseObjectEditRevision;
    }

    EditorRuntimeApplyChange change{};
    change.sessionSerial = request.playSession->SessionSerial();
    change.includesCourse = keepCourse;
    change.beforeCourse = *beforeCourse;
    change.afterCourse = keepCourse ? *request.course : *beforeCourse;
    change.includesTerrain = keepTerrain;
    change.beforeTerrain = *beforeTerrain;
    change.afterTerrain = keepTerrain ? request.runtimeState->terrain : *beforeTerrain;
    if (keepVfx) {
        const EditorVfxAuthoringSnapshot* beforeVfx = request.snapshot->CapturedVfxAuthoring();
        if (beforeVfx == nullptr || request.effectRuntime == nullptr) {
            request.runtimeState->terrain.courseObjectEditRevision = previousTerrainRevision;
            return Fail(request, "VFX authoring snapshot is unavailable for Keep Changes.");
        }
        change.includesVfxAuthoring = true;
        change.beforeVfxAuthoring = *beforeVfx;
        change.afterVfxAuthoring = request.effectRuntime->Assets();
    }
    if (keepPostProcess) {
        const EditorPostProcessAuthoringSnapshot* beforePostProcess = request.snapshot->CapturedPostProcess();
        if (beforePostProcess == nullptr || request.postProcessStack == nullptr) {
            request.runtimeState->terrain.courseObjectEditRevision = previousTerrainRevision;
            return Fail(request, "Post-process snapshot is unavailable for Keep Changes.");
        }
        change.includesPostProcess = true;
        change.beforePostProcess = *beforePostProcess;
        change.afterPostProcess = request.postProcessStack->Passes();
    }
    const std::string beforeSummary = BuildSummary(change.beforeCourse, change.beforeTerrain);
    const std::string afterSummary = BuildSummary(change.afterCourse, change.afterTerrain);

    auto command = std::make_shared<EditorRuntimeApplyUndoCommand>(change);
    const EditorObjectHandle commandTarget = RuntimeApplyTarget(change.sessionSerial);
    EditorError transactionError;
    if (!request.transactions->CanPushCommand(
            "Keep Selected Runtime Changes", commandTarget, command, &transactionError)) {
        request.runtimeState->terrain.courseObjectEditRevision = previousTerrainRevision;
        return Fail(
            request,
            transactionError.message.empty()
                ? std::string("Runtime Apply command cannot be stored in transaction history.")
                : transactionError.message);
    }

    std::string adoptError;
    if (!request.snapshot->AdoptSelected(
            EditorPlaySessionIsolationSnapshotTarget{
                request.course, request.runtimeState, request.effectRuntime, request.postProcessStack},
            &adoptError)) {
        request.runtimeState->terrain.courseObjectEditRevision = previousTerrainRevision;
        return Fail(
            request,
            adoptError.empty()
                ? std::string("Failed to adopt runtime state into Play/Sim snapshot.")
                : adoptError);
    }
    request.snapshot->BindSession(request.playSession->SessionSerial());
    if (!request.transactions->PushCommand(
            "Keep Selected Runtime Changes", commandTarget, std::move(command), &transactionError)) {
        request.runtimeState->terrain.courseObjectEditRevision = previousTerrainRevision;
        return Fail(
            request,
            transactionError.message.empty()
                ? std::string("Failed to register Runtime Apply command.")
                : transactionError.message);
    }
    MarkRuntimeApplyDirty(request);

    EditorRuntimeAuthoringApplyResult result{};
    result.succeeded = true;
    result.changed = true;
    result.sessionSerial = request.playSession->SessionSerial();
    result.beforeSummary = beforeSummary;
    result.afterSummary = afterSummary;
    result.message =
        "Kept " + std::to_string(selectedChangeCount) +
        " selected runtime change provider(s).";
    NotifySuccess(request, result.message);
    return result;
}

} // namespace editor
