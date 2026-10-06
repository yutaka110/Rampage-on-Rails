#include "CourseEnemyDetailsPanel.h"

#include <algorithm>
#include <cstring>

#include "../../../externals/imgui/imgui.h"

namespace editor {
namespace {

template <typename Owner, typename Settings>
void ApplyValidatedSettings(Owner& owner, const Settings& candidate) {
    std::string error;
    if (!owner.SetSettings(candidate, &error)) {
        ImGui::TextWrapped("Settings rejected: %s", error.c_str());
    }
}


constexpr std::string_view kPlacementPrefix = "course-enemy-placement:";

bool IsPlacementHandle(const EditorObjectHandle* handle) {
    return handle != nullptr && handle->domain == EditorDomainId::CourseEnemyPlacement &&
        handle->stableId.starts_with(kPlacementPrefix);
}

template <std::size_t N>
void CopyText(std::array<char, N>& destination, std::string_view source) {
    destination.fill('\0');
    const std::size_t count = (std::min)(source.size(), N - 1);
    if (count > 0) std::memcpy(destination.data(), source.data(), count);
}

} // namespace

bool CourseEnemyDetailsPanel::HandlesSelection(
    const EditorSelection* selection) const {
    return IsPlacementHandle(selection != nullptr ? selection->Primary() : nullptr);
}

void CourseEnemyDetailsPanel::Draw(
    const CourseEnemyDetailsPanelContext& context) {
    const EditorObjectHandle* primary = context.selection != nullptr
        ? context.selection->Primary() : nullptr;
    if (context.controller == nullptr || !IsPlacementHandle(primary)) {
        ImGui::TextUnformatted("Course Enemy details are unavailable.");
        return;
    }
    if (continuousEditActive_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelEdit();
        lastMessage_ = "Enemy Details edit cancelled.";
    }
    const std::string guid = primary->stableId.substr(kPlacementPrefix.size());
    SyncPlacement(context, guid);
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const CourseEnemyPlacement* placement = model != nullptr ? model->Find(guid) : nullptr;
    if (placement == nullptr) {
        ImGui::TextUnformatted("Selected enemy placement no longer exists.");
        return;
    }
    const std::vector<std::string> selected = SelectedGuids(context.selection);
    const bool canMutate = context.canMutateAuthoring &&
        context.controller->State().authoringAllowed;

    ImGui::Text("Enemy Placement%s", selected.size() > 1 ? "s" : "");
    ImGui::Text("Selected: %u", static_cast<uint32_t>(selected.size()));
    ImGui::TextDisabled("GUID: %s", selectedGuid_.c_str());
    const CourseEnemyPlacementResolution resolved = model->Resolve(selectedGuid_);
    if (resolved.valid) {
        ImGui::Text("World: %.3f, %.3f, %.3f",
            resolved.worldPosition.x, resolved.worldPosition.y, resolved.worldPosition.z);
        ImGui::Text("Rail distance: %.3f", resolved.runtimeDistance);
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!canMutate);
    ImGui::BeginDisabled(buffer_.editorLocked);

    const auto continuous = [&](bool changed, const char* label) {
        if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
        if (changed) {
            BeginContinuousEdit(context);
            RefreshPreview(context);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitContinuousEdit(context, label);
        }
    };

    const bool actorChanged = ImGui::InputText(
        "Actor Asset", actorAssetBuffer_.data(), actorAssetBuffer_.size());
    if (actorChanged) ReadTextBuffers();
    continuous(actorChanged, "Edit Enemy Actor Asset");

    const bool patternChanged = ImGui::InputText(
        "Bullet Pattern Override",
        bulletPatternBuffer_.data(), bulletPatternBuffer_.size());
    if (patternChanged) ReadTextBuffers();
    continuous(patternChanged, "Edit Enemy Bullet Pattern");

    const bool waveChanged = ImGui::InputText(
        "Wave Group", waveGroupBuffer_.data(), waveGroupBuffer_.size());
    if (waveChanged) ReadTextBuffers();
    continuous(waveChanged, "Edit Enemy Wave Group");

    const auto& segments = model->RailModel().Segments();
    const CourseRailSegment* currentSegment =
        model->RailModel().FindSegment(buffer_.railAnchor.segmentGuid);
    const char* segmentPreview = currentSegment != nullptr
        ? currentSegment->guid.c_str() : "Invalid Segment";
    if (ImGui::BeginCombo("Rail Segment", segmentPreview)) {
        for (const CourseRailSegment& segment : segments) {
            const bool selectedSegment = segment.guid == buffer_.railAnchor.segmentGuid;
            if (ImGui::Selectable(segment.guid.c_str(), selectedSegment)) {
                CourseEnemyPlacement changed = buffer_;
                changed.railAnchor.segmentGuid = segment.guid;
                changed.railAnchor.normalizedT =
                    (std::clamp)(changed.railAnchor.normalizedT, 0.0f, 1.0f);
                CommitPlacement(context, changed, "Change Enemy Rail Segment");
            }
            if (selectedSegment) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const bool tChanged = ImGui::SliderFloat(
        "Segment T", &buffer_.railAnchor.normalizedT, 0.0f, 1.0f, "%.4f");
    continuous(tChanged, "Edit Enemy Rail Position");

    float offsets[3]{
        buffer_.railAnchor.lateralOffset,
        buffer_.railAnchor.verticalOffset,
        buffer_.railAnchor.forwardOffset};
    const bool offsetsChanged = ImGui::DragFloat3(
        "Rail Offsets (L/V/F)", offsets, 0.1f, 0.0f, 0.0f, "%.3f");
    if (offsetsChanged) {
        buffer_.railAnchor.lateralOffset = offsets[0];
        buffer_.railAnchor.verticalOffset = offsets[1];
        buffer_.railAnchor.forwardOffset = offsets[2];
    }
    continuous(offsetsChanged, "Edit Enemy Rail Offsets");

    float rotation[3]{
        buffer_.localRotation.x, buffer_.localRotation.y, buffer_.localRotation.z};
    const bool rotationChanged = ImGui::DragFloat3(
        "Local Rotation", rotation, 0.25f, 0.0f, 0.0f, "%.2f deg");
    if (rotationChanged) buffer_.localRotation = {rotation[0], rotation[1], rotation[2]};
    continuous(rotationChanged, "Edit Enemy Rotation");

    float scale[3]{buffer_.localScale.x, buffer_.localScale.y, buffer_.localScale.z};
    const bool scaleChanged = ImGui::DragFloat3(
        "Local Scale", scale, 0.01f, 0.01f, 1000.0f, "%.3f");
    if (scaleChanged) {
        buffer_.localScale = {
            (std::max)(scale[0], 0.01f),
            (std::max)(scale[1], 0.01f),
            (std::max)(scale[2], 0.01f)};
    }
    continuous(scaleChanged, "Edit Enemy Scale");

    const bool leadChanged = ImGui::DragFloat(
        "Activation Lead Distance", &buffer_.activationLeadDistance,
        0.5f, 0.0f, 100000.0f, "%.2f");
    buffer_.activationLeadDistance = (std::max)(buffer_.activationLeadDistance, 0.0f);
    continuous(leadChanged, "Edit Enemy Activation Distance");
    ImGui::EndDisabled();

    bool enabled = buffer_.enabled;
    ImGui::BeginDisabled(buffer_.editorLocked);
    if (ImGui::Checkbox("Gameplay Enabled", &enabled)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetEnabled,
            enabled, "Set Enemy Gameplay State");
    }
    ImGui::EndDisabled();
    bool visible = buffer_.editorVisible;
    if (ImGui::Checkbox("Editor Visible", &visible)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetVisible,
            visible, "Set Enemy Editor Visibility");
    }
    bool locked = buffer_.editorLocked;
    if (ImGui::Checkbox("Editor Locked", &locked)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetLocked,
            locked, "Set Enemy Editor Lock");
    }

    ImGui::Separator();
    if (ImGui::Button("Duplicate Selected")) DuplicateSelection(context);
    ImGui::SameLine();
    if (ImGui::Button("Delete Selected")) RemoveSelection(context);
    if (ImGui::Button("Copy Placement Values")) {
        clipboard_ = buffer_;
        lastMessage_ = "Enemy placement values copied.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!clipboard_.has_value() || buffer_.editorLocked);
    if (ImGui::Button("Paste Placement Values") && clipboard_.has_value()) {
        CourseEnemyPlacement pasted = *clipboard_;
        pasted.editorGuid = selectedGuid_;
        CommitPlacement(context, pasted, "Paste Enemy Placement Values");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    DrawWaveEditing(context);
    DrawGizmoSettings(context);
    if (!lastMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", lastMessage_.c_str());
    }
}

void CourseEnemyDetailsPanel::DrawWaveEditing(
    const CourseEnemyDetailsPanelContext& context) {
    ImGui::Separator();
    if (!ImGui::CollapsingHeader(
            "Wave Group Bulk Edit", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (buffer_.waveGroupGuid.empty()) {
        ImGui::TextDisabled("Assign a Wave Group before using bulk editing.");
        return;
    }
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const std::vector<const CourseEnemyPlacement*> members = model != nullptr
        ? model->FindWaveGroup(buffer_.waveGroupGuid)
        : std::vector<const CourseEnemyPlacement*>{};
    ImGui::Text("%s (%u placements)", buffer_.waveGroupGuid.c_str(),
        static_cast<uint32_t>(members.size()));
    ImGui::Checkbox("Include Locked##EnemyWave", &waveIncludeLocked_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Off: any locked member rejects the whole operation. On: the wave is edited atomically.");
    }

    if (ImGui::Button("Select Wave")) {
        SelectWave(context, buffer_.waveGroupGuid);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.canMutateAuthoring || members.empty());
    if (ImGui::Button("Duplicate Wave")) DuplicateWave(context);
    ImGui::SameLine();
    if (ImGui::Button("Delete Wave")) RemoveWave(context);

    if (waveRenameBuffer_[0] == '\0') {
        CopyText(waveRenameBuffer_, buffer_.waveGroupGuid);
    }
    ImGui::InputText("Destination Wave", waveRenameBuffer_.data(),
        waveRenameBuffer_.size());
    if (ImGui::Button("Reassign Entire Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.replacementWaveGroupGuid = std::string(waveRenameBuffer_.data());
        request.includeLocked = waveIncludeLocked_;
        request.label = "Reassign Enemy Wave Group";
        ApplyWaveEdit(context, std::move(request));
    }

    float offset[3]{
        waveOffsetDelta_.x, waveOffsetDelta_.y, waveOffsetDelta_.z};
    if (ImGui::DragFloat3(
            "Wave Offset Delta (L/V/F)", offset, 0.1f, 0.0f, 0.0f, "%.3f")) {
        waveOffsetDelta_ = {offset[0], offset[1], offset[2]};
    }
    if (ImGui::Button("Apply Wave Offset")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.anchorOffsetDelta = waveOffsetDelta_;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Offset Enemy Wave";
        if (ApplyWaveEdit(context, std::move(request))) waveOffsetDelta_ = {};
    }
    if (ImGui::Button("Apply Actor/Pattern/Lead From Primary")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.actorAssetId = buffer_.actorAssetId;
        request.bulletPatternOverrideId = buffer_.bulletPatternOverrideId;
        request.activationLeadDistance = buffer_.activationLeadDistance;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Apply Enemy Wave Combat Settings";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("Enable Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.enabled = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Enable Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.enabled = false;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Disable Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("Show Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorVisible = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Show Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorVisible = false;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Hide Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("Lock Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorLocked = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Lock Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("Unlock Wave")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorLocked = false;
        request.includeLocked = true;
        request.label = "Unlock Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::EndDisabled();
}

void CourseEnemyDetailsPanel::SelectWave(
    const CourseEnemyDetailsPanelContext& context,
    std::string_view waveGroupGuid) const {
    if (context.selection == nullptr || context.controller->Model() == nullptr) return;
    std::vector<EditorObjectHandle> handles;
    for (const CourseEnemyPlacement* member :
         context.controller->Model()->FindWaveGroup(waveGroupGuid)) {
        if (member == nullptr) continue;
        const std::optional<std::size_t> index =
            context.controller->Model()->FindIndex(member->editorGuid);
        if (!index.has_value()) continue;
        EditorObjectHandle handle{};
        handle.domain = EditorDomainId::CourseEnemyPlacement;
        handle.stableId = std::string(kPlacementPrefix) + member->editorGuid;
        handle.localIndex = *index;
        handle.generation = static_cast<uint32_t>(
            context.controller->State().mutationRevision);
        handle.displayName = member->actorAssetId;
        handles.push_back(std::move(handle));
    }
    context.selection->Set(std::move(handles));
}

bool CourseEnemyDetailsPanel::ApplyWaveEdit(
    const CourseEnemyDetailsPanelContext& context,
    CourseEnemyWaveBulkEditRequest request) {
    const CourseEnemyMutationResult result =
        context.controller->MutateWave(request);
    lastMessage_ = result.message;
    if (result.succeeded) {
        syncedRevision_ = (std::numeric_limits<uint64_t>::max)();
        if (request.replacementWaveGroupGuid.has_value() &&
            !request.replacementWaveGroupGuid->empty()) {
            selectedGuid_.clear();
        }
    }
    return result.succeeded;
}

void CourseEnemyDetailsPanel::DuplicateWave(
    const CourseEnemyDetailsPanelContext& context) {
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::DuplicatePlacements;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.allowLocked = waveIncludeLocked_;
    request.label = "Duplicate Enemy Wave";
    if (context.viewportTool != nullptr) {
        request.duplicateOffset = context.viewportTool->Settings().duplicateOffset;
    }
    for (const CourseEnemyPlacement* member :
         context.controller->Model()->FindWaveGroup(buffer_.waveGroupGuid)) {
        if (member != nullptr) request.placementGuids.push_back(member->editorGuid);
    }
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded && context.selection != nullptr) {
        std::vector<EditorObjectHandle> handles;
        for (const std::string& guid : result.affectedPlacementGuids) {
            const CourseEnemyPlacement* member = context.controller->Model()->Find(guid);
            const std::optional<std::size_t> index =
                context.controller->Model()->FindIndex(guid);
            if (member == nullptr || !index.has_value()) continue;
            EditorObjectHandle handle{};
            handle.domain = EditorDomainId::CourseEnemyPlacement;
            handle.stableId = std::string(kPlacementPrefix) + guid;
            handle.localIndex = *index;
            handle.generation = static_cast<uint32_t>(result.revision);
            handle.displayName = member->actorAssetId;
            handles.push_back(std::move(handle));
        }
        context.selection->Set(std::move(handles));
    }
}

void CourseEnemyDetailsPanel::RemoveWave(
    const CourseEnemyDetailsPanelContext& context) {
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::RemovePlacements;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.allowLocked = waveIncludeLocked_;
    request.label = "Delete Enemy Wave";
    for (const CourseEnemyPlacement* member :
         context.controller->Model()->FindWaveGroup(buffer_.waveGroupGuid)) {
        if (member != nullptr) request.placementGuids.push_back(member->editorGuid);
    }
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded && context.selection != nullptr) {
        context.selection->Clear();
        selectedGuid_.clear();
        CancelEdit();
    }
}

void CourseEnemyDetailsPanel::CancelEdit() {
    continuousEditActive_ = false;
    previewModel_.reset();
    previewCourse_ = {};
    syncedRevision_ = (std::numeric_limits<uint64_t>::max)();
}

void CourseEnemyDetailsPanel::SyncPlacement(
    const CourseEnemyDetailsPanelContext& context,
    std::string_view guid) {
    if (continuousEditActive_) return;
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const CourseEnemyPlacement* placement = model != nullptr ? model->Find(guid) : nullptr;
    if (placement == nullptr) return;
    const uint64_t revision = context.controller->State().mutationRevision;
    if (selectedGuid_ == guid && syncedRevision_ == revision) return;
    const bool selectionChanged = selectedGuid_ != guid;
    selectedGuid_ = std::string(guid);
    syncedRevision_ = revision;
    buffer_ = *placement;
    if (selectionChanged) {
        CopyText(waveRenameBuffer_, buffer_.waveGroupGuid);
    }
    SyncTextBuffers();
    previewModel_.reset();
}

void CourseEnemyDetailsPanel::BeginContinuousEdit(
    const CourseEnemyDetailsPanelContext& context) {
    if (continuousEditActive_) return;
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const CourseEnemyPlacement* placement = model != nullptr
        ? model->Find(selectedGuid_) : nullptr;
    if (placement == nullptr || placement->editorLocked) return;
    editOriginal_ = *placement;
    editExpectedRevision_ = context.controller->State().mutationRevision;
    continuousEditActive_ = true;
}

void CourseEnemyDetailsPanel::RefreshPreview(
    const CourseEnemyDetailsPanelContext& context) {
    const CourseAsset* course = context.controller->Course();
    if (course == nullptr || !continuousEditActive_) return;
    previewCourse_ = *course;
    const auto found = std::find_if(
        previewCourse_.enemyPlacements.begin(), previewCourse_.enemyPlacements.end(),
        [this](const CourseEnemyPlacement& placement) {
            return placement.editorGuid == selectedGuid_;
        });
    if (found == previewCourse_.enemyPlacements.end()) return;
    *found = buffer_;
    found->editorGuid = selectedGuid_;
    previewModel_.emplace(previewCourse_);
    if (!previewModel_->IsValid()) lastMessage_ = previewModel_->ValidationError();
}

void CourseEnemyDetailsPanel::CommitContinuousEdit(
    const CourseEnemyDetailsPanelContext& context,
    std::string label) {
    if (!continuousEditActive_) return;
    continuousEditActive_ = false;
    previewModel_.reset();
    previewCourse_ = {};
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::SetPlacements;
    request.expectedRevision = editExpectedRevision_;
    request.placements.push_back(buffer_);
    request.label = std::move(label);
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    syncedRevision_ = result.succeeded
        ? result.revision : (std::numeric_limits<uint64_t>::max)();
    if (!result.succeeded) {
        buffer_ = editOriginal_;
        SyncTextBuffers();
    }
}

bool CourseEnemyDetailsPanel::CommitPlacement(
    const CourseEnemyDetailsPanelContext& context,
    const CourseEnemyPlacement& placement,
    std::string label) {
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::SetPlacements;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.placements.push_back(placement);
    request.label = std::move(label);
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded) {
        buffer_ = placement;
        buffer_.editorGuid = selectedGuid_;
        syncedRevision_ = result.revision;
        SyncTextBuffers();
    }
    return result.succeeded;
}

bool CourseEnemyDetailsPanel::CommitBulkState(
    const CourseEnemyDetailsPanelContext& context,
    CourseEnemyMutationKind kind,
    bool value,
    std::string label) {
    CourseEnemyMutationRequest request{};
    request.kind = kind;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.placementGuids = SelectedGuids(context.selection);
    request.stateValue = value;
    request.label = std::move(label);
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded) syncedRevision_ = (std::numeric_limits<uint64_t>::max)();
    return result.succeeded;
}

void CourseEnemyDetailsPanel::DuplicateSelection(
    const CourseEnemyDetailsPanelContext& context) {
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::DuplicatePlacements;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.placementGuids = SelectedGuids(context.selection);
    request.label = "Duplicate Enemy Placements";
    if (context.viewportTool != nullptr) {
        request.duplicateOffset = context.viewportTool->Settings().duplicateOffset;
    }
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded && !result.affectedPlacementGuids.empty()) {
        SelectPlacement(context, result.affectedPlacementGuids.front());
    }
}

void CourseEnemyDetailsPanel::RemoveSelection(
    const CourseEnemyDetailsPanelContext& context) {
    CourseEnemyMutationRequest request{};
    request.kind = CourseEnemyMutationKind::RemovePlacements;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.placementGuids = SelectedGuids(context.selection);
    request.label = "Delete Enemy Placements";
    const CourseEnemyMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded && context.selection != nullptr) {
        context.selection->Clear();
        selectedGuid_.clear();
        CancelEdit();
    }
}

void CourseEnemyDetailsPanel::SelectPlacement(
    const CourseEnemyDetailsPanelContext& context,
    std::string_view guid) const {
    if (context.selection == nullptr || context.controller->Model() == nullptr) return;
    const std::optional<std::size_t> index = context.controller->Model()->FindIndex(guid);
    const CourseEnemyPlacement* placement = context.controller->Model()->Find(guid);
    if (!index.has_value() || placement == nullptr) return;
    EditorObjectHandle handle{};
    handle.domain = EditorDomainId::CourseEnemyPlacement;
    handle.stableId = std::string(kPlacementPrefix) + std::string(guid);
    handle.localIndex = static_cast<uint32_t>(*index);
    handle.generation = static_cast<uint32_t>(
        context.controller->State().mutationRevision);
    handle.displayName = placement->actorAssetId;
    context.selection->SetPrimary(std::move(handle));
}

std::vector<std::string> CourseEnemyDetailsPanel::SelectedGuids(
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

void CourseEnemyDetailsPanel::DrawGizmoSettings(
    const CourseEnemyDetailsPanelContext& context) {
    if (context.viewportTool != nullptr) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Enemy Placement Tool")) {
            CourseEnemyViewportEditSettings tool = context.viewportTool->Settings();
            ImGui::Text("Add default: %s", tool.defaultActorAssetId.c_str());
            if (ImGui::Button("Use Selected As Add Defaults")) {
                tool.defaultActorAssetId = buffer_.actorAssetId;
                tool.defaultBulletPatternId = buffer_.bulletPatternOverrideId;
                tool.defaultWaveGroupGuid = buffer_.waveGroupGuid;
            }
            float duplicateOffset[3]{
                tool.duplicateOffset.x,
                tool.duplicateOffset.y,
                tool.duplicateOffset.z};
            if (ImGui::DragFloat3(
                    "Duplicate Offset (L/V/F)", duplicateOffset,
                    0.1f, 0.0f, 0.0f, "%.3f")) {
                tool.duplicateOffset = {
                    duplicateOffset[0], duplicateOffset[1], duplicateOffset[2]};
            }
            ApplyValidatedSettings(*context.viewportTool, std::move(tool));
        }
    }
    if (context.transformGizmo == nullptr) return;
    ImGui::Separator();
    if (!ImGui::CollapsingHeader(
            "Enemy Transform Gizmo", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    CourseEnemyTransformGizmoSettings settings = context.transformGizmo->Settings();
    int mode = static_cast<int>(settings.mode);
    const char* modes[] = {"Translate", "Scale", "Rotate"};
    if (ImGui::Combo("Mode", &mode, modes, 3)) {
        settings.mode = static_cast<EditorTransformGizmoMode>(mode);
    }
    int space = settings.space == EditorTransformGizmoSpace::World ? 0 : 1;
    const char* spaces[] = {"World", "Rail Local"};
    if (ImGui::Combo("Space", &space, spaces, 2)) {
        settings.space = space == 0
            ? EditorTransformGizmoSpace::World : EditorTransformGizmoSpace::Local;
    }
    ImGui::Checkbox("Snap", &settings.snapEnabled);
    if (settings.mode == EditorTransformGizmoMode::Translate) {
        ImGui::DragFloat("Translation Snap", &settings.translationSnap,
            0.1f, 0.01f, 1000.0f, "%.3f");
    } else if (settings.mode == EditorTransformGizmoMode::Rotate) {
        ImGui::DragFloat("Rotation Snap", &settings.rotationSnapDegrees,
            1.0f, 0.1f, 180.0f, "%.1f deg");
    } else {
        ImGui::DragFloat("Scale Snap", &settings.scaleSnap,
            0.01f, 0.001f, 10.0f, "%.3f");
    }
    ImGui::SliderFloat("Handle Scale", &settings.handleLengthScale,
        0.1f, 4.0f, "%.2f");
    ApplyValidatedSettings(*context.transformGizmo, settings);
    if (context.viewportTool != nullptr) {
        CourseEnemyViewportEditSettings tool = context.viewportTool->Settings();
        tool.offsetSnap = settings.snapEnabled;
        tool.offsetSnapSize = settings.translationSnap;
        ApplyValidatedSettings(*context.viewportTool, std::move(tool));
    }
    ImGui::Text("Selected placements: %u",
        context.transformGizmo->State().selectedPlacementCount);
    ImGui::Text("Hover: %s  Active: %s",
        ToString(context.transformGizmo->State().hovered),
        ToString(context.transformGizmo->State().active));
    if (context.transformGizmo->State().containsLockedPlacement) {
        ImGui::TextDisabled("Unlock all selected placements to transform them.");
    }
}

void CourseEnemyDetailsPanel::SyncTextBuffers() {
    CopyText(actorAssetBuffer_, buffer_.actorAssetId);
    CopyText(bulletPatternBuffer_, buffer_.bulletPatternOverrideId);
    CopyText(waveGroupBuffer_, buffer_.waveGroupGuid);
}

void CourseEnemyDetailsPanel::ReadTextBuffers() {
    buffer_.actorAssetId = actorAssetBuffer_.data();
    buffer_.bulletPatternOverrideId = bulletPatternBuffer_.data();
    buffer_.waveGroupGuid = waveGroupBuffer_.data();
}

} // namespace editor
