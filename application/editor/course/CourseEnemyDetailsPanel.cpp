#include "CourseEnemyDetailsPanel.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include <algorithm>
#include <cstring>

#include "../../../externals/imgui/imgui.h"

namespace editor {
namespace {

template <typename Owner, typename Settings>
void ApplyValidatedSettings(Owner& owner, const Settings& candidate) {
    std::string error;
    if (!owner.SetSettings(candidate, &error)) {
        ImGui::TextWrapped("設定を反映できません: %s", error.c_str());
    }
}


constexpr std::string_view kPlacementPrefix = "course-enemy-placement:";

const char* GizmoHandleLabel(CourseEnemyGizmoHandle handle) {
    if (handle == CourseEnemyGizmoHandle::None) return "なし";
    if (handle == CourseEnemyGizmoHandle::Uniform) return "全方向";
    return ToString(handle);
}

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
        ImGui::TextUnformatted("敵配置の詳細設定を表示できません。");
        return;
    }
    if (continuousEditActive_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelEdit();
        lastMessage_ = "敵配置の調整を取り消しました。";
    }
    const std::string guid = primary->stableId.substr(kPlacementPrefix.size());
    SyncPlacement(context, guid);
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const CourseEnemyPlacement* placement = model != nullptr ? model->Find(guid) : nullptr;
    if (placement == nullptr) {
        ImGui::TextUnformatted("選択した敵配置は存在しません。");
        return;
    }
    const std::vector<std::string> selected = SelectedGuids(context.selection);
    const bool canMutate = context.canMutateAuthoring &&
        context.controller->State().authoringAllowed;

    ImGui::TextUnformatted("敵の配置");
    ImGui::Text("選択数: %u", static_cast<uint32_t>(selected.size()));
    ImGui::TextDisabled("GUID: %s", selectedGuid_.c_str());
    const CourseEnemyPlacementResolution resolved = model->Resolve(selectedGuid_);
    if (resolved.valid) {
        ImGui::Text("ワールド位置: %.3f, %.3f, %.3f",
            resolved.worldPosition.x, resolved.worldPosition.y, resolved.worldPosition.z);
        ImGui::Text("レール上の距離: %.3f m", resolved.runtimeDistance);
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
        "敵アセット", actorAssetBuffer_.data(), actorAssetBuffer_.size());
    if (actorChanged) ReadTextBuffers();
    continuous(actorChanged, "Edit Enemy Actor Asset");

    const bool patternChanged = ImGui::InputText(
        "弾幕の上書き",
        bulletPatternBuffer_.data(), bulletPatternBuffer_.size());
    if (patternChanged) ReadTextBuffers();
    continuous(patternChanged, "Edit Enemy Bullet Pattern");

    const bool waveChanged = ImGui::InputText(
        "出現グループ", waveGroupBuffer_.data(), waveGroupBuffer_.size());
    if (waveChanged) ReadTextBuffers();
    continuous(waveChanged, "Edit Enemy Wave Group");

    const auto& segments = model->RailModel().Segments();
    const CourseRailSegment* currentSegment =
        model->RailModel().FindSegment(buffer_.railAnchor.segmentGuid);
    const char* segmentPreview = currentSegment != nullptr
        ? currentSegment->guid.c_str() : "無効な区間";
    if (ImGui::BeginCombo("レール区間", segmentPreview)) {
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
        "区間内の位置", &buffer_.railAnchor.normalizedT, 0.0f, 1.0f, "%.4f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0が区間の開始、1が区間の終了です。");
    continuous(tChanged, "Edit Enemy Rail Position");

    float offsets[3]{
        buffer_.railAnchor.lateralOffset,
        buffer_.railAnchor.verticalOffset,
        buffer_.railAnchor.forwardOffset};
    const bool offsetsChanged = ImGui::DragFloat3(
        "レールからのずれ", offsets, 0.1f, 0.0f, 0.0f, "%.3f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("左から、左右・上下・進行方向の移動量です。単位はm。");
    if (offsetsChanged) {
        buffer_.railAnchor.lateralOffset = offsets[0];
        buffer_.railAnchor.verticalOffset = offsets[1];
        buffer_.railAnchor.forwardOffset = offsets[2];
    }
    continuous(offsetsChanged, "Edit Enemy Rail Offsets");

    float rotation[3]{
        buffer_.localRotation.x, buffer_.localRotation.y, buffer_.localRotation.z};
    const bool rotationChanged = ImGui::DragFloat3(
        "回転（X / Y / Z）", rotation, 0.25f, 0.0f, 0.0f, "%.2f deg");
    if (rotationChanged) buffer_.localRotation = {rotation[0], rotation[1], rotation[2]};
    continuous(rotationChanged, "Edit Enemy Rotation");

    float scale[3]{buffer_.localScale.x, buffer_.localScale.y, buffer_.localScale.z};
    const bool scaleChanged = ImGui::DragFloat3(
        "拡大率（X / Y / Z）", scale, 0.01f, 0.01f, 1000.0f, "%.3f");
    if (scaleChanged) {
        buffer_.localScale = {
            (std::max)(scale[0], 0.01f),
            (std::max)(scale[1], 0.01f),
            (std::max)(scale[2], 0.01f)};
    }
    continuous(scaleChanged, "Edit Enemy Scale");

    const bool leadChanged = ImGui::DragFloat(
        "起動の先行距離", &buffer_.activationLeadDistance,
        0.5f, 0.0f, 100000.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("配置位置に到達する何m手前から敵を起動するかを指定します。");
    buffer_.activationLeadDistance = (std::max)(buffer_.activationLeadDistance, 0.0f);
    continuous(leadChanged, "Edit Enemy Activation Distance");
    ImGui::EndDisabled();

    bool enabled = buffer_.enabled;
    ImGui::BeginDisabled(buffer_.editorLocked);
    if (ImGui::Checkbox("ゲーム内で有効", &enabled)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetEnabled,
            enabled, "Set Enemy Gameplay State");
    }
    ImGui::EndDisabled();
    bool visible = buffer_.editorVisible;
    if (ImGui::Checkbox("マップに表示", &visible)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetVisible,
            visible, "Set Enemy Editor Visibility");
    }
    bool locked = buffer_.editorLocked;
    if (ImGui::Checkbox("編集をロック", &locked)) {
        CommitBulkState(context, CourseEnemyMutationKind::SetLocked,
            locked, "Set Enemy Editor Lock");
    }

    ImGui::Separator();
    if (ImGui::Button("選択した敵を複製")) DuplicateSelection(context);
    ImGui::SameLine();
    if (ImGui::Button("選択した敵を削除")) RemoveSelection(context);
    if (ImGui::Button("配置の値をコピー")) {
        clipboard_ = buffer_;
        lastMessage_ = "敵配置の値をコピーしました。";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!clipboard_.has_value() || buffer_.editorLocked);
    if (ImGui::Button("配置の値を貼り付け") && clipboard_.has_value()) {
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
            "出現グループの一括編集", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (buffer_.waveGroupGuid.empty()) {
        ImGui::TextDisabled("一括編集するには、出現グループを指定してください。");
        return;
    }
    const CourseEnemyAuthoringModel* model = context.controller->Model();
    const std::vector<const CourseEnemyPlacement*> members = model != nullptr
        ? model->FindWaveGroup(buffer_.waveGroupGuid)
        : std::vector<const CourseEnemyPlacement*>{};
    ImGui::Text("%s（敵配置: %u）", buffer_.waveGroupGuid.c_str(),
        static_cast<uint32_t>(members.size()));
    ImGui::Checkbox("ロック中も対象にする##EnemyWave", &waveIncludeLocked_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "OFF: ロック中の敵が含まれると一括編集を中止します。ON: ロック中の敵も含めて全体を編集します。");
    }

    if (ImGui::Button("グループを選択")) {
        SelectWave(context, buffer_.waveGroupGuid);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.canMutateAuthoring || members.empty());
    if (ImGui::Button("グループを複製")) DuplicateWave(context);
    ImGui::SameLine();
    if (ImGui::Button("グループを削除")) RemoveWave(context);

    if (waveRenameBuffer_[0] == '\0') {
        CopyText(waveRenameBuffer_, buffer_.waveGroupGuid);
    }
    ImGui::InputText("移動先グループ", waveRenameBuffer_.data(),
        waveRenameBuffer_.size());
    if (ImGui::Button("全員を指定グループへ移動")) {
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
            "一括移動量", offset, 0.1f, 0.0f, 0.0f, "%.3f")) {
        waveOffsetDelta_ = {offset[0], offset[1], offset[2]};
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("グループ全体を、左から左右・上下・進行方向へ移動します。");
    if (ImGui::Button("一括移動を反映")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.anchorOffsetDelta = waveOffsetDelta_;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Offset Enemy Wave";
        if (ApplyWaveEdit(context, std::move(request))) waveOffsetDelta_ = {};
    }
    if (ImGui::Button("選択元の敵・弾幕・起動距離を全員に反映")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.actorAssetId = buffer_.actorAssetId;
        request.bulletPatternOverrideId = buffer_.bulletPatternOverrideId;
        request.activationLeadDistance = buffer_.activationLeadDistance;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Apply Enemy Wave Combat Settings";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("グループを有効化")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.enabled = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Enable Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("グループを無効化")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.enabled = false;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Disable Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("グループを表示")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorVisible = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Show Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("グループを非表示")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorVisible = false;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Hide Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    if (ImGui::Button("グループをロック")) {
        CourseEnemyWaveBulkEditRequest request{};
        request.waveGroupGuid = buffer_.waveGroupGuid;
        request.editorLocked = true;
        request.includeLocked = waveIncludeLocked_;
        request.label = "Lock Enemy Wave";
        ApplyWaveEdit(context, std::move(request));
    }
    ImGui::SameLine();
    if (ImGui::Button("グループのロックを解除")) {
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
        if (ImGui::CollapsingHeader("敵配置ツール")) {
            CourseEnemyViewportEditSettings tool = context.viewportTool->Settings();
            ImGui::Text("追加時の敵: %s", tool.defaultActorAssetId.c_str());
            if (ImGui::Button("選択中の設定を追加時に使う")) {
                tool.defaultActorAssetId = buffer_.actorAssetId;
                tool.defaultBulletPatternId = buffer_.bulletPatternOverrideId;
                tool.defaultWaveGroupGuid = buffer_.waveGroupGuid;
            }
            float duplicateOffset[3]{
                tool.duplicateOffset.x,
                tool.duplicateOffset.y,
                tool.duplicateOffset.z};
            if (ImGui::DragFloat3(
                    "複製時のずれ", duplicateOffset,
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
            "敵の変形ハンドル", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    CourseEnemyTransformGizmoSettings settings = context.transformGizmo->Settings();
    int mode = static_cast<int>(settings.mode);
    const char* modes[] = {"移動", "拡大・縮小", "回転"};
    if (ImGui::Combo("操作の種類", &mode, modes, 3)) {
        settings.mode = static_cast<EditorTransformGizmoMode>(mode);
    }
    int space = settings.space == EditorTransformGizmoSpace::World ? 0 : 1;
    const char* spaces[] = {"ワールド基準", "レール基準"};
    if (ImGui::Combo("座標の基準", &space, spaces, 2)) {
        settings.space = space == 0
            ? EditorTransformGizmoSpace::World : EditorTransformGizmoSpace::Local;
    }
    ImGui::Checkbox("一定の刻みに合わせる", &settings.snapEnabled);
    if (settings.mode == EditorTransformGizmoMode::Translate) {
        ImGui::DragFloat("移動の刻み幅", &settings.translationSnap,
            0.1f, 0.01f, 1000.0f, "%.3f");
    } else if (settings.mode == EditorTransformGizmoMode::Rotate) {
        ImGui::DragFloat("回転の刻み角", &settings.rotationSnapDegrees,
            1.0f, 0.1f, 180.0f, "%.1f deg");
    } else {
        ImGui::DragFloat("拡大率の刻み幅", &settings.scaleSnap,
            0.01f, 0.001f, 10.0f, "%.3f");
    }
    ImGui::SliderFloat("ハンドルの大きさ", &settings.handleLengthScale,
        0.1f, 4.0f, "%.2f");
    ApplyValidatedSettings(*context.transformGizmo, settings);
    if (context.viewportTool != nullptr) {
        CourseEnemyViewportEditSettings tool = context.viewportTool->Settings();
        tool.offsetSnap = settings.snapEnabled;
        tool.offsetSnapSize = settings.translationSnap;
        ApplyValidatedSettings(*context.viewportTool, std::move(tool));
    }
    ImGui::Text("選択中の敵配置: %u",
        context.transformGizmo->State().selectedPlacementCount);
    ImGui::Text("選択候補: %s / 操作中: %s",
        GizmoHandleLabel(context.transformGizmo->State().hovered),
        GizmoHandleLabel(context.transformGizmo->State().active));
    if (context.transformGizmo->State().containsLockedPlacement) {
        ImGui::TextDisabled("変形するには、選択した敵のロックをすべて解除してください。");
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
