#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CourseRailEditorController.h"
#include "../EditorSelection.h"
#include "../EditorTransformGizmoService.h"
#include "../EditorViewportCoordinateService.h"
#include "../EditorViewportOverlay.h"

namespace editor {

enum class CourseRailGizmoHandle {
    None,
    X,
    Y,
    Z,
    XY,
    YZ,
    ZX,
};

struct CourseRailTransformGizmoSettings final {
    EditorTransformGizmoSpace space = EditorTransformGizmoSpace::World;
    bool snapEnabled = false;
    float gridSize = 1.0f;
    float handleLengthScale = 0.65f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseRailTransformGizmoInput final {
    const EditorSelection* selection = nullptr;
    const EditorViewportCoordinateService* coordinates = nullptr;
    bool enabled = false;
    bool canMutate = false;
    float displayX = 0.0f;
    float displayY = 0.0f;
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool primaryCancelled = false;
    bool cancelPressed = false;
};

struct CourseRailTransformGizmoState final {
    bool visible = false;
    bool dragging = false;
    bool previewValid = false;
    bool canMutate = false;
    uint32_t selectedPointCount = 0;
    CourseRailGizmoHandle hovered = CourseRailGizmoHandle::None;
    CourseRailGizmoHandle active = CourseRailGizmoHandle::None;
    Vector3 pivot{};
    std::string message;
    uint64_t editRevision = 0;
};

class CourseRailTransformGizmo final {
public:
    void Bind(CourseRailEditorController* controller);
    bool SetSettings(CourseRailTransformGizmoSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseRailTransformGizmoInput& input);
    void BuildViewportOverlay(EditorViewportOverlayService& overlay) const;
    void Cancel(std::string message = {});

    const CourseRailTransformGizmoSettings& Settings() const noexcept { return settings_; }
    const CourseRailTransformGizmoState& State() const noexcept { return state_; }
    bool WantsPointer() const noexcept {
        return state_.dragging || state_.hovered != CourseRailGizmoHandle::None;
    }
    const CourseRailAuthoringModel* PreviewModel() const noexcept {
        return previewModel_.has_value() ? &*previewModel_ : nullptr;
    }

private:
    struct Basis final {
        Vector3 x{1.0f, 0.0f, 0.0f};
        Vector3 y{0.0f, 1.0f, 0.0f};
        Vector3 z{0.0f, 0.0f, 1.0f};
    };

    std::vector<std::string> SelectedPointGuids(const EditorSelection* selection) const;
    bool RefreshFrame(const CourseRailTransformGizmoInput& input);
    CourseRailGizmoHandle PickHandle(float displayX, float displayY) const;
    bool BeginDrag(CourseRailGizmoHandle handle, float displayX, float displayY);
    void UpdateDrag(float displayX, float displayY);
    void CommitDrag();
    Basis ResolveBasis(const CourseRailAuthoringModel& model, uint32_t pointIndex) const;
    Vector3 Axis(CourseRailGizmoHandle handle) const;
    bool PlaneAxes(CourseRailGizmoHandle handle, Vector3& a, Vector3& b) const;
    bool RayPlane(
        float displayX, float displayY,
        const Vector3& planePoint, const Vector3& planeNormal,
        Vector3& intersection) const;
    bool RayAxisParameter(
        float displayX, float displayY,
        const Vector3& origin, const Vector3& axis,
        float& parameter) const;
    Vector3 SnapDelta(Vector3 delta, CourseRailGizmoHandle handle) const;
    void RefreshPreview();

    CourseRailEditorController* controller_ = nullptr;
    uint32_t bindingGeneration_ = 0;
    CourseRailTransformGizmoSettings settings_{};
    CourseRailTransformGizmoState state_{};
    const EditorViewportCoordinateService* coordinates_ = nullptr;
    std::vector<std::string> selectedGuids_;
    Basis basis_{};
    float handleLength_ = 5.0f;
    CourseAsset previewCourse_{};
    std::optional<CourseRailAuthoringModel> previewModel_;
    std::vector<Vector3> originalPositions_;
    Vector3 dragStartWorld_{};
    Vector3 dragOriginPivot_{};
    float dragStartAxisParameter_ = 0.0f;
    uint64_t dragExpectedRevision_ = 0;
};

const char* ToString(CourseRailGizmoHandle handle);

} // namespace editor
