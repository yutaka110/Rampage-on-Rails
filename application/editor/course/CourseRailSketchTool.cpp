#include "CourseRailSketchTool.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>

namespace editor {
namespace {

float SquaredDistance(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

bool SamePosition(Vector3 a, Vector3 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z <= 0.000001f;
}

} // namespace

void CourseRailSketchTool::Bind(
    CourseOverviewMapController* overview,
    CourseRailEditorController* rail,
    EditorSelection* selection,
    const CourseRailCurveFitService* curveFit) {
    if (overview_ == overview && rail_ == rail && selection_ == selection &&
        curveFit_ == curveFit) return;
    Cancel();
    overview_ = overview;
    rail_ = rail;
    selection_ = selection;
    curveFit_ = curveFit;
}

void CourseRailSketchTool::SetActive(bool active) {
    if (state_.active == active) return;
    Cancel();
    state_.active = active;
    state_.message = active
        ? "Rail Sketch enabled. Draw in Top, Side, or Free projection."
        : "Rail Sketch disabled.";
}

void CourseRailSketchTool::SetMode(CourseRailSketchMode mode) {
    if (state_.mode == mode) return;
    Cancel();
    state_.mode = mode;
    state_.message = std::string("Rail Sketch mode: ") + ToString(mode);
}

bool CourseRailSketchSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(minimumSamplePixels, .5f, 64.0f) &&
        curveFit.Validate();
    return settings::Result(valid, errorMessage,
        "CourseRailSketchSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseRailSketchTool::SetSettings(
    CourseRailSketchSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

void CourseRailSketchTool::Tick(const CourseRailSketchInput& input) {
    state_.canMutate = CanMutate();
    if (!state_.active || overview_ == nullptr || rail_ == nullptr || curveFit_ == nullptr) {
        if (state_.drawing) Cancel("Rail stroke cancelled because its document became unavailable.");
        return;
    }
    if (input.cancelPressed) Cancel("Rail stroke cancelled.");
    if (state_.drawing) {
        if (input.primaryDown) Sample(input.mapPosition, false);
        if (input.primaryReleased) {
            Sample(input.mapPosition, true);
            RefreshPreview(true);
            Commit();
        }
        return;
    }
    if (input.primaryPressed) Begin(input);
}

void CourseRailSketchTool::Cancel(std::string message) {
    if (overview_ != nullptr) overview_->ClearPreviewCourse();
    state_.drawing = false;
    state_.previewValid = false;
    state_.rawSamples = 0;
    state_.fittedControlPoints = 0;
    state_.fittedLength = 0.0f;
    state_.minimumTurnRadius = 0.0f;
    state_.targetSegmentGuid.clear();
    mapSamples_.clear();
    worldSamples_.clear();
    lastFit_ = {};
    sourceCourse_ = {};
    previewCourse_ = {};
    if (!message.empty()) state_.message = std::move(message);
}

bool CourseRailSketchTool::CanMutate() const {
    return rail_ != nullptr && rail_->State().bound && rail_->State().authoringAllowed &&
        rail_->State().status == CourseRailEditorControllerStatus::Ready &&
        rail_->Model() != nullptr && rail_->Course() != nullptr;
}

bool CourseRailSketchTool::Begin(const CourseRailSketchInput& input) {
    if (!CanMutate()) {
        state_.message = "Rail Sketch requires writable Course authoring.";
        return false;
    }
    if (overview_->Projection().Settings().mode ==
        CourseOverviewMapProjectionMode::RailUnwrapped) {
        state_.message = "Rail Sketch is disabled in Rail Unwrapped view; use Top, Side, or Free.";
        return false;
    }
    sourceCourse_ = *rail_->Course();
    previewCourse_ = sourceCourse_;
    expectedRevision_ = rail_->State().mutationRevision;
    const auto& points = rail_->Model()->RuntimePath().ControlPoints();
    RailPathControlPoint anchor{};
    if (state_.mode == CourseRailSketchMode::Append) {
        anchor = points.back();
    } else if (state_.mode == CourseRailSketchMode::Prepend) {
        anchor = points.front();
    } else {
        if (!input.hovered.hit ||
            input.hovered.kind != CourseOverviewMapItemKind::RailSegment) {
            state_.message = "Replace Segment mode must begin on a rail segment.";
            return false;
        }
        const CourseRailSegment* segment = rail_->Model()->FindSegment(input.hovered.guid);
        if (segment == nullptr) return false;
        replaceSegmentIndex_ = segment->pointIndex;
        state_.targetSegmentGuid = segment->guid;
        anchor = points[replaceSegmentIndex_];
    }
    const auto projectedAnchor = overview_->Projection().ProjectWorld(anchor.position);
    if (!projectedAnchor.valid) return false;
    drawingDepth_ = projectedAnchor.depth;
    mapSamples_.push_back(projectedAnchor.mapPosition);
    worldSamples_.push_back(anchor.position);
    state_.drawing = true;
    Sample(input.mapPosition, true);
    state_.message = "Rail stroke started; release fits and commits one ReplaceRail transaction.";
    return true;
}

void CourseRailSketchTool::Sample(Vector2 mapPosition, bool force) {
    if (!state_.drawing) return;
    const float threshold = settings_.minimumSamplePixels * settings_.minimumSamplePixels;
    if (!mapSamples_.empty()) {
        const float screenDistance = SquaredDistance(mapSamples_.back(), mapPosition);
        if (screenDistance <= 0.000001f || (!force && screenDistance < threshold)) return;
    }
    Vector3 world = overview_->Projection().Unproject(mapPosition, drawingDepth_);
    if (!worldSamples_.empty() && SamePosition(worldSamples_.back(), world)) return;
    mapSamples_.push_back(mapPosition);
    worldSamples_.push_back(world);
    state_.rawSamples = static_cast<uint32_t>(worldSamples_.size());
    RefreshPreview(false);
}

void CourseRailSketchTool::RefreshPreview(bool finalSample) {
    if (worldSamples_.size() < 2) return;
    std::vector<Vector3> fittingSamples = worldSamples_;
    if (state_.mode == CourseRailSketchMode::ReplaceSegment) {
        const Vector3 end = sourceCourse_.railPoints[replaceSegmentIndex_ + 1].position;
        if (!SamePosition(fittingSamples.back(), end)) fittingSamples.push_back(end);
    }
    const RailPathControlPoint& sourceStyle = state_.mode == CourseRailSketchMode::Append
        ? sourceCourse_.railPoints.back()
        : state_.mode == CourseRailSketchMode::Prepend
            ? sourceCourse_.railPoints.front()
            : sourceCourse_.railPoints[replaceSegmentIndex_];
    lastFit_ = curveFit_->Fit(fittingSamples, settings_.curveFit,
        sourceStyle.corridorRadius, sourceStyle.speed);
    state_.fittedControlPoints = static_cast<uint32_t>(lastFit_.controlPoints.size());
    state_.fittedLength = lastFit_.fittedLength;
    state_.minimumTurnRadius = lastFit_.minimumObservedRadius;
    if (!lastFit_.succeeded) {
        state_.previewValid = false;
        overview_->ClearPreviewCourse();
        if (finalSample) state_.message = lastFit_.message;
        return;
    }
    previewCourse_ = sourceCourse_;
    previewCourse_.railPoints = BuildReplacement(lastFit_.controlPoints);
    CourseRailAuthoringModel::EnsureStableIdentity(
        previewCourse_, rail_->State().courseIdentity + ":sketch-preview");
    const CourseRailAuthoringModel before(sourceCourse_);
    ReprojectAnchors(before);
    const CourseRailAuthoringModel previewRail(previewCourse_);
    const CourseEnemyAuthoringModel previewEnemies(previewCourse_);
    state_.previewValid = previewRail.IsValid() && previewEnemies.IsValid();
    if (state_.previewValid) overview_->SetPreviewCourse(&previewCourse_);
    else overview_->ClearPreviewCourse();
}

std::vector<RailPathControlPoint> CourseRailSketchTool::BuildReplacement(
    const std::vector<RailPathControlPoint>& fittedInput) const {
    std::vector<RailPathControlPoint> fitted = fittedInput;
    if (fitted.size() < 2) return sourceCourse_.railPoints;
    if (state_.mode == CourseRailSketchMode::Append) {
        fitted.front() = sourceCourse_.railPoints.back();
        std::vector<RailPathControlPoint> result = sourceCourse_.railPoints;
        result.insert(result.end(), fitted.begin() + 1, fitted.end());
        return result;
    }
    if (state_.mode == CourseRailSketchMode::Prepend) {
        fitted.front() = sourceCourse_.railPoints.front();
        std::reverse(fitted.begin(), fitted.end());
        std::vector<RailPathControlPoint> result = fitted;
        result.insert(result.end(), sourceCourse_.railPoints.begin() + 1,
            sourceCourse_.railPoints.end());
        return result;
    }
    fitted.front() = sourceCourse_.railPoints[replaceSegmentIndex_];
    fitted.back() = sourceCourse_.railPoints[replaceSegmentIndex_ + 1];
    std::vector<RailPathControlPoint> result;
    result.insert(result.end(), sourceCourse_.railPoints.begin(),
        sourceCourse_.railPoints.begin() + replaceSegmentIndex_ + 1);
    if (fitted.size() > 2) result.insert(result.end(), fitted.begin() + 1, fitted.end() - 1);
    result.insert(result.end(), sourceCourse_.railPoints.begin() + replaceSegmentIndex_ + 1,
        sourceCourse_.railPoints.end());
    return result;
}

void CourseRailSketchTool::ReprojectAnchors(
    const CourseRailAuthoringModel& before) {
    CourseAsset geometry = previewCourse_;
    geometry.railAnchors.clear();
    geometry.enemyPlacements.clear();
    const CourseRailAuthoringModel after(geometry);
    if (!before.IsValid() || !after.IsValid()) return;
    for (CourseRailAnchorBinding& binding : previewCourse_.railAnchors) {
        const RailAnchorResolution old = before.Resolve(binding.anchor);
        if (!old.valid) continue;
        const RailAnchorProjection projected = after.Project(old.worldPosition, 64);
        if (projected.valid) binding.anchor = projected.anchor;
    }
    for (CourseEnemyPlacement& placement : previewCourse_.enemyPlacements) {
        const RailAnchorResolution old = before.Resolve(placement.railAnchor);
        if (!old.valid) continue;
        const RailAnchorProjection projected = after.Project(old.worldPosition, 64);
        if (projected.valid) placement.railAnchor = projected.anchor;
    }
}

void CourseRailSketchTool::Commit() {
    if (!state_.previewValid || previewCourse_.railPoints.size() < 2) {
        Cancel(lastFit_.message.empty()
            ? "Rail stroke preview is invalid and was not committed."
            : lastFit_.message);
        return;
    }
    const std::vector<RailPathControlPoint> replacement = previewCourse_.railPoints;
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::ReplaceRail;
    request.expectedRevision = expectedRevision_;
    request.replacementPoints = replacement;
    request.reprojectOrphanedAnchors = settings_.reprojectOrphanedAnchors;
    request.label = std::string("Sketch Rail: ") + ToString(state_.mode);
    const CourseRailSketchMode completedMode = state_.mode;
    if (overview_ != nullptr) overview_->ClearPreviewCourse();
    state_.drawing = false;
    const CourseRailMutationResult result = rail_->Mutate(request);
    state_.message = result.message;
    state_.previewValid = false;
    if (!result.succeeded) return;
    ++state_.editRevision;
    state_.mode = completedMode;
    SelectResultPoint();
    mapSamples_.clear();
    worldSamples_.clear();
}

void CourseRailSketchTool::SelectResultPoint() {
    if (selection_ == nullptr || rail_->Model() == nullptr ||
        rail_->Model()->RuntimePath().ControlPoints().empty()) return;
    const auto& points = rail_->Model()->RuntimePath().ControlPoints();
    uint32_t index = state_.mode == CourseRailSketchMode::Prepend
        ? 0u : static_cast<uint32_t>(points.size() - 1u);
    if (state_.mode == CourseRailSketchMode::ReplaceSegment) {
        index = (std::min)(replaceSegmentIndex_, static_cast<uint32_t>(points.size() - 1u));
    }
    selection_->SetPrimary({EditorDomainId::CourseRailControlPoint,
        "course-rail-point:" + points[index].editorGuid,
        index, rail_->State().bindingGeneration, "Rail Control Point"});
}

const char* ToString(CourseRailSketchMode mode) {
    switch (mode) {
    case CourseRailSketchMode::Append: return "Append";
    case CourseRailSketchMode::Prepend: return "Prepend";
    case CourseRailSketchMode::ReplaceSegment: return "Replace Segment";
    }
    return "Unknown";
}

} // namespace editor
