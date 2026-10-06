#include "CourseOverviewMapController.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace editor {
namespace {

float DistanceSquared(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

uint64_t HashValue(uint64_t hash, uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ull;
}

uint64_t HashFloat(uint64_t hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return HashValue(hash, bits);
}

uint64_t RendererSignature(const CourseOverviewMapStyle& style) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, style.railColor);
    hash = HashValue(hash, style.railPointColor);
    hash = HashValue(hash, style.enemyColor);
    hash = HashValue(hash, style.waveColor);
    hash = HashValue(hash, style.selectedColor);
    hash = HashValue(hash, style.disabledColor);
    hash = HashValue(hash, style.lockedColor);
    hash = HashValue(hash, style.prewarmColor);
    hash = HashValue(hash, style.transitionColor);
    hash = HashValue(hash, style.playheadColor);
    hash = HashFloat(hash, style.railThickness);
    hash = HashFloat(hash, style.pointRadius);
    hash = HashFloat(hash, style.enemyRadius);
    hash = HashFloat(hash, style.waveRadius);
    hash = HashValue(hash, style.samplesPerSegment);
    hash = HashValue(hash, style.showLabels);
    hash = HashValue(hash, style.showDisabled);
    hash = HashValue(hash, style.showTransitions);
    hash = HashValue(hash, style.showPrewarm);
    return hash;
}

bool SameRect(const CourseOverviewMapRect& lhs, const CourseOverviewMapRect& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width &&
        lhs.height == rhs.height;
}

bool SameProjection(
    const CourseOverviewMapProjectionSettings& lhs,
    const CourseOverviewMapProjectionSettings& rhs) {
    return lhs.mode == rhs.mode && lhs.zoom == rhs.zoom &&
        lhs.panPixels.x == rhs.panPixels.x && lhs.panPixels.y == rhs.panPixels.y &&
        lhs.paddingPixels == rhs.paddingPixels &&
        lhs.freeYawRadians == rhs.freeYawRadians &&
        lhs.freePitchRadians == rhs.freePitchRadians &&
        lhs.fitSamplesPerSegment == rhs.fitSamplesPerSegment;
}

} // namespace

bool CourseOverviewMapController::Bind(
    CourseOverviewMapControllerBinding binding,
    std::string* errorMessage) {
    if (state_.bound && binding.rail == binding_.rail &&
        binding.enemies == binding_.enemies && binding.waves == binding_.waves &&
        binding.selection == binding_.selection && binding.preview == binding_.preview) {
        return ValidateBinding(errorMessage);
    }
    ClearPreviewCourse();
    frame_ = {};
    visibility_.Invalidate();
    playheadOverlay_ = {};
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
    overlapCycle_ = 0;
    lastPickStableId_.clear();
    binding_ = binding;
    if (!ValidateBinding(errorMessage)) {
        state_ = {};
        state_.message = errorMessage != nullptr ? *errorMessage : "Invalid Overview Map binding.";
        binding_ = {};
        return false;
    }
    state_ = {};
    state_.bound = true;
    state_.mode = projectionSettings_.mode;
    return true;
}

void CourseOverviewMapController::Unbind() {
    binding_ = {};
    state_ = {};
    frame_ = {};
    visibility_.Invalidate();
    playheadOverlay_ = {};
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
    ClearPreviewCourse();
    overlapCycle_ = 0;
    lastPickStableId_.clear();
    sceneFitPoints_ = nullptr;
    sceneBoundsRevision_ = 0;
    interactivePanActive_ = false;
    presentationOffset_ = {};
}

void CourseOverviewMapController::BeginInteractivePan() noexcept {
    if (interactivePanActive_) return;
    interactivePanActive_ = true;
    presentationOffset_ = {};
}

bool CourseOverviewMapController::Synchronize(std::string* errorMessage) {
    if (!ValidateBinding(errorMessage)) {
        state_.valid = false;
        state_.message = errorMessage != nullptr ? *errorMessage : "Invalid Overview Map binding.";
        return false;
    }
    return true;
}

void CourseOverviewMapController::SetViewport(CourseOverviewMapRect rect) {
    if (SameRect(viewport_, rect)) return;
    viewport_ = rect;
    ++viewportRevision_;
    InvalidateFrameCache();
}

void CourseOverviewMapController::SetSceneFitPoints(
    const std::vector<Vector3>* fitPoints,
    uint64_t revision) {
    if (sceneFitPoints_ == fitPoints && sceneBoundsRevision_ == revision) return;
    sceneFitPoints_ = fitPoints;
    sceneBoundsRevision_ = revision;
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseOverviewMapController::ConfigureProjection(const CourseOverviewMapProjectionSettings& settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameProjection(settings, projectionSettings_)) return true;
    EndInteractivePan();
    projectionSettings_ = settings;
    state_.mode = settings.mode;
    ++viewportRevision_;
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
    return true;
}

void CourseOverviewMapController::SetMode(CourseOverviewMapProjectionMode mode) {
    if (mode > CourseOverviewMapProjectionMode::Free || projectionSettings_.mode == mode) return;
    projectionSettings_.mode = mode;
    projectionSettings_.panPixels = {};
    projectionSettings_.zoom = 1.0f;
    state_.mode = mode;
    ++viewportRevision_;
    InvalidateFrameCache();
}

void CourseOverviewMapController::FrameAll() {
    if (projectionSettings_.panPixels.x == 0.0f &&
        projectionSettings_.panPixels.y == 0.0f && projectionSettings_.zoom == 1.0f) return;
    projectionSettings_.panPixels = {};
    projectionSettings_.zoom = 1.0f;
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseOverviewMapController::FrameMapPoints(
    const std::vector<Vector2>& mapPoints,
    float minimumZoom,
    float paddingPixels) {
    if (!projection_.State().valid || mapPoints.empty() || !viewport_.Valid()) {
        return false;
    }
    Vector2 rawMinimum = projection_.MapToRaw(mapPoints.front());
    Vector2 rawMaximum = rawMinimum;
    for (std::size_t index = 1; index < mapPoints.size(); ++index) {
        const Vector2 raw = projection_.MapToRaw(mapPoints[index]);
        rawMinimum.x = (std::min)(rawMinimum.x, raw.x);
        rawMinimum.y = (std::min)(rawMinimum.y, raw.y);
        rawMaximum.x = (std::max)(rawMaximum.x, raw.x);
        rawMaximum.y = (std::max)(rawMaximum.y, raw.y);
    }
    const Vector2 rawCenter{(rawMinimum.x + rawMaximum.x) * 0.5f,
        (rawMinimum.y + rawMaximum.y) * 0.5f};
    float targetZoom = (std::max)(projectionSettings_.zoom, minimumZoom);
    if (mapPoints.size() > 1u) {
        const float width = (std::max)(0.001f, rawMaximum.x - rawMinimum.x);
        const float height = (std::max)(0.001f, rawMaximum.y - rawMinimum.y);
        const float availableWidth = (std::max)(1.0f,
            viewport_.width - paddingPixels * 2.0f);
        const float availableHeight = (std::max)(1.0f,
            viewport_.height - paddingPixels * 2.0f);
        const float desiredScale = (std::min)(
            availableWidth / width, availableHeight / height);
        targetZoom = (std::clamp)(desiredScale /
            (std::max)(0.000001f, projection_.State().baseScale),
            0.05f, 64.0f);
    }
    projectionSettings_.zoom = (std::clamp)(targetZoom, 0.05f, 64.0f);
    const Vector2 viewportCenter{viewport_.x + viewport_.width * 0.5f,
        viewport_.y + viewport_.height * 0.5f};
    // RawToMap used the old scale. Reconfigure a lightweight projection once
    // to center accurately at the new zoom; the retained frame is rebuilt by
    // the caller exactly once afterwards.
    const CourseRailAuthoringModel* rail = previewRail_.has_value()
        ? &*previewRail_ : binding_.rail->Model();
    const CourseRailAuthoringModel* boundsRail = previewRail_.has_value()
        ? binding_.rail->Model() : rail;
    std::string ignored;
    if (!projection_.Configure(rail, viewport_, projectionSettings_, &ignored,
            boundsRail, sceneFitPoints_)) {
        return false;
    }
    const Vector2 projectedCenter = projection_.RawToMap(rawCenter);
    projectionSettings_.panPixels.x += viewportCenter.x - projectedCenter.x;
    projectionSettings_.panPixels.y += viewportCenter.y - projectedCenter.y;
    ++viewportRevision_;
    InvalidateFrameCache();
    InvalidatePlayheadOverlay();
    return true;
}

void CourseOverviewMapController::PanPixels(Vector2 delta) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(projectionSettings_.panPixels.x + delta.x) ||
        !std::isfinite(projectionSettings_.panPixels.y + delta.y) ||
        (delta.x == 0.0f && delta.y == 0.0f)) return;
    projectionSettings_.panPixels.x += delta.x;
    projectionSettings_.panPixels.y += delta.y;
    if (interactivePanActive_) {
        presentationOffset_.x += delta.x;
        presentationOffset_.y += delta.y;
        return;
    }
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseOverviewMapController::EndInteractivePan() noexcept {
    if (!interactivePanActive_) return false;
    interactivePanActive_ = false;
    const bool changed = presentationOffset_.x != 0.0f ||
        presentationOffset_.y != 0.0f;
    presentationOffset_ = {};
    if (changed) {
        ++viewportRevision_;
        InvalidateFrameCache();
        InvalidatePlayheadOverlay();
    }
    return changed;
}

void CourseOverviewMapController::ZoomAt(Vector2 mapPosition, float factor) {
    if (!projection_.State().valid || !std::isfinite(factor) || factor <= 0.0f ||
        !std::isfinite(mapPosition.x) || !std::isfinite(mapPosition.y)) return;
    const Vector2 rawBefore = projection_.MapToRaw(mapPosition);
    projectionSettings_.zoom = (std::clamp)(projectionSettings_.zoom * factor, 0.05f, 64.0f);
    std::string ignored;
    if (!projection_.Configure(binding_.rail->Model(), viewport_, projectionSettings_,
            &ignored, nullptr, sceneFitPoints_)) return;
    const Vector2 mapAfter = projection_.RawToMap(rawBefore);
    projectionSettings_.panPixels.x += mapPosition.x - mapAfter.x;
    projectionSettings_.panPixels.y += mapPosition.y - mapAfter.y;
    ++viewportRevision_;
    InvalidateFrameCache();
}

bool CourseOverviewMapController::Rebuild(
    float fallbackPlayheadDistance,
    std::string* errorMessage) {
    if (!Synchronize(errorMessage)) {
        InvalidateFrameCache();
        InvalidatePlayheadOverlay();
        return false;
    }
    const float playheadDistance = ResolvePlayheadDistance(fallbackPlayheadDistance);
    if (interactivePanActive_ && frame_.valid) {
        // A pan gesture presents the immutable previous frame with a screen
        // offset. Even an accidental Rebuild call must not enter projection,
        // visibility or label work until the gesture commits.
        ++state_.frameCacheHits;
        RefreshPlayheadOverlay(playheadDistance);
        return true;
    }
    const RetainedFrameKey key = BuildFrameKey();
    if (retainedFrameKey_.has_value() && frame_.valid &&
        SameFrameKey(*retainedFrameKey_, key)) {
        ++state_.frameCacheHits;
        RefreshPlayheadOverlay(playheadDistance);
        return true;
    }
    const CourseRailAuthoringModel* rail = previewRail_.has_value()
        ? &*previewRail_ : binding_.rail->Model();
    const CourseEnemyAuthoringModel* enemies = previewEnemies_.has_value()
        ? &*previewEnemies_
        : (binding_.enemies != nullptr ? binding_.enemies->Model() : nullptr);
    const CourseWaveAuthoringModel* waves = previewWaves_.has_value()
        ? &*previewWaves_
        : (binding_.waves != nullptr ? binding_.waves->Model() : nullptr);
    // Direct manipulation must not change the map transform underneath the
    // pointer. Fit the projection to the canonical rail while rendering the
    // transient preview rail through that stable transform.
    const CourseRailAuthoringModel* projectionRail = previewRail_.has_value()
        ? binding_.rail->Model() : rail;
    if (!projection_.Configure(
            rail, viewport_, projectionSettings_, errorMessage, projectionRail,
            sceneFitPoints_)) {
        state_.valid = false;
        state_.message = errorMessage != nullptr ? *errorMessage : "Overview Map projection failed.";
        frame_ = {};
        visibility_.Invalidate();
        playheadOverlay_ = {};
        InvalidateFrameCache();
        InvalidatePlayheadOverlay();
        return false;
    }
    CourseOverviewMapRenderInput input{};
    input.projection = &projection_;
    input.rail = rail;
    input.enemies = enemies;
    input.waves = waves;
    input.selection = binding_.selection;
    input.playheadDistance = -1.0f;
    input.railGeneration = binding_.rail->State().bindingGeneration;
    if (binding_.enemies != nullptr) input.enemyGeneration = binding_.enemies->State().bindingGeneration;
    if (binding_.waves != nullptr) input.waveGeneration = binding_.waves->State().bindingGeneration;
    frame_ = renderer_.BuildStatic(input);
    state_.valid = frame_.valid;
    state_.message = frame_.message;
    state_.mode = projectionSettings_.mode;
    retainedFrameKey_ = key;
    ++state_.frameRevision;
    visibility_.Build(frame_, state_.frameRevision);
    RefreshPlayheadOverlay(playheadDistance);
    return state_.valid;
}

void CourseOverviewMapController::SetPreviewCourse(const CourseAsset* previewCourse) {
    if (previewCourse == nullptr) {
        ClearPreviewCourse();
        return;
    }
    previewRail_.emplace(*previewCourse);
    previewEnemies_.emplace(*previewCourse);
    previewWaves_.emplace(*previewCourse);
    ++previewRevision_;
    InvalidateFrameCache();
}

void CourseOverviewMapController::ClearPreviewCourse() {
    const bool hadPreview = previewRail_.has_value() || previewEnemies_.has_value() ||
        previewWaves_.has_value();
    previewWaves_.reset();
    previewEnemies_.reset();
    previewRail_.reset();
    if (hadPreview) {
        ++previewRevision_;
        InvalidateFrameCache();
    }
}

CourseOverviewMapController::RetainedFrameKey
CourseOverviewMapController::BuildFrameKey() const {
    RetainedFrameKey key{};
    key.railRevision = binding_.rail->State().mutationRevision;
    key.railGeneration = binding_.rail->State().bindingGeneration;
    key.hasEnemies = binding_.enemies != nullptr;
    key.hasWaves = binding_.waves != nullptr;
    if (binding_.enemies != nullptr) {
        key.enemyRevision = binding_.enemies->State().mutationRevision;
        key.enemyGeneration = binding_.enemies->State().bindingGeneration;
    }
    if (binding_.waves != nullptr) {
        key.waveRevision = binding_.waves->State().mutationRevision;
        key.waveGeneration = binding_.waves->State().bindingGeneration;
    }
    key.selectionRevision = binding_.selection->Revision();
    key.previewRevision = previewRevision_;
    key.rendererSignature = RendererSignature(renderer_.Style());
    key.viewportRevision = viewportRevision_;
    key.visibilitySettingsRevision = visibility_.SettingsRevision();
    key.sceneBoundsRevision = sceneBoundsRevision_;
    key.viewport = viewport_;
    key.projection = projectionSettings_;
    return key;
}

bool CourseOverviewMapController::SameFrameKey(
    const RetainedFrameKey& lhs,
    const RetainedFrameKey& rhs) noexcept {
    return lhs.railRevision == rhs.railRevision &&
        lhs.enemyRevision == rhs.enemyRevision && lhs.waveRevision == rhs.waveRevision &&
        lhs.previewRevision == rhs.previewRevision &&
        lhs.rendererSignature == rhs.rendererSignature &&
        lhs.viewportRevision == rhs.viewportRevision &&
        lhs.visibilitySettingsRevision == rhs.visibilitySettingsRevision &&
        lhs.sceneBoundsRevision == rhs.sceneBoundsRevision &&
        lhs.railGeneration == rhs.railGeneration &&
        lhs.enemyGeneration == rhs.enemyGeneration &&
        lhs.waveGeneration == rhs.waveGeneration &&
        lhs.selectionRevision == rhs.selectionRevision &&
        SameRect(lhs.viewport, rhs.viewport) &&
        SameProjection(lhs.projection, rhs.projection) &&
        lhs.hasEnemies == rhs.hasEnemies && lhs.hasWaves == rhs.hasWaves;
}

void CourseOverviewMapController::InvalidateFrameCache() noexcept {
    retainedFrameKey_.reset();
}

void CourseOverviewMapController::RefreshPlayheadOverlay(float playheadDistance) {
    if (!frame_.valid || !projection_.State().valid) {
        playheadOverlay_ = {};
        InvalidatePlayheadOverlay();
        return;
    }
    const PlayheadOverlayKey key{
        state_.frameRevision,
        RendererSignature(renderer_.Style()),
        playheadDistance};
    if (playheadOverlayKey_.has_value() && playheadOverlay_.valid &&
        playheadOverlayKey_->staticFrameRevision == key.staticFrameRevision &&
        playheadOverlayKey_->rendererSignature == key.rendererSignature &&
        playheadOverlayKey_->playheadDistance == key.playheadDistance) {
        ++state_.playheadOverlayCacheHits;
        return;
    }
    playheadOverlay_ = renderer_.BuildPlayheadOverlay(projection_, playheadDistance);
    playheadOverlay_.revision = ++state_.playheadOverlayRevision;
    playheadOverlayKey_ = key;
}

void CourseOverviewMapController::InvalidatePlayheadOverlay() noexcept {
    playheadOverlayKey_.reset();
}

CourseOverviewMapPickResult CourseOverviewMapController::HoverAt(Vector2 mapPosition) {
    const Vector2 retainedPosition = interactivePanActive_
        ? Vector2{mapPosition.x - presentationOffset_.x,
            mapPosition.y - presentationOffset_.y}
        : mapPosition;
    state_.hovered = picking_.Pick(frame_, retainedPosition);
    return state_.hovered;
}

CourseOverviewMapPickResult CourseOverviewMapController::SelectAt(
    Vector2 mapPosition,
    bool additive,
    bool toggle,
    bool cycleOverlaps) {
    if (binding_.selection == nullptr) return {};
    const Vector2 retainedPosition = interactivePanActive_
        ? Vector2{mapPosition.x - presentationOffset_.x,
            mapPosition.y - presentationOffset_.y}
        : mapPosition;
    const auto candidates = picking_.PickAll(frame_, retainedPosition);
    if (candidates.empty()) {
        if (!additive && !toggle) binding_.selection->Clear();
        overlapCycle_ = 0;
        lastPickStableId_.clear();
        return {};
    }
    if (cycleOverlaps && DistanceSquared(lastPickPosition_, mapPosition) <= 16.0f &&
        lastPickStableId_ == candidates.front().handle.stableId) {
        ++overlapCycle_;
    } else {
        overlapCycle_ = 0;
    }
    const CourseOverviewMapPickResult pick = candidates[overlapCycle_ % candidates.size()];
    if (toggle) binding_.selection->Toggle(pick.handle);
    else if (additive) binding_.selection->Add(pick.handle);
    else binding_.selection->SetPrimary(pick.handle);
    lastPickPosition_ = mapPosition;
    lastPickStableId_ = candidates.front().handle.stableId;
    state_.hovered = pick;
    return pick;
}

bool CourseOverviewMapController::ValidateBinding(std::string* errorMessage) const {
    const auto fail = [&](const char* message) {
        if (errorMessage != nullptr) *errorMessage = message;
        return false;
    };
    if (binding_.rail == nullptr || binding_.selection == nullptr) {
        return fail("Overview Map requires Rail controller and EditorSelection.");
    }
    if (binding_.rail->Course() == nullptr || binding_.rail->Model() == nullptr) {
        return fail("Overview Map Rail controller is not ready.");
    }
    const CourseAsset* course = binding_.rail->Course();
    if (binding_.enemies != nullptr && binding_.enemies->Course() != course) {
        return fail("Overview Map controllers must reference the same CourseAsset.");
    }
    if (binding_.waves != nullptr && binding_.waves->Course() != course) {
        return fail("Overview Map controllers must reference the same CourseAsset.");
    }
    return true;
}

float CourseOverviewMapController::ResolvePlayheadDistance(float fallback) const {
    if (binding_.preview != nullptr && binding_.preview->HasSnapshot()) {
        return binding_.preview->Frame().distance;
    }
    return fallback;
}

} // namespace editor
