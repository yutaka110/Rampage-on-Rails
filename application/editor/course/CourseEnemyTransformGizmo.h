#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CourseEnemyEditorController.h"
#include "../EditorSelection.h"
#include "../EditorTransformGizmoService.h"
#include "../EditorViewportCoordinateService.h"
#include "../EditorViewportOverlay.h"

namespace editor {

enum class CourseEnemyGizmoHandle {
    None,
    X,
    Y,
    Z,
    XY,
    YZ,
    ZX,
    Uniform,
};

struct CourseEnemyTransformGizmoSettings final {
    EditorTransformGizmoMode mode = EditorTransformGizmoMode::Translate;
    EditorTransformGizmoSpace space = EditorTransformGizmoSpace::Local;
    bool snapEnabled = false;
    float translationSnap = 1.0f;
    float rotationSnapDegrees = 15.0f;
    float scaleSnap = 0.1f;
    float handleLengthScale = 0.65f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseEnemyTransformGizmoInput final {
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

struct CourseEnemyTransformGizmoState final {
    bool visible = false;
    bool dragging = false;
    bool previewValid = false;
    bool canMutate = false;
    bool containsLockedPlacement = false;
    uint32_t selectedPlacementCount = 0;
    CourseEnemyGizmoHandle hovered = CourseEnemyGizmoHandle::None;
    CourseEnemyGizmoHandle active = CourseEnemyGizmoHandle::None;
    Vector3 pivot{};
    std::string message;
    uint64_t editRevision = 0;
};

// Translate/rotate/scale manipulator for rail-anchored enemy placements.
// Translation is converted back into RailAnchor data before persistence.
class CourseEnemyTransformGizmo final {
public:
    void Bind(CourseEnemyEditorController* controller);
    bool SetSettings(CourseEnemyTransformGizmoSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseEnemyTransformGizmoInput& input);
    void BuildViewportOverlay(EditorViewportOverlayService& overlay) const;
    void Cancel(std::string message = {});

    const CourseEnemyTransformGizmoSettings& Settings() const noexcept {
        return settings_;
    }
    const CourseEnemyTransformGizmoState& State() const noexcept { return state_; }
    bool WantsPointer() const noexcept {
        return state_.dragging || state_.hovered != CourseEnemyGizmoHandle::None;
    }
    const CourseEnemyAuthoringModel* PreviewModel() const noexcept {
        return previewModel_.has_value() ? &*previewModel_ : nullptr;
    }

private:
    struct Basis final {
        Vector3 x{1.0f, 0.0f, 0.0f};
        Vector3 y{0.0f, 1.0f, 0.0f};
        Vector3 z{0.0f, 0.0f, 1.0f};
    };

    std::vector<std::string> SelectedPlacementGuids(
        const EditorSelection* selection) const;
    bool RefreshFrame(const CourseEnemyTransformGizmoInput& input);
    CourseEnemyGizmoHandle PickHandle(float displayX, float displayY) const;
    bool BeginDrag(CourseEnemyGizmoHandle handle, float displayX, float displayY);
    void UpdateDrag(float displayX, float displayY);
    void CommitDrag();
    Basis ResolveBasis(const CourseEnemyPlacementResolution& placement) const;
    Vector3 Axis(CourseEnemyGizmoHandle handle) const;
    bool PlaneAxes(CourseEnemyGizmoHandle handle, Vector3& a, Vector3& b) const;
    bool RayPlane(
        float displayX, float displayY,
        const Vector3& planePoint, const Vector3& planeNormal,
        Vector3& intersection) const;
    bool RayAxisParameter(
        float displayX, float displayY,
        const Vector3& origin, const Vector3& axis,
        float& parameter) const;
    Vector3 SnapTranslation(Vector3 delta, CourseEnemyGizmoHandle handle) const;
    float SnapRotation(float degrees) const;
    float SnapScale(float factor) const;
    void RefreshPreview();

    CourseEnemyEditorController* controller_ = nullptr;
    uint32_t bindingGeneration_ = 0;
    CourseEnemyTransformGizmoSettings settings_{};
    CourseEnemyTransformGizmoState state_{};
    const EditorViewportCoordinateService* coordinates_ = nullptr;
    std::vector<std::string> selectedGuids_;
    Basis basis_{};
    float handleLength_ = 5.0f;
    CourseAsset previewCourse_{};
    std::optional<CourseEnemyAuthoringModel> previewModel_;
    std::vector<CourseEnemyPlacement> originalPlacements_;
    std::vector<Vector3> originalWorldPositions_;
    Vector3 dragStartWorld_{};
    Vector3 dragStartVector_{};
    Vector3 dragOriginPivot_{};
    float dragStartAxisParameter_ = 0.0f;
    float dragStartDisplayY_ = 0.0f;
    uint64_t dragExpectedRevision_ = 0;
};

const char* ToString(CourseEnemyGizmoHandle handle);

} // namespace editor
