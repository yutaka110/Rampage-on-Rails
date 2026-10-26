#include "CourseCameraShotDetailsPanel.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace editor {
namespace {

const char* ViewModeLabel(CourseCameraViewMode mode) {
    switch (mode) {
    case CourseCameraViewMode::Inherit: return "通常追従";
    case CourseCameraViewMode::FirstPerson: return "一人称";
    case CourseCameraViewMode::Rear: return "後方視点";
    case CourseCameraViewMode::CloseUp: return "アップ";
    case CourseCameraViewMode::Custom: return "自由構図";
    }
    return "不明";
}

constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kDegreesToRadians = 0.017453292519943295f;

template <std::size_t N>
void CopyText(std::array<char, N>& destination, const std::string& source) {
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(),
        (std::min)(source.size(), N - 1));
}

bool ValidShot(const CourseCinematicCameraShot& shot, std::string& error) {
    if (shot.id.empty()) error = "カメラ区間IDを入力してください。";
    else if (!std::isfinite(shot.startDistance) ||
        !std::isfinite(shot.endDistance) || shot.endDistance <= shot.startDistance) {
        error = "開始・終了距離を確認してください。";
    } else if (!std::isfinite(shot.blendInDistance) || shot.blendInDistance < 0.0f ||
        !std::isfinite(shot.blendOutDistance) || shot.blendOutDistance < 0.0f ||
        !std::isfinite(shot.weightScale) || shot.weightScale < 0.0f) {
        error = "切り替え距離と影響度を確認してください。";
    } else if (!shot.view.Validate(&error)) {
        return false;
    } else {
        error.clear();
    }
    return error.empty();
}

} // namespace

CourseCinematicCameraShot* CourseCameraShotDetailsPanel::FindShot(
    CourseAsset* course,
    std::string_view id) {
    if (course == nullptr) return nullptr;
    const auto found = std::find_if(
        course->cinematicCameraShots.begin(), course->cinematicCameraShots.end(),
        [id](const CourseCinematicCameraShot& shot) { return shot.id == id; });
    return found == course->cinematicCameraShots.end() ? nullptr : &*found;
}

bool CourseCameraShotDetailsPanel::HandlesSelection(
    const EditorSequencerService* sequencer,
    const CourseSequencerTrackProvider* provider) const {
    return sequencer != nullptr && provider != nullptr &&
        sequencer->Selection().size() == 1 &&
        provider->IsCameraShotHandle(sequencer->Selection().front());
}

void CourseCameraShotDetailsPanel::Draw(
    const CourseCameraShotDetailsPanelContext& context) {
    if (!HandlesSelection(context.sequencer, context.provider) ||
        context.course == nullptr) {
        CancelEdit(context.course);
        return;
    }
    const std::string id = context.provider->CameraShotId(
        context.sequencer->Selection().front());
    if (continuousEditActive_ && id != selectedId_) CancelEdit(context.course);
    Sync(context, id);
    if (FindShot(context.course, id) == nullptr) {
        ImGui::TextDisabled("選択したカメラ区間は存在しません。");
        return;
    }

    ImGui::TextUnformatted("カメラ区間の設定");
    ImGui::TextDisabled("ID: %s", selectedId_.c_str());
    if (continuousEditActive_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f), "調整をプレビュー中");
    }
    DrawPreviewControls(context);

    const auto editFloat = [&](const char* label, float* value, float speed,
                               float minimum, float maximum,
                               const char* transactionLabel) {
        const bool changed = ImGui::DragFloat(
            label, value, speed, minimum, maximum, "%.3f");
        if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
        if (changed) RefreshPreview(context);
        if (ImGui::IsItemDeactivatedAfterEdit())
            CommitContinuousEdit(context, transactionLabel);
    };
    const auto editDegrees = [&](const char* label, float* radians,
                                 float minimum, float maximum,
                                 const char* transactionLabel) {
        float degrees = *radians * kRadiansToDegrees;
        const bool changed = ImGui::DragFloat(
            label, &degrees, 0.1f, minimum, maximum, "%.2f deg");
        if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
        if (changed) {
            *radians = degrees * kDegreesToRadians;
            RefreshPreview(context);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            CommitContinuousEdit(context, transactionLabel);
    };

    ImGui::BeginDisabled(!context.canMutateAuthoring);
    const float maximumDistance = (std::max)(
        context.railLength, buffer_.endDistance + 1.0f);
    editFloat("開始距離", &buffer_.startDistance, 0.5f, 0.0f,
        maximumDistance, "Move Camera Shot Start");
    buffer_.startDistance = (std::min)(
        buffer_.startDistance, buffer_.endDistance - 0.001f);
    editFloat("終了距離", &buffer_.endDistance, 0.5f,
        buffer_.startDistance + 0.001f, maximumDistance,
        "Move Camera Shot End");

    const bool modeChanged = ImGui::InputText(
        "分類タグ", modeBuffer_.data(), modeBuffer_.size());
    if (modeChanged) ReadTextBuffers();
    if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
    if (modeChanged) RefreshPreview(context);
    if (ImGui::IsItemDeactivatedAfterEdit())
        CommitContinuousEdit(context, "Edit Camera Shot Mode");

    ImGui::SeparatorText("構図の初期設定");
    int viewMode = static_cast<int>(buffer_.view.mode);
    if (ImGui::Combo("初期構図", &viewMode, "通常追従\0一人称\0後方視点\0アップ\0自由構図\0")) {
        auto changed = buffer_;
        const int priority = changed.view.priority;
        if (viewMode == static_cast<int>(CourseCameraViewMode::Custom))
            changed.view.mode = CourseCameraViewMode::Custom;
        else changed.view = CourseCameraViewDefinition::Preset(static_cast<CourseCameraViewMode>(viewMode));
        changed.view.priority = priority;
        composition_.Stop();
        if (CommitShot(context, changed, "Apply Camera Preset") && cameraPreview_)
            PreviewAt(context, (buffer_.startDistance + buffer_.endDistance)*0.5f, false);
    }
    ImGui::TextWrapped("初期構図を選んだ後、画面や3Dマップ上で自由に調整できます。");
    if (buffer_.view.mode != CourseCameraViewMode::Inherit) {
        bool hide = buffer_.view.hideVehicle;
        if (ImGui::Checkbox("自機と搭乗者を隠す", &hide)) {
            auto changed = buffer_; changed.view.hideVehicle = hide;
            if (CommitShot(context, changed, "Set Camera Vehicle Visibility"))
                composition_.SetHideVehicle(hide);
        }
    }
    if (ImGui::CollapsingHeader("位置・向き・優先度の詳細")) {
    int priority = buffer_.view.priority;
    if (ImGui::InputInt("優先度", &priority)) {
        auto changed = buffer_;
        changed.view.priority = (std::clamp)(priority, -1000, 1000);
        CommitShot(context, changed, "Set Camera Priority");
    }
    ImGui::TextWrapped("重複時は優先度が高い区間を優先し、同じ場合は影響度で選びます。");
    if (buffer_.view.mode != CourseCameraViewMode::Inherit) {
        editFloat("撮影位置・左右", &buffer_.view.positionOffset.x, 0.05f, -1000, 1000, "Move Camera Side");
        editFloat("撮影位置・高さ", &buffer_.view.positionOffset.y, 0.05f, -1000, 1000, "Move Camera Height");
        editFloat("撮影位置・前後", &buffer_.view.positionOffset.z, 0.05f, -1000, 1000, "Move Camera Forward");
        editDegrees("左右の向き", &buffer_.view.yaw, -360, 360, "Rotate Camera Yaw");
        editDegrees("上下の向き", &buffer_.view.pitch, -85, 85, "Rotate Camera Pitch");
        editDegrees("画角", &buffer_.view.fovY, 15, 110, "Edit Camera Lens");
        ImGui::TextWrapped("位置はレール基準。左右の向きは0度が進行方向、180度が後方です。");
    }
    }

    ImGui::SeparatorText("参照する設定");
    const char* presetLabel = buffer_.presetId.empty()
        ? "なし" : buffer_.presetId.c_str();
    if (ImGui::BeginCombo("補正プリセット", presetLabel)) {
        if (ImGui::Selectable("なし", buffer_.presetId.empty())) {
            CourseCinematicCameraShot changed = buffer_;
            changed.presetId.clear();
            CommitShot(context, changed, "Clear Camera Shot Preset");
        }
        for (const CourseCameraShotPreset& preset : context.course->cameraShotPresets) {
            if (ImGui::Selectable(preset.id.c_str(), preset.id == buffer_.presetId)) {
                CourseCinematicCameraShot changed = buffer_;
                changed.presetId = preset.id;
                CommitShot(context, changed, "Set Camera Shot Preset");
            }
        }
        ImGui::EndCombo();
    }
    const char* blendLabel = buffer_.blendAssetId.empty()
        ? "なし" : buffer_.blendAssetId.c_str();
    if (ImGui::BeginCombo("切替設定アセット", blendLabel)) {
        if (ImGui::Selectable("なし", buffer_.blendAssetId.empty())) {
            CourseCinematicCameraShot changed = buffer_;
            changed.blendAssetId.clear();
            CommitShot(context, changed, "Clear Camera Blend Asset");
        }
        for (const CourseCameraBlendAsset& blend : context.course->cameraBlendAssets) {
            if (ImGui::Selectable(blend.id.c_str(), blend.id == buffer_.blendAssetId)) {
                CourseCinematicCameraShot changed = buffer_;
                changed.blendAssetId = blend.id;
                CommitShot(context, changed, "Set Camera Blend Asset");
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("切り替えの調整");
    editFloat("入りの切替距離", &buffer_.blendInDistance,
        0.25f, 0.0f, 1000.0f, "Edit Camera Blend In");
    editFloat("戻りの切替距離", &buffer_.blendOutDistance,
        0.25f, 0.0f, 1000.0f, "Edit Camera Blend Out");
    editFloat("影響度の倍率", &buffer_.weightScale,
        0.01f, 0.0f, 4.0f, "Edit Camera Shot Weight");

    ImGui::SeparatorText("通常追従の補正");
    ImGui::BeginDisabled(buffer_.view.mode != CourseCameraViewMode::Inherit);
    editFloat("後方への距離", &buffer_.backDistanceOffset,
        0.05f, -100.0f, 100.0f, "Edit Camera Back Offset");
    editFloat("高さの補正", &buffer_.verticalOffset,
        0.05f, -100.0f, 100.0f, "Edit Camera Vertical Offset");
    editFloat("左右の補正", &buffer_.lateralOffset,
        0.05f, -100.0f, 100.0f, "Edit Camera Lateral Offset");
    editFloat("先読み距離の補正", &buffer_.lookAheadOffset,
        0.1f, -200.0f, 200.0f, "Edit Camera Look Ahead");
    editFloat("注視点・高さ", &buffer_.lookUpOffset,
        0.05f, -100.0f, 100.0f, "Edit Camera Look Up");
    editFloat("注視点・前後", &buffer_.lookForwardOffset,
        0.05f, -100.0f, 100.0f, "Edit Camera Look Forward");
    editDegrees("画角の補正", &buffer_.fovOffset,
        -45.0f, 45.0f, "Edit Camera FOV Offset");
    ImGui::EndDisabled();
    if (buffer_.view.mode != CourseCameraViewMode::Inherit)
        ImGui::TextWrapped("一人称などの構図は、上の撮影位置・向きで調整してください。");
    editDegrees("画面の傾き補正", &buffer_.rollOffset,
        -90.0f, 90.0f, "Edit Camera Roll Offset");
    editFloat("揺れの強さ", &buffer_.shakeAmount,
        0.01f, 0.0f, 4.0f, "Edit Camera Shake");
    ImGui::EndDisabled();


}

void CourseCameraShotDetailsPanel::BeginComposition(const CourseCameraShotDetailsPanelContext& context) {
    const float distance = cameraPreview_ && context.preview &&
        context.preview->Frame().distance > buffer_.startDistance &&
        context.preview->Frame().distance < buffer_.endDistance
        ? context.preview->Frame().distance : (buffer_.startDistance + buffer_.endDistance)*0.5f;
    PreviewAt(context, distance, false);
    if (cameraPreview_) {
        composition_.Begin(buffer_.view.hideVehicle);
        lastMessage_ = "構図を調整中です。固定バーの「構図を反映」で区間に登録できます。";
    }
}

void CourseCameraShotDetailsPanel::DrawPreviewControls(const CourseCameraShotDetailsPanelContext& context, bool map) {
    ImGui::SeparatorText("構図の調整と確認");
    ImGui::BeginDisabled(!context.canMutateAuthoring || !context.canPreview || !context.preview);
    ImGui::BeginDisabled(map && composition_.Active());
    if (ImGui::Button(map ? "構図を調整する" : "画面上で構図を調整")) BeginComposition(context);
    ImGui::EndDisabled();
    if (!map && context.onOpenMapEditor) {
        if (ImGui::Button("3Dマップで編集")) {
            if (!composition_.Active()) BeginComposition(context);
            context.onOpenMapEditor();
        }
    }
    if (composition_.Active()) {
        if (map) ImGui::TextWrapped("撮影位置・注視点、またはXYZ軸をドラッグ。Escで移動を取り消します。");
        else ImGui::TextWrapped("右ドラッグ: 視線 / 右ボタン＋WASD・Q/E: 移動 / 右ボタン＋ホイール: 前後移動 / F: 自機へ向ける");
        ImGui::BeginDisabled(!composition_.Ready());
        if (!map) {
        if (ImGui::Button("左へ")) composition_.Nudge(-0.5f, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button("右へ")) composition_.Nudge(0.5f, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button("上へ")) composition_.Nudge(0, 0.5f, 0);
        ImGui::SameLine();
        if (ImGui::Button("下へ")) composition_.Nudge(0, -0.5f, 0);
        if (ImGui::Button("近づく")) composition_.Nudge(0, 0, 1);
        ImGui::SameLine();
        if (ImGui::Button("遠ざかる")) composition_.Nudge(0, 0, -1);
        ImGui::SameLine();
        if (ImGui::Button("自機へ向ける")) composition_.LookAtVehicle();
        } else if (ImGui::Button("自機へ向ける")) composition_.LookAtVehicle();
        float lens = composition_.FovY() * kRadiansToDegrees;
        if (ImGui::SliderFloat("画角", &lens, 15, 110, "%.1f deg"))
            composition_.SetFovY(lens * kDegreesToRadians);
        bool hide = composition_.HideVehicle();
        if (ImGui::Checkbox("自機を非表示にする", &hide)) composition_.SetHideVehicle(hide);
        if (!map) {
        if (ImGui::Button("構図を反映")) RegisterComposition(context, false);
        ImGui::SameLine();
        if (ImGui::Button("反映して再生")) RegisterComposition(context, true);
        }
        ImGui::EndDisabled();
        if (ImGui::Button("調整を破棄して戻す")) {
            composition_.Stop();
            lastMessage_ = "未反映の調整を破棄し、登録済みの構図に戻しました。";
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!context.canPreview || context.preview == nullptr);
    ImGui::BeginDisabled(map && composition_.Changed());
    if (ImGui::Button("開始位置")) PreviewAt(context, buffer_.startDistance, false);
    ImGui::SameLine();
    if (ImGui::Button("中央位置")) PreviewAt(context, (buffer_.startDistance + buffer_.endDistance)*0.5f, false);
    ImGui::SameLine();
    if (ImGui::Button("終了位置")) PreviewAt(context, buffer_.endDistance, false);
    ImGui::EndDisabled();
    if (!map) {
    if (ImGui::Button("区間を再生")) PreviewAt(context, buffer_.startDistance, true);
    if (cameraPreview_ && context.preview && context.preview->IsActive()) {
        ImGui::SameLine();
        if (ImGui::Button(context.preview->IsPlaying() ? "一時停止" : "再開")) {
            composition_.Stop();
            if (context.preview->IsPlaying()) context.preview->Pause(); else context.preview->Play();
        }
        ImGui::SameLine();
        if (ImGui::Button("プレビュー終了")) StopPreview(context);
    }
    }
    if (cameraPreview_ && context.preview && context.preview->IsActive()) {
        float distance = context.preview->Frame().distance;
        ImGui::BeginDisabled(map && composition_.Changed());
        if (ImGui::SliderFloat("確認する距離", &distance, 0, context.railLength, "%.2f m"))
            PreviewAt(context, distance, false);
        ImGui::EndDisabled();
        float speed = context.preview->Settings().travelSpeed;
        if (ImGui::DragFloat("再生速度（0はコース設定）", &speed, 1, 0, 300)) {
            auto settings = context.preview->Settings(); settings.travelSpeed = speed;
            context.preview->Configure(settings);
        }
        ImGui::TextDisabled("確認位置: %.2f m", context.preview->Frame().distance);
    }
    ImGui::EndDisabled();
    if (!map && !lastMessage_.empty()) ImGui::TextWrapped("%s", lastMessage_.c_str());
}

void CourseCameraShotDetailsPanel::DrawEditor(const CourseCameraShotDetailsPanelContext& context) {
    if (!context.course || !context.provider || !context.sequencer) {
        ImGui::TextDisabled("コースを開くとカメラ区間を編集できます。"); return;
    }
    ImGui::TextWrapped("区間を選び、構図を調整して「反映して再生」で確認します。一人称などは初期構図として使えます。");
    if (cameraPreview_ && ImGui::Button("プレビューを終了して戻る")) StopPreview(context);
    ImGui::BeginDisabled(!context.canMutateAuthoring);
    ImGui::Combo("追加時の構図", &newViewMode_, "通常追従\0一人称\0後方視点\0アップ\0自由構図\0");
    ImGui::DragFloat("区間の長さ", &newShotLength_, 1, 1, (std::max)(1.0f, context.railLength));
    if (ImGui::Button("現在位置に区間を追加")) AddSection(context);
    ImGui::EndDisabled();
    if (ImGui::BeginListBox("カメラ区間一覧", ImVec2(-1, 95))) {
        for (const auto& shot : context.course->cinematicCameraShots) {
            const auto state = context.provider->BuildCameraShotState(shot);
            const std::string label = shot.id + "  [" + std::to_string(int(shot.startDistance)) +
                  " - " + std::to_string(int(shot.endDistance)) + "]  " + ViewModeLabel(shot.view.mode);
            if (ImGui::Selectable(label.c_str(), context.sequencer->IsSelected(state.handle)))
                context.sequencer->Select(state.handle, false, false);
        }
        ImGui::EndListBox();
    }
    if (HandlesSelection(context.sequencer, context.provider)) {
        ImGui::BeginDisabled(!context.canMutateAuthoring);
        if (ImGui::Button("選択した区間を削除")) {
            CancelEdit(context.course);
            EditorSequencerKeyState before;
            if (context.sequencer->CaptureKeyState(context.sequencer->Selection().front(), before, lastMessage_)) {
                EditorSequencerKeyState after; after.handle = before.handle;
                if (context.sequencer->CommitKeyStateChange("Delete Camera Section", before, after, lastMessage_))
                    context.sequencer->ClearSelection();
            }
        }
        ImGui::EndDisabled();
        if (HandlesSelection(context.sequencer, context.provider)) {
            Sync(context, context.provider->CameraShotId(context.sequencer->Selection().front()));
            ImGui::TextDisabled("右側の詳細設定で、区間の範囲や構図を調整できます。");
            DrawPreviewControls(context);
        }
    } else ImGui::TextDisabled("一覧から区間を選ぶか、現在位置に区間を追加してください。");
}

void CourseCameraShotDetailsPanel::AddSection(const CourseCameraShotDetailsPanelContext& context) {
        if (!context.canMutateAuthoring || !context.course || !context.provider || !context.sequencer) return;
        CancelEdit(context.course);
        CourseCinematicCameraShot shot;
        do { shot.id = "camera_view_" + std::to_string(++newShotSerial_); }
        while (FindShot(context.course, shot.id));
        shot.startDistance = (std::clamp)(context.currentDistance, 0.0f, (std::max)(0.0f, context.railLength-1));
        shot.endDistance = (std::min)(context.railLength, shot.startDistance+newShotLength_);
        shot.blendInDistance = shot.blendOutDistance = (std::min)(20.0f, (shot.endDistance-shot.startDistance)*0.2f);
        shot.view = CourseCameraViewDefinition::Preset(static_cast<CourseCameraViewMode>(newViewMode_));
        const auto after = context.provider->BuildCameraShotState(shot);
        EditorSequencerKeyState before; before.handle = after.handle;
        if (context.sequencer->CommitKeyStateChange("区間を追加", before, after, lastMessage_))
            context.sequencer->Select(after.handle, false, false);
}

bool CourseCameraShotDetailsPanel::AddMapSection(const CourseCameraShotDetailsPanelContext& context,float start,float end) {
    if (!context.canMutateAuthoring || !context.course || !context.provider || !context.sequencer ||
        composition_.Changed() || !std::isfinite(start) || !std::isfinite(end) || !std::isfinite(context.railLength) || context.railLength<1) return false;
    if (start>end) std::swap(start,end);
    start=(std::clamp)(start,0.0f,context.railLength);end=(std::clamp)(end,0.0f,context.railLength);
    if (end-start<1) { lastMessage_="区間は1m以上離して指定してください。";return false; }
    CourseCinematicCameraShot shot;
    do { shot.id="camera_view_"+std::to_string(++newShotSerial_); } while (FindShot(context.course,shot.id));
    shot.startDistance=start;shot.endDistance=end;
    shot.blendInDistance=shot.blendOutDistance=(std::min)(20.0f,(end-start)*0.2f);
    shot.view=CourseCameraViewDefinition::Preset(static_cast<CourseCameraViewMode>(newViewMode_));
    const auto after=context.provider->BuildCameraShotState(shot);
    EditorSequencerKeyState before;before.handle=after.handle;
    if (!context.sequencer->CommitKeyStateChange("Create Camera Section On 3D Rail",before,after,lastMessage_)) return false;
    context.sequencer->Select(after.handle,false,false);Sync(context,shot.id);
    if (context.canPreview && context.preview) PreviewAt(context,(start+end)*0.5f,false);
    lastMessage_="レール上に区間を作成しました。両端をドラッグして調整できます。保存は別途必要です。";
    return true;
}

bool CourseCameraShotDetailsPanel::SelectMapSection(const CourseCameraShotDetailsPanelContext& context,std::string_view id) {
    if (!context.course || !context.provider || !context.sequencer || composition_.Changed()) return false;
    const auto* shot=FindShot(context.course,id);if (!shot) return false;
    const auto state=context.provider->BuildCameraShotState(*shot);
    context.sequencer->Select(state.handle,false,false);Sync(context,id);
    return true;
}

bool CourseCameraShotDetailsPanel::SetMapSectionRange(const CourseCameraShotDetailsPanelContext& context,
    std::string_view id,float start,float end,bool previewStart) {
    if (!context.canMutateAuthoring || composition_.Changed() || !std::isfinite(start) || !std::isfinite(end) ||
        !std::isfinite(context.railLength) || context.railLength<1 || !context.course || !context.provider || !context.sequencer) return false;
    const auto* original=FindShot(context.course,id);if (!original) return false;
    start=(std::clamp)(start,0.0f,context.railLength);end=(std::clamp)(end,0.0f,context.railLength);
    if (end-start<1) return false;
    if (std::abs(original->startDistance-start)<0.001f && std::abs(original->endDistance-end)<0.001f) return true;
    if (!SelectMapSection(context,id)) return false;
    auto changed=buffer_;changed.startDistance=start;changed.endDistance=end;
    changed.blendInDistance=(std::min)(changed.blendInDistance,(end-start)*0.5f);
    changed.blendOutDistance=(std::min)(changed.blendOutDistance,(end-start)*0.5f);
    if (!CommitShot(context,changed,"Resize Camera Section On 3D Rail")) return false;
    if (context.canPreview && context.preview) PreviewAt(context,previewStart?start:end,false);
    lastMessage_="区間の範囲を変更しました。移動した端のゲーム映像を確認できます。保存は別途必要です。";
    return true;
}

void CourseCameraShotDetailsPanel::OnCourseSaved() {
    lastMessage_ = "コースをファイルに保存しました。";
}

void CourseCameraShotDetailsPanel::SyncMapSelection(const CourseCameraShotDetailsPanelContext& context) {
    if (HandlesSelection(context.sequencer, context.provider)) {
        const auto id=context.provider->CameraShotId(context.sequencer->Selection().front());
        if (FindShot(context.course,id)) { Sync(context,id);return; }
        // Undoing creation removes the selected key. Do not retain its handles
        // or continue playing a snapshot of a section that no longer exists.
        if (cameraPreview_) StopPreview(context);
    }
    CancelEdit(context.course);
}

bool CourseCameraShotDetailsPanel::ReturnToFollow(const CourseCameraShotDetailsPanelContext& context) {
    if (!context.canMutateAuthoring || !HandlesSelection(context.sequencer, context.provider)) return false;
    SyncMapSelection(context);
    CourseCinematicCameraShot changed{};
    changed.id = buffer_.id;
    changed.startDistance = buffer_.startDistance;
    changed.endDistance = buffer_.endDistance;
    changed.blendInDistance = buffer_.blendInDistance;
    changed.blendOutDistance = buffer_.blendOutDistance;
    changed.view.priority = buffer_.view.priority;
    if (!CommitShot(context, changed, "Restore Normal Follow Camera")) return false;
    composition_.Stop();
    if (cameraPreview_) PreviewAt(context, (buffer_.startDistance+buffer_.endDistance)*0.5f, false);
    lastMessage_ = "通常追従に戻しました。ファイルに保存すると確定します。取り消しも可能です。";
    return true;
}

const char* CourseCameraShotDetailsPanel::PlaybackStatus(
    const CoursePreviewSimulationSystem* preview) const {
    if (!cameraPreview_ || !preview || !preview->IsActive() || composition_.Active()) return nullptr;
    if (preview->IsPlaying()) return "プレビュー: 再生中";
    return preview->Frame().distance >= previewEndDistance_ - 0.001f
        ? "プレビュー: 区間終端で停止" : "プレビュー: 一時停止";
}

void CourseCameraShotDetailsPanel::DrawMapActions(const CourseCameraShotDetailsPanelContext& context) {
    const bool selected = SelectedShot() && HandlesSelection(context.sequencer, context.provider);
    const auto next = [](const char* label) {
        const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x*2;
        if (ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x > width + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
    };
    ImGui::BeginDisabled(!selected || !context.canMutateAuthoring || !context.canPreview || !composition_.Ready());
    if (ImGui::Button("構図を反映")) RegisterComposition(context, false);
    next("反映して再生");
    if (ImGui::Button("反映して再生")) RegisterComposition(context, true);
    ImGui::EndDisabled();
    next("区間を再生");
    ImGui::BeginDisabled(!selected || !context.canPreview || !context.preview || composition_.Changed());
    if (ImGui::Button("区間を再生")) PreviewAt(context, buffer_.startDistance, true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && composition_.Changed())
        ImGui::SetTooltip("調整中の構図は「反映して再生」で確認できます。");
    next("一時停止");
    ImGui::BeginDisabled(!cameraPreview_ || !context.preview || !context.preview->IsActive() || composition_.Active());
    if (ImGui::Button(context.preview && context.preview->IsPlaying() ? "一時停止" : "再開", {ImGui::CalcTextSize("一時停止").x+ImGui::GetStyle().FramePadding.x*2, 0})) {
        if (context.preview->IsPlaying()) context.preview->Pause(); else context.preview->Play();
    }
    ImGui::EndDisabled();
    next("プレビュー終了");
    ImGui::BeginDisabled(!cameraPreview_ || composition_.Changed());
    if (ImGui::Button("プレビュー終了")) StopPreview(context);
    ImGui::EndDisabled();
    next("通常追従に戻す");
    ImGui::BeginDisabled(!selected || !context.canMutateAuthoring);
    if (ImGui::Button("通常追従に戻す")) ReturnToFollow(context);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("選択区間の構図・揺れ・従来のカメラ補正を解除します。区間と切り替え距離は残ります。");
}

void CourseCameraShotDetailsPanel::DrawMapControls(const CourseCameraShotDetailsPanelContext& context) {
    if (!context.course || !context.provider || !context.sequencer) {
        ImGui::TextDisabled("コースを開くとカメラを編集できます。");return;
    }
    ImGui::PushID("MapCameraControls");
    SyncMapSelection(context);
    ImGui::BeginDisabled(composition_.Changed());
    if (ImGui::BeginCombo("編集する区間", selectedId_.empty() ? "区間を選択してください" : selectedId_.c_str())) {
        for (const auto& shot : context.course->cinematicCameraShots) {
            const auto state = context.provider->BuildCameraShotState(shot);
            const std::string label = shot.id + "  [" + std::to_string(int(shot.startDistance)) +
                " - " + std::to_string(int(shot.endDistance)) + "]";
            if (ImGui::Selectable(label.c_str(), context.sequencer->IsSelected(state.handle))) {
                context.sequencer->Select(state.handle, false, false);
                Sync(context, shot.id);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (composition_.Changed()) ImGui::TextWrapped("別の区間に移る前に、構図を反映するか調整を破棄してください。");
    if (SelectedShot() && HandlesSelection(context.sequencer, context.provider)) {
        ImGui::TextDisabled("区間: %.1f - %.1f m",buffer_.startDistance,buffer_.endDistance);
        ImGui::BeginDisabled(!context.canMutateAuthoring);
        ImGui::BeginDisabled(composition_.Changed());
        ImGui::TextWrapped("初期構図の選択は区間に直接反映されます。");
        int preset=static_cast<int>(buffer_.view.mode);
        if (ImGui::Combo("初期構図",&preset,"通常追従\0一人称\0後方視点\0アップ\0自由構図\0")) {
            auto changed=buffer_;
            if (preset==static_cast<int>(CourseCameraViewMode::Custom)) changed.view.mode=CourseCameraViewMode::Custom;
            else {
                changed.view=CourseCameraViewDefinition::Preset(static_cast<CourseCameraViewMode>(preset));
                changed.view.priority=buffer_.view.priority;
            }
            if (CommitShot(context,changed,"Apply Camera Preset")) BeginComposition(context);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        DrawPreviewControls(context,true);
    }
    ImGui::BeginDisabled(composition_.Changed());
    if (ImGui::CollapsingHeader("区間を追加")) {
        ImGui::BeginDisabled(!context.canMutateAuthoring);
        ImGui::Combo("追加時の構図",&newViewMode_,"通常追従\0一人称\0後方視点\0アップ\0自由構図\0");
        ImGui::DragFloat("区間の長さ",&newShotLength_,1,1,(std::max)(1.0f,context.railLength));
        if (ImGui::Button("現在位置に区間を追加")) AddSection(context);
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void CourseCameraShotDetailsPanel::PreviewAt(const CourseCameraShotDetailsPanelContext& context, float distance, bool play) {
    if (!context.preview || !context.course || !context.canPreview) return;
    composition_.Stop();
    const bool wasPreview = cameraPreview_;
    if (!cameraPreview_) {
        previousDistance_ = context.currentDistance;
        previousFreeze_ = context.freezeRuntime && *context.freezeRuntime;
    }
    if (!context.preview->BeginPreview(*context.course, distance, &lastMessage_)) {
        cameraPreview_ = false;
        if (wasPreview && context.freezeRuntime) *context.freezeRuntime = previousFreeze_;
        return;
    }
    cameraPreview_ = true;
    previewShotId_ = selectedId_;
    previewEndDistance_ = selectedId_.empty() ? context.railLength : buffer_.endDistance;
    if (context.freezeRuntime) *context.freezeRuntime = false;
    if (!play) context.preview->Pause();
    context.sequencer->SetPreviewPosition(distance, false);
    lastMessage_ = "ゲーム映像を確認中です。「プレビュー終了」で開始前の位置に戻ります。";
}

void CourseCameraShotDetailsPanel::StopPreview(const CourseCameraShotDetailsPanelContext& context) {
    composition_.Stop();
    if (!cameraPreview_) return;
    cameraPreview_ = false;
    if (context.preview) context.preview->Stop();
    if (context.freezeRuntime) *context.freezeRuntime = previousFreeze_;
    context.sequencer->SetPreviewPosition(previousDistance_, true);
    previewShotId_.clear();
    lastMessage_ = "プレビューを終了し、開始前のコース位置に戻りました。";
}

void CourseCameraShotDetailsPanel::RegisterComposition(
    const CourseCameraShotDetailsPanelContext& context, bool play) {
    auto changed = buffer_;
    if (!composition_.Capture(changed.view, &lastMessage_)) return;
    const float distance = composition_.Distance();
    // Avoid inherited shake/roll changing the freely composed horizon.
    changed.rollOffset = changed.shakeAmount = 0;
    if (!CommitShot(context, changed, "Register Camera Composition")) return;
    PreviewAt(context, play ? buffer_.startDistance : distance, play);
    lastMessage_ = play ? "構図を反映し、区間再生を開始しました。ファイルへの保存は別途必要です。"
        : "構図を区間に反映しました。「ファイルに保存」で保存できます。";
}

void CourseCameraShotDetailsPanel::Sync(
    const CourseCameraShotDetailsPanelContext& context,
    std::string_view id) {
    if (continuousEditActive_) return;
    CourseCinematicCameraShot* shot = FindShot(context.course, id);
    if (shot == nullptr) return;
    const std::string payload = context.provider->BuildCameraShotState(*shot).payload;
    if (selectedId_ == id && syncedPayload_ == payload) return;
    composition_.Stop();
    lastMessage_ = "区間の設定を読み込みました。構図の調整や再生確認ができます。";
    selectedId_ = std::string(id);
    syncedPayload_ = payload;
    buffer_ = *shot;
    if (cameraPreview_ && previewShotId_ == selectedId_) previewEndDistance_ = buffer_.endDistance;
    SyncTextBuffers();
}

void CourseCameraShotDetailsPanel::BeginContinuousEdit(
    const CourseCameraShotDetailsPanelContext& context) {
    if (continuousEditActive_ || !context.canMutateAuthoring) return;
    CourseCinematicCameraShot* shot = FindShot(context.course, selectedId_);
    if (shot == nullptr) return;
    editOriginal_ = *shot;
    previewCourse_ = context.course;
    continuousEditActive_ = true;
}

void CourseCameraShotDetailsPanel::RefreshPreview(
    const CourseCameraShotDetailsPanelContext& context) {
    if (!continuousEditActive_) return;
    ReadTextBuffers();
    std::string error;
    if (!ValidShot(buffer_, error)) { lastMessage_ = error; return; }
    CourseCinematicCameraShot* shot = FindShot(context.course, selectedId_);
    if (shot == nullptr) return;
    *shot = buffer_;
    shot->id = selectedId_;
    context.course->SortForRuntime();
    previewApplied_ = true;
    if (cameraPreview_ && previewShotId_ == selectedId_) previewEndDistance_ = buffer_.endDistance;
    lastMessage_ = "カメラの調整をプレビュー中です。";
}

void CourseCameraShotDetailsPanel::RestorePreview(CourseAsset* course) {
    if (!previewApplied_ || course == nullptr) return;
    CourseCinematicCameraShot* shot = FindShot(course, selectedId_);
    if (shot != nullptr) *shot = editOriginal_;
    course->SortForRuntime();
    previewApplied_ = false;
}

void CourseCameraShotDetailsPanel::CommitContinuousEdit(
    const CourseCameraShotDetailsPanelContext& context,
    std::string_view label) {
    if (!continuousEditActive_) return;
    ReadTextBuffers();
    const CourseCinematicCameraShot changed = buffer_;
    RestorePreview(context.course);
    continuousEditActive_ = false;
    previewCourse_ = nullptr;
    if (!CommitShot(context, changed, label)) buffer_ = editOriginal_;
    SyncTextBuffers();
}

bool CourseCameraShotDetailsPanel::CommitShot(
    const CourseCameraShotDetailsPanelContext& context,
    const CourseCinematicCameraShot& shot,
    std::string_view label) {
    CourseCinematicCameraShot* current = FindShot(context.course, selectedId_);
    if (current == nullptr || !context.canMutateAuthoring) {
        lastMessage_ = "現在はカメラ区間を編集できません。";
        return false;
    }
    CourseCinematicCameraShot changed = shot;
    changed.id = selectedId_;
    std::string error;
    if (!ValidShot(changed, error)) { lastMessage_ = error; return false; }
    const EditorSequencerKeyState before =
        context.provider->BuildCameraShotState(*current);
    const EditorSequencerKeyState after =
        context.provider->BuildCameraShotState(changed);
    if (!context.sequencer->CommitKeyStateChange(label, before, after, error)) {
        lastMessage_ = error;
        return false;
    }
    buffer_ = changed;
    if (cameraPreview_ && previewShotId_ == selectedId_) previewEndDistance_ = buffer_.endDistance;
    syncedPayload_ = after.payload;
    SyncTextBuffers();
    lastMessage_ = std::string(label) + ".";
    return true;
}

void CourseCameraShotDetailsPanel::CancelEdit(CourseAsset* course) {
    composition_.Stop();
    RestorePreview(course != nullptr ? course : previewCourse_);
    continuousEditActive_ = false;
    previewCourse_ = nullptr;
    previewApplied_ = false;
    selectedId_.clear();
    syncedPayload_.clear();
}

void CourseCameraShotDetailsPanel::SyncTextBuffers() {
    CopyText(modeBuffer_, buffer_.mode);
}

void CourseCameraShotDetailsPanel::ReadTextBuffers() {
    buffer_.mode = modeBuffer_.data();
}

} // namespace editor
