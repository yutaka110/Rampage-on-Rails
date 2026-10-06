#include "CourseRailTransformGizmo.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace editor {
namespace {

constexpr std::string_view kPointPrefix = "course-rail-point:";

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

Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

float LengthSquared(const Vector3& value) { return Dot(value, value); }

Vector3 NormalizeOr(const Vector3& value, const Vector3& fallback) {
    const float lengthSquared = LengthSquared(value);
    return lengthSquared > 0.000001f
        ? Scale(value, 1.0f / std::sqrt(lengthSquared)) : fallback;
}

float ScreenSegmentDistanceSquared(
    float x, float y,
    const EditorViewportProjectedPoint& a,
    const EditorViewportProjectedPoint& b) {
    const float dx = b.display.x - a.display.x;
    const float dy = b.display.y - a.display.y;
    const float denominator = dx * dx + dy * dy;
    const float t = denominator > 0.000001f
        ? (std::clamp)(((x - a.display.x) * dx + (y - a.display.y) * dy) /
            denominator, 0.0f, 1.0f)
        : 0.0f;
    const float nearestX = a.display.x + dx * t;
    const float nearestY = a.display.y + dy * t;
    const float offsetX = x - nearestX;
    const float offsetY = y - nearestY;
    return offsetX * offsetX + offsetY * offsetY;
}

uint32_t HandleColor(CourseRailGizmoHandle handle, bool highlighted) {
    uint32_t color = 0xfff0f0f0u;
    if (handle == CourseRailGizmoHandle::X || handle == CourseRailGizmoHandle::YZ) {
        color = 0xff5b5bffu;
    } else if (handle == CourseRailGizmoHandle::Y || handle == CourseRailGizmoHandle::ZX) {
        color = 0xff62dc62u;
    } else if (handle == CourseRailGizmoHandle::Z || handle == CourseRailGizmoHandle::XY) {
        color = 0xffffa34du;
    }
    return highlighted ? 0xffffffffu : color;
}

} // namespace

void CourseRailTransformGizmo::Bind(CourseRailEditorController* controller) {
    const uint32_t generation = controller != nullptr
        ? controller->State().bindingGeneration : 0;
    if (controller_ == controller && bindingGeneration_ == generation) return;
    Cancel();
    controller_ = controller;
    bindingGeneration_ = generation;
    selectedGuids_.clear();
}

bool CourseRailTransformGizmoSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(gridSize, .01f, 1000.0f) &&
        settings::InRange(handleLengthScale, .1f, 4.0f) &&
        space >= EditorTransformGizmoSpace::World && space <= EditorTransformGizmoSpace::Local;
    return settings::Result(valid, errorMessage,
        "CourseRailTransformGizmoSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseRailTransformGizmo::SetSettings(
    CourseRailTransformGizmoSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

void CourseRailTransformGizmo::Tick(const CourseRailTransformGizmoInput& input) {
    state_.canMutate = input.canMutate && controller_ != nullptr &&
        controller_->State().authoringAllowed;
    coordinates_ = input.coordinates;
    if (input.primaryCancelled || input.cancelPressed) Cancel("Rail gizmo drag cancelled.");
    if (!RefreshFrame(input)) return;
    if (!state_.dragging) state_.hovered = PickHandle(input.displayX, input.displayY);
    if (!state_.dragging && input.primaryPressed &&
        state_.hovered != CourseRailGizmoHandle::None && state_.canMutate) {
        BeginDrag(state_.hovered, input.displayX, input.displayY);
    }
    if (!state_.dragging) return;
    if (input.primaryDown) UpdateDrag(input.displayX, input.displayY);
    if (input.primaryReleased) CommitDrag();
}

void CourseRailTransformGizmo::BuildViewportOverlay(
    EditorViewportOverlayService& overlay) const {
    if (!state_.visible || coordinates_ == nullptr) return;
    auto sink = overlay.Sink(EditorViewportOverlayLayerId::AuthoringHelpers);
    const auto submitLine = [&](const Vector3& a, const Vector3& b, uint32_t color, float width) {
        const EditorViewportProjectedPoint pa = coordinates_->ProjectWorld(a);
        const EditorViewportProjectedPoint pb = coordinates_->ProjectWorld(b);
        if (pa.valid && pb.valid && pa.inDepth && pb.inDepth) {
            sink.Line(pa.render.x, pa.render.y, pb.render.x, pb.render.y, color, width,
                EditorViewportOverlayItemOptions{true});
        }
    };
    const auto drawAxis = [&](CourseRailGizmoHandle handle, const Vector3& axis, const char* label) {
        const Vector3 end = Add(state_.pivot, Scale(axis, handleLength_));
        const bool highlighted = state_.active == handle || state_.hovered == handle;
        const uint32_t color = HandleColor(handle, highlighted);
        submitLine(state_.pivot, end, color, highlighted ? 4.0f : 2.5f);
        const EditorViewportProjectedPoint projected = coordinates_->ProjectWorld(end);
        if (projected.valid && projected.inDepth) {
            sink.CircleFilled(projected.render.x, projected.render.y,
                highlighted ? 6.0f : 4.5f, color, EditorViewportOverlayItemOptions{true});
            sink.Label(projected.render.x + 6.0f, projected.render.y - 8.0f,
                label, color, EditorViewportOverlayItemOptions{true});
        }
    };
    drawAxis(CourseRailGizmoHandle::X, basis_.x, "X");
    drawAxis(CourseRailGizmoHandle::Y, basis_.y, "Y");
    drawAxis(CourseRailGizmoHandle::Z, basis_.z, "Z");

    const auto drawPlane = [&](CourseRailGizmoHandle handle, const Vector3& a, const Vector3& b) {
        const float inner = handleLength_ * 0.16f;
        const float outer = handleLength_ * 0.34f;
        const Vector3 p0 = Add(state_.pivot, Add(Scale(a, inner), Scale(b, inner)));
        const Vector3 p1 = Add(state_.pivot, Add(Scale(a, outer), Scale(b, inner)));
        const Vector3 p2 = Add(state_.pivot, Add(Scale(a, outer), Scale(b, outer)));
        const Vector3 p3 = Add(state_.pivot, Add(Scale(a, inner), Scale(b, outer)));
        const bool highlighted = state_.active == handle || state_.hovered == handle;
        const uint32_t color = HandleColor(handle, highlighted);
        submitLine(p0, p1, color, highlighted ? 3.0f : 1.5f);
        submitLine(p1, p2, color, highlighted ? 3.0f : 1.5f);
        submitLine(p2, p3, color, highlighted ? 3.0f : 1.5f);
        submitLine(p3, p0, color, highlighted ? 3.0f : 1.5f);
    };
    drawPlane(CourseRailGizmoHandle::XY, basis_.x, basis_.y);
    drawPlane(CourseRailGizmoHandle::YZ, basis_.y, basis_.z);
    drawPlane(CourseRailGizmoHandle::ZX, basis_.z, basis_.x);
}

void CourseRailTransformGizmo::Cancel(std::string message) {
    state_.dragging = false;
    state_.previewValid = false;
    state_.active = CourseRailGizmoHandle::None;
    previewModel_.reset();
    previewCourse_ = {};
    originalPositions_.clear();
    if (!message.empty()) state_.message = std::move(message);
}

std::vector<std::string> CourseRailTransformGizmo::SelectedPointGuids(
    const EditorSelection* selection) const {
    std::vector<std::string> result;
    if (selection == nullptr) return result;
    for (const EditorObjectHandle& handle : selection->Handles()) {
        if (handle.domain == EditorDomainId::CourseRailControlPoint &&
            handle.stableId.starts_with(kPointPrefix)) {
            result.push_back(handle.stableId.substr(kPointPrefix.size()));
        }
    }
    return result;
}

bool CourseRailTransformGizmo::RefreshFrame(
    const CourseRailTransformGizmoInput& input) {
    if (controller_ == nullptr || !controller_->State().bound ||
        input.coordinates == nullptr || !input.enabled) {
        if (state_.dragging) Cancel("Rail gizmo became unavailable.");
        state_.visible = false;
        state_.hovered = CourseRailGizmoHandle::None;
        state_.selectedPointCount = 0;
        return false;
    }
    const CourseRailAuthoringModel* model = previewModel_.has_value()
        ? &*previewModel_ : controller_->Model();
    if (model == nullptr) return false;
    if (!state_.dragging) selectedGuids_ = SelectedPointGuids(input.selection);
    selectedGuids_.erase(
        std::remove_if(selectedGuids_.begin(), selectedGuids_.end(),
            [model](const std::string& guid) { return model->FindPoint(guid) == nullptr; }),
        selectedGuids_.end());
    state_.selectedPointCount = static_cast<uint32_t>(selectedGuids_.size());
    state_.visible = !selectedGuids_.empty();
    if (!state_.visible) return false;

    state_.pivot = {};
    for (const std::string& guid : selectedGuids_) {
        state_.pivot = Add(state_.pivot, model->FindPoint(guid)->position);
    }
    state_.pivot = Scale(state_.pivot, 1.0f / static_cast<float>(selectedGuids_.size()));
    const std::optional<uint32_t> primaryIndex = model->FindPointIndex(selectedGuids_.front());
    basis_ = primaryIndex.has_value() ? ResolveBasis(*model, *primaryIndex) : Basis{};
    const RailPathControlPoint* primary = model->FindPoint(selectedGuids_.front());
    handleLength_ = primary != nullptr
        ? (std::clamp)(primary->corridorRadius * settings_.handleLengthScale, 0.1f, 30.0f)
        : 5.0f;
    return true;
}

CourseRailGizmoHandle CourseRailTransformGizmo::PickHandle(
    float displayX, float displayY) const {
    if (!state_.visible || coordinates_ == nullptr) return CourseRailGizmoHandle::None;
    const EditorViewportProjectedPoint pivot = coordinates_->ProjectWorld(state_.pivot);
    if (!pivot.valid || !pivot.inDepth) return CourseRailGizmoHandle::None;
    constexpr float axisRadiusSquared = 8.0f * 8.0f;
    float bestDistance = (std::numeric_limits<float>::max)();
    CourseRailGizmoHandle best = CourseRailGizmoHandle::None;
    for (const auto& candidate : {
            std::pair{CourseRailGizmoHandle::X, basis_.x},
            std::pair{CourseRailGizmoHandle::Y, basis_.y},
            std::pair{CourseRailGizmoHandle::Z, basis_.z}}) {
        const Vector3 startWorld = Add(state_.pivot, Scale(candidate.second, handleLength_ * 0.38f));
        const Vector3 endWorld = Add(state_.pivot, Scale(candidate.second, handleLength_));
        const auto start = coordinates_->ProjectWorld(startWorld);
        const auto end = coordinates_->ProjectWorld(endWorld);
        if (!start.valid || !end.valid || !start.inDepth || !end.inDepth) continue;
        const float distance = ScreenSegmentDistanceSquared(displayX, displayY, start, end);
        if (distance <= axisRadiusSquared && distance < bestDistance) {
            bestDistance = distance;
            best = candidate.first;
        }
    }
    if (best != CourseRailGizmoHandle::None) return best;
    constexpr float planeRadiusSquared = 13.0f * 13.0f;
    for (const auto& candidate : {
            std::tuple{CourseRailGizmoHandle::XY, basis_.x, basis_.y},
            std::tuple{CourseRailGizmoHandle::YZ, basis_.y, basis_.z},
            std::tuple{CourseRailGizmoHandle::ZX, basis_.z, basis_.x}}) {
        const Vector3 center = Add(state_.pivot,
            Add(Scale(std::get<1>(candidate), handleLength_ * 0.25f),
                Scale(std::get<2>(candidate), handleLength_ * 0.25f)));
        const auto projected = coordinates_->ProjectWorld(center);
        if (!projected.valid || !projected.inDepth) continue;
        const float dx = displayX - projected.display.x;
        const float dy = displayY - projected.display.y;
        const float distance = dx * dx + dy * dy;
        if (distance <= planeRadiusSquared && distance < bestDistance) {
            bestDistance = distance;
            best = std::get<0>(candidate);
        }
    }
    return best;
}

bool CourseRailTransformGizmo::BeginDrag(
    CourseRailGizmoHandle handle, float displayX, float displayY) {
    const CourseAsset* course = controller_->Course();
    if (course == nullptr || selectedGuids_.empty()) return false;
    previewCourse_ = *course;
    originalPositions_.clear();
    originalPositions_.reserve(selectedGuids_.size());
    for (const std::string& guid : selectedGuids_) {
        const auto found = std::find_if(previewCourse_.railPoints.begin(),
            previewCourse_.railPoints.end(), [&guid](const RailPathControlPoint& point) {
                return point.editorGuid == guid;
            });
        if (found == previewCourse_.railPoints.end()) return false;
        originalPositions_.push_back(found->position);
    }
    state_.active = handle;
    state_.dragging = true;
    dragOriginPivot_ = state_.pivot;
    dragExpectedRevision_ = controller_->State().mutationRevision;
    Vector3 planeA{};
    Vector3 planeB{};
    if (PlaneAxes(handle, planeA, planeB)) {
        if (!RayPlane(displayX, displayY, dragOriginPivot_,
                NormalizeOr(Cross(planeA, planeB), basis_.y), dragStartWorld_)) {
            Cancel("Rail gizmo plane could not be resolved.");
            return false;
        }
    } else if (!RayAxisParameter(displayX, displayY, dragOriginPivot_,
            Axis(handle), dragStartAxisParameter_)) {
        Cancel("Rail gizmo axis could not be resolved.");
        return false;
    }
    RefreshPreview();
    state_.message = std::string("Rail gizmo drag: ") + ToString(handle);
    return true;
}

void CourseRailTransformGizmo::UpdateDrag(float displayX, float displayY) {
    Vector3 delta{};
    Vector3 planeA{};
    Vector3 planeB{};
    if (PlaneAxes(state_.active, planeA, planeB)) {
        Vector3 current{};
        const Vector3 normal = NormalizeOr(Cross(planeA, planeB), basis_.y);
        if (!RayPlane(displayX, displayY, dragOriginPivot_, normal, current)) return;
        const Vector3 raw = Subtract(current, dragStartWorld_);
        delta = Add(Scale(planeA, Dot(raw, planeA)), Scale(planeB, Dot(raw, planeB)));
    } else {
        float parameter = 0.0f;
        const Vector3 axis = Axis(state_.active);
        if (!RayAxisParameter(displayX, displayY, dragOriginPivot_, axis, parameter)) return;
        delta = Scale(axis, parameter - dragStartAxisParameter_);
    }
    delta = SnapDelta(delta, state_.active);
    for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
        const auto found = std::find_if(previewCourse_.railPoints.begin(),
            previewCourse_.railPoints.end(), [&](const RailPathControlPoint& point) {
                return point.editorGuid == selectedGuids_[index];
            });
        if (found != previewCourse_.railPoints.end()) {
            found->position = Add(originalPositions_[index], delta);
        }
    }
    RefreshPreview();
}

void CourseRailTransformGizmo::CommitDrag() {
    if (!state_.dragging || !state_.previewValid) {
        Cancel("Rail gizmo preview was invalid.");
        return;
    }
    bool changed = false;
    for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
        const auto found = std::find_if(previewCourse_.railPoints.begin(),
            previewCourse_.railPoints.end(), [&](const RailPathControlPoint& point) {
                return point.editorGuid == selectedGuids_[index];
            });
        changed = changed || (found != previewCourse_.railPoints.end() &&
            LengthSquared(Subtract(found->position, originalPositions_[index])) > 0.000001f);
    }
    if (!changed) {
        Cancel("Rail gizmo ended without changes.");
        return;
    }
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::ReplaceRail;
    request.expectedRevision = dragExpectedRevision_;
    request.replacementPoints = previewCourse_.railPoints;
    request.label = selectedGuids_.size() > 1
        ? "Move Rail Control Points" : "Move Rail Control Point";
    Cancel();
    const CourseRailMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) ++state_.editRevision;
}

CourseRailTransformGizmo::Basis CourseRailTransformGizmo::ResolveBasis(
    const CourseRailAuthoringModel& model, uint32_t pointIndex) const {
    if (settings_.space == EditorTransformGizmoSpace::World) return {};
    const RailPath& path = model.RuntimePath();
    const auto& points = path.ControlPoints();
    if (pointIndex >= points.size()) return {};
    const Vector3 previous = points[pointIndex > 0 ? pointIndex - 1 : pointIndex].position;
    const Vector3 next = points[(std::min)(pointIndex + 1,
        static_cast<uint32_t>(points.size() - 1))].position;
    Basis result{};
    result.z = NormalizeOr(Subtract(next, previous), {0.0f, 0.0f, 1.0f});
    result.x = NormalizeOr(Cross({0.0f, 1.0f, 0.0f}, result.z), {1.0f, 0.0f, 0.0f});
    result.y = NormalizeOr(Cross(result.z, result.x), {0.0f, 1.0f, 0.0f});
    return result;
}

Vector3 CourseRailTransformGizmo::Axis(CourseRailGizmoHandle handle) const {
    if (handle == CourseRailGizmoHandle::X) return basis_.x;
    if (handle == CourseRailGizmoHandle::Y) return basis_.y;
    return basis_.z;
}

bool CourseRailTransformGizmo::PlaneAxes(
    CourseRailGizmoHandle handle, Vector3& a, Vector3& b) const {
    if (handle == CourseRailGizmoHandle::XY) { a = basis_.x; b = basis_.y; return true; }
    if (handle == CourseRailGizmoHandle::YZ) { a = basis_.y; b = basis_.z; return true; }
    if (handle == CourseRailGizmoHandle::ZX) { a = basis_.z; b = basis_.x; return true; }
    return false;
}

bool CourseRailTransformGizmo::RayPlane(
    float displayX, float displayY,
    const Vector3& planePoint, const Vector3& planeNormal,
    Vector3& intersection) const {
    if (coordinates_ == nullptr) return false;
    const EditorViewportWorldRay ray = coordinates_->DisplayToWorldRay(displayX, displayY);
    const float denominator = ray.valid ? Dot(ray.direction, planeNormal) : 0.0f;
    if (!ray.valid || std::fabs(denominator) < 0.00001f) return false;
    const float distance = Dot(Subtract(planePoint, ray.origin), planeNormal) / denominator;
    if (!std::isfinite(distance) || distance < 0.0f) return false;
    intersection = Add(ray.origin, Scale(ray.direction, distance));
    return true;
}

bool CourseRailTransformGizmo::RayAxisParameter(
    float displayX, float displayY,
    const Vector3& origin, const Vector3& axis,
    float& parameter) const {
    if (coordinates_ == nullptr) return false;
    const EditorViewportWorldRay ray = coordinates_->DisplayToWorldRay(displayX, displayY);
    if (!ray.valid) return false;
    const Vector3 w0 = Subtract(origin, ray.origin);
    const float b = Dot(axis, ray.direction);
    const float d = Dot(axis, w0);
    const float e = Dot(ray.direction, w0);
    const float denominator = 1.0f - b * b;
    if (std::fabs(denominator) < 0.00001f) return false;
    parameter = (b * e - d) / denominator;
    return std::isfinite(parameter);
}

Vector3 CourseRailTransformGizmo::SnapDelta(
    Vector3 delta, CourseRailGizmoHandle handle) const {
    if (!settings_.snapEnabled) return delta;
    const auto snapped = [this](float value) {
        return std::round(value / settings_.gridSize) * settings_.gridSize;
    };
    Vector3 a{};
    Vector3 b{};
    if (PlaneAxes(handle, a, b)) {
        return Add(Scale(a, snapped(Dot(delta, a))), Scale(b, snapped(Dot(delta, b))));
    }
    const Vector3 axis = Axis(handle);
    return Scale(axis, snapped(Dot(delta, axis)));
}

void CourseRailTransformGizmo::RefreshPreview() {
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    if (!state_.previewValid) state_.message = previewModel_->ValidationError();
}

const char* ToString(CourseRailGizmoHandle handle) {
    switch (handle) {
    case CourseRailGizmoHandle::None: return "None";
    case CourseRailGizmoHandle::X: return "X";
    case CourseRailGizmoHandle::Y: return "Y";
    case CourseRailGizmoHandle::Z: return "Z";
    case CourseRailGizmoHandle::XY: return "XY";
    case CourseRailGizmoHandle::YZ: return "YZ";
    case CourseRailGizmoHandle::ZX: return "ZX";
    }
    return "Unknown";
}

} // namespace editor
