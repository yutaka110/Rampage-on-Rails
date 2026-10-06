#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "CourseRailEditorController.h"
#include "CourseRailPickingService.h"

namespace editor {

enum class CourseRailEditMode {
    SelectMove,
    Add,
    Tangent,
    Delete,
};

struct CourseRailViewportEditSettings final {
    bool gridSnap = false;
    float gridSize = 1.0f;
    bool mirrorTangents = true;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseRailViewportEditInput final {
    const EditorViewportCoordinateService* coordinates = nullptr;
    float displayX = 0.0f;
    float displayY = 0.0f;
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool primaryCancelled = false;
    bool cancelPressed = false;
    bool deletePressed = false;
    bool undoPressed = false;
    bool redoPressed = false;
    bool toggleSelection = false;
};

struct CourseRailViewportEditState final {
    bool active = false;
    bool dragging = false;
    bool previewValid = false;
    bool canMutate = false;
    CourseRailEditMode mode = CourseRailEditMode::SelectMove;
    CourseRailPickResult hovered{};
    std::string selectedPointGuid;
    std::string message;
    uint64_t editRevision = 0;
};

// Persistent viewport authoring tool for the active Course document. Dragging
// only changes a private preview; mouse release emits exactly one Controller
// mutation and therefore exactly one undoable command.
class CourseRailViewportEditTool final {
public:
    void Bind(
        CourseRailEditorController* controller,
        const CourseRailPickingService* picking);
    void SetActive(bool active);
    void SetMode(CourseRailEditMode mode);
    void SetSelectedPoint(std::string guid);
    bool SetSettings(CourseRailViewportEditSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseRailViewportEditInput& input);
    void CancelDrag(std::string message = {});

    bool Active() const noexcept { return state_.active; }
    const CourseRailViewportEditState& State() const noexcept { return state_; }
    const CourseRailViewportEditSettings& Settings() const noexcept { return settings_; }
    const CourseRailAuthoringModel* PreviewModel() const noexcept {
        return previewModel_.has_value() ? &*previewModel_ : nullptr;
    }
    std::optional<CourseRailPickResult> ConsumeSelectionRequest();
    bool ConsumeClearSelectionRequest();
    std::string ViewportHint() const;

private:
    enum class DragKind { None, Point, IncomingTangent, OutgoingTangent };

    bool CanMutate() const;
    CourseRailPickResult Pick(const CourseRailViewportEditInput& input) const;
    bool BeginDrag(const CourseRailPickResult& pick, const CourseRailViewportEditInput& input);
    void UpdateDrag(const CourseRailViewportEditInput& input);
    void CommitDrag();
    void AddAtSegment(const CourseRailPickResult& pick);
    void RemovePoint(std::string_view guid);
    void ApplyUndoRedo(bool redo);
    void RequestSelection(const CourseRailPickResult& pick);
    void RefreshPreview();
    bool IntersectDragPlane(
        const EditorViewportCoordinateService& coordinates,
        float displayX,
        float displayY,
        Vector3& worldPosition) const;
    Vector3 Snap(Vector3 value) const;

    CourseRailEditorController* controller_ = nullptr;
    const CourseRailPickingService* picking_ = nullptr;
    CourseRailViewportEditSettings settings_{};
    CourseRailViewportEditState state_{};
    CourseAsset previewCourse_{};
    std::optional<CourseRailAuthoringModel> previewModel_;
    std::optional<CourseRailPickResult> selectionRequest_;
    bool clearSelectionRequested_ = false;
    DragKind dragKind_ = DragKind::None;
    RailPathControlPoint dragPoint_{};
    Vector3 dragPlanePoint_{};
    Vector3 dragPlaneNormal_{0.0f, 1.0f, 0.0f};
    Vector3 dragOffset_{};
    uint64_t dragExpectedRevision_ = 0;
    uint32_t bindingGeneration_ = 0;
};

const char* ToString(CourseRailEditMode mode);

} // namespace editor
