#include "CourseRailViewportEditTool.h"
#include "../EditorSettingsValidation.h"

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

float DistanceSquared(const Vector3& a, const Vector3& b) {
    const Vector3 delta = Subtract(a, b);
    return Dot(delta, delta);
}

} // namespace

void CourseRailViewportEditTool::Bind(
    CourseRailEditorController* controller,
    const CourseRailPickingService* picking) {
    const uint32_t generation = controller != nullptr
        ? controller->State().bindingGeneration : 0;
    if (controller_ == controller && picking_ == picking &&
        bindingGeneration_ == generation) return;
    CancelDrag();
    controller_ = controller;
    picking_ = picking;
    bindingGeneration_ = generation;
    if (controller_ == nullptr || !controller_->State().bound) {
        state_.active = false;
        state_.selectedPointGuid.clear();
        state_.message = "Course rail controller is unavailable.";
    } else if (controller_->Model() == nullptr ||
        controller_->Model()->FindPoint(state_.selectedPointGuid) == nullptr) {
        state_.selectedPointGuid.clear();
    }
}

void CourseRailViewportEditTool::SetActive(bool active) {
    if (state_.active == active) return;
    CancelDrag();
    state_.active = active && controller_ != nullptr && controller_->State().bound;
    state_.message = state_.active
        ? "Course Rail viewport editing enabled."
        : "Course Rail viewport editing disabled.";
}

void CourseRailViewportEditTool::SetMode(CourseRailEditMode mode) {
    if (state_.mode == mode) return;
    CancelDrag();
    state_.mode = mode;
    state_.message = std::string("Rail tool mode: ") + ToString(mode);
}

void CourseRailViewportEditTool::SetSelectedPoint(std::string guid) {
    if (state_.dragging) return;
    state_.selectedPointGuid = std::move(guid);
}

bool CourseRailViewportEditSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(gridSize, .01f, 1000.0f);
    return settings::Result(valid, errorMessage,
        "CourseRailViewportEditSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseRailViewportEditTool::SetSettings(
    CourseRailViewportEditSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

void CourseRailViewportEditTool::Tick(const CourseRailViewportEditInput& input) {
    state_.canMutate = CanMutate();
    if (!state_.active || controller_ == nullptr || picking_ == nullptr ||
        input.coordinates == nullptr) {
        state_.hovered = {};
        if (state_.dragging) CancelDrag("Rail drag cancelled because the viewport became unavailable.");
        return;
    }

    if (input.primaryCancelled || input.cancelPressed) {
        CancelDrag("Rail drag cancelled.");
    }
    if (input.undoPressed) ApplyUndoRedo(false);
    if (input.redoPressed) ApplyUndoRedo(true);

    state_.hovered = Pick(input);
    if (input.deletePressed && !state_.selectedPointGuid.empty() && !state_.dragging) {
        RemovePoint(state_.selectedPointGuid);
        return;
    }

    if (state_.dragging) {
        if (input.primaryDown) UpdateDrag(input);
        if (input.primaryReleased) CommitDrag();
        return;
    }
    if (!input.primaryPressed) return;

    if (state_.mode == CourseRailEditMode::Add) {
        if (state_.hovered.kind == CourseRailPickKind::Segment) AddAtSegment(state_.hovered);
        else state_.message = "Add mode requires clicking a visible rail segment.";
        return;
    }
    if (state_.mode == CourseRailEditMode::Delete) {
        if (state_.hovered.kind == CourseRailPickKind::ControlPoint) {
            RemovePoint(state_.hovered.guid);
        } else {
            state_.message = "Delete mode requires clicking a control point.";
        }
        return;
    }

    if (state_.hovered.hit) RequestSelection(state_.hovered);
    if (input.toggleSelection) return;
    if (!state_.canMutate) return;
    if (state_.mode == CourseRailEditMode::SelectMove &&
        state_.hovered.kind == CourseRailPickKind::ControlPoint) {
        BeginDrag(state_.hovered, input);
    } else if (state_.mode == CourseRailEditMode::Tangent &&
        state_.hovered.IsTangentHandle()) {
        BeginDrag(state_.hovered, input);
    }
}

void CourseRailViewportEditTool::CancelDrag(std::string message) {
    dragKind_ = DragKind::None;
    state_.dragging = false;
    state_.previewValid = false;
    previewModel_.reset();
    previewCourse_ = {};
    if (!message.empty()) state_.message = std::move(message);
}

std::optional<CourseRailPickResult>
CourseRailViewportEditTool::ConsumeSelectionRequest() {
    std::optional<CourseRailPickResult> result = std::move(selectionRequest_);
    selectionRequest_.reset();
    return result;
}

bool CourseRailViewportEditTool::ConsumeClearSelectionRequest() {
    const bool result = clearSelectionRequested_;
    clearSelectionRequested_ = false;
    return result;
}

std::string CourseRailViewportEditTool::ViewportHint() const {
    if (!state_.active) return {};
    if (!state_.canMutate) return "Course Rail: authoring is locked during Play/Sim";
    if (state_.dragging) return "Course Rail: drag preview - release commits one Undo transaction; Esc cancels";
    switch (state_.mode) {
    case CourseRailEditMode::SelectMove:
        return "Course Rail: click/drag a point to move; Delete removes; Ctrl+Z/Y undo/redo";
    case CourseRailEditMode::Add:
        return "Course Rail: click a segment to insert a shape-preserving control point";
    case CourseRailEditMode::Tangent:
        return "Course Rail: select a point, then drag its tangent handles";
    case CourseRailEditMode::Delete:
        return "Course Rail: click a control point to delete it";
    }
    return {};
}

bool CourseRailViewportEditTool::CanMutate() const {
    return controller_ != nullptr && controller_->State().bound &&
        controller_->State().authoringAllowed &&
        controller_->State().status == CourseRailEditorControllerStatus::Ready;
}

CourseRailPickResult CourseRailViewportEditTool::Pick(
    const CourseRailViewportEditInput& input) const {
    const CourseRailAuthoringModel* model = previewModel_.has_value()
        ? &*previewModel_ : controller_->Model();
    if (model == nullptr) return {};
    CourseRailPickingSettings settings{};
    settings.tangentPointGuid = state_.selectedPointGuid;
    settings.includeTangentHandles = state_.mode == CourseRailEditMode::Tangent;
    return picking_->PickDisplay(
        *model, *input.coordinates, input.displayX, input.displayY, settings);
}

bool CourseRailViewportEditTool::BeginDrag(
    const CourseRailPickResult& pick,
    const CourseRailViewportEditInput& input) {
    const CourseAsset* course = controller_->Course();
    const CourseRailAuthoringModel* model = controller_->Model();
    if (course == nullptr || model == nullptr || input.coordinates == nullptr) return false;
    const std::optional<uint32_t> pointIndex = model->FindPointIndex(pick.guid);
    if (!pointIndex.has_value()) return false;

    dragPoint_ = course->railPoints[*pointIndex];
    dragKind_ = pick.kind == CourseRailPickKind::ControlPoint ? DragKind::Point
        : pick.kind == CourseRailPickKind::IncomingTangent ? DragKind::IncomingTangent
        : DragKind::OutgoingTangent;
    const EditorViewportWorldRay ray = input.coordinates->DisplayToWorldRay(
        input.displayX, input.displayY);
    if (!ray.valid) return false;
    dragPlanePoint_ = pick.worldPosition;
    dragPlaneNormal_ = ray.direction;
    Vector3 intersection{};
    if (!IntersectDragPlane(*input.coordinates, input.displayX, input.displayY, intersection)) {
        return false;
    }
    dragOffset_ = Subtract(pick.worldPosition, intersection);
    dragExpectedRevision_ = controller_->State().mutationRevision;
    previewCourse_ = *course;
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    state_.dragging = true;
    state_.selectedPointGuid = pick.guid;
    state_.message = "Rail drag preview started.";
    return true;
}

void CourseRailViewportEditTool::UpdateDrag(const CourseRailViewportEditInput& input) {
    if (input.coordinates == nullptr) return;
    Vector3 intersection{};
    if (!IntersectDragPlane(*input.coordinates, input.displayX, input.displayY, intersection)) return;
    const Vector3 target = Snap(Add(intersection, dragOffset_));
    const auto it = std::find_if(
        previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
        [this](const RailPathControlPoint& point) {
            return point.editorGuid == state_.selectedPointGuid;
        });
    if (it == previewCourse_.railPoints.end()) return;

    if (dragKind_ == DragKind::Point) {
        it->position = target;
    } else {
        const Vector3 tangent = Subtract(target, it->position);
        if (DistanceSquared(target, it->position) < 0.0001f) return;
        if (dragKind_ == DragKind::IncomingTangent) {
            it->incomingTangent = tangent;
            if (settings_.mirrorTangents) it->outgoingTangent = Scale(tangent, -1.0f);
        } else {
            it->outgoingTangent = tangent;
            if (settings_.mirrorTangents) it->incomingTangent = Scale(tangent, -1.0f);
        }
        it->tangentMode = settings_.mirrorTangents
            ? RailPathTangentMode::Mirrored : RailPathTangentMode::Broken;
    }
    RefreshPreview();
}

void CourseRailViewportEditTool::CommitDrag() {
    if (!state_.dragging) return;
    const auto it = std::find_if(
        previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
        [this](const RailPathControlPoint& point) {
            return point.editorGuid == state_.selectedPointGuid;
        });
    if (it == previewCourse_.railPoints.end() || !state_.previewValid) {
        CancelDrag("Rail drag preview is invalid and was not committed.");
        return;
    }
    const bool changed = dragKind_ == DragKind::Point
        ? DistanceSquared(it->position, dragPoint_.position) > 0.000001f
        : it->tangentMode != dragPoint_.tangentMode ||
            DistanceSquared(it->incomingTangent, dragPoint_.incomingTangent) > 0.000001f ||
            DistanceSquared(it->outgoingTangent, dragPoint_.outgoingTangent) > 0.000001f;
    if (!changed) {
        CancelDrag("Rail drag ended without changes.");
        return;
    }

    CourseRailMutationRequest request{};
    request.kind = dragKind_ == DragKind::Point
        ? CourseRailMutationKind::MovePoint : CourseRailMutationKind::SetPoint;
    request.expectedRevision = dragExpectedRevision_;
    request.pointGuid = state_.selectedPointGuid;
    request.point = *it;
    request.label = dragKind_ == DragKind::Point
        ? "Move Rail Control Point" : "Edit Rail Tangent";
    CancelDrag();
    const CourseRailMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) ++state_.editRevision;
}

void CourseRailViewportEditTool::AddAtSegment(const CourseRailPickResult& pick) {
    if (!CanMutate() || pick.kind != CourseRailPickKind::Segment) return;
    const CourseRailAuthoringModel* model = controller_->Model();
    if (model == nullptr || pick.segmentIndex >= model->Segments().size()) return;
    const CourseRailSegment& segment = model->Segments()[pick.segmentIndex];
    const RailPathSample sample = model->RuntimePath().EvaluateSegmentAt(
        segment.pointIndex, pick.normalizedT);
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::InsertPoint;
    request.expectedRevision = controller_->State().mutationRevision;
    request.segmentGuid = segment.guid;
    request.normalizedT = (std::clamp)(pick.normalizedT, 0.001f, 0.999f);
    request.point.position = sample.position;
    request.point.corridorRadius = sample.corridorRadius;
    request.point.speed = sample.speed;
    request.label = "Insert Rail Control Point";
    const CourseRailMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (!result.succeeded) return;
    ++state_.editRevision;
    state_.selectedPointGuid = result.affectedPointGuid;
    if (const CourseRailAuthoringModel* updated = controller_->Model()) {
        if (const std::optional<uint32_t> index = updated->FindPointIndex(result.affectedPointGuid)) {
            CourseRailPickResult selection{};
            selection.hit = true;
            selection.kind = CourseRailPickKind::ControlPoint;
            selection.guid = result.affectedPointGuid;
            selection.pointIndex = *index;
            selection.worldPosition = updated->RuntimePath().ControlPoints()[*index].position;
            RequestSelection(selection);
        }
    }
}

void CourseRailViewportEditTool::RemovePoint(std::string_view guid) {
    if (!CanMutate() || guid.empty()) return;
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::RemovePoint;
    request.expectedRevision = controller_->State().mutationRevision;
    request.pointGuid = std::string(guid);
    request.label = "Remove Rail Control Point";
    const CourseRailMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (!result.succeeded) return;
    ++state_.editRevision;
    state_.selectedPointGuid.clear();
    clearSelectionRequested_ = true;
}

void CourseRailViewportEditTool::ApplyUndoRedo(bool redo) {
    if (!CanMutate() || state_.dragging) return;
    std::string error;
    const bool succeeded = redo ? controller_->Redo(&error) : controller_->Undo(&error);
    state_.message = succeeded
        ? (redo ? "Course rail edit redone." : "Course rail edit undone.")
        : (error.empty() ? "No Course rail transaction is available." : error);
    if (!succeeded) return;
    ++state_.editRevision;
    const CourseRailAuthoringModel* model = controller_->Model();
    if (model == nullptr || model->FindPoint(state_.selectedPointGuid) == nullptr) {
        state_.selectedPointGuid.clear();
        clearSelectionRequested_ = true;
    }
}

void CourseRailViewportEditTool::RequestSelection(const CourseRailPickResult& pick) {
    selectionRequest_ = pick;
    if (pick.kind == CourseRailPickKind::ControlPoint || pick.IsTangentHandle()) {
        state_.selectedPointGuid = pick.guid;
    }
}

void CourseRailViewportEditTool::RefreshPreview() {
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    if (!state_.previewValid) state_.message = previewModel_->ValidationError();
}

bool CourseRailViewportEditTool::IntersectDragPlane(
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

Vector3 CourseRailViewportEditTool::Snap(Vector3 value) const {
    if (!settings_.gridSnap || settings_.gridSize <= 0.0f) return value;
    const auto snap = [this](float component) {
        return std::round(component / settings_.gridSize) * settings_.gridSize;
    };
    return {snap(value.x), snap(value.y), snap(value.z)};
}

const char* ToString(CourseRailEditMode mode) {
    switch (mode) {
    case CourseRailEditMode::SelectMove: return "Select / Move";
    case CourseRailEditMode::Add: return "Add";
    case CourseRailEditMode::Tangent: return "Tangent";
    case CourseRailEditMode::Delete: return "Delete";
    }
    return "Unknown";
}

} // namespace editor
