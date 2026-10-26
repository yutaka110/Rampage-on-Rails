#include "CourseMapEditorMajorTab.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include "../../../externals/imgui/imgui.h"

#include <algorithm>

namespace editor {
namespace {

constexpr ImGuiWindowFlags kMajorTabFlags =
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse;

ImVec4 OpaqueStyleColor(ImGuiCol color) {
    ImVec4 value = ImGui::GetStyleColorVec4(color);
    value.w = 1.0f;
    return value;
}

} // namespace

EditorPanelRect CourseMapEditorMajorTab::ResolvePresentationRect(
    const EditorPanelLayoutService& layout,
    bool maximized, bool cameraEditing) {
    const EditorPanelRect& viewport = layout.ViewportRect();
    const EditorPanelRect& bottom = layout.BottomDockRect();
    if (!maximized || !viewport.Valid() || !bottom.Valid()) {
        return viewport;
    }
    return EditorPanelRect{
        viewport.x,
        viewport.y,
        cameraEditing && layout.InspectorRect().Valid()
            ? layout.InspectorRect().x + layout.InspectorRect().width - viewport.x : viewport.width,
        viewport.height + bottom.height};
}

void CourseMapEditorMajorTab::Draw(
    CourseMapEditorWorkspace& workspace,
    CourseOverviewMapController& controller,
    const CourseOverviewMapPanelContext& context,
    const EditorPanelRect& rect) {
    if (!workspace.IsOpen() || !rect.Valid()) return;

    if (workspace.ConsumeFocusRequest()) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowPos({rect.x, rect.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({rect.width, rect.height}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, OpaqueStyleColor(ImGuiCol_WindowBg));
    bool open = true;
    const bool visible = ImGui::Begin(
        "マップエディター###CourseMapEditorMajorTab",
        &open,
        kMajorTabFlags);
    ImGui::PopStyleColor();
    if (!open) {
        if (context.cameraPanel) cameraTool_.Cancel(context.cameraPanel->Composition());
        rangeTool_.Cancel();
        workspace.Close();
    }
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("マップエディター");
    ImGui::SameLine();
    ImGui::TextDisabled("作成 / 調整 / 確認 / 保存");
    const char* sizeLabel = workspace.IsMaximized()
        ? "表示領域を戻す"
        : "作業領域を広げる";
    const float actionsWidth = ImGui::CalcTextSize(sizeLabel).x +
        ImGui::CalcTextSize("閉じる").x +
        ImGui::GetStyle().FramePadding.x * 4.0f +
        ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine((ImGui::GetWindowContentRegionMax().x - actionsWidth));
    if (ImGui::Button(sizeLabel)) {
        workspace.SetMaximized(!workspace.IsMaximized());
    }
    ImGui::SameLine();
    if (ImGui::Button("閉じる")) {
        if (context.cameraPanel) cameraTool_.Cancel(context.cameraPanel->Composition());
        rangeTool_.Cancel();
        workspace.Close();
    }
    ImGui::Separator();

    const auto modeButton = [&](const char* label, bool selected, CourseMapEditCategory category, bool camera) {
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(42, 117, 145, 255));
        if (ImGui::Button(label) && !selected) {
            if (context.cameraPanel) cameraTool_.Cancel(context.cameraPanel->Composition());
            rangeTool_.Cancel();
            if (context.editTool) {
                context.editTool->Cancel();
                context.editTool->SetMode(CourseOverviewMapEditMode::SelectMove);
            }
            if (context.sketchTool) context.sketchTool->SetActive(false);
            cameraEditing_ = camera;
            editCategory_ = category;
            wasCompositionReady_ = false;
        }
        if (selected) ImGui::PopStyleColor();
    };
    ImGui::BeginDisabled(rangeTool_.Busy() || (context.cameraPanel && context.cameraPanel->Composition().Changed()));
    modeButton("コース編集", !cameraEditing_ && editCategory_ == CourseMapEditCategory::Course, CourseMapEditCategory::Course, false);
    ImGui::SameLine();
    modeButton("敵・出現編集", !cameraEditing_ && editCategory_ == CourseMapEditCategory::Encounters, CourseMapEditCategory::Encounters, false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!context.cameraPanel);
    modeButton("カメラ編集", cameraEditing_, editCategory_, true);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (context.cameraPanel) context.cameraPanel->SyncMapSelection(context.cameraContext);

    // Actions and state live outside the scrolling work area in every mode.
    const char* actionLabels[] = {"構図を反映", "反映して再生", "区間を再生", "一時停止", "プレビュー終了", "通常追従に戻す", "ファイルに保存"};
    int actionRows=1;
    float rowWidth=0;
    const float availableWidth=ImGui::GetContentRegionAvail().x-ImGui::GetStyle().WindowPadding.x*2;
    for (const char* label:actionLabels) {
        if (!cameraEditing_ && std::string_view(label)!="ファイルに保存") continue;
        const float width=ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetStyle().ItemSpacing.x;
        if (rowWidth>0 && rowWidth+width>availableWidth) { ++actionRows;rowWidth=0; }
        rowWidth+=width;
    }
    const float barHeight=actionRows*ImGui::GetFrameHeightWithSpacing()+
        (cameraEditing_ ? 3 : 2)*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetStyle().WindowPadding.y*2;
    ImGui::BeginChild("PinnedMapActions", {0, barHeight}, true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (cameraEditing_ && context.cameraPanel) {
        ImGui::BeginDisabled(rangeTool_.Busy());
        context.cameraPanel->DrawMapActions(context.cameraContext);
        ImGui::EndDisabled();
        const float width = ImGui::CalcTextSize("ファイルに保存").x + ImGui::GetStyle().FramePadding.x*2;
        if (ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x-ImGui::GetItemRectMax().x > width+ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
    }
    const bool pending = context.cameraPanel && context.cameraPanel->Composition().Changed();
    const auto* save = context.commands ? context.commands->Find("course.save") : nullptr;
    ImGui::BeginDisabled(pending || rangeTool_.Busy() || !save || !context.commands->IsEnabled(*save));
    if (ImGui::Button("ファイルに保存")) {
        const auto result=context.commands->Execute("course.save");
        if (result.succeeded && context.cameraPanel) context.cameraPanel->OnCourseSaved();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (rangeTool_.Busy()) ImGui::SetTooltip("区間の指定を完了するか、Escで取り消してから保存してください。");
        else if (pending) ImGui::SetTooltip("調整中の構図を反映するか、破棄してから保存してください。");
        else if (save && !context.commands->IsEnabled(*save)) {
            if (context.courseDirty && !context.courseDirty()) ImGui::SetTooltip("ファイルは保存済みです。保存する変更はありません。");
            else ImGui::SetTooltip("現在は保存できません。詳細: %s",context.commands->DisabledReason(*save).c_str());
        }
        else ImGui::SetTooltip("コース全体をファイルに保存します。");
    }
    const bool dirty = context.courseDirty ? context.courseDirty() : (context.rail && context.rail->State().dirty);
    ImGui::TextColored(pending ? ImVec4(1,0.76f,0.3f,1) : ImVec4(0.48f,0.9f,0.75f,1),
        "%s", pending ? "構図: 調整あり・未反映" : context.cameraPanel && context.cameraPanel->Composition().Active()
            ? "構図: 調整モード（変更なし）" : "構図: 未反映の調整なし");
    ImGui::SameLine();
    ImGui::TextColored(dirty ? ImVec4(1,0.76f,0.3f,1) : ImVec4(0.48f,0.9f,0.75f,1),
        "%s", dirty ? "ファイル: 未保存の変更あり" : "ファイル: 保存済み");
    if (rangeTool_.Dragging()) ImGui::Text("区間: 範囲調整中 %.1f - %.1f m（離して確定 / Escで取消）",rangeTool_.Start(),rangeTool_.End());
    else if (rangeTool_.Creating()) ImGui::Text("区間: 開始 %.1f m を指定済み。終了位置をクリック / Escで取消",rangeTool_.Anchor());
    else if (cameraEditing_ && context.cameraPanel && !context.cameraPanel->Message().empty())
    {
        if (const char* status = context.cameraPanel->PlaybackStatus(context.preview)) {
            ImGui::TextUnformatted(status);
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(context.cameraPanel->Message().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",context.cameraPanel->Message().c_str());
    }
    if (context.commands) {
        const auto* status=context.commands->ExecutionStatus();
        if (status && status->hasResult && status->commandId=="course.save") {
            ImGui::TextColored(status->succeeded ? ImVec4(0.48f,0.9f,0.75f,1) : ImVec4(1,0.4f,0.3f,1),
                "%s", status->succeeded ? "前回の保存: 完了" : "保存に失敗しました。ここにマウスを重ねると詳細を表示します。");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",status->message.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::BeginChild("MapWorkArea", {0,0}, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (cameraEditing_ && context.cameraPanel && context.viewport3D) {
        auto& composition=context.cameraPanel->Composition();
        context.viewport3D->SetActive(true);
        if (context.rail && context.rail->Model())
            context.viewport3D->Bind({context.rail,context.enemies,context.waves,context.selection});
        const auto& mapRect=context.viewport3D->Frame().viewport;
        const float mapAspect=mapRect.Valid()?mapRect.width/mapRect.height:0.8f;
        if (ImGui::Button("撮影位置を表示") && composition.HasPose())
            context.viewport3D->ConfigureCamera(CourseMapCameraEditTool::FocusCamera(composition,context.viewport3D->Camera(),mapAspect));
        ImGui::SameLine();
        ImGui::BeginDisabled(rangeTool_.Busy());
        if (ImGui::Button("選択区間を表示") && context.cameraPanel->SelectedShot() && context.rail && context.rail->Model()) {
            const auto& shot=*context.cameraPanel->SelectedShot();
            context.viewport3D->ConfigureCamera(CourseMapCameraRangeTool::FocusRange(context.rail->Model()->RuntimePath(),
                shot.startDistance,shot.endDistance,context.viewport3D->Camera(),mapAspect));
        }
        ImGui::SameLine();
        if (ImGui::Button("レール全体を表示")) context.viewport3D->FrameAll();
        ImGui::SameLine();
        ImGui::Checkbox("再生中のカメラを追う",&followPreview_);
        ImGui::EndDisabled();
        if (ImGui::BeginTable("MapCameraSplit",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("3D Map",ImGuiTableColumnFlags_WidthStretch,1.2f);
            ImGui::TableSetupColumn("Game Preview",ImGuiTableColumnFlags_WidthStretch,1.0f);
            ImGui::TableNextRow();
            // Draw controls first so selection and registration are visible to
            // the map in this same frame, while preserving the visual layout.
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("GameCameraAndControls",{0,0},false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextUnformatted("ゲーム映像（実際の見え方）");
            if (context.gamePreviewTexture) {
                const float aspect=(std::max)(0.1f,context.gamePreviewAspect);
                const float width=(std::min)((std::max)(1.0f,ImGui::GetContentRegionAvail().x),
                    (std::max)(80.0f,ImGui::GetContentRegionAvail().y*0.42f)*aspect);
                ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(context.gamePreviewTexture)),
                    {width,width/(std::max)(0.1f,context.gamePreviewAspect)});
            }
            ImGui::BeginChild("CameraSettingsScroll",{0,0},false);
            ImGui::BeginDisabled(rangeTool_.Busy());
            context.cameraPanel->DrawMapControls(context.cameraContext);
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndChild();
            const auto* shot=context.cameraPanel->SelectedShot();
            const std::string shotId=shot?shot->id:std::string{};
            if (composition.Ready() && (!wasCompositionReady_ || shotId!=lastCameraShot_)) {
                rangeTool_.SetMode(CourseMapCameraEditMode::Composition);
                context.viewport3D->ConfigureCamera(CourseMapCameraEditTool::FocusCamera(composition,context.viewport3D->Camera(),mapAspect));
            }
            const bool playing=context.preview && context.preview->IsActive() && context.preview->IsPlaying();
            if (!rangeTool_.Busy() && followPreview_ && !composition.Ready() && composition.HasPose() && (playing || wasPlaying_)) {
                auto camera=context.viewport3D->Camera();
                const auto eye=composition.Eye(), target=composition.Target();
                camera.target={(eye.x+target.x)*0.5f,(eye.y+target.y)*0.5f,(eye.z+target.z)*0.5f};
                context.viewport3D->ConfigureCamera(camera);
            }
            wasPlaying_=playing;
            wasCompositionReady_=composition.Ready();lastCameraShot_=shotId;
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("CameraMapCanvas",{0,0},false,
                ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            auto cameraMapContext=context;
            cameraMapContext.cameraEditing=true;cameraMapContext.cameraTool=&cameraTool_;
            cameraMapContext.cameraRangeTool=&rangeTool_;
            DrawCourseOverviewMapPanel(controller,cameraMapContext);
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else {
        auto mapContext=context;
        mapContext.editCategory=editCategory_;
        DrawCourseOverviewMapPanel(controller, mapContext);
    }
    ImGui::EndChild();
    ImGui::End();
}

void CourseMapEditorMajorTab::DrawCompactEntry(
    CourseMapEditorWorkspace& workspace) const {
    ImGui::TextWrapped(
        "マップ上でコース・敵の出現・撮影カメラを編集できます。");
    if (workspace.IsOpen()) {
        ImGui::TextColored(
            {0.40f, 0.86f, 1.0f, 1.0f},
            "マップエディターを表示中です。");
        if (ImGui::Button("マップエディターを表示")) workspace.Open();
        ImGui::SameLine();
        if (ImGui::Button("マップエディターを閉じる")) workspace.Close();
    } else if (ImGui::Button("マップエディターを開く")) {
        workspace.Open();
    }
}

} // namespace editor
