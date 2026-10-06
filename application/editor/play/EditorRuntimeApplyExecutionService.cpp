#include "EditorRuntimeApplyExecutionService.h"

#include "../EditorDirtyStateService.h"
#include "../EditorNotificationCenter.h"
#include "../../EffectRuntime.h"

#include <utility>

namespace editor {

EditorUndoResult EditorRuntimeApplyExecutionService::ApplyRuntimeChange(
    const EditorRuntimeApplyChange& change,
    EditorTransactionApplyMode mode) {
    if (targets_.course == nullptr || targets_.runtimeState == nullptr) {
        return EditorUndoResult::Failure(
            EditorErrorCode::MissingService,
            "Runtime Apply authoring targets are unavailable.");
    }
    if (change.includesVfxAuthoring && targets_.effectRuntime == nullptr) {
        return EditorUndoResult::Failure(EditorErrorCode::MissingService, "VFX authoring target is unavailable.");
    }
    if (change.includesPostProcess && targets_.postProcessStack == nullptr) {
        return EditorUndoResult::Failure(EditorErrorCode::MissingService, "Post-process target is unavailable.");
    }

    const bool undo = mode == EditorTransactionApplyMode::Undo;
    CourseAsset desiredCourse = undo ? change.beforeCourse : change.afterCourse;
    TerrainAuthoringState desiredTerrain = undo ? change.beforeTerrain : change.afterTerrain;
    auto desiredVfx = change.includesVfxAuthoring
        ? (undo ? change.beforeVfxAuthoring : change.afterVfxAuthoring)
        : decltype(change.beforeVfxAuthoring){};
    auto desiredPost = change.includesPostProcess
        ? (undo ? change.beforePostProcess : change.afterPostProcess)
        : decltype(change.beforePostProcess){};

    std::string validationError;
    if ((change.includesVfxAuthoring && !targets_.effectRuntime->ValidateAssetReplacement(desiredVfx, &validationError)) ||
        (change.includesPostProcess && !PostProcessStack::ValidatePasses(desiredPost, &validationError))) {
        return EditorUndoResult::Failure(EditorErrorCode::NotAvailable, validationError);
    }

    if (change.includesCourse) {
        using std::swap;
        swap(*targets_.course, desiredCourse);
    }
    if (change.includesTerrain) targets_.runtimeState->terrain = desiredTerrain;
    if (change.includesVfxAuthoring) {
        (void)targets_.effectRuntime->ReplaceAssets(std::move(desiredVfx));
        targets_.effectRuntime->ClearInstances();
    }
    if (change.includesPostProcess) (void)targets_.postProcessStack->ReplacePasses(std::move(desiredPost));

    if (targets_.dirtyState != nullptr) {
        targets_.dirtyState->MarkDirty(
            EditorDirtyDomain::CourseAuthoring,
            "runtime-authoring-apply",
            "Runtime Apply",
            "Runtime changes explicitly applied to authoring.",
            targets_.runtimeState->terrain.courseObjectEditRevision);
    }
    const std::string message = undo
        ? "Undid runtime authoring apply."
        : "Redid runtime authoring apply.";
    if (targets_.notifications != nullptr) {
        targets_.notifications->Push(EditorNotificationSeverity::Info, targets_.source, message);
    }
    return EditorUndoResult::Success(message);
}

} // namespace editor
