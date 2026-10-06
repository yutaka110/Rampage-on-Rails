#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CourseEnemyMutationService.h"
#include "../EditorDirtyStateService.h"
#include "../EditorTransactionStack.h"
#include "../core/EditorExecutionContext.h"

namespace editor {

enum class CourseEnemyEditorControllerStatus {
    Unbound,
    Ready,
    ReadOnly,
    Invalid,
};

struct CourseEnemyEditorControllerBinding final {
    CourseAsset* course = nullptr;
    EditorTransactionStack* transactions = nullptr;
    EditorDirtyStateService* dirtyState = nullptr;
    std::string courseIdentity;
    bool authoringAllowed = true;
};

struct CourseEnemyEditorControllerState final {
    CourseEnemyEditorControllerStatus status =
        CourseEnemyEditorControllerStatus::Unbound;
    std::string courseIdentity;
    std::string message;
    uint64_t mutationRevision = 0;
    uint32_t bindingGeneration = 0;
    bool bound = false;
    bool authoringAllowed = false;
    bool dirty = false;
};

// Wave-scoped edit payload. Every populated field is applied to every member
// and committed by one SetPlacements transaction (all succeed or none do).
struct CourseEnemyWaveBulkEditRequest final {
    std::string waveGroupGuid;
    std::optional<std::string> actorAssetId;
    std::optional<std::string> bulletPatternOverrideId;
    std::optional<std::string> replacementWaveGroupGuid;
    std::optional<float> activationLeadDistance;
    std::optional<Vector3> anchorOffsetDelta;
    std::optional<bool> enabled;
    std::optional<bool> editorVisible;
    std::optional<bool> editorLocked;
    bool includeLocked = false;
    std::string label = "Edit Enemy Wave";
};

// Document-scoped lifecycle and command boundary for persistent enemy
// instances. Viewport code never writes CourseAsset directly.
class CourseEnemyEditorController final {
public:
    CourseEnemyEditorController() = default;
    ~CourseEnemyEditorController();

    bool Bind(
        CourseEnemyEditorControllerBinding binding,
        std::string* errorMessage = nullptr);
    void Unbind();
    bool RefreshAfterExternalReload(
        bool clearUndoHistory,
        std::string* errorMessage = nullptr);
    bool SynchronizeExternalChanges(std::string* errorMessage = nullptr);
    void SetAuthoringAllowed(bool allowed);
    void MarkSaved();

    CourseEnemyMutationResult Mutate(const CourseEnemyMutationRequest& request);
    CourseEnemyMutationResult MutateWave(
        const CourseEnemyWaveBulkEditRequest& request);
    bool Undo(std::string* errorMessage = nullptr);
    bool Redo(std::string* errorMessage = nullptr);

    std::optional<CourseEnemyAuthoringModel> BuildModel() const;
    const CourseEnemyAuthoringModel* Model() const noexcept {
        return modelCache_.has_value() ? &*modelCache_ : nullptr;
    }
    const CourseEnemyEditorControllerState& State() const noexcept { return state_; }
    const CourseAsset* Course() const noexcept { return binding_.course; }
    const CourseEnemyMutationService* MutationService() const noexcept {
        return mutations_.get();
    }
    EditorTransactionStack* Transactions() noexcept;
    const EditorExecutionContext& ExecutionContext() const noexcept {
        return executionContext_;
    }

private:
    bool CreateMutationService(bool clearUndoHistory, std::string* errorMessage);
    bool RebuildModel(std::string* errorMessage = nullptr);
    uint64_t ComputeSourceSignature() const;
    void MarkDirty(std::string reason);
    void SetInvalid(std::string message);

    CourseEnemyEditorControllerBinding binding_{};
    CourseEnemyEditorControllerState state_{};
    EditorTransactionStack ownedTransactions_{};
    std::unique_ptr<CourseEnemyMutationService> mutations_;
    std::optional<CourseEnemyAuthoringModel> modelCache_;
    EditorExecutionContext executionContext_{};
    uint64_t sourceSignature_ = 0;
};

const char* ToString(CourseEnemyEditorControllerStatus status);

} // namespace editor
