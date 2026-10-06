#include "CourseMap3DViewportController.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 Scale(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(Vector3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
bool SamePosition(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y < 1.0f;
}

bool SameRect(CourseOverviewMapRect a, CourseOverviewMapRect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

bool CourseMap3DCamera::Validate(std::string* errorMessage) const {
    const bool valid = std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z) &&
        std::isfinite(yawRadians) && std::isfinite(pitchRadians) && std::abs(pitchRadians) <= 1.51843645f &&
        std::isfinite(distance) && distance >= 2.0f && distance <= 200000.0f &&
        std::isfinite(verticalFovRadians) && verticalFovRadians >= 0.01f && verticalFovRadians <= 3.13f &&
        std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0.0f && farPlane > nearPlane;
    if (errorMessage != nullptr) *errorMessage = valid ? "" : "Map camera requires finite values, distance 2..200000, safe FOV/pitch and 0 < near < far.";
    return valid;
}

bool CourseMap3DViewportController::ConfigureCamera(const CourseMap3DCamera& camera, std::string* errorMessage) {
    if (!camera.Validate(errorMessage)) return false;
    camera_ = camera;
    TouchCamera();
    return true;
}

bool CourseMap3DViewportController::Bind(CourseMap3DViewportBinding binding,
    std::string* errorMessage) {
    if (binding_.rail == binding.rail && binding_.enemies == binding.enemies &&
        binding_.waves == binding.waves && binding_.selection == binding.selection &&
        state_.bound) return ValidateBinding(errorMessage);
    Unbind();
    binding_ = binding;
    state_.bound = binding_.rail != nullptr;
    if (!ValidateBinding(errorMessage)) return false;
    state_.valid = true;
    state_.message = "Course Map 3D viewport ready.";
    FrameAll();
    return true;
}

void CourseMap3DViewportController::Unbind() {
    const bool active = state_.active;
    binding_ = {};
    state_ = {};
    state_.active = active;
    frame_ = {};
    dynamicOverlay_ = {};
    frameKey_.reset();
    overlaySourceFrameRevision_ = 0;
    overlayRailDistance_ = -1.0f;
    overlapCycle_ = 0;
    lastPickStableId_.clear();
}

void CourseMap3DViewportController::SetActive(bool active) noexcept {
    state_.active = active;
    state_.hovered = {};
}

void CourseMap3DViewportController::SetViewport(CourseOverviewMapRect viewport) {
    if (SameRect(viewport_, viewport)) return;
    viewport_ = viewport;
    frameKey_.reset();
}

void CourseMap3DViewportController::Orbit(Vector2 deltaPixels) {
    if (!std::isfinite(deltaPixels.x) || !std::isfinite(deltaPixels.y)) return;
    camera_.yawRadians = std::remainder(camera_.yawRadians + deltaPixels.x * 0.006f, 6.283185307f);
    camera_.pitchRadians = (std::clamp)(camera_.pitchRadians + deltaPixels.y * 0.006f,
        -1.51843645f, 1.51843645f);
    TouchCamera();
}

void CourseMap3DViewportController::Pan(Vector2 deltaPixels) {
    if (!viewport_.Valid() || !std::isfinite(deltaPixels.x) || !std::isfinite(deltaPixels.y)) return;
    const CourseMap3DCameraBasis basis = BuildCourseMap3DCameraBasis(camera_);
    const float worldPerPixel = 2.0f * camera_.distance *
        std::tan(camera_.verticalFovRadians * 0.5f) / viewport_.height;
    CourseMap3DCamera candidate = camera_;
    candidate.target = Add(camera_.target, Add(
        Scale(basis.right, -deltaPixels.x * worldPerPixel),
        Scale(basis.up, deltaPixels.y * worldPerPixel)));
    (void)ConfigureCamera(candidate);
}

void CourseMap3DViewportController::Dolly(float wheelSteps) {
    if (!std::isfinite(wheelSteps) || wheelSteps == 0.0f) return;
    camera_.distance = (std::clamp)(camera_.distance *
        std::pow(0.86f, wheelSteps), 2.0f, 200000.0f);
    TouchCamera();
}

void CourseMap3DViewportController::FrameAll(const CourseTerrainMapAsset* terrain,
    const CourseMapSceneVisualizationFrame* sceneVisualization) {
    if (binding_.rail == nullptr || binding_.rail->Model() == nullptr ||
        !binding_.rail->Model()->IsValid()) return;
    const float maximum = (std::numeric_limits<float>::max)();
    Vector3 minimum{maximum, maximum, maximum};
    Vector3 maximumPoint{-maximum, -maximum, -maximum};
    const auto include = [&](Vector3 point) {
        minimum.x = (std::min)(minimum.x, point.x);
        minimum.y = (std::min)(minimum.y, point.y);
        minimum.z = (std::min)(minimum.z, point.z);
        maximumPoint.x = (std::max)(maximumPoint.x, point.x);
        maximumPoint.y = (std::max)(maximumPoint.y, point.y);
        maximumPoint.z = (std::max)(maximumPoint.z, point.z);
    };
    const RailPath& path = binding_.rail->Model()->RuntimePath();
    const uint32_t sampleCount = (std::max)(2u,
        static_cast<uint32_t>(path.SegmentCount()) * 16u + 1u);
    for (uint32_t index = 0; index < sampleCount; ++index) {
        include(path.Evaluate(binding_.rail->Model()->Length() *
            static_cast<float>(index) / static_cast<float>(sampleCount - 1u)).position);
    }
    if (terrain != nullptr && !terrain->Empty()) {
        include(terrain->worldMinimum);
        include(terrain->worldMaximum);
    }
    if (sceneVisualization != nullptr && sceneVisualization->valid) {
        for (const CourseMapScreenSpaceProxy& proxy :
            sceneVisualization->screenSpaceProxies) include(proxy.worldPosition);
        for (const CourseMapSceneActorProxy& actor :
            sceneVisualization->actors) include(actor.worldPosition);
    }
    camera_.target = Scale(Add(minimum, maximumPoint), 0.5f);
    const float radius = (std::max)(10.0f, Length(Sub(maximumPoint, minimum)) * 0.5f);
    camera_.distance = (std::clamp)(radius /
        std::tan(camera_.verticalFovRadians * 0.5f) * 1.2f, 10.0f, 200000.0f);
    TouchCamera();
}

bool CourseMap3DViewportController::FrameSelected() {
    const std::optional<Vector3> selected = SelectedWorldPosition();
    if (!selected.has_value()) return false;
    camera_.target = *selected;
    camera_.distance = (std::max)(20.0f, camera_.distance * 0.35f);
    TouchCamera();
    return true;
}

bool CourseMap3DViewportController::Rebuild(const CourseTerrainMapAsset* terrain,
    const CourseMapSceneVisualizationFrame* sceneVisualization,
    std::string* errorMessage) {
    if (!ValidateBinding(errorMessage) || !viewport_.Valid()) {
        frame_ = {};
        state_.valid = false;
        return false;
    }
    const FrameKey key{
        binding_.rail->State().mutationRevision,
        binding_.enemies != nullptr ? binding_.enemies->State().mutationRevision : 0u,
        binding_.waves != nullptr ? binding_.waves->State().mutationRevision : 0u,
        terrain != nullptr ? terrain->contentRevision : 0u,
        sceneVisualization != nullptr ? sceneVisualization->stats.builds : 0u,
        state_.cameraRevision, renderer_.SettingsRevision(),
        binding_.selection != nullptr ? binding_.selection->Revision() : 0u,
        viewport_};
    if (frameKey_.has_value() && SameKey(*frameKey_, key)) {
        ++state_.frameCacheHits;
        return frame_.valid;
    }
    frame_ = renderer_.Build({viewport_, camera_, binding_.rail->Model(),
        binding_.enemies != nullptr ? binding_.enemies->Model() : nullptr,
        binding_.waves != nullptr ? binding_.waves->Model() : nullptr,
        terrain, sceneVisualization, binding_.selection,
        binding_.rail->State().bindingGeneration,
        binding_.enemies != nullptr ? binding_.enemies->State().bindingGeneration : 0u,
        binding_.waves != nullptr ? binding_.waves->State().bindingGeneration : 0u});
    frameKey_ = key;
    ++state_.frameRevision;
    state_.valid = frame_.valid;
    state_.message = frame_.message;
    return frame_.valid;
}

void CourseMap3DViewportController::UpdatePlayhead(float railDistance) {
    if (!frame_.valid || binding_.rail == nullptr ||
        binding_.rail->Model() == nullptr) {
        dynamicOverlay_ = {};
        return;
    }
    const float clamped = (std::clamp)(railDistance, 0.0f,
        binding_.rail->Model()->Length());
    if (overlaySourceFrameRevision_ == state_.frameRevision &&
        overlayRailDistance_ == clamped) return;

    const RailPathSample sample =
        binding_.rail->Model()->RuntimePath().Evaluate(clamped);
    const CourseMap3DProjectedPoint projected = ProjectCourseMap3DPoint(
        sample.position, camera_, viewport_);
    CourseMap3DDynamicOverlay overlay{};
    overlay.revision = dynamicOverlay_.revision + 1u;
    overlay.railDistance = clamped;
    if (projected.valid) {
        const float headingLength = (std::max)(8.0f, camera_.distance * 0.04f);
        const Vector3 headingWorld = Add(sample.position,
            Scale(sample.tangent, headingLength));
        const CourseMap3DProjectedPoint heading = ProjectCourseMap3DPoint(
            headingWorld, camera_, viewport_);
        overlay.valid = true;
        overlay.player.kind = CourseOverviewMapItemKind::Playhead;
        overlay.player.screen = projected.screen;
        overlay.player.world = sample.position;
        overlay.player.depth = projected.depth;
        overlay.player.radiusPixels = 9.0f;
        overlay.player.worldRadius = 9.0f * 2.0f * projected.depth *
            std::tan(camera_.verticalFovRadians * 0.5f) /
            (std::max)(1.0f, viewport_.height);
        overlay.player.railDistance = clamped;
        overlay.player.color = 0xfffff06au;
        overlay.heading.kind = CourseOverviewMapItemKind::Playhead;
        overlay.heading.start = projected.screen;
        overlay.heading.end = projected.screen;
        overlay.heading.worldStart = sample.position;
        overlay.heading.worldEnd = sample.position;
        overlay.heading.startDepth = projected.depth;
        overlay.heading.endDepth = projected.depth;
        overlay.heading.color = 0xfffff06au;
        overlay.heading.thickness = 2.2f;
        if (heading.valid) {
            overlay.heading.end = heading.screen;
            overlay.heading.worldEnd = headingWorld;
            overlay.heading.endDepth = heading.depth;
        }
        overlay.label = {{projected.screen.x + 14.0f, projected.screen.y - 14.0f},
            projected.depth, 0xfffff5b0u,
            "Player  " + std::to_string(static_cast<int>(clamped)) + " m"};
    }
    dynamicOverlay_ = std::move(overlay);
    overlaySourceFrameRevision_ = state_.frameRevision;
    overlayRailDistance_ = clamped;
}

CourseMap3DPickResult CourseMap3DViewportController::HoverAt(
    Vector2 screenPosition) {
    state_.hovered = picking_.Pick(frame_, screenPosition);
    return state_.hovered;
}

CourseMap3DPickResult CourseMap3DViewportController::SelectAt(
    Vector2 screenPosition, bool additive, bool toggle, bool cycleOverlaps) {
    if (!cycleOverlaps || !SamePosition(lastPickPosition_, screenPosition)) {
        overlapCycle_ = 0;
    } else {
        ++overlapCycle_;
    }
    CourseMap3DPickResult picked = picking_.Pick(frame_, screenPosition,
        cycleOverlaps ? overlapCycle_ : 0u);
    lastPickPosition_ = screenPosition;
    lastPickStableId_ = picked.handle.stableId;
    if (binding_.selection == nullptr) return picked;
    if (!picked.hit) {
        if (!additive && !toggle) binding_.selection->Clear();
    } else if (toggle) {
        binding_.selection->Toggle(picked.handle);
    } else if (additive) {
        binding_.selection->Add(picked.handle);
    } else {
        binding_.selection->SetPrimary(picked.handle);
    }
    frameKey_.reset();
    state_.hovered = picked;
    return picked;
}

bool CourseMap3DViewportController::ValidateBinding(
    std::string* errorMessage) const {
    if (binding_.rail == nullptr || !binding_.rail->State().bound ||
        binding_.rail->Model() == nullptr || !binding_.rail->Model()->IsValid()) {
        if (errorMessage != nullptr) *errorMessage =
            "Course Map 3D viewport requires a valid rail controller.";
        return false;
    }
    return true;
}

std::optional<Vector3> CourseMap3DViewportController::SelectedWorldPosition() const {
    if (binding_.selection == nullptr || binding_.selection->Primary() == nullptr ||
        binding_.rail == nullptr || binding_.rail->Model() == nullptr) return std::nullopt;
    const EditorObjectHandle& selected = *binding_.selection->Primary();
    for (const CourseMap3DMarker& marker : frame_.markers) {
        if (marker.handle.SameObject(selected)) return marker.world;
    }
    if (selected.domain == EditorDomainId::CourseRailControlPoint) {
        const auto& points = binding_.rail->Model()->RuntimePath().ControlPoints();
        if (selected.localIndex < points.size()) return points[selected.localIndex].position;
    } else if (selected.domain == EditorDomainId::CourseRailSegment) {
        const auto& segments = binding_.rail->Model()->Segments();
        if (selected.localIndex < segments.size()) {
            return binding_.rail->Model()->RuntimePath().Evaluate(
                segments[selected.localIndex].startDistance +
                segments[selected.localIndex].length * 0.5f).position;
        }
    } else if (selected.domain == EditorDomainId::CourseEnemyPlacement &&
        binding_.enemies != nullptr && binding_.enemies->Model() != nullptr) {
        const auto& placements = binding_.enemies->Model()->Placements();
        if (selected.localIndex < placements.size()) {
            const auto resolution = binding_.enemies->Model()->Resolve(
                placements[selected.localIndex]);
            if (resolution.valid) return resolution.worldPosition;
        }
    } else if (selected.domain == EditorDomainId::CourseWaveDefinition &&
        binding_.waves != nullptr && binding_.waves->Model() != nullptr) {
        const auto& waves = binding_.waves->Model()->Waves();
        if (selected.localIndex < waves.size()) {
            return binding_.rail->Model()->RuntimePath().Evaluate(
                (std::clamp)(waves[selected.localIndex].triggerRailDistance,
                    0.0f, binding_.rail->Model()->Length())).position;
        }
    }
    return std::nullopt;
}

bool CourseMap3DViewportController::SameKey(
    const FrameKey& a, const FrameKey& b) noexcept {
    return a.railRevision == b.railRevision &&
        a.enemyRevision == b.enemyRevision && a.waveRevision == b.waveRevision &&
        a.terrainRevision == b.terrainRevision &&
        a.sceneVisualizationRevision == b.sceneVisualizationRevision &&
        a.cameraRevision == b.cameraRevision &&
        a.rendererRevision == b.rendererRevision &&
        a.selectionRevision == b.selectionRevision && SameRect(a.viewport, b.viewport);
}

void CourseMap3DViewportController::TouchCamera() noexcept {
    ++state_.cameraRevision;
    frameKey_.reset();
}

} // namespace editor
