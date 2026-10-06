#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CourseEnemyEditorController.h"
#include "CourseEnemyPickingService.h"
#include "CourseRailPickingService.h"

namespace editor {

class EditorViewportOverlayService;

enum class CourseEnemyEditMode {
    SelectMove,
    Add,
    Duplicate,
    Delete,
};

struct CourseEnemyViewportEditSettings final {
    std::string defaultActorAssetId = "drone";
    std::string defaultBulletPatternId;
    std::string defaultWaveGroupGuid;
    Vector3 duplicateOffset{2.0f, 0.0f, 0.0f};
    bool offsetSnap = false;
    float offsetSnapSize = 1.0f;
    uint32_t railProjectionSubdivisions = 48;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseEnemyViewportEditInput final {
    const EditorViewportCoordinateService* coordinates = nullptr;
    float displayX = 0.0f;
    float displayY = 0.0f;
    bool primaryPressed = false;
    bool primaryDown = false;
    bool primaryReleased = false;
    bool primaryCancelled = false;
    bool cancelPressed = false;
    bool deletePressed = false;
    bool duplicatePressed = false;
    bool undoPressed = false;
    bool redoPressed = false;
    bool toggleSelection = false;
};

struct CourseEnemyViewportEditState final {
    bool active = false;
    bool dragging = false;
    bool marqueeSelecting = false;
    bool previewValid = false;
    bool canMutate = false;
    CourseEnemyEditMode mode = CourseEnemyEditMode::SelectMove;
    CourseEnemyPickResult hovered{};
    uint32_t selectedPlacementCount = 0;
    std::string primaryPlacementGuid;
    std::string message;
    uint64_t editRevision = 0;
};

struct CourseEnemyRangeSelectionRequest final {
    std::vector<CourseEnemyPickResult> placements;
    bool additive = false;
};

// Direct manipulation boundary for persistent enemy placements. Pointer drags
// edit a private CourseAsset and commit one SetAnchors request on release.
class CourseEnemyViewportEditTool final {
public:
    void Bind(
        CourseEnemyEditorController* controller,
        const CourseEnemyPickingService* picking,
        const CourseRailPickingService* railPicking);
    void SetActive(bool active);
    void SetMode(CourseEnemyEditMode mode);
    void SetSelectedPlacements(std::vector<std::string> guids);
    bool SetSettings(CourseEnemyViewportEditSettings settings, std::string* errorMessage = nullptr);
    void Tick(const CourseEnemyViewportEditInput& input);
    void CancelDrag(std::string message = {});
    void BuildViewportOverlay(EditorViewportOverlayService& overlay) const;

    CourseEnemyMutationResult PlaceActorAssetAtDisplay(
        std::string actorAssetId,
        const EditorViewportCoordinateService& coordinates,
        float displayX,
        float displayY);

    bool Active() const noexcept { return state_.active; }
    const CourseEnemyViewportEditState& State() const noexcept { return state_; }
    const CourseEnemyViewportEditSettings& Settings() const noexcept { return settings_; }
    const CourseEnemyAuthoringModel* PreviewModel() const noexcept {
        return previewModel_.has_value() ? &*previewModel_ : nullptr;
    }
    std::optional<CourseEnemyPickResult> ConsumeSelectionRequest();
    std::optional<CourseEnemyRangeSelectionRequest>
        ConsumeRangeSelectionRequest();
    bool ConsumeClearSelectionRequest();
    std::string ViewportHint() const;

private:
    bool CanMutate() const;
    CourseEnemyPickResult PickEnemy(const CourseEnemyViewportEditInput& input) const;
    CourseRailPickResult PickRail(const CourseEnemyViewportEditInput& input) const;
    bool BeginDrag(
        const CourseEnemyPickResult& pick,
        const CourseEnemyViewportEditInput& input);
    void UpdateDrag(const CourseEnemyViewportEditInput& input);
    void CommitDrag();
    void AddAtRail(const CourseRailPickResult& pick);
    CourseEnemyMutationResult AddAtRail(
        const CourseRailPickResult& pick,
        std::string actorAssetId,
        std::string label);
    void DuplicateSelected(std::string_view fallbackGuid = {});
    void RemoveSelected(std::string_view fallbackGuid = {});
    void ApplyUndoRedo(bool redo);
    void RequestSelection(std::string_view guid);
    void RefreshPreview();
    bool IntersectDragPlane(
        const EditorViewportCoordinateService& coordinates,
        float displayX,
        float displayY,
        Vector3& worldPosition) const;
    RailAnchor SnapAnchor(RailAnchor anchor) const;

    CourseEnemyEditorController* controller_ = nullptr;
    const CourseEnemyPickingService* picking_ = nullptr;
    const CourseRailPickingService* railPicking_ = nullptr;
    CourseEnemyViewportEditSettings settings_{};
    CourseEnemyViewportEditState state_{};
    std::vector<std::string> selectedGuids_;
    CourseAsset previewCourse_{};
    std::optional<CourseEnemyAuthoringModel> previewModel_;
    std::optional<CourseEnemyPickResult> selectionRequest_;
    std::optional<CourseEnemyRangeSelectionRequest> rangeSelectionRequest_;
    bool clearSelectionRequested_ = false;
    float marqueeStartDisplayX_ = 0.0f;
    float marqueeStartDisplayY_ = 0.0f;
    float marqueeCurrentDisplayX_ = 0.0f;
    float marqueeCurrentDisplayY_ = 0.0f;
    float marqueeStartRenderX_ = 0.0f;
    float marqueeStartRenderY_ = 0.0f;
    float marqueeCurrentRenderX_ = 0.0f;
    float marqueeCurrentRenderY_ = 0.0f;
    bool marqueeAdditive_ = false;
    CourseEnemyPlacement dragOriginal_{};
    Vector3 dragPlanePoint_{};
    Vector3 dragPlaneNormal_{0.0f, 1.0f, 0.0f};
    Vector3 dragOffset_{};
    uint64_t dragExpectedRevision_ = 0;
    uint32_t bindingGeneration_ = 0;
};

const char* ToString(CourseEnemyEditMode mode);

} // namespace editor
