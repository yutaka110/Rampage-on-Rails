#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "CourseWaveMutationService.h"
#include "../EditorDirtyStateService.h"
#include "../EditorTransactionStack.h"
#include "../core/EditorExecutionContext.h"

namespace editor {

enum class CourseWaveEditorControllerStatus {
    Unbound,
    Ready,
    ReadOnly,
    Invalid,
};

struct CourseWaveEditorControllerBinding final {
    CourseAsset* course = nullptr;
    EditorTransactionStack* transactions = nullptr;
    EditorDirtyStateService* dirtyState = nullptr;
    std::string courseIdentity;
    bool authoringAllowed = true;
};

struct CourseWaveEditorControllerState final {
    CourseWaveEditorControllerStatus status =
        CourseWaveEditorControllerStatus::Unbound;
    std::string courseIdentity;
    std::string message;
    uint64_t mutationRevision = 0;
    uint32_t bindingGeneration = 0;
    bool bound = false;
    bool authoringAllowed = false;
    bool dirty = false;
};

// Document-scoped command boundary for schema-v7 encounter waves. Viewport,
// Details and Sequencer integrations all route writes through this controller.
class CourseWaveEditorController final {
public:
    CourseWaveEditorController() = default;
    ~CourseWaveEditorController();

    bool Bind(
        CourseWaveEditorControllerBinding binding,
        std::string* errorMessage = nullptr);
    void Unbind();
    bool RefreshAfterExternalReload(
        bool clearUndoHistory,
        std::string* errorMessage = nullptr);
    bool SynchronizeExternalChanges(std::string* errorMessage = nullptr);
    void SetAuthoringAllowed(bool allowed);
    void MarkSaved();

    CourseWaveMutationResult Mutate(const CourseWaveMutationRequest& request);
    // Used when a higher-level editor service (Sequencer) owns the transaction.
    // It validates and commits through CourseWaveMutationService but does not
    // push a second nested undo command or mark the document dirty during preview.
    CourseWaveMutationResult MutateForExternalTransaction(
        const CourseWaveMutationRequest& request);
    bool Undo(std::string* errorMessage = nullptr);
    bool Redo(std::string* errorMessage = nullptr);

    std::optional<CourseWaveAuthoringModel> BuildModel() const;
    const CourseWaveAuthoringModel* Model() const noexcept {
        return modelCache_.has_value() ? &*modelCache_ : nullptr;
    }
    const CourseWaveEditorControllerState& State() const noexcept { return state_; }
    const CourseAsset* Course() const noexcept { return binding_.course; }
    const CourseWaveMutationService* MutationService() const noexcept {
        return mutations_.get();
    }
    EditorTransactionStack* Transactions() noexcept;
    const EditorExecutionContext& ExecutionContext() const noexcept {
        return executionContext_;
    }

private:
    CourseWaveMutationResult MutateInternal(
        const CourseWaveMutationRequest& request,
        EditorTransactionStack* transactions,
        bool markDirty);
    bool CreateMutationService(bool clearUndoHistory, std::string* errorMessage);
    bool RebuildModel(std::string* errorMessage = nullptr);
    uint64_t ComputeSourceSignature() const;
    void MarkDirty(std::string reason);
    void SetInvalid(std::string message);

    CourseWaveEditorControllerBinding binding_{};
    CourseWaveEditorControllerState state_{};
    EditorTransactionStack ownedTransactions_{};
    std::unique_ptr<CourseWaveMutationService> mutations_;
    std::optional<CourseWaveAuthoringModel> modelCache_;
    EditorExecutionContext executionContext_{};
    uint64_t sourceSignature_ = 0;
};

const char* ToString(CourseWaveEditorControllerStatus status);

} // namespace editor
