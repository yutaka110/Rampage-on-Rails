#include "CourseEnemyViewportEditTool.h"
#include "../EditorSettingsValidation.h"

#include "../EditorViewportOverlay.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace editor {
namespace {

Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(const Vector3& a, const Vector3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

void CourseEnemyViewportEditTool::Bind(
    CourseEnemyEditorController* controller,
    const CourseEnemyPickingService* picking,
    const CourseRailPickingService* railPicking) {
    const uint32_t generation = controller != nullptr
        ? controller->State().bindingGeneration : 0;
    if (controller_ == controller && picking_ == picking &&
        railPicking_ == railPicking && bindingGeneration_ == generation) {
        return;
    }
    CancelDrag();
    controller_ = controller;
    picking_ = picking;
    railPicking_ = railPicking;
    bindingGeneration_ = generation;
    if (controller_ == nullptr || !controller_->State().bound) {
        state_.active = false;
        selectedGuids_.clear();
        state_.primaryPlacementGuid.clear();
        state_.message = "Course enemy controller is unavailable.";
    }
}

void CourseEnemyViewportEditTool::SetActive(bool active) {
    if (state_.active == active) return;
    CancelDrag();
    state_.active = active && controller_ != nullptr && controller_->State().bound;
    state_.message = state_.active
        ? "Course Enemy viewport editing enabled."
        : "Course Enemy viewport editing disabled.";
}

void CourseEnemyViewportEditTool::SetMode(CourseEnemyEditMode mode) {
    if (state_.mode == mode) return;
    CancelDrag();
    state_.mode = mode;
    state_.message = std::string("Enemy tool mode: ") + ToString(mode);
}

void CourseEnemyViewportEditTool::SetSelectedPlacements(
    std::vector<std::string> guids) {
    if (state_.dragging) return;
    std::sort(guids.begin(), guids.end());
    guids.erase(std::unique(guids.begin(), guids.end()), guids.end());
    selectedGuids_ = std::move(guids);
    state_.selectedPlacementCount = static_cast<uint32_t>(selectedGuids_.size());
    state_.primaryPlacementGuid = selectedGuids_.empty() ? std::string{} : selectedGuids_.front();
}

bool CourseEnemyViewportEditSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(offsetSnapSize, .01f, 1000.0f) &&
        settings::InRange(railProjectionSubdivisions, 8u, 256u) &&
        settings::Finite(duplicateOffset.x) && settings::Finite(duplicateOffset.y) && settings::Finite(duplicateOffset.z);
    return settings::Result(valid, errorMessage,
        "CourseEnemyViewportEditSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseEnemyViewportEditTool::SetSettings(
    CourseEnemyViewportEditSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (settings.defaultActorAssetId.empty()) settings.defaultActorAssetId = "drone";
    settings_ = std::move(settings);
    return true;
}

void CourseEnemyViewportEditTool::Tick(const CourseEnemyViewportEditInput& input) {
    state_.canMutate = CanMutate();
    state_.selectedPlacementCount = static_cast<uint32_t>(selectedGuids_.size());
    if (!state_.active || controller_ == nullptr || picking_ == nullptr ||
        input.coordinates == nullptr) {
        state_.hovered = {};
        if (state_.dragging) {
            CancelDrag("Enemy drag cancelled because the viewport became unavailable.");
        }
        return;
    }

    if (input.primaryCancelled || input.cancelPressed) {
        CancelDrag("Enemy drag cancelled.");
    }
    if (input.undoPressed) ApplyUndoRedo(false);
    if (input.redoPressed) ApplyUndoRedo(true);

    state_.hovered = PickEnemy(input);
    if (input.deletePressed && !state_.dragging) {
        RemoveSelected();
        return;
    }
    if (input.duplicatePressed && !state_.dragging) {
        DuplicateSelected();
        return;
    }

    if (state_.dragging) {
        if (input.primaryDown) UpdateDrag(input);
        if (input.primaryReleased) CommitDrag();
        return;
    }

    if (state_.marqueeSelecting) {
        marqueeCurrentDisplayX_ = input.displayX;
        marqueeCurrentDisplayY_ = input.displayY;
        const EditorViewportCoordinatePoint render =
            input.coordinates->DisplayToRender(input.displayX, input.displayY);
        if (render.valid) {
            marqueeCurrentRenderX_ = render.x;
            marqueeCurrentRenderY_ = render.y;
        }
        if (input.primaryReleased) {
            const float width = std::fabs(
                marqueeCurrentDisplayX_ - marqueeStartDisplayX_);
            const float height = std::fabs(
                marqueeCurrentDisplayY_ - marqueeStartDisplayY_);
            if (width >= 4.0f || height >= 4.0f) {
                CourseEnemyRangeSelectionRequest request{};
                request.additive = marqueeAdditive_;
                const CourseEnemyAuthoringModel* model = controller_->Model();
                if (model != nullptr) {
                    request.placements = picking_->PickDisplayRect(
                        *model, *input.coordinates,
                        marqueeStartDisplayX_, marqueeStartDisplayY_,
                        marqueeCurrentDisplayX_, marqueeCurrentDisplayY_);
                }
                rangeSelectionRequest_ = std::move(request);
                state_.message = "Enemy marquee selection completed.";
            } else if (!marqueeAdditive_) {
                clearSelectionRequested_ = true;
            }
            state_.marqueeSelecting = false;
        }
        return;
    }
    if (!input.primaryPressed) return;

    if (state_.mode == CourseEnemyEditMode::Add) {
        const CourseRailPickResult railPick = PickRail(input);
        if (railPick.kind == CourseRailPickKind::Segment) AddAtRail(railPick);
        else state_.message = "Add mode requires clicking a visible rail segment.";
        return;
    }
    if (state_.mode == CourseEnemyEditMode::Delete) {
        if (state_.hovered.hit) RemoveSelected(state_.hovered.placementGuid);
        else state_.message = "Delete mode requires clicking an enemy placement.";
        return;
    }
    if (state_.mode == CourseEnemyEditMode::Duplicate) {
        if (state_.hovered.hit) DuplicateSelected(state_.hovered.placementGuid);
        else state_.message = "Duplicate mode requires clicking an enemy placement.";
        return;
    }

    if (!state_.hovered.hit) {
        state_.marqueeSelecting = true;
        marqueeStartDisplayX_ = marqueeCurrentDisplayX_ = input.displayX;
        marqueeStartDisplayY_ = marqueeCurrentDisplayY_ = input.displayY;
        marqueeAdditive_ = input.toggleSelection;
        const EditorViewportCoordinatePoint render =
            input.coordinates->DisplayToRender(input.displayX, input.displayY);
        if (render.valid) {
            marqueeStartRenderX_ = marqueeCurrentRenderX_ = render.x;
            marqueeStartRenderY_ = marqueeCurrentRenderY_ = render.y;
        }
        state_.message = marqueeAdditive_
            ? "Additive enemy marquee selection started."
            : "Enemy marquee selection started.";
        return;
    }
    RequestSelection(state_.hovered.placementGuid);
    if (input.toggleSelection || !state_.canMutate || state_.hovered.locked) return;
    BeginDrag(state_.hovered, input);
}

void CourseEnemyViewportEditTool::CancelDrag(std::string message) {
    state_.dragging = false;
    state_.marqueeSelecting = false;
    state_.previewValid = false;
    previewModel_.reset();
    previewCourse_ = {};
    dragOriginal_ = {};
    if (!message.empty()) state_.message = std::move(message);
}

void CourseEnemyViewportEditTool::BuildViewportOverlay(
    EditorViewportOverlayService& overlay) const {
    if (!state_.active || !state_.marqueeSelecting) return;
    EditorViewportOverlayItemOptions options{};
    options.selected = true;
    options.priority = 245;
    const float left = (std::min)(marqueeStartRenderX_, marqueeCurrentRenderX_);
    const float right = (std::max)(marqueeStartRenderX_, marqueeCurrentRenderX_);
    const float top = (std::min)(marqueeStartRenderY_, marqueeCurrentRenderY_);
    const float bottom = (std::max)(marqueeStartRenderY_, marqueeCurrentRenderY_);
    auto sink = overlay.Sink(EditorViewportOverlayLayerId::SelectionOutline);
    sink.RectFilled(
        left, top, right, bottom,
        IM_COL32(48, 168, 230, 40), options);
    sink.Rect(
        left, top, right, bottom,
        IM_COL32(92, 208, 255, 245), 1.5f, options);
}

CourseEnemyMutationResult CourseEnemyViewportEditTool::PlaceActorAssetAtDisplay(
    std::string actorAssetId,
    const EditorViewportCoordinateService& coordinates,
    float displayX,
    float displayY) {
    CourseEnemyMutationResult result{};
    result.revision = controller_ != nullptr
        ? controller_->State().mutationRevision : 0;
    if (!state_.active || !CanMutate() || controller_ == nullptr ||
        railPicking_ == nullptr || actorAssetId.empty()) {
        result.message = "Enable writable Course Enemy editing before dropping an ActorAsset.";
        state_.message = result.message;
        return result;
    }
    const CourseEnemyAuthoringModel* model = controller_->Model();
    if (model == nullptr) {
        result.message = "Course enemy model is unavailable.";
        state_.message = result.message;
        return result;
    }
    CourseRailPickingSettings pickingSettings{};
    pickingSettings.preferControlPoints = false;
    pickingSettings.includeTangentHandles = false;
    pickingSettings.segmentRadiusPixels = 64.0f;
    const CourseRailPickResult railPick = railPicking_->PickDisplay(
        model->RailModel(), coordinates, displayX, displayY, pickingSettings);
    if (railPick.kind != CourseRailPickKind::Segment) {
        result.message = "Drop the ActorAsset within 64 px of a visible rail segment.";
        state_.message = result.message;
        return result;
    }
    return AddAtRail(
        railPick, std::move(actorAssetId), "Place ActorAsset From Content Browser");
}

std::optional<CourseEnemyPickResult>
CourseEnemyViewportEditTool::ConsumeSelectionRequest() {
    std::optional<CourseEnemyPickResult> result = std::move(selectionRequest_);
    selectionRequest_.reset();
    return result;
}

std::optional<CourseEnemyRangeSelectionRequest>
CourseEnemyViewportEditTool::ConsumeRangeSelectionRequest() {
    std::optional<CourseEnemyRangeSelectionRequest> result =
        std::move(rangeSelectionRequest_);
    rangeSelectionRequest_.reset();
    return result;
}

bool CourseEnemyViewportEditTool::ConsumeClearSelectionRequest() {
    const bool result = clearSelectionRequested_;
    clearSelectionRequested_ = false;
    return result;
}

std::string CourseEnemyViewportEditTool::ViewportHint() const {
    if (!state_.active) return {};
    if (!state_.canMutate) return "Course Enemy: authoring is locked during Play/Sim";
    if (state_.dragging) {
        return "Course Enemy: RailAnchor preview - release commits one Undo transaction; Esc cancels";
    }
    switch (state_.mode) {
    case CourseEnemyEditMode::SelectMove:
        return "Course Enemy: click/drag empty space for marquee (Shift adds); drag marker to move; W/E/R gizmo";
    case CourseEnemyEditMode::Add:
        return "Course Enemy: click a rail segment to add the configured enemy";
    case CourseEnemyEditMode::Duplicate:
        return "Course Enemy: click a placement to duplicate with authored local offset";
    case CourseEnemyEditMode::Delete:
        return "Course Enemy: click a placement to delete it";
    }
    return {};
}

bool CourseEnemyViewportEditTool::CanMutate() const {
    return controller_ != nullptr && controller_->State().bound &&
        controller_->State().authoringAllowed &&
        controller_->State().status == CourseEnemyEditorControllerStatus::Ready;
}

CourseEnemyPickResult CourseEnemyViewportEditTool::PickEnemy(
    const CourseEnemyViewportEditInput& input) const {
    const CourseEnemyAuthoringModel* model = previewModel_.has_value()
        ? &*previewModel_ : controller_->Model();
    if (model == nullptr) return {};
    return picking_->PickDisplay(
        *model, *input.coordinates, input.displayX, input.displayY);
}

CourseRailPickResult CourseEnemyViewportEditTool::PickRail(
    const CourseEnemyViewportEditInput& input) const {
    const CourseEnemyAuthoringModel* model = controller_->Model();
    if (model == nullptr || railPicking_ == nullptr) return {};
    CourseRailPickingSettings settings{};
    settings.preferControlPoints = false;
    settings.includeTangentHandles = false;
    return railPicking_->PickDisplay(
        model->RailModel(), *input.coordinates,
        input.displayX, input.displayY, settings);
}

bool CourseEnemyViewportEditTool::BeginDrag(
    const CourseEnemyPickResult& pick,
    const CourseEnemyViewportEditInput& input) {
    const CourseAsset* course = controller_->Course();
    const CourseEnemyAuthoringModel* model = controller_->Model();
    const CourseEnemyPlacement* placement = model != nullptr
        ? model->Find(pick.placementGuid) : nullptr;
    if (course == nullptr || placement == nullptr || placement->editorLocked ||
        input.coordinates == nullptr) {
        return false;
    }
    const EditorViewportWorldRay ray = input.coordinates->DisplayToWorldRay(
        input.displayX, input.displayY);
    if (!ray.valid) return false;
    dragOriginal_ = *placement;
    dragPlanePoint_ = pick.worldPosition;
    dragPlaneNormal_ = ray.direction;
    Vector3 intersection{};
    if (!IntersectDragPlane(
            *input.coordinates, input.displayX, input.displayY, intersection)) {
        return false;
    }
    dragOffset_ = Subtract(pick.worldPosition, intersection);
    dragExpectedRevision_ = controller_->State().mutationRevision;
    previewCourse_ = *course;
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    state_.dragging = state_.previewValid;
    state_.primaryPlacementGuid = pick.placementGuid;
    state_.message = state_.dragging
        ? "Enemy RailAnchor drag preview started."
        : previewModel_->ValidationError();
    return state_.dragging;
}

void CourseEnemyViewportEditTool::UpdateDrag(
    const CourseEnemyViewportEditInput& input) {
    if (input.coordinates == nullptr) return;
    Vector3 intersection{};
    if (!IntersectDragPlane(
            *input.coordinates, input.displayX, input.displayY, intersection)) {
        return;
    }
    CourseEnemyPlacement* placement = nullptr;
    for (CourseEnemyPlacement& candidate : previewCourse_.enemyPlacements) {
        if (candidate.editorGuid == state_.primaryPlacementGuid) {
            placement = &candidate;
            break;
        }
    }
    if (placement == nullptr || previewModel_ == std::nullopt) return;
    const Vector3 target = Add(intersection, dragOffset_);
    const RailAnchorProjection projected = previewModel_->RailModel().Project(
        target, settings_.railProjectionSubdivisions);
    if (!projected.valid) return;
    placement->railAnchor = SnapAnchor(projected.anchor);
    RefreshPreview();
}

void CourseEnemyViewportEditTool::CommitDrag() {
    if (!state_.dragging) return;
    const CourseEnemyPlacement* changed = nullptr;
    for (const CourseEnemyPlacement& candidate : previewCourse_.enemyPlacements) {
        if (candidate.editorGuid == state_.primaryPlacementGuid) {
            changed = &candidate;
            break;
        }
    }
    if (changed == nullptr || !state_.previewValid) {
        CancelDrag("Enemy drag preview is invalid and was not committed.");
        return;
    }
    const RailAnchor& before = dragOriginal_.railAnchor;
    const RailAnchor& after = changed->railAnchor;
    const bool hasChanges = before.segmentGuid != after.segmentGuid ||
        before.normalizedT != after.normalizedT ||
        before.lateralOffset != after.lateralOffset ||
        before.verticalOffset != after.verticalOffset ||
        before.forwardOffset != after.forwardOffset;
    if (!hasChanges) {
        CancelDrag("Enemy drag ended without changes.");
        return;
    }
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::SetAnchors;
    request.expectedRevision = dragExpectedRevision_;
    request.placements.push_back(*changed);
    request.label = "Move Enemy Placement";
    CancelDrag();
    const CourseEnemyMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) ++state_.editRevision;
}

void CourseEnemyViewportEditTool::AddAtRail(const CourseRailPickResult& pick) {
    AddAtRail(pick, settings_.defaultActorAssetId, "Add Enemy Placement");
}

CourseEnemyMutationResult CourseEnemyViewportEditTool::AddAtRail(
    const CourseRailPickResult& pick,
    std::string actorAssetId,
    std::string label) {
    CourseEnemyMutationResult unavailable{};
    unavailable.revision = controller_ != nullptr
        ? controller_->State().mutationRevision : 0;
    if (!CanMutate() || pick.kind != CourseRailPickKind::Segment) {
        unavailable.message = "Enemy placement requires a writable course and rail segment.";
        state_.message = unavailable.message;
        return unavailable;
    }
    CourseEnemyPlacement placement{};
    placement.actorAssetId = std::move(actorAssetId);
    placement.bulletPatternOverrideId = settings_.defaultBulletPatternId;
    placement.waveGroupGuid = settings_.defaultWaveGroupGuid;
    placement.railAnchor.segmentGuid = pick.guid;
    placement.railAnchor.normalizedT = (std::clamp)(pick.normalizedT, 0.0f, 1.0f);
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::AddPlacements;
    request.expectedRevision = controller_->State().mutationRevision;
    request.placements.push_back(std::move(placement));
    request.label = std::move(label);
    const CourseEnemyMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (!result.succeeded || result.affectedPlacementGuids.empty()) return result;
    ++state_.editRevision;
    RequestSelection(result.affectedPlacementGuids.front());
    return result;
}

void CourseEnemyViewportEditTool::DuplicateSelected(std::string_view fallbackGuid) {
    if (!CanMutate()) return;
    std::vector<std::string> targets = selectedGuids_;
    if (targets.empty() && !fallbackGuid.empty()) targets.emplace_back(fallbackGuid);
    if (targets.empty()) {
        state_.message = "Select an enemy placement to duplicate.";
        return;
    }
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::DuplicatePlacements;
    request.expectedRevision = controller_->State().mutationRevision;
    request.placementGuids = std::move(targets);
    request.duplicateOffset = settings_.duplicateOffset;
    request.label = "Duplicate Enemy Placements";
    const CourseEnemyMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (!result.succeeded || result.affectedPlacementGuids.empty()) return;
    ++state_.editRevision;
    RequestSelection(result.affectedPlacementGuids.front());
}

void CourseEnemyViewportEditTool::RemoveSelected(std::string_view fallbackGuid) {
    if (!CanMutate()) return;
    std::vector<std::string> targets = selectedGuids_;
    if (targets.empty() && !fallbackGuid.empty()) targets.emplace_back(fallbackGuid);
    if (targets.empty()) {
        state_.message = "Select an enemy placement to delete.";
        return;
    }
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::RemovePlacements;
    request.expectedRevision = controller_->State().mutationRevision;
    request.placementGuids = std::move(targets);
    request.label = "Delete Enemy Placements";
    const CourseEnemyMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (!result.succeeded) return;
    ++state_.editRevision;
    selectedGuids_.clear();
    state_.primaryPlacementGuid.clear();
    state_.selectedPlacementCount = 0;
    clearSelectionRequested_ = true;
}

void CourseEnemyViewportEditTool::ApplyUndoRedo(bool redo) {
    if (!CanMutate() || state_.dragging) return;
    std::string error;
    const bool succeeded = redo ? controller_->Redo(&error) : controller_->Undo(&error);
    state_.message = succeeded
        ? (redo ? "Course enemy edit redone." : "Course enemy edit undone.")
        : (error.empty() ? "No Course enemy transaction is available." : error);
    if (!succeeded) return;
    ++state_.editRevision;
    const CourseEnemyAuthoringModel* model = controller_->Model();
    selectedGuids_.erase(
        std::remove_if(selectedGuids_.begin(), selectedGuids_.end(),
            [model](const std::string& guid) {
                return model == nullptr || model->Find(guid) == nullptr;
            }),
        selectedGuids_.end());
    if (selectedGuids_.empty()) clearSelectionRequested_ = true;
}

void CourseEnemyViewportEditTool::RequestSelection(std::string_view guid) {
    const CourseEnemyAuthoringModel* model = controller_->Model();
    const std::optional<std::size_t> index = model != nullptr
        ? model->FindIndex(guid) : std::nullopt;
    const CourseEnemyPlacementResolution resolved = model != nullptr
        ? model->Resolve(guid) : CourseEnemyPlacementResolution{};
    const CourseEnemyPlacement* placement = model != nullptr
        ? model->Find(guid) : nullptr;
    if (!index.has_value() || !resolved.valid || placement == nullptr) return;
    CourseEnemyPickResult pick{};
    pick.hit = true;
    pick.placementGuid = std::string(guid);
    pick.actorAssetId = placement->actorAssetId;
    pick.placementIndex = static_cast<uint32_t>(*index);
    pick.worldPosition = resolved.worldPosition;
    pick.enabled = placement->enabled;
    pick.locked = placement->editorLocked;
    selectionRequest_ = std::move(pick);
    state_.primaryPlacementGuid = std::string(guid);
}

void CourseEnemyViewportEditTool::RefreshPreview() {
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    if (!state_.previewValid) state_.message = previewModel_->ValidationError();
}

bool CourseEnemyViewportEditTool::IntersectDragPlane(
    const EditorViewportCoordinateService& coordinates,
    float displayX,
    float displayY,
    Vector3& worldPosition) const {
    const EditorViewportWorldRay ray = coordinates.DisplayToWorldRay(displayX, displayY);
    if (!ray.valid) return false;
    const float denominator = Dot(ray.direction, dragPlaneNormal_);
    if (std::fabs(denominator) <= 0.00001f) return false;
    const float distance = Dot(Subtract(dragPlanePoint_, ray.origin), dragPlaneNormal_) /
        denominator;
    if (!std::isfinite(distance) || distance < 0.0f) return false;
    worldPosition = Add(ray.origin, Scale(ray.direction, distance));
    return true;
}

RailAnchor CourseEnemyViewportEditTool::SnapAnchor(RailAnchor anchor) const {
    if (!settings_.offsetSnap) return anchor;
    const auto snap = [this](float value) {
        return std::round(value / settings_.offsetSnapSize) * settings_.offsetSnapSize;
    };
    anchor.lateralOffset = snap(anchor.lateralOffset);
    anchor.verticalOffset = snap(anchor.verticalOffset);
    anchor.forwardOffset = snap(anchor.forwardOffset);
    return anchor;
}

const char* ToString(CourseEnemyEditMode mode) {
    switch (mode) {
    case CourseEnemyEditMode::SelectMove: return "Select / Move";
    case CourseEnemyEditMode::Add: return "Add";
    case CourseEnemyEditMode::Duplicate: return "Duplicate";
    case CourseEnemyEditMode::Delete: return "Delete";
    }
    return "Unknown";
}

} // namespace editor
