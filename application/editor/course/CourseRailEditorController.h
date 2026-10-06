#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "CourseRailMutationService.h"
#include "../EditorDirtyStateService.h"
#include "../EditorTransactionStack.h"
#include "../core/EditorExecutionContext.h"

namespace editor {

enum class CourseRailEditorControllerStatus {
    Unbound,
    Ready,
    ReadOnly,
    Invalid,
};

struct CourseRailEditorControllerBinding final {
    CourseAsset* course = nullptr;
    RailPath* runtimeRailPath = nullptr;
    EditorTransactionStack* transactions = nullptr;
    EditorDirtyStateService* dirtyState = nullptr;
    std::string courseIdentity;
    bool authoringAllowed = true;
};

struct CourseRailEditorControllerState final {
    CourseRailEditorControllerStatus status = CourseRailEditorControllerStatus::Unbound;
    std::string courseIdentity;
    std::string message;
    uint64_t mutationRevision = 0;
    uint32_t bindingGeneration = 0;
    bool bound = false;
    bool authoringAllowed = false;
    bool dirty = false;
};

// Document-scoped lifecycle owner for rail authoring. It keeps the mutation
// service registered for command Undo/Redo and is the only API viewport tools
// need in order to mutate the active CourseAsset.
class CourseRailEditorController final {
public:
    CourseRailEditorController() = default;
    ~CourseRailEditorController();

    bool Bind(CourseRailEditorControllerBinding binding, std::string* errorMessage = nullptr);
    void Unbind();
    bool RefreshAfterExternalReload(bool clearUndoHistory, std::string* errorMessage = nullptr);
    void SetAuthoringAllowed(bool allowed);
    void MarkSaved();

    CourseRailMutationResult Mutate(const CourseRailMutationRequest& request);
    bool Undo(std::string* errorMessage = nullptr);
    bool Redo(std::string* errorMessage = nullptr);

    std::optional<CourseRailAuthoringModel> BuildModel() const;
    const CourseRailAuthoringModel* Model() const noexcept {
        return modelCache_.has_value() ? &*modelCache_ : nullptr;
    }
    const CourseRailEditorControllerState& State() const noexcept { return state_; }
    const CourseAsset* Course() const noexcept { return binding_.course; }
    const CourseRailMutationService* MutationService() const noexcept { return mutations_.get(); }
    EditorTransactionStack* Transactions() noexcept;
    const EditorExecutionContext& ExecutionContext() const noexcept { return executionContext_; }

private:
    bool CreateMutationService(bool clearUndoHistory, std::string* errorMessage);
    void MarkDirty(std::string reason);
    void SetInvalid(std::string message);
    bool RebuildModel(std::string* errorMessage = nullptr);

    CourseRailEditorControllerBinding binding_{};
    CourseRailEditorControllerState state_{};
    EditorTransactionStack ownedTransactions_{};
    std::unique_ptr<CourseRailMutationService> mutations_;
    std::optional<CourseRailAuthoringModel> modelCache_;
    EditorExecutionContext executionContext_{};
};

const char* ToString(CourseRailEditorControllerStatus status);

} // namespace editor
