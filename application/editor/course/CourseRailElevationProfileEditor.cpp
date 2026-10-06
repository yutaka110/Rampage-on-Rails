#include "CourseRailElevationProfileEditor.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

float SquaredDistance(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

RailPathSample EvaluateDistance(const CourseRailAuthoringModel& rail, float distance) {
    if (distance >= rail.Length() && !rail.Segments().empty()) {
        return rail.RuntimePath().EvaluateSegmentAt(rail.Segments().back().pointIndex, 1.0f);
    }
    return rail.RuntimePath().Evaluate((std::max)(0.0f, distance));
}

uint32_t ConstraintColor(CourseRailConstraintSeverity severity) {
    switch (severity) {
    case CourseRailConstraintSeverity::Error: return 0xff5353ffu;
    case CourseRailConstraintSeverity::Warning: return 0xff46b8ffu;
    case CourseRailConstraintSeverity::Info: return 0xff73d5a0u;
    }
    return 0xffffffffu;
}

uint64_t HashValue(uint64_t hash, uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ull;
}

bool SameRect(const CourseOverviewMapRect& lhs, const CourseOverviewMapRect& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
        lhs.height == rhs.height;
}

bool SameSettings(
    const CourseRailElevationProfileSettings& lhs,
    const CourseRailElevationProfileSettings& rhs) {
    return lhs.paddingPixels == rhs.paddingPixels &&
        lhs.zoomDistance == rhs.zoomDistance && lhs.zoomHeight == rhs.zoomHeight &&
        lhs.panPixels.x == rhs.panPixels.x && lhs.panPixels.y == rhs.panPixels.y &&
        lhs.heightSnapEnabled == rhs.heightSnapEnabled &&
        lhs.heightSnapStep == rhs.heightSnapStep &&
        lhs.samplesPerSegment == rhs.samplesPerSegment;
}

} // namespace

bool CourseRailElevationProfileEditor::Bind(
    CourseRailEditorController* rail,
    EditorSelection* selection,
    CourseOverviewMapController* overview,
    CourseOverviewMapMultiViewCoordinator* multiView,
    std::string* errorMessage) {
    if (rail_ == rail && selection_ == selection && overview_ == overview &&
        multiView_ == multiView && state_.bound) return Validate(errorMessage);
    Cancel();
    frame_ = {};
    playheadOverlay_ = {};
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
    state_.hoveredMarker = -1;
    rail_ = rail;
    selection_ = selection;
    overview_ = overview;
    multiView_ = multiView;
    if (!Validate(errorMessage)) {
        state_.bound = false;
        return false;
    }
    state_.bound = true;
    state_.message = "Elevation Profile ready.";
    return true;
}

void CourseRailElevationProfileEditor::Unbind() {
    Cancel();
    rail_ = nullptr;
    selection_ = nullptr;
    overview_ = nullptr;
    multiView_ = nullptr;
    state_ = {};
    frame_ = {};
    playheadOverlay_ = {};
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
}

void CourseRailElevationProfileEditor::SetActive(bool active) {
    if (state_.active == active) return;
    Cancel();
    state_.active = active;
}

void CourseRailElevationProfileEditor::SetViewport(CourseOverviewMapRect rect) {
    if (SameRect(viewport_, rect)) return;
    viewport_ = rect;
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseRailElevationProfileSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(paddingPixels, 4.0f, 96.0f) &&
        settings::InRange(zoomDistance, .05f, 64.0f) &&
        settings::InRange(zoomHeight, .05f, 64.0f) &&
        settings::InRange(heightSnapStep, .01f, 1000.0f) &&
        settings::InRange(samplesPerSegment, 4u, 128u) &&
        settings::Finite(panPixels.x) && settings::Finite(panPixels.y);
    return settings::Result(valid, errorMessage,
        "CourseRailElevationProfileSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseRailElevationProfileEditor::SetSettings(
    CourseRailElevationProfileSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++viewportRevision_;
    InvalidateFrameCache();
    return true;
}

void CourseRailElevationProfileEditor::Pan(Vector2 deltaPixels) {
    if (deltaPixels.x == 0.0f && deltaPixels.y == 0.0f) return;
    CourseRailElevationProfileSettings candidate = settings_;
    candidate.panPixels.x += deltaPixels.x;
    candidate.panPixels.y += deltaPixels.y;
    (void)SetSettings(candidate);
}

void CourseRailElevationProfileEditor::ZoomAt(
    Vector2 mapPosition,
    float distanceFactor,
    float heightFactor) {
    if (!frame_.valid || !std::isfinite(mapPosition.x) || !std::isfinite(mapPosition.y) ||
        !std::isfinite(distanceFactor) || !std::isfinite(heightFactor) ||
        distanceFactor <= 0.0f || heightFactor <= 0.0f) return;
    CourseRailElevationProfileSettings candidate = settings_;
    candidate.zoomDistance = (std::clamp)(settings_.zoomDistance * distanceFactor, 0.05f, 64.0f);
    candidate.zoomHeight = (std::clamp)(settings_.zoomHeight * heightFactor, 0.05f, 64.0f);
    // Preserve the point under the cursor without changing live settings first.
    const float centerX = viewport_.x + viewport_.width * 0.5f + settings_.panPixels.x;
    const float centerY = viewport_.y + viewport_.height * 0.5f + settings_.panPixels.y;
    candidate.panPixels.x += (mapPosition.x - centerX) *
        (1.0f - candidate.zoomDistance / settings_.zoomDistance);
    candidate.panPixels.y += (mapPosition.y - centerY) *
        (1.0f - candidate.zoomHeight / settings_.zoomHeight);
    (void)SetSettings(candidate);
}

void CourseRailElevationProfileEditor::FrameAll() {
    if (settings_.zoomDistance == 1.0f && settings_.zoomHeight == 1.0f &&
        settings_.panPixels.x == 0.0f && settings_.panPixels.y == 0.0f) return;
    settings_.zoomDistance = 1.0f;
    settings_.zoomHeight = 1.0f;
    settings_.panPixels = {};
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseRailElevationProfileEditor::Rebuild(
    float playheadDistance,
    const CourseRailConstraintReport* constraints,
    std::string* errorMessage) {
    if (!Validate(errorMessage) || !viewport_.Valid()) {
        frame_ = {};
        playheadOverlay_ = {};
        InvalidateFrameCache();
        InvalidatePlayheadOverlay();
        return false;
    }
    const RetainedFrameKey key = BuildFrameKey(constraints);
    if (retainedFrameKey_.has_value() && frame_.valid &&
        SameFrameKey(*retainedFrameKey_, key)) {
        ++state_.frameCacheHits;
        RefreshPlayheadOverlay(playheadDistance);
        return true;
    }
    const CourseRailAuthoringModel* rail = DisplayRail();
    const CourseRailAuthoringModel* fitRail = rail_->Model();
    if (rail == nullptr || fitRail == nullptr || !rail->IsValid()) return false;
    frame_ = {};
    frame_.rect = viewport_;
    frame_.railLength = rail->Length();

    fitMinimumHeight_ = (std::numeric_limits<float>::max)();
    fitMaximumHeight_ = (std::numeric_limits<float>::lowest)();
    const uint32_t fitSamples = (std::max)(2u,
        fitRail->RuntimePath().SegmentCount() * settings_.samplesPerSegment + 1u);
    for (uint32_t index = 0; index < fitSamples; ++index) {
        const float distance = fitRail->Length() * static_cast<float>(index) /
            static_cast<float>(fitSamples - 1u);
        const float height = EvaluateDistance(*fitRail, distance).position.y;
        fitMinimumHeight_ = (std::min)(fitMinimumHeight_, height);
        fitMaximumHeight_ = (std::max)(fitMaximumHeight_, height);
    }
    float range = fitMaximumHeight_ - fitMinimumHeight_;
    if (range < 1.0f) {
        const float center = (fitMaximumHeight_ + fitMinimumHeight_) * 0.5f;
        fitMinimumHeight_ = center - 0.5f;
        fitMaximumHeight_ = center + 0.5f;
    } else {
        fitMinimumHeight_ -= range * 0.12f;
        fitMaximumHeight_ += range * 0.12f;
    }
    frame_.minimumHeight = fitMinimumHeight_;
    frame_.maximumHeight = fitMaximumHeight_;

    constexpr uint32_t gridColor = 0xff273740u;
    for (uint32_t index = 0; index <= 4; ++index) {
        const float t = static_cast<float>(index) / 4.0f;
        const float distance = frame_.railLength * t;
        const float height = fitMinimumHeight_ + (fitMaximumHeight_ - fitMinimumHeight_) * t;
        frame_.gridLines.push_back({Project(distance, fitMinimumHeight_),
            Project(distance, fitMaximumHeight_), gridColor, 1.0f});
        frame_.gridLines.push_back({Project(0.0f, height),
            Project(frame_.railLength, height), gridColor, 1.0f});
    }

    const uint32_t samples = (std::max)(2u,
        rail->RuntimePath().SegmentCount() * settings_.samplesPerSegment + 1u);
    for (uint32_t index = 1; index < samples; ++index) {
        const float aDistance = rail->Length() * static_cast<float>(index - 1u) /
            static_cast<float>(samples - 1u);
        const float bDistance = rail->Length() * static_cast<float>(index) /
            static_cast<float>(samples - 1u);
        uint32_t color = 0xff51bce6u;
        if (constraints != nullptr && constraints->valid && !constraints->samples.empty()) {
            const std::size_t constraintIndex = (std::min)(
                static_cast<std::size_t>(index) * constraints->samples.size() / samples,
                constraints->samples.size() - 1u);
            color = ConstraintColor(constraints->samples[constraintIndex].severity);
        }
        frame_.profileLines.push_back({
            Project(aDistance, EvaluateDistance(*rail, aDistance).position.y),
            Project(bDistance, EvaluateDistance(*rail, bDistance).position.y),
            color, 2.5f});
    }

    const auto& points = rail->RuntimePath().ControlPoints();
    frame_.markers.reserve(points.size());
    for (uint32_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        EditorObjectHandle handle{EditorDomainId::CourseRailControlPoint,
            "course-rail-point:" + point.editorGuid, index,
            rail_->State().bindingGeneration, "Rail Control Point"};
        frame_.markers.push_back({Project(PointDistance(index, *rail), point.position.y),
            point.editorGuid, index, PointDistance(index, *rail), point.position.y,
            0xffffd37au, 5.0f,
            selection_ != nullptr && selection_->Contains(handle)});
    }
    frame_.valid = true;
    frame_.message = "Elevation Profile synchronized.";
    retainedFrameKey_ = key;
    ++state_.frameRevision;
    RefreshPlayheadOverlay(playheadDistance);
    return true;
}

void CourseRailElevationProfileEditor::Tick(
    const CourseRailElevationProfileInput& input) {
    if (!state_.active || !state_.bound || !frame_.valid) return;
    UpdateHover(input.mapPosition);
    if (input.cancelPressed) {
        Cancel("Elevation edit cancelled.");
        return;
    }
    if (state_.dragging) {
        if (input.primaryDown) UpdateDrag(input.mapPosition.y);
        if (input.primaryReleased) CommitDrag();
        return;
    }
    if (input.primaryPressed && state_.hoveredMarker >= 0) {
        BeginDrag(static_cast<uint32_t>(state_.hoveredMarker));
    } else if (frame_.rect.Contains(input.mapPosition)) {
        SetFocusDistance(UnprojectDistance(input.mapPosition.x));
    }
}

void CourseRailElevationProfileEditor::Cancel(std::string message) {
    const bool hadPreview = previewRail_.has_value();
    ClearPublishedPreview();
    previewRail_.reset();
    previewCourse_ = {};
    state_.dragging = false;
    state_.previewValid = false;
    dragPointGuid_.clear();
    if (hadPreview) {
        ++previewRevision_;
        InvalidateFrameCache();
    }
    if (!message.empty()) state_.message = std::move(message);
}

void CourseRailElevationProfileEditor::SetFocusDistance(float railDistance) {
    const CourseRailAuthoringModel* rail = DisplayRail();
    if (rail == nullptr || !std::isfinite(railDistance)) return;
    state_.focusDistance = (std::clamp)(railDistance, 0.0f, rail->Length());
    if (multiView_ != nullptr) multiView_->SetFocusDistance(state_.focusDistance);
}

Vector2 CourseRailElevationProfileEditor::Project(float railDistance, float height) const {
    const float innerWidth = (std::max)(1.0f, viewport_.width - settings_.paddingPixels * 2.0f);
    const float innerHeight = (std::max)(1.0f, viewport_.height - settings_.paddingPixels * 2.0f);
    const float length = (std::max)(0.001f, frame_.railLength);
    const float heightRange = (std::max)(0.001f, fitMaximumHeight_ - fitMinimumHeight_);
    const float normalizedDistance = railDistance / length - 0.5f;
    const float normalizedHeight = (height - fitMinimumHeight_) / heightRange - 0.5f;
    return {viewport_.x + viewport_.width * 0.5f + normalizedDistance * innerWidth * settings_.zoomDistance + settings_.panPixels.x,
        viewport_.y + viewport_.height * 0.5f - normalizedHeight * innerHeight * settings_.zoomHeight + settings_.panPixels.y};
}

float CourseRailElevationProfileEditor::UnprojectDistance(float mapX) const {
    const float innerWidth = (std::max)(1.0f, viewport_.width - settings_.paddingPixels * 2.0f);
    const float normalized = (mapX - viewport_.x - viewport_.width * 0.5f - settings_.panPixels.x) /
        (innerWidth * settings_.zoomDistance) + 0.5f;
    return (std::clamp)(normalized * frame_.railLength, 0.0f, frame_.railLength);
}

float CourseRailElevationProfileEditor::UnprojectHeight(float mapY) const {
    const float innerHeight = (std::max)(1.0f, viewport_.height - settings_.paddingPixels * 2.0f);
    const float normalized = 0.5f -
        (mapY - viewport_.y - viewport_.height * 0.5f - settings_.panPixels.y) /
        (innerHeight * settings_.zoomHeight);
    return fitMinimumHeight_ + normalized * (fitMaximumHeight_ - fitMinimumHeight_);
}

bool CourseRailElevationProfileEditor::Validate(std::string* errorMessage) const {
    if (rail_ == nullptr || rail_->Course() == nullptr || rail_->Model() == nullptr ||
        selection_ == nullptr) {
        if (errorMessage != nullptr) *errorMessage =
            "Elevation Profile requires a valid Rail controller and EditorSelection.";
        return false;
    }
    return true;
}

bool CourseRailElevationProfileEditor::CanMutate() const {
    return rail_ != nullptr && rail_->State().bound && rail_->State().authoringAllowed &&
        rail_->State().status == CourseRailEditorControllerStatus::Ready;
}

const CourseRailAuthoringModel* CourseRailElevationProfileEditor::DisplayRail() const {
    return previewRail_.has_value() ? &*previewRail_ : rail_ != nullptr ? rail_->Model() : nullptr;
}

void CourseRailElevationProfileEditor::BeginDrag(uint32_t markerIndex) {
    if (!CanMutate() || markerIndex >= frame_.markers.size() || rail_->Course() == nullptr) {
        state_.message = "Elevation Profile is read-only.";
        return;
    }
    const auto& marker = frame_.markers[markerIndex];
    const auto pointIndex = rail_->Model()->FindPointIndex(marker.pointGuid);
    if (!pointIndex.has_value()) return;
    previewCourse_ = *rail_->Course();
    dragOriginal_ = previewCourse_.railPoints[*pointIndex];
    dragPointGuid_ = marker.pointGuid;
    dragExpectedRevision_ = rail_->State().mutationRevision;
    previewRail_.emplace(previewCourse_);
    ++previewRevision_;
    InvalidateFrameCache();
    state_.dragging = true;
    state_.previewValid = previewRail_->IsValid();
    selection_->SetPrimary({EditorDomainId::CourseRailControlPoint,
        "course-rail-point:" + marker.pointGuid, *pointIndex,
        rail_->State().bindingGeneration, "Rail Control Point"});
    state_.message = "Dragging rail elevation preview; release commits one Undo transaction.";
    PublishPreview();
}

void CourseRailElevationProfileEditor::UpdateDrag(float mapY) {
    auto it = std::find_if(previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
        [this](const RailPathControlPoint& point) { return point.editorGuid == dragPointGuid_; });
    if (it == previewCourse_.railPoints.end()) return;
    float height = UnprojectHeight(mapY);
    if (settings_.heightSnapEnabled) {
        height = std::round(height / settings_.heightSnapStep) * settings_.heightSnapStep;
    }
    if (std::fabs(it->position.y - height) <= 0.000001f) return;
    it->position.y = height;
    previewRail_.emplace(previewCourse_);
    ++previewRevision_;
    InvalidateFrameCache();
    state_.previewValid = previewRail_->IsValid();
    if (state_.previewValid) PublishPreview();
    else ClearPublishedPreview();
}

void CourseRailElevationProfileEditor::CommitDrag() {
    if (!state_.dragging) return;
    const auto it = std::find_if(previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
        [this](const RailPathControlPoint& point) { return point.editorGuid == dragPointGuid_; });
    if (it == previewCourse_.railPoints.end() || !state_.previewValid) {
        Cancel("Elevation preview is invalid and was not committed.");
        return;
    }
    if (std::fabs(it->position.y - dragOriginal_.position.y) <= 0.000001f) {
        Cancel("Elevation drag ended without changes.");
        return;
    }
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::MovePoint;
    request.expectedRevision = dragExpectedRevision_;
    request.pointGuid = dragPointGuid_;
    request.point = *it;
    request.label = "Edit Rail Elevation";
    ClearPublishedPreview();
    state_.dragging = false;
    state_.previewValid = false;
    previewRail_.reset();
    ++previewRevision_;
    InvalidateFrameCache();
    const CourseRailMutationResult result = rail_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) ++state_.editRevision;
    previewCourse_ = {};
    dragPointGuid_.clear();
}

void CourseRailElevationProfileEditor::PublishPreview() {
    if (!state_.previewValid) return;
    if (overview_ != nullptr) overview_->SetPreviewCourse(&previewCourse_);
    if (multiView_ != nullptr) multiView_->SetPreviewCourse(&previewCourse_);
}

void CourseRailElevationProfileEditor::ClearPublishedPreview() {
    if (overview_ != nullptr) overview_->ClearPreviewCourse();
    if (multiView_ != nullptr) multiView_->ClearPreviewCourse();
}

void CourseRailElevationProfileEditor::UpdateHover(Vector2 mapPosition) {
    state_.hoveredMarker = -1;
    float best = 100.0f;
    for (uint32_t index = 0; index < frame_.markers.size(); ++index) {
        const float distance = SquaredDistance(frame_.markers[index].position, mapPosition);
        const float threshold = frame_.markers[index].radius + 4.0f;
        if (distance <= threshold * threshold && distance < best) {
            best = distance;
            state_.hoveredMarker = static_cast<int32_t>(index);
        }
    }
}

float CourseRailElevationProfileEditor::PointDistance(
    uint32_t pointIndex,
    const CourseRailAuthoringModel& rail) const {
    if (pointIndex == 0) return 0.0f;
    if (pointIndex >= rail.RuntimePath().ControlPoints().size() - 1u) return rail.Length();
    return pointIndex < rail.Segments().size()
        ? rail.Segments()[pointIndex].startDistance : rail.Length();
}

CourseRailElevationProfileEditor::RetainedFrameKey
CourseRailElevationProfileEditor::BuildFrameKey(
    const CourseRailConstraintReport* constraints) const {
    RetainedFrameKey key{};
    key.railRevision = rail_->State().mutationRevision;
    key.railGeneration = rail_->State().bindingGeneration;
    key.selectionRevision = selection_->Revision();
    key.previewRevision = previewRevision_;
    key.viewport = viewport_;
    key.settings = settings_;
    uint64_t constraintSignature = 1469598103934665603ull;
    if (constraints != nullptr) {
        constraintSignature = HashValue(constraintSignature, constraints->valid);
        constraintSignature = HashValue(constraintSignature, constraints->revision);
        constraintSignature = HashValue(constraintSignature, constraints->sourceSignature);
        constraintSignature = HashValue(constraintSignature, constraints->errors);
        constraintSignature = HashValue(constraintSignature, constraints->warnings);
        constraintSignature = HashValue(constraintSignature, constraints->infos);
        constraintSignature = HashValue(constraintSignature, constraints->samples.size());
        // Cached reports carry a stable O(1) revision. Direct one-shot reports
        // retain revision 0 and use the severity scan as a compatibility path.
        if (constraints->revision == 0) {
            for (const CourseRailConstraintSample& sample : constraints->samples) {
                constraintSignature = HashValue(
                    constraintSignature, static_cast<uint64_t>(sample.severity));
            }
        }
    }
    key.constraintSignature = constraintSignature;
    key.viewportRevision = viewportRevision_;
    return key;
}

bool CourseRailElevationProfileEditor::SameFrameKey(
    const RetainedFrameKey& lhs,
    const RetainedFrameKey& rhs) noexcept {
    return lhs.railRevision == rhs.railRevision &&
        lhs.previewRevision == rhs.previewRevision &&
        lhs.constraintSignature == rhs.constraintSignature &&
        lhs.viewportRevision == rhs.viewportRevision &&
        lhs.railGeneration == rhs.railGeneration &&
        lhs.selectionRevision == rhs.selectionRevision &&
        SameRect(lhs.viewport, rhs.viewport) &&
        SameSettings(lhs.settings, rhs.settings);
}

void CourseRailElevationProfileEditor::InvalidateFrameCache() noexcept {
    retainedFrameKey_.reset();
}

void CourseRailElevationProfileEditor::RefreshPlayheadOverlay(
    float playheadDistance) {
    if (!frame_.valid) {
        playheadOverlay_ = {};
        InvalidatePlayheadOverlay();
        return;
    }
    const PlayheadOverlayKey key{state_.frameRevision, playheadDistance};
    if (playheadOverlayKey_.has_value() && playheadOverlay_.valid &&
        playheadOverlayKey_->staticFrameRevision == key.staticFrameRevision &&
        playheadOverlayKey_->playheadDistance == key.playheadDistance) {
        ++state_.playheadOverlayCacheHits;
        return;
    }
    playheadOverlay_ = {};
    playheadOverlay_.valid = true;
    playheadOverlay_.rect = frame_.rect;
    if (playheadDistance >= 0.0f) {
        const CourseRailAuthoringModel* rail = DisplayRail();
        if (rail != nullptr && rail->IsValid()) {
            const float distance = (std::clamp)(
                playheadDistance, 0.0f, rail->Length());
            playheadOverlay_.visible = true;
            playheadOverlay_.railDistance = distance;
            playheadOverlay_.position = Project(
                distance, EvaluateDistance(*rail, distance).position.y);
        }
    }
    playheadOverlay_.revision = ++state_.playheadOverlayRevision;
    playheadOverlayKey_ = key;
}

void CourseRailElevationProfileEditor::InvalidatePlayheadOverlay() noexcept {
    playheadOverlayKey_.reset();
}

} // namespace editor
