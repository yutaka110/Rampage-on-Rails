#include "CourseRideProfileDetailsPanel.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cstring>

namespace editor {
namespace {

template <std::size_t N>
void CopyText(std::array<char, N>& destination, const std::string& source) {
    destination.fill('\0');
    if constexpr (N > 1) {
        std::memcpy(destination.data(), source.data(),
            (std::min)(source.size(), N - 1));
    }
}

constexpr CourseRideSpeedMode kSpeedModes[]{
    CourseRideSpeedMode::Inherit,
    CourseRideSpeedMode::Cruise,
    CourseRideSpeedMode::Combat,
    CourseRideSpeedMode::HighSpeed,
    CourseRideSpeedMode::Tunnel,
    CourseRideSpeedMode::Boss,
    CourseRideSpeedMode::Setpiece,
    CourseRideSpeedMode::Cinematic,
    CourseRideSpeedMode::Recovery,
};

const char* SpeedModeLabel(CourseRideSpeedMode mode) {
    switch (mode) {
    case CourseRideSpeedMode::Inherit: return "コース設定を継承";
    case CourseRideSpeedMode::Cruise: return "通常走行";
    case CourseRideSpeedMode::Combat: return "戦闘";
    case CourseRideSpeedMode::HighSpeed: return "高速走行";
    case CourseRideSpeedMode::Tunnel: return "トンネル";
    case CourseRideSpeedMode::Boss: return "ボス戦";
    case CourseRideSpeedMode::Setpiece: return "特殊演出";
    case CourseRideSpeedMode::Cinematic: return "映像演出";
    case CourseRideSpeedMode::Recovery: return "復帰";
    }
    return "不明";
}

} // namespace

CourseRideProfileDefinition* CourseRideProfileDetailsPanel::FindProfile(
    CourseAsset* course,
    std::string_view guid) {
    if (course == nullptr) return nullptr;
    const auto found = std::find_if(
        course->rideProfiles.begin(), course->rideProfiles.end(),
        [guid](const CourseRideProfileDefinition& profile) {
            return profile.editorGuid == guid;
        });
    return found != course->rideProfiles.end() ? &*found : nullptr;
}

const CourseRideProfileDefinition* CourseRideProfileDetailsPanel::FindProfile(
    const CourseAsset* course,
    std::string_view guid) {
    return FindProfile(const_cast<CourseAsset*>(course), guid);
}

bool CourseRideProfileDetailsPanel::HandlesSelection(
    const EditorSequencerService* sequencer,
    const CourseRideSequencerTrackBridge* bridge) const {
    if (sequencer == nullptr || bridge == nullptr ||
        sequencer->Selection().size() != 1) {
        return false;
    }
    return bridge->IsRideProfileHandle(sequencer->Selection().front());
}

void CourseRideProfileDetailsPanel::Draw(
    const CourseRideProfileDetailsPanelContext& context) {
    if (context.course == nullptr || context.bridge == nullptr ||
        context.sequencer == nullptr ||
        !HandlesSelection(context.sequencer, context.bridge)) {
        CancelEdit(context.course);
        return;
    }
    const EditorSequencerKeyHandle& handle =
        context.sequencer->Selection().front();
    const std::string guid = context.bridge->ProfileGuid(handle);
    if (continuousEditActive_ && guid != selectedGuid_) {
        CancelEdit(context.course);
    }
    SyncProfile(context, guid);
    const CourseRideProfileDefinition* source =
        FindProfile(context.course, guid);
    if (source == nullptr) {
        ImGui::TextDisabled("選択した走行設定は存在しません。");
        return;
    }

    ImGui::TextUnformatted("区間の走行設定");
    ImGui::TextDisabled("GUID: %s", source->editorGuid.c_str());
    ImGui::Text("範囲: %.2f〜%.2f m（長さ %.2f m）",
        buffer_.startDistance,
        buffer_.endDistance,
        buffer_.endDistance - buffer_.startDistance);
    if (continuousEditActive_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f), "調整をプレビュー中");
    }

    const bool canEdit = context.canMutateAuthoring && !buffer_.editorLocked;
    const auto editFloat = [&](const char* label, float* value, float speed,
                               float minimum, float maximum,
                               const char* transactionLabel) {
        const bool changed = ImGui::DragFloat(
            label, value, speed, minimum, maximum, "%.3f");
        if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
        if (changed) RefreshPreview(context);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitContinuousEdit(context, transactionLabel);
        }
    };

    ImGui::BeginDisabled(!canEdit);
    const bool nameChanged = ImGui::InputText(
        "表示名", displayNameBuffer_.data(), displayNameBuffer_.size());
    if (nameChanged) ReadTextBuffers();
    if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
    if (nameChanged) RefreshPreview(context);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CommitContinuousEdit(context, "Rename Ride Profile");
    }

    const float maximumDistance = (std::max)(
        context.railLength, buffer_.endDistance + 1.0f);
    editFloat("開始距離", &buffer_.startDistance, 0.5f, 0.0f,
        maximumDistance, "Move Ride Profile Start");
    buffer_.startDistance = (std::clamp)(
        buffer_.startDistance, 0.0f, buffer_.endDistance - 0.001f);
    editFloat("終了距離", &buffer_.endDistance, 0.5f,
        buffer_.startDistance + 0.001f, maximumDistance,
        "Move Ride Profile End");
    buffer_.endDistance = (std::max)(
        buffer_.endDistance, buffer_.startDistance + 0.001f);

    if (ImGui::BeginCombo(
            "走行モード", SpeedModeLabel(buffer_.speedMode))) {
        for (const CourseRideSpeedMode mode : kSpeedModes) {
            if (ImGui::Selectable(
                    SpeedModeLabel(mode), mode == buffer_.speedMode)) {
                CourseRideProfileDefinition changed = buffer_;
                changed.speedMode = mode;
                CommitProfile(context, changed, "Set Ride Profile Speed Mode");
            }
        }
        ImGui::EndCombo();
    }
    editFloat("速度の倍率", &buffer_.speedMultiplier, 0.01f,
        0.0f, 4.0f, "Edit Ride Speed Multiplier");
    editFloat("目標速度", &buffer_.targetSpeedOverride,
        0.25f, -1.0f, 1000.0f, "Edit Ride Target Speed");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("-1の場合は、区間に入る前の速度設定を継承します。");

    ImGui::SeparatorText("加減速の調整");
    editFloat("加速の倍率", &buffer_.accelerationScale,
        0.01f, 0.05f, 4.0f, "Edit Ride Acceleration Scale");
    editFloat("減速の倍率", &buffer_.brakingScale,
        0.01f, 0.05f, 4.0f, "Edit Ride Braking Scale");
    editFloat("加速度変化の上限", &buffer_.maximumJerk,
        1.0f, 0.1f, 2000.0f, "Edit Ride Maximum Jerk");
    editFloat("カーブ先読み距離", &buffer_.cornerEntryLookAheadDistance,
        0.5f, 0.1f, 1000.0f, "Edit Ride Corner Look Ahead");
    editFloat("カーブ速度の倍率", &buffer_.cornerSpeedScale,
        0.01f, 0.1f, 2.0f, "Edit Ride Corner Speed Scale");

    ImGui::SeparatorText("曲がり方の調整");
    editFloat("旋回の先読み距離", &buffer_.turnAnticipationDistance,
        0.5f, 0.0f, 500.0f, "Edit Ride Turn Anticipation");
    editFloat("車体傾きの倍率", &buffer_.visualBankScale,
        0.01f, 0.0f, 4.0f, "Edit Ride Bank Scale");
    editFloat("車体傾きの上限角", &buffer_.maximumVisualBankDegrees,
        0.25f, 0.0f, 60.0f, "Edit Ride Maximum Bank");

    ImGui::SeparatorText("区間の切り替え");
    const float intervalLength = (std::max)(
        0.001f, buffer_.endDistance - buffer_.startDistance);
    editFloat("入りの切替距離", &buffer_.blendInDistance,
        0.25f, 0.0f, intervalLength, "Edit Ride Blend In");
    buffer_.blendInDistance = (std::clamp)(
        buffer_.blendInDistance, 0.0f, intervalLength);
    editFloat("戻りの切替距離", &buffer_.blendOutDistance,
        0.25f, 0.0f, intervalLength, "Edit Ride Blend Out");
    buffer_.blendOutDistance = (std::clamp)(
        buffer_.blendOutDistance, 0.0f, intervalLength);

    ImGui::SeparatorText("区間で使用するカメラ");
    const char* cameraLabel = buffer_.cameraShotId.empty()
        ? "なし" : buffer_.cameraShotId.c_str();
    if (ImGui::BeginCombo("カメラ区間", cameraLabel)) {
        if (ImGui::Selectable("なし", buffer_.cameraShotId.empty())) {
            CourseRideProfileDefinition changed = buffer_;
            changed.cameraShotId.clear();
            CommitProfile(context, changed, "Clear Ride Camera Shot");
        }
        for (const CourseCinematicCameraShot& shot :
             context.course->cinematicCameraShots) {
            if (shot.id.empty()) continue;
            if (ImGui::Selectable(
                    shot.id.c_str(), shot.id == buffer_.cameraShotId)) {
                CourseRideProfileDefinition changed = buffer_;
                changed.cameraShotId = shot.id;
                CommitProfile(context, changed, "Set Ride Camera Shot");
            }
        }
        ImGui::EndCombo();
    }
    const bool cameraTextChanged = ImGui::InputText(
        "カメラ区間ID", cameraShotBuffer_.data(), cameraShotBuffer_.size());
    if (cameraTextChanged) ReadTextBuffers();
    if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
    if (cameraTextChanged) RefreshPreview(context);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CommitContinuousEdit(context, "Edit Ride Camera Shot ID");
    }

    bool enabled = buffer_.enabled;
    if (ImGui::Checkbox("ゲーム内で有効", &enabled)) {
        CourseRideProfileDefinition changed = buffer_;
        changed.enabled = enabled;
        CommitProfile(context, changed, "Set Ride Profile Gameplay State");
    }
    ImGui::EndDisabled();

    bool visible = buffer_.editorVisible;
    ImGui::BeginDisabled(!context.canMutateAuthoring);
    if (ImGui::Checkbox("マップに表示", &visible)) {
        CourseRideProfileDefinition changed = buffer_;
        changed.editorVisible = visible;
        CommitProfile(context, changed, "Set Ride Profile Visibility");
    }
    bool locked = buffer_.editorLocked;
    if (ImGui::Checkbox("編集をロック", &locked)) {
        CourseRideProfileDefinition changed = buffer_;
        changed.editorLocked = locked;
        CommitProfile(context, changed, "Set Ride Profile Lock");
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("プレビュー");
    if (ImGui::Button("開始位置")) {
        context.sequencer->SetPreviewPosition(buffer_.startDistance, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("中央位置")) {
        context.sequencer->SetPreviewPosition(
            (buffer_.startDistance + buffer_.endDistance) * 0.5f, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("終了位置")) {
        context.sequencer->SetPreviewPosition(buffer_.endDistance, true);
    }
    ImGui::TextDisabled(
        "確認位置: %.2f m", context.sequencer->PreviewPosition());

    std::string validationError;
    if (!buffer_.Validate(&validationError)) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", validationError.c_str());
    }
    if (!lastMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", lastMessage_.c_str());
    }
}

void CourseRideProfileDetailsPanel::SyncProfile(
    const CourseRideProfileDetailsPanelContext& context,
    std::string_view guid) {
    if (continuousEditActive_) return;
    const CourseRideProfileDefinition* profile = FindProfile(context.course, guid);
    if (profile == nullptr) return;
    const std::string payload = context.bridge->BuildProfileState(*profile).payload;
    if (selectedGuid_ == guid && syncedPayload_ == payload) return;
    selectedGuid_ = std::string(guid);
    syncedPayload_ = payload;
    buffer_ = *profile;
    SyncTextBuffers();
}

void CourseRideProfileDetailsPanel::BeginContinuousEdit(
    const CourseRideProfileDetailsPanelContext& context) {
    if (continuousEditActive_ || !context.canMutateAuthoring) return;
    const CourseRideProfileDefinition* profile =
        FindProfile(context.course, selectedGuid_);
    if (profile == nullptr || profile->editorLocked) return;
    editOriginal_ = *profile;
    previewCourse_ = context.course;
    continuousEditActive_ = true;
    previewApplied_ = false;
}

void CourseRideProfileDetailsPanel::RefreshPreview(
    const CourseRideProfileDetailsPanelContext& context) {
    if (!continuousEditActive_ || context.course == nullptr) return;
    ReadTextBuffers();
    std::string error;
    if (!buffer_.Validate(&error)) {
        lastMessage_ = error;
        return;
    }
    CourseRideProfileDefinition* profile =
        FindProfile(context.course, selectedGuid_);
    if (profile == nullptr) return;
    *profile = buffer_;
    profile->editorGuid = selectedGuid_;
    context.course->SortForRuntime();
    previewApplied_ = true;
    lastMessage_ = "走行設定の調整をプレビュー中です。";
}

void CourseRideProfileDetailsPanel::RestorePreview(CourseAsset* course) {
    if (!previewApplied_ || course == nullptr) return;
    CourseRideProfileDefinition* profile = FindProfile(course, selectedGuid_);
    if (profile != nullptr) *profile = editOriginal_;
    course->SortForRuntime();
    previewApplied_ = false;
}

void CourseRideProfileDetailsPanel::CommitContinuousEdit(
    const CourseRideProfileDetailsPanelContext& context,
    std::string_view label) {
    if (!continuousEditActive_) return;
    ReadTextBuffers();
    const CourseRideProfileDefinition changed = buffer_;
    RestorePreview(context.course);
    continuousEditActive_ = false;
    previewCourse_ = nullptr;
    if (!CommitProfile(context, changed, label)) {
        buffer_ = editOriginal_;
        SyncTextBuffers();
    }
}

bool CourseRideProfileDetailsPanel::CommitProfile(
    const CourseRideProfileDetailsPanelContext& context,
    const CourseRideProfileDefinition& profile,
    std::string_view label) {
    if (context.course == nullptr || context.bridge == nullptr ||
        context.sequencer == nullptr || !context.canMutateAuthoring) {
        lastMessage_ = "現在は走行設定を編集できません。";
        return false;
    }
    const CourseRideProfileDefinition* current =
        FindProfile(context.course, selectedGuid_);
    if (current == nullptr) {
        lastMessage_ = "選択した走行設定は存在しません。";
        return false;
    }
    CourseRideProfileDefinition changed = profile;
    changed.editorGuid = selectedGuid_;
    std::string validationError;
    if (!changed.Validate(&validationError)) {
        lastMessage_ = validationError;
        return false;
    }
    const EditorSequencerKeyState before =
        context.bridge->BuildProfileState(*current);
    const EditorSequencerKeyState after =
        context.bridge->BuildProfileState(changed);
    if (!context.sequencer->CommitKeyStateChange(
            label, before, after, validationError)) {
        lastMessage_ = validationError;
        return false;
    }
    buffer_ = changed;
    syncedPayload_ = after.payload;
    SyncTextBuffers();
    lastMessage_ = std::string(label) + ".";
    return true;
}

void CourseRideProfileDetailsPanel::CancelEdit(CourseAsset* course) {
    RestorePreview(course != nullptr ? course : previewCourse_);
    continuousEditActive_ = false;
    previewCourse_ = nullptr;
    previewApplied_ = false;
    selectedGuid_.clear();
    syncedPayload_.clear();
}

void CourseRideProfileDetailsPanel::SyncTextBuffers() {
    CopyText(displayNameBuffer_, buffer_.displayName);
    CopyText(cameraShotBuffer_, buffer_.cameraShotId);
}

void CourseRideProfileDetailsPanel::ReadTextBuffers() {
    buffer_.displayName = displayNameBuffer_.data();
    buffer_.cameraShotId = cameraShotBuffer_.data();
}

} // namespace editor
