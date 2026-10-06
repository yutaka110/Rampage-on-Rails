#include "CourseEnemyTransformGizmo.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>

namespace editor {
namespace {

constexpr std::string_view kPlacementPrefix = "course-enemy-placement:";

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

float SegmentDistanceSquared(
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
    const float ox = x - nearestX;
    const float oy = y - nearestY;
    return ox * ox + oy * oy;
}

uint32_t HandleColor(CourseEnemyGizmoHandle handle, bool highlighted) {
    uint32_t color = 0xfff0f0f0u;
    if (handle == CourseEnemyGizmoHandle::X || handle == CourseEnemyGizmoHandle::YZ) {
        color = 0xff5b5bffu;
    } else if (handle == CourseEnemyGizmoHandle::Y ||
        handle == CourseEnemyGizmoHandle::ZX) {
        color = 0xff62dc62u;
    } else if (handle == CourseEnemyGizmoHandle::Z ||
        handle == CourseEnemyGizmoHandle::XY) {
        color = 0xffffa34du;
    } else if (handle == CourseEnemyGizmoHandle::Uniform) {
        color = 0xfff2f2f2u;
    }
    return highlighted ? 0xffffffffu : color;
}

float& AxisComponent(Vector3& value, CourseEnemyGizmoHandle handle) {
    if (handle == CourseEnemyGizmoHandle::X) return value.x;
    if (handle == CourseEnemyGizmoHandle::Y) return value.y;
    return value.z;
}

} // namespace

void CourseEnemyTransformGizmo::Bind(CourseEnemyEditorController* controller) {
    const uint32_t generation = controller != nullptr
        ? controller->State().bindingGeneration : 0;
    if (controller_ == controller && bindingGeneration_ == generation) return;
    Cancel();
    controller_ = controller;
    bindingGeneration_ = generation;
    selectedGuids_.clear();
}

bool CourseEnemyTransformGizmoSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(translationSnap, .01f, 1000.0f) &&
        settings::InRange(rotationSnapDegrees, .1f, 180.0f) &&
        settings::InRange(scaleSnap, .001f, 10.0f) &&
        settings::InRange(handleLengthScale, .1f, 4.0f) &&
        mode >= EditorTransformGizmoMode::Translate && mode <= EditorTransformGizmoMode::Rotate &&
        space >= EditorTransformGizmoSpace::World && space <= EditorTransformGizmoSpace::Local;
    return settings::Result(valid, errorMessage,
        "CourseEnemyTransformGizmoSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseEnemyTransformGizmo::SetSettings(
    CourseEnemyTransformGizmoSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

void CourseEnemyTransformGizmo::Tick(
    const CourseEnemyTransformGizmoInput& input) {
    state_.canMutate = input.canMutate && controller_ != nullptr &&
        controller_->State().authoringAllowed && !state_.containsLockedPlacement;
    coordinates_ = input.coordinates;
    if (input.primaryCancelled || input.cancelPressed) {
        Cancel("Enemy gizmo drag cancelled.");
    }
    if (!RefreshFrame(input)) return;
    state_.canMutate = input.canMutate && controller_->State().authoringAllowed &&
        !state_.containsLockedPlacement;
    if (!state_.dragging) state_.hovered = PickHandle(input.displayX, input.displayY);
    if (!state_.dragging && input.primaryPressed &&
        state_.hovered != CourseEnemyGizmoHandle::None && state_.canMutate) {
        BeginDrag(state_.hovered, input.displayX, input.displayY);
    }
    if (!state_.dragging) return;
    if (input.primaryDown) UpdateDrag(input.displayX, input.displayY);
    if (input.primaryReleased) CommitDrag();
}

void CourseEnemyTransformGizmo::BuildViewportOverlay(
    EditorViewportOverlayService& overlay) const {
    if (!state_.visible || coordinates_ == nullptr) return;
    auto sink = overlay.Sink(EditorViewportOverlayLayerId::AuthoringHelpers);
    const auto line = [&](const Vector3& a, const Vector3& b, uint32_t color, float width) {
        const auto pa = coordinates_->ProjectWorld(a);
        const auto pb = coordinates_->ProjectWorld(b);
        if (pa.valid && pb.valid && pa.inDepth && pb.inDepth) {
            sink.Line(pa.render.x, pa.render.y, pb.render.x, pb.render.y,
                color, width, EditorViewportOverlayItemOptions{true});
        }
    };
    const auto axis = [&](CourseEnemyGizmoHandle handle,
                          const Vector3& direction,
                          const char* label) {
        const Vector3 end = Add(state_.pivot, Scale(direction, handleLength_));
        const bool highlighted = state_.active == handle || state_.hovered == handle;
        const uint32_t color = HandleColor(handle, highlighted);
        line(state_.pivot, end, color, highlighted ? 4.0f : 2.5f);
        const auto projected = coordinates_->ProjectWorld(end);
        if (projected.valid && projected.inDepth) {
            const float radius = settings_.mode == EditorTransformGizmoMode::Scale
                ? (highlighted ? 7.0f : 5.5f) : (highlighted ? 6.0f : 4.5f);
            sink.CircleFilled(projected.render.x, projected.render.y, radius,
                color, EditorViewportOverlayItemOptions{true});
            sink.Label(projected.render.x + 6.0f, projected.render.y - 8.0f,
                label, color, EditorViewportOverlayItemOptions{true});
        }
    };

    if (settings_.mode == EditorTransformGizmoMode::Rotate) {
        constexpr uint32_t kSteps = 36;
        const auto ring = [&](CourseEnemyGizmoHandle handle,
                              const Vector3& a,
                              const Vector3& b) {
            const bool highlighted = state_.active == handle || state_.hovered == handle;
            const uint32_t color = HandleColor(handle, highlighted);
            Vector3 previous = Add(state_.pivot, Scale(a, handleLength_ * 0.82f));
            for (uint32_t step = 1; step <= kSteps; ++step) {
                const float angle = static_cast<float>(step) *
                    2.0f * std::numbers::pi_v<float> / static_cast<float>(kSteps);
                const Vector3 current = Add(state_.pivot,
                    Add(Scale(a, std::cos(angle) * handleLength_ * 0.82f),
                        Scale(b, std::sin(angle) * handleLength_ * 0.82f)));
                line(previous, current, color, highlighted ? 3.5f : 2.0f);
                previous = current;
            }
        };
        ring(CourseEnemyGizmoHandle::X, basis_.y, basis_.z);
        ring(CourseEnemyGizmoHandle::Y, basis_.z, basis_.x);
        ring(CourseEnemyGizmoHandle::Z, basis_.x, basis_.y);
        return;
    }

    axis(CourseEnemyGizmoHandle::X, basis_.x, "X");
    axis(CourseEnemyGizmoHandle::Y, basis_.y, "Y");
    axis(CourseEnemyGizmoHandle::Z, basis_.z, "Z");
    if (settings_.mode == EditorTransformGizmoMode::Scale) {
        const auto pivot = coordinates_->ProjectWorld(state_.pivot);
        const bool highlighted = state_.active == CourseEnemyGizmoHandle::Uniform ||
            state_.hovered == CourseEnemyGizmoHandle::Uniform;
        if (pivot.valid && pivot.inDepth) {
            sink.CircleFilled(pivot.render.x, pivot.render.y,
                highlighted ? 7.0f : 5.0f,
                HandleColor(CourseEnemyGizmoHandle::Uniform, highlighted),
                EditorViewportOverlayItemOptions{true});
        }
        return;
    }
    const auto plane = [&](CourseEnemyGizmoHandle handle,
                           const Vector3& a,
                           const Vector3& b) {
        const float inner = handleLength_ * 0.16f;
        const float outer = handleLength_ * 0.34f;
        const Vector3 p0 = Add(state_.pivot, Add(Scale(a, inner), Scale(b, inner)));
        const Vector3 p1 = Add(state_.pivot, Add(Scale(a, outer), Scale(b, inner)));
        const Vector3 p2 = Add(state_.pivot, Add(Scale(a, outer), Scale(b, outer)));
        const Vector3 p3 = Add(state_.pivot, Add(Scale(a, inner), Scale(b, outer)));
        const bool highlighted = state_.active == handle || state_.hovered == handle;
        const uint32_t color = HandleColor(handle, highlighted);
        line(p0, p1, color, highlighted ? 3.0f : 1.5f);
        line(p1, p2, color, highlighted ? 3.0f : 1.5f);
        line(p2, p3, color, highlighted ? 3.0f : 1.5f);
        line(p3, p0, color, highlighted ? 3.0f : 1.5f);
    };
    plane(CourseEnemyGizmoHandle::XY, basis_.x, basis_.y);
    plane(CourseEnemyGizmoHandle::YZ, basis_.y, basis_.z);
    plane(CourseEnemyGizmoHandle::ZX, basis_.z, basis_.x);
}

void CourseEnemyTransformGizmo::Cancel(std::string message) {
    state_.dragging = false;
    state_.previewValid = false;
    state_.active = CourseEnemyGizmoHandle::None;
    previewModel_.reset();
    previewCourse_ = {};
    originalPlacements_.clear();
    originalWorldPositions_.clear();
    if (!message.empty()) state_.message = std::move(message);
}

std::vector<std::string> CourseEnemyTransformGizmo::SelectedPlacementGuids(
    const EditorSelection* selection) const {
    std::vector<std::string> result;
    if (selection == nullptr) return result;
    for (const EditorObjectHandle& handle : selection->Handles()) {
        if (handle.domain == EditorDomainId::CourseEnemyPlacement &&
            handle.stableId.starts_with(kPlacementPrefix)) {
            result.push_back(handle.stableId.substr(kPlacementPrefix.size()));
        }
    }
    return result;
}

bool CourseEnemyTransformGizmo::RefreshFrame(
    const CourseEnemyTransformGizmoInput& input) {
    if (controller_ == nullptr || !controller_->State().bound ||
        input.coordinates == nullptr || !input.enabled) {
        if (state_.dragging) Cancel("Enemy gizmo became unavailable.");
        state_.visible = false;
        state_.hovered = CourseEnemyGizmoHandle::None;
        state_.selectedPlacementCount = 0;
        return false;
    }
    const CourseEnemyAuthoringModel* model = previewModel_.has_value()
        ? &*previewModel_ : controller_->Model();
    if (model == nullptr || !model->IsValid()) return false;
    if (!state_.dragging) selectedGuids_ = SelectedPlacementGuids(input.selection);
    selectedGuids_.erase(
        std::remove_if(selectedGuids_.begin(), selectedGuids_.end(),
            [model](const std::string& guid) { return model->Find(guid) == nullptr; }),
        selectedGuids_.end());
    state_.selectedPlacementCount = static_cast<uint32_t>(selectedGuids_.size());
    state_.visible = !selectedGuids_.empty();
    state_.containsLockedPlacement = false;
    if (!state_.visible) return false;

    state_.pivot = {};
    CourseEnemyPlacementResolution primary{};
    for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
        const CourseEnemyPlacement* placement = model->Find(selectedGuids_[index]);
        const CourseEnemyPlacementResolution resolved = model->Resolve(selectedGuids_[index]);
        if (placement == nullptr || !resolved.valid) continue;
        if (index == 0) primary = resolved;
        state_.pivot = Add(state_.pivot, resolved.worldPosition);
        state_.containsLockedPlacement = state_.containsLockedPlacement ||
            placement->editorLocked;
    }
    state_.pivot = Scale(state_.pivot, 1.0f / static_cast<float>(selectedGuids_.size()));
    basis_ = ResolveBasis(primary);
    handleLength_ = (std::clamp)(
        primary.railSample.corridorRadius * settings_.handleLengthScale,
        0.5f, 30.0f);
    return true;
}

CourseEnemyGizmoHandle CourseEnemyTransformGizmo::PickHandle(
    float displayX, float displayY) const {
    if (!state_.visible || coordinates_ == nullptr) return CourseEnemyGizmoHandle::None;
    const auto pivot = coordinates_->ProjectWorld(state_.pivot);
    if (!pivot.valid || !pivot.inDepth) return CourseEnemyGizmoHandle::None;
    float bestDistance = (std::numeric_limits<float>::max)();
    CourseEnemyGizmoHandle best = CourseEnemyGizmoHandle::None;

    if (settings_.mode == EditorTransformGizmoMode::Scale) {
        const float dx = displayX - pivot.display.x;
        const float dy = displayY - pivot.display.y;
        if (dx * dx + dy * dy <= 9.0f * 9.0f) {
            return CourseEnemyGizmoHandle::Uniform;
        }
    }
    if (settings_.mode == EditorTransformGizmoMode::Rotate) {
        constexpr uint32_t kSteps = 36;
        for (const auto& candidate : {
                std::tuple{CourseEnemyGizmoHandle::X, basis_.y, basis_.z},
                std::tuple{CourseEnemyGizmoHandle::Y, basis_.z, basis_.x},
                std::tuple{CourseEnemyGizmoHandle::Z, basis_.x, basis_.y}}) {
            Vector3 previousWorld = Add(state_.pivot,
                Scale(std::get<1>(candidate), handleLength_ * 0.82f));
            auto previous = coordinates_->ProjectWorld(previousWorld);
            for (uint32_t step = 1; step <= kSteps; ++step) {
                const float angle = static_cast<float>(step) *
                    2.0f * std::numbers::pi_v<float> / static_cast<float>(kSteps);
                const Vector3 currentWorld = Add(state_.pivot,
                    Add(Scale(std::get<1>(candidate),
                            std::cos(angle) * handleLength_ * 0.82f),
                        Scale(std::get<2>(candidate),
                            std::sin(angle) * handleLength_ * 0.82f)));
                const auto current = coordinates_->ProjectWorld(currentWorld);
                if (previous.valid && current.valid && previous.inDepth && current.inDepth) {
                    const float distance = SegmentDistanceSquared(
                        displayX, displayY, previous, current);
                    if (distance <= 8.0f * 8.0f && distance < bestDistance) {
                        bestDistance = distance;
                        best = std::get<0>(candidate);
                    }
                }
                previous = current;
            }
        }
        return best;
    }

    for (const auto& candidate : {
            std::pair{CourseEnemyGizmoHandle::X, basis_.x},
            std::pair{CourseEnemyGizmoHandle::Y, basis_.y},
            std::pair{CourseEnemyGizmoHandle::Z, basis_.z}}) {
        const auto start = coordinates_->ProjectWorld(Add(
            state_.pivot, Scale(candidate.second, handleLength_ * 0.38f)));
        const auto end = coordinates_->ProjectWorld(Add(
            state_.pivot, Scale(candidate.second, handleLength_)));
        if (!start.valid || !end.valid || !start.inDepth || !end.inDepth) continue;
        const float distance = SegmentDistanceSquared(displayX, displayY, start, end);
        if (distance <= 8.0f * 8.0f && distance < bestDistance) {
            bestDistance = distance;
            best = candidate.first;
        }
    }
    if (best != CourseEnemyGizmoHandle::None ||
        settings_.mode == EditorTransformGizmoMode::Scale) {
        return best;
    }
    for (const auto& candidate : {
            std::tuple{CourseEnemyGizmoHandle::XY, basis_.x, basis_.y},
            std::tuple{CourseEnemyGizmoHandle::YZ, basis_.y, basis_.z},
            std::tuple{CourseEnemyGizmoHandle::ZX, basis_.z, basis_.x}}) {
        const Vector3 center = Add(state_.pivot,
            Add(Scale(std::get<1>(candidate), handleLength_ * 0.25f),
                Scale(std::get<2>(candidate), handleLength_ * 0.25f)));
        const auto projected = coordinates_->ProjectWorld(center);
        if (!projected.valid || !projected.inDepth) continue;
        const float dx = displayX - projected.display.x;
        const float dy = displayY - projected.display.y;
        const float distance = dx * dx + dy * dy;
        if (distance <= 13.0f * 13.0f && distance < bestDistance) {
            bestDistance = distance;
            best = std::get<0>(candidate);
        }
    }
    return best;
}

bool CourseEnemyTransformGizmo::BeginDrag(
    CourseEnemyGizmoHandle handle,
    float displayX,
    float displayY) {
    const CourseAsset* course = controller_->Course();
    const CourseEnemyAuthoringModel* model = controller_->Model();
    if (course == nullptr || model == nullptr || selectedGuids_.empty() ||
        state_.containsLockedPlacement) {
        state_.message = "Locked enemy placements cannot be transformed.";
        return false;
    }
    previewCourse_ = *course;
    originalPlacements_.clear();
    originalWorldPositions_.clear();
    for (const std::string& guid : selectedGuids_) {
        const CourseEnemyPlacement* placement = model->Find(guid);
        const CourseEnemyPlacementResolution resolved = model->Resolve(guid);
        if (placement == nullptr || !resolved.valid) return false;
        originalPlacements_.push_back(*placement);
        originalWorldPositions_.push_back(resolved.worldPosition);
    }
    state_.active = handle;
    state_.dragging = true;
    dragOriginPivot_ = state_.pivot;
    dragStartDisplayY_ = displayY;
    dragExpectedRevision_ = controller_->State().mutationRevision;

    if (settings_.mode == EditorTransformGizmoMode::Rotate) {
        const Vector3 axis = Axis(handle);
        Vector3 hit{};
        if (!RayPlane(displayX, displayY, dragOriginPivot_, axis, hit)) {
            Cancel("Enemy rotation plane could not be resolved.");
            return false;
        }
        dragStartVector_ = NormalizeOr(Subtract(hit, dragOriginPivot_), basis_.x);
    } else if (settings_.mode == EditorTransformGizmoMode::Scale &&
        handle == CourseEnemyGizmoHandle::Uniform) {
        dragStartAxisParameter_ = 0.0f;
    } else {
        Vector3 planeA{};
        Vector3 planeB{};
        if (PlaneAxes(handle, planeA, planeB)) {
            if (!RayPlane(displayX, displayY, dragOriginPivot_,
                    NormalizeOr(Cross(planeA, planeB), basis_.y), dragStartWorld_)) {
                Cancel("Enemy gizmo plane could not be resolved.");
                return false;
            }
        } else if (!RayAxisParameter(displayX, displayY, dragOriginPivot_,
                Axis(handle), dragStartAxisParameter_)) {
            Cancel("Enemy gizmo axis could not be resolved.");
            return false;
        }
    }
    RefreshPreview();
    state_.message = std::string("Enemy gizmo drag: ") + ToString(handle);
    return true;
}

void CourseEnemyTransformGizmo::UpdateDrag(float displayX, float displayY) {
    if (previewModel_ == std::nullopt) return;
    if (settings_.mode == EditorTransformGizmoMode::Translate) {
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
        delta = SnapTranslation(delta, state_.active);
        for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
            const RailAnchorProjection projected = previewModel_->RailModel().Project(
                Add(originalWorldPositions_[index], delta), 64);
            if (!projected.valid) continue;
            for (CourseEnemyPlacement& placement : previewCourse_.enemyPlacements) {
                if (placement.editorGuid == selectedGuids_[index]) {
                    placement.railAnchor = projected.anchor;
                    break;
                }
            }
        }
    } else if (settings_.mode == EditorTransformGizmoMode::Scale) {
        float factor = 1.0f;
        if (state_.active == CourseEnemyGizmoHandle::Uniform) {
            factor += (dragStartDisplayY_ - displayY) / 120.0f;
        } else {
            float parameter = 0.0f;
            if (!RayAxisParameter(displayX, displayY, dragOriginPivot_,
                    Axis(state_.active), parameter)) {
                return;
            }
            factor += (parameter - dragStartAxisParameter_) /
                (std::max)(handleLength_, 0.01f);
        }
        factor = (std::max)(SnapScale(factor), 0.01f);
        for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
            for (CourseEnemyPlacement& placement : previewCourse_.enemyPlacements) {
                if (placement.editorGuid != selectedGuids_[index]) continue;
                placement.localScale = originalPlacements_[index].localScale;
                if (state_.active == CourseEnemyGizmoHandle::Uniform) {
                    placement.localScale = Scale(placement.localScale, factor);
                } else {
                    AxisComponent(placement.localScale, state_.active) =
                        AxisComponent(placement.localScale, state_.active) * factor;
                }
                break;
            }
        }
    } else {
        const Vector3 axis = Axis(state_.active);
        Vector3 hit{};
        if (!RayPlane(displayX, displayY, dragOriginPivot_, axis, hit)) return;
        const Vector3 current = NormalizeOr(Subtract(hit, dragOriginPivot_), dragStartVector_);
        const float radians = std::atan2(
            Dot(axis, Cross(dragStartVector_, current)),
            Dot(dragStartVector_, current));
        const float degrees = SnapRotation(
            radians * 180.0f / std::numbers::pi_v<float>);
        for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
            for (CourseEnemyPlacement& placement : previewCourse_.enemyPlacements) {
                if (placement.editorGuid != selectedGuids_[index]) continue;
                placement.localRotation = originalPlacements_[index].localRotation;
                AxisComponent(placement.localRotation, state_.active) += degrees;
                break;
            }
        }
    }
    RefreshPreview();
}

void CourseEnemyTransformGizmo::CommitDrag() {
    if (!state_.dragging || !state_.previewValid) {
        Cancel("Enemy gizmo preview was invalid.");
        return;
    }
    std::vector<CourseEnemyPlacement> changed;
    changed.reserve(selectedGuids_.size());
    bool hasChanges = false;
    for (std::size_t index = 0; index < selectedGuids_.size(); ++index) {
        const auto found = std::find_if(
            previewCourse_.enemyPlacements.begin(), previewCourse_.enemyPlacements.end(),
            [&](const CourseEnemyPlacement& placement) {
                return placement.editorGuid == selectedGuids_[index];
            });
        if (found == previewCourse_.enemyPlacements.end()) continue;
        const CourseEnemyPlacement& before = originalPlacements_[index];
        hasChanges = hasChanges || before.railAnchor.segmentGuid != found->railAnchor.segmentGuid ||
            before.railAnchor.normalizedT != found->railAnchor.normalizedT ||
            before.railAnchor.lateralOffset != found->railAnchor.lateralOffset ||
            before.railAnchor.verticalOffset != found->railAnchor.verticalOffset ||
            before.railAnchor.forwardOffset != found->railAnchor.forwardOffset ||
            LengthSquared(Subtract(before.localRotation, found->localRotation)) > 0.000001f ||
            LengthSquared(Subtract(before.localScale, found->localScale)) > 0.000001f;
        changed.push_back(*found);
    }
    if (!hasChanges || changed.empty()) {
        Cancel("Enemy gizmo ended without changes.");
        return;
    }
    CourseEnemyMutationRequest request{};
    request.kind = settings_.mode == EditorTransformGizmoMode::Translate
        ? CourseEnemyMutationKind::SetAnchors : CourseEnemyMutationKind::SetPlacements;
    request.expectedRevision = dragExpectedRevision_;
    request.placements = std::move(changed);
    request.label = settings_.mode == EditorTransformGizmoMode::Translate
        ? "Move Enemy Placements"
        : settings_.mode == EditorTransformGizmoMode::Rotate
            ? "Rotate Enemy Placements" : "Scale Enemy Placements";
    Cancel();
    const CourseEnemyMutationResult result = controller_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) ++state_.editRevision;
}

CourseEnemyTransformGizmo::Basis CourseEnemyTransformGizmo::ResolveBasis(
    const CourseEnemyPlacementResolution& placement) const {
    if (settings_.space == EditorTransformGizmoSpace::World || !placement.valid) return {};
    Basis result{};
    result.x = NormalizeOr(placement.railSample.right, {1.0f, 0.0f, 0.0f});
    result.y = NormalizeOr(placement.railSample.up, {0.0f, 1.0f, 0.0f});
    result.z = NormalizeOr(placement.railSample.tangent, {0.0f, 0.0f, 1.0f});
    return result;
}

Vector3 CourseEnemyTransformGizmo::Axis(CourseEnemyGizmoHandle handle) const {
    if (handle == CourseEnemyGizmoHandle::X) return basis_.x;
    if (handle == CourseEnemyGizmoHandle::Y) return basis_.y;
    return basis_.z;
}

bool CourseEnemyTransformGizmo::PlaneAxes(
    CourseEnemyGizmoHandle handle,
    Vector3& a,
    Vector3& b) const {
    if (handle == CourseEnemyGizmoHandle::XY) { a = basis_.x; b = basis_.y; return true; }
    if (handle == CourseEnemyGizmoHandle::YZ) { a = basis_.y; b = basis_.z; return true; }
    if (handle == CourseEnemyGizmoHandle::ZX) { a = basis_.z; b = basis_.x; return true; }
    return false;
}

bool CourseEnemyTransformGizmo::RayPlane(
    float displayX,
    float displayY,
    const Vector3& planePoint,
    const Vector3& planeNormal,
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

bool CourseEnemyTransformGizmo::RayAxisParameter(
    float displayX,
    float displayY,
    const Vector3& origin,
    const Vector3& axis,
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

Vector3 CourseEnemyTransformGizmo::SnapTranslation(
    Vector3 delta,
    CourseEnemyGizmoHandle handle) const {
    if (!settings_.snapEnabled) return delta;
    const auto snap = [this](float value) {
        return std::round(value / settings_.translationSnap) * settings_.translationSnap;
    };
    Vector3 a{};
    Vector3 b{};
    if (PlaneAxes(handle, a, b)) {
        return Add(Scale(a, snap(Dot(delta, a))), Scale(b, snap(Dot(delta, b))));
    }
    const Vector3 axis = Axis(handle);
    return Scale(axis, snap(Dot(delta, axis)));
}

float CourseEnemyTransformGizmo::SnapRotation(float degrees) const {
    return settings_.snapEnabled
        ? std::round(degrees / settings_.rotationSnapDegrees) *
            settings_.rotationSnapDegrees
        : degrees;
}

float CourseEnemyTransformGizmo::SnapScale(float factor) const {
    return settings_.snapEnabled
        ? std::round(factor / settings_.scaleSnap) * settings_.scaleSnap
        : factor;
}

void CourseEnemyTransformGizmo::RefreshPreview() {
    previewModel_.emplace(previewCourse_);
    state_.previewValid = previewModel_->IsValid();
    if (!state_.previewValid) state_.message = previewModel_->ValidationError();
}

const char* ToString(CourseEnemyGizmoHandle handle) {
    switch (handle) {
    case CourseEnemyGizmoHandle::None: return "None";
    case CourseEnemyGizmoHandle::X: return "X";
    case CourseEnemyGizmoHandle::Y: return "Y";
    case CourseEnemyGizmoHandle::Z: return "Z";
    case CourseEnemyGizmoHandle::XY: return "XY";
    case CourseEnemyGizmoHandle::YZ: return "YZ";
    case CourseEnemyGizmoHandle::ZX: return "ZX";
    case CourseEnemyGizmoHandle::Uniform: return "Uniform";
    }
    return "Unknown";
}

} // namespace editor
