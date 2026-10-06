#include "CourseOverviewMapEditTool.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

bool Different(Vector3 a, Vector3 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z > 0.000001f;
}

} // namespace

void CourseOverviewMapEditTool::Bind(
    CourseOverviewMapController* overview,
    CourseRailEditorController* rail,
    CourseEnemyEditorController* enemies,
    CourseWaveEditorController* waves,
    EditorSelection* selection,
    const CourseOverviewMapSnapService* snapping) {
    if (overview_ == overview && rail_ == rail && enemies_ == enemies &&
        waves_ == waves && selection_ == selection && snapping_ == snapping) return;
    Cancel();
    overview_ = overview;
    rail_ = rail;
    enemies_ = enemies;
    waves_ = waves;
    selection_ = selection;
    snapping_ = snapping;
}

void CourseOverviewMapEditTool::SetActive(bool active) {
    if (state_.active == active) return;
    Cancel();
    state_.active = active;
}

void CourseOverviewMapEditTool::SetMode(CourseOverviewMapEditMode mode) {
    if (state_.mode == mode) return;
    Cancel();
    state_.mode = mode;
    state_.message = std::string("Overview tool mode: ") + ToString(mode);
}

bool CourseOverviewMapEditSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(defaultWavePrewarmDistance, 0.0f, 100000.0f);
    return settings::Result(valid, errorMessage,
        "CourseOverviewMapEditSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseOverviewMapEditTool::SetSettings(
    CourseOverviewMapEditSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (settings.defaultActorAssetId.empty()) settings.defaultActorAssetId = "drone";
    if (settings.defaultWaveName.empty()) settings.defaultWaveName = "Wave";
    settings_ = std::move(settings);
    return true;
}

void CourseOverviewMapEditTool::Tick(const CourseOverviewMapEditInput& input) {
    state_.canMutate = CanMutate(input.hovered.kind);
    if (!state_.active || overview_ == nullptr || rail_ == nullptr || snapping_ == nullptr) {
        if (state_.dragging) Cancel("Overview edit cancelled because its document became unavailable.");
        return;
    }
    if (input.cancelPressed) Cancel("Overview drag cancelled.");
    if (state_.dragging) {
        if (input.primaryDown) UpdateDrag(input.mapPosition);
        if (input.primaryReleased) CommitDrag();
        return;
    }
    if (input.deletePressed && input.hovered.hit) {
        Delete(input.hovered);
        return;
    }
    if (!input.primaryPressed) return;
    switch (state_.mode) {
    case CourseOverviewMapEditMode::SelectMove:
        if (input.hovered.hit) BeginDrag(input.hovered);
        break;
    case CourseOverviewMapEditMode::AddRailPoint:
        AddRailPoint(input.mapPosition);
        break;
    case CourseOverviewMapEditMode::AddEnemy:
        AddEnemy(input.mapPosition);
        break;
    case CourseOverviewMapEditMode::AddWave:
        AddWave(input.mapPosition);
        break;
    case CourseOverviewMapEditMode::Delete:
        if (input.hovered.hit) Delete(input.hovered);
        else state_.message = "Delete mode requires a rail point, enemy or wave marker.";
        break;
    }
}

void CourseOverviewMapEditTool::Cancel(std::string message) {
    if (overview_ != nullptr) overview_->ClearPreviewCourse();
    state_.dragging = false;
    state_.previewValid = false;
    state_.dragKind = CourseOverviewMapItemKind::None;
    state_.dragGuid.clear();
    previewCourse_ = {};
    if (!message.empty()) state_.message = std::move(message);
}

bool CourseOverviewMapEditTool::CanMutate(CourseOverviewMapItemKind kind) const {
    if (kind == CourseOverviewMapItemKind::EnemyPlacement) {
        return enemies_ != nullptr && enemies_->State().bound &&
            enemies_->State().authoringAllowed &&
            enemies_->State().status == CourseEnemyEditorControllerStatus::Ready;
    }
    if (kind == CourseOverviewMapItemKind::Wave) {
        return waves_ != nullptr && waves_->State().bound &&
            waves_->State().authoringAllowed &&
            waves_->State().status == CourseWaveEditorControllerStatus::Ready;
    }
    return rail_ != nullptr && rail_->State().bound && rail_->State().authoringAllowed &&
        rail_->State().status == CourseRailEditorControllerStatus::Ready;
}

bool CourseOverviewMapEditTool::BeginDrag(const CourseOverviewMapPickResult& pick) {
    if (!CanMutate(pick.kind) || pick.handle.stableId.empty() || rail_->Course() == nullptr) {
        state_.message = "Selected Overview item is locked or read-only.";
        return false;
    }
    previewCourse_ = *rail_->Course();
    state_.dragKind = pick.kind;
    state_.dragGuid = pick.guid;
    dragDepth_ = overview_->Projection().ProjectWorld(pick.worldPosition).depth;
    if (pick.kind == CourseOverviewMapItemKind::RailControlPoint) {
        const auto found = std::find_if(previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
            [&pick](const RailPathControlPoint& point) { return point.editorGuid == pick.guid; });
        if (found == previewCourse_.railPoints.end()) return false;
        originalPoint_ = *found;
        expectedRevision_ = rail_->State().mutationRevision;
    } else if (pick.kind == CourseOverviewMapItemKind::EnemyPlacement) {
        const auto found = std::find_if(previewCourse_.enemyPlacements.begin(), previewCourse_.enemyPlacements.end(),
            [&pick](const CourseEnemyPlacement& enemy) { return enemy.editorGuid == pick.guid; });
        if (found == previewCourse_.enemyPlacements.end() || found->editorLocked) return false;
        originalEnemy_ = *found;
        expectedRevision_ = enemies_->State().mutationRevision;
    } else if (pick.kind == CourseOverviewMapItemKind::Wave) {
        const auto found = std::find_if(previewCourse_.waveDefinitions.begin(), previewCourse_.waveDefinitions.end(),
            [&pick](const CourseWaveDefinition& wave) { return wave.editorGuid == pick.guid; });
        if (found == previewCourse_.waveDefinitions.end() || found->editorLocked) return false;
        originalWave_ = *found;
        expectedRevision_ = waves_->State().mutationRevision;
    } else {
        return false;
    }
    state_.dragging = true;
    RefreshPreview();
    state_.message = "Overview drag preview started; release commits one Undo transaction.";
    return state_.previewValid;
}

void CourseOverviewMapEditTool::UpdateDrag(Vector2 mapPosition) {
    CourseRailAuthoringModel previewRail(previewCourse_);
    if (!previewRail.IsValid()) return;
    if (state_.dragKind == CourseOverviewMapItemKind::RailControlPoint) {
        const auto snapped = snapping_->SnapControlPoint(
            mapPosition, dragDepth_, overview_->Projection(), previewRail, state_.dragGuid);
        const auto found = std::find_if(previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
            [this](const RailPathControlPoint& point) { return point.editorGuid == state_.dragGuid; });
        if (snapped.valid && found != previewCourse_.railPoints.end()) found->position = snapped.worldPosition;
    } else if (state_.dragKind == CourseOverviewMapItemKind::EnemyPlacement) {
        const auto snapped = snapping_->SnapRailAnchor(
            mapPosition, dragDepth_, overview_->Projection(), previewRail);
        const auto found = std::find_if(previewCourse_.enemyPlacements.begin(), previewCourse_.enemyPlacements.end(),
            [this](const CourseEnemyPlacement& enemy) { return enemy.editorGuid == state_.dragGuid; });
        if (snapped.valid && found != previewCourse_.enemyPlacements.end()) found->railAnchor = snapped.railAnchor;
    } else if (state_.dragKind == CourseOverviewMapItemKind::Wave) {
        const auto snapped = snapping_->SnapRailDistance(
            mapPosition, overview_->Projection(), previewRail);
        const auto found = std::find_if(previewCourse_.waveDefinitions.begin(), previewCourse_.waveDefinitions.end(),
            [this](const CourseWaveDefinition& wave) { return wave.editorGuid == state_.dragGuid; });
        if (snapped.valid && found != previewCourse_.waveDefinitions.end()) {
            found->triggerRailDistance = snapped.railDistance;
        }
    }
    RefreshPreview();
}

void CourseOverviewMapEditTool::CommitDrag() {
    if (!state_.dragging || !state_.previewValid) {
        Cancel("Invalid Overview preview was not committed.");
        return;
    }
    const CourseOverviewMapItemKind kind = state_.dragKind;
    const std::string guid = state_.dragGuid;
    if (kind == CourseOverviewMapItemKind::RailControlPoint) {
        const auto found = std::find_if(previewCourse_.railPoints.begin(), previewCourse_.railPoints.end(),
            [&guid](const RailPathControlPoint& point) { return point.editorGuid == guid; });
        if (found == previewCourse_.railPoints.end() || !Different(found->position, originalPoint_.position)) {
            Cancel("Overview drag ended without changes.");
            return;
        }
        CourseRailMutationRequest request{};
        request.kind = CourseRailMutationKind::MovePoint;
        request.expectedRevision = expectedRevision_;
        request.pointGuid = guid;
        request.point = *found;
        request.label = "Move Rail Point From Overview Map";
        const RailPathControlPoint changed = *found;
        Cancel();
        request.point = changed;
        const auto result = rail_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded) ++state_.editRevision;
        return;
    }
    if (kind == CourseOverviewMapItemKind::EnemyPlacement) {
        const auto found = std::find_if(previewCourse_.enemyPlacements.begin(), previewCourse_.enemyPlacements.end(),
            [&guid](const CourseEnemyPlacement& enemy) { return enemy.editorGuid == guid; });
        if (found == previewCourse_.enemyPlacements.end()) { Cancel(); return; }
        const CourseEnemyPlacement changed = *found;
        CourseEnemyMutationRequest request{};
        request.kind = CourseEnemyMutationKind::SetAnchors;
        request.expectedRevision = expectedRevision_;
        request.placements.push_back(changed);
        request.label = "Move Enemy From Overview Map";
        Cancel();
        const auto result = enemies_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded && result.changed) ++state_.editRevision;
        return;
    }
    if (kind == CourseOverviewMapItemKind::Wave) {
        const auto found = std::find_if(previewCourse_.waveDefinitions.begin(), previewCourse_.waveDefinitions.end(),
            [&guid](const CourseWaveDefinition& wave) { return wave.editorGuid == guid; });
        if (found == previewCourse_.waveDefinitions.end()) { Cancel(); return; }
        const CourseWaveDefinition changed = *found;
        CourseWaveMutationRequest request{};
        request.kind = CourseWaveMutationKind::SetWaves;
        request.expectedRevision = expectedRevision_;
        request.waves.push_back(changed);
        request.label = "Move Wave From Overview Map";
        Cancel();
        const auto result = waves_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded && result.changed) ++state_.editRevision;
    }
}

void CourseOverviewMapEditTool::AddRailPoint(Vector2 mapPosition) {
    if (!CanMutate(CourseOverviewMapItemKind::RailControlPoint) || rail_->Model() == nullptr) return;
    const auto snapped = snapping_->SnapRailAnchor(
        mapPosition, (std::numeric_limits<float>::quiet_NaN)(),
        overview_->Projection(), *rail_->Model());
    if (!snapped.valid) { state_.message = "Could not project rail point insertion."; return; }
    const RailAnchorResolution resolved = rail_->Model()->Resolve(snapped.railAnchor);
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::InsertPoint;
    request.expectedRevision = rail_->State().mutationRevision;
    request.segmentGuid = snapped.railAnchor.segmentGuid;
    request.normalizedT = (std::clamp)(snapped.railAnchor.normalizedT, 0.001f, 0.999f);
    request.point.position = resolved.worldPosition;
    request.point.corridorRadius = resolved.railSample.corridorRadius;
    request.point.speed = resolved.railSample.speed;
    request.label = "Insert Rail Point From Overview Map";
    const auto result = rail_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded) {
        ++state_.editRevision;
        SelectCreated(EditorDomainId::CourseRailControlPoint,
            result.affectedPointGuid, "Rail Control Point");
    }
}

void CourseOverviewMapEditTool::AddEnemy(Vector2 mapPosition) {
    if (!CanMutate(CourseOverviewMapItemKind::EnemyPlacement) || enemies_->Model() == nullptr) return;
    const auto snapped = snapping_->SnapRailAnchor(
        mapPosition, (std::numeric_limits<float>::quiet_NaN)(),
        overview_->Projection(), enemies_->Model()->RailModel());
    if (!snapped.valid) { state_.message = "Could not project enemy placement."; return; }
    CourseEnemyPlacement placement{};
    placement.actorAssetId = settings_.defaultActorAssetId;
    placement.railAnchor = snapped.railAnchor;
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::AddPlacements;
    request.expectedRevision = enemies_->State().mutationRevision;
    request.placements.push_back(std::move(placement));
    request.label = "Add Enemy From Overview Map";
    const auto result = enemies_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded && !result.affectedPlacementGuids.empty()) {
        ++state_.editRevision;
        SelectCreated(EditorDomainId::CourseEnemyPlacement,
            result.affectedPlacementGuids.front(), settings_.defaultActorAssetId);
    }
}

void CourseOverviewMapEditTool::AddWave(Vector2 mapPosition) {
    if (!CanMutate(CourseOverviewMapItemKind::Wave) || rail_->Model() == nullptr) return;
    const auto snapped = snapping_->SnapRailDistance(
        mapPosition, overview_->Projection(), *rail_->Model());
    if (!snapped.valid) { state_.message = "Could not project Wave trigger."; return; }
    CourseWaveDefinition wave{};
    wave.displayName = settings_.defaultWaveName;
    wave.triggerRailDistance = snapped.railDistance;
    wave.prewarmDistance = settings_.defaultWavePrewarmDistance;
    CourseWaveMutationRequest request{};
    request.kind = CourseWaveMutationKind::AddWaves;
    request.expectedRevision = waves_->State().mutationRevision;
    request.waves.push_back(std::move(wave));
    request.label = "Add Wave From Overview Map";
    const auto result = waves_->Mutate(request);
    state_.message = result.message;
    if (result.succeeded && !result.affectedWaveGuids.empty()) {
        ++state_.editRevision;
        SelectCreated(EditorDomainId::CourseWaveDefinition,
            result.affectedWaveGuids.front(), settings_.defaultWaveName);
    }
}

void CourseOverviewMapEditTool::Delete(const CourseOverviewMapPickResult& pick) {
    if (!CanMutate(pick.kind)) return;
    if (pick.kind == CourseOverviewMapItemKind::RailControlPoint) {
        CourseRailMutationRequest request{};
        request.kind = CourseRailMutationKind::RemovePoint;
        request.expectedRevision = rail_->State().mutationRevision;
        request.pointGuid = pick.guid;
        request.label = "Delete Rail Point From Overview Map";
        const auto result = rail_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded) ++state_.editRevision;
    } else if (pick.kind == CourseOverviewMapItemKind::EnemyPlacement) {
        CourseEnemyMutationRequest request{};
        request.kind = CourseEnemyMutationKind::RemovePlacements;
        request.expectedRevision = enemies_->State().mutationRevision;
        request.placementGuids.push_back(pick.guid);
        request.label = "Delete Enemy From Overview Map";
        const auto result = enemies_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded) ++state_.editRevision;
    } else if (pick.kind == CourseOverviewMapItemKind::Wave) {
        CourseWaveMutationRequest request{};
        request.kind = CourseWaveMutationKind::RemoveWaves;
        request.expectedRevision = waves_->State().mutationRevision;
        request.waveGuids.push_back(pick.guid);
        request.referencePolicy = CourseWaveReferencePolicy::ClearReferences;
        request.label = "Delete Wave From Overview Map";
        const auto result = waves_->Mutate(request);
        state_.message = result.message;
        if (result.succeeded) ++state_.editRevision;
    } else {
        state_.message = "Only rail points, enemies and waves can be deleted from Overview Map.";
        return;
    }
    if (selection_ != nullptr) selection_->Clear();
}

void CourseOverviewMapEditTool::RefreshPreview() {
    CourseRailAuthoringModel rail(previewCourse_);
    CourseEnemyAuthoringModel enemies(previewCourse_);
    CourseWaveAuthoringModel waves(previewCourse_);
    state_.previewValid = rail.IsValid() && enemies.IsValid() && waves.IsValid();
    if (state_.previewValid) overview_->SetPreviewCourse(&previewCourse_);
    else overview_->ClearPreviewCourse();
}

void CourseOverviewMapEditTool::SelectCreated(
    EditorDomainId domain,
    std::string guid,
    std::string displayName) {
    if (selection_ == nullptr || guid.empty()) return;
    const char* prefix = domain == EditorDomainId::CourseRailControlPoint
        ? "course-rail-point:"
        : domain == EditorDomainId::CourseEnemyPlacement
            ? "course-enemy-placement:" : "course-wave:";
    uint64_t index = 0;
    uint32_t generation = 0;
    if (domain == EditorDomainId::CourseRailControlPoint && rail_->Model() != nullptr) {
        index = rail_->Model()->FindPointIndex(guid).value_or(0);
        generation = rail_->State().bindingGeneration;
    } else if (domain == EditorDomainId::CourseEnemyPlacement && enemies_->Model() != nullptr) {
        index = enemies_->Model()->FindIndex(guid).value_or(0);
        generation = enemies_->State().bindingGeneration;
    } else if (domain == EditorDomainId::CourseWaveDefinition && waves_->Model() != nullptr) {
        index = waves_->Model()->FindIndex(guid).value_or(0);
        generation = waves_->State().bindingGeneration;
    }
    selection_->SetPrimary({domain, std::string(prefix) + guid, index, generation, std::move(displayName)});
}

const char* ToString(CourseOverviewMapEditMode mode) {
    switch (mode) {
    case CourseOverviewMapEditMode::SelectMove: return "Select / Move";
    case CourseOverviewMapEditMode::AddRailPoint: return "Add Rail Point";
    case CourseOverviewMapEditMode::AddEnemy: return "Add Enemy";
    case CourseOverviewMapEditMode::AddWave: return "Add Wave";
    case CourseOverviewMapEditMode::Delete: return "Delete";
    }
    return "Unknown";
}

} // namespace editor
