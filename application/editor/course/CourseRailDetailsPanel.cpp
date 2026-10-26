#include "CourseRailDetailsPanel.h"

#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include <algorithm>
#include <cmath>

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


constexpr std::string_view kPointPrefix = "course-rail-point:";
constexpr std::string_view kSegmentPrefix = "course-rail-segment:";

const char* GizmoHandleLabel(CourseRailGizmoHandle handle) {
    return handle == CourseRailGizmoHandle::None ? "なし" : ToString(handle);
}

Vector3 Subtract(const Vector3& a, const Vector3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool IsPointHandle(const EditorObjectHandle* handle) {
    return handle != nullptr && handle->domain == EditorDomainId::CourseRailControlPoint &&
        handle->stableId.starts_with(kPointPrefix);
}

bool IsSegmentHandle(const EditorObjectHandle* handle) {
    return handle != nullptr && handle->domain == EditorDomainId::CourseRailSegment &&
        handle->stableId.starts_with(kSegmentPrefix);
}

} // namespace

bool CourseRailDetailsPanel::HandlesSelection(const EditorSelection* selection) const {
    const EditorObjectHandle* primary = selection != nullptr ? selection->Primary() : nullptr;
    return IsPointHandle(primary) || IsSegmentHandle(primary);
}

void CourseRailDetailsPanel::Draw(const CourseRailDetailsPanelContext& context) {
    const EditorObjectHandle* primary =
        context.selection != nullptr ? context.selection->Primary() : nullptr;
    if (context.controller == nullptr || primary == nullptr) {
        ImGui::TextUnformatted("レールの詳細設定を表示できません。");
        return;
    }
    if (continuousEditActive_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelEdit();
        lastMessage_ = "レールの調整を取り消しました。";
    }
    if (IsPointHandle(primary)) {
        DrawPoint(context, primary->stableId.substr(kPointPrefix.size()));
    } else if (IsSegmentHandle(primary)) {
        CancelEdit();
        DrawSegment(context, primary->stableId.substr(kSegmentPrefix.size()));
    }
    DrawTransformSettings(context);
    if (!lastMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", lastMessage_.c_str());
    }
}

void CourseRailDetailsPanel::CancelEdit() {
    continuousEditActive_ = false;
    previewModel_.reset();
    previewCourse_ = {};
    syncedRevision_ = (std::numeric_limits<uint64_t>::max)();
}

void CourseRailDetailsPanel::SyncPoint(
    const CourseRailDetailsPanelContext& context,
    std::string_view guid) {
    if (continuousEditActive_) return;
    const CourseRailAuthoringModel* model = context.controller->Model();
    const RailPathControlPoint* point = model != nullptr ? model->FindPoint(guid) : nullptr;
    if (point == nullptr) return;
    const uint64_t revision = context.controller->State().mutationRevision;
    if (selectedGuid_ == guid && syncedRevision_ == revision) return;
    selectedGuid_ = std::string(guid);
    syncedRevision_ = revision;
    buffer_ = *point;
    previewModel_.reset();
}

void CourseRailDetailsPanel::BeginContinuousEdit(
    const CourseRailDetailsPanelContext& context) {
    if (continuousEditActive_) return;
    const CourseRailAuthoringModel* model = context.controller->Model();
    const RailPathControlPoint* point = model != nullptr
        ? model->FindPoint(selectedGuid_) : nullptr;
    if (point == nullptr) return;
    editOriginal_ = *point;
    editExpectedRevision_ = context.controller->State().mutationRevision;
    continuousEditActive_ = true;
}

void CourseRailDetailsPanel::RefreshPreview(
    const CourseRailDetailsPanelContext& context) {
    const CourseAsset* course = context.controller->Course();
    if (course == nullptr) return;
    previewCourse_ = *course;
    const auto found = std::find_if(previewCourse_.railPoints.begin(),
        previewCourse_.railPoints.end(), [this](const RailPathControlPoint& point) {
            return point.editorGuid == selectedGuid_;
        });
    if (found == previewCourse_.railPoints.end()) return;
    *found = buffer_;
    found->editorGuid = selectedGuid_;
    previewModel_.emplace(previewCourse_);
    if (!previewModel_->IsValid()) lastMessage_ = previewModel_->ValidationError();
}

void CourseRailDetailsPanel::CommitContinuousEdit(
    const CourseRailDetailsPanelContext& context,
    std::string label) {
    if (!continuousEditActive_) return;
    continuousEditActive_ = false;
    previewModel_.reset();
    previewCourse_ = {};
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::SetPoint;
    request.expectedRevision = editExpectedRevision_;
    request.pointGuid = selectedGuid_;
    request.point = buffer_;
    request.label = std::move(label);
    const CourseRailMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    syncedRevision_ = result.succeeded
        ? result.revision : (std::numeric_limits<uint64_t>::max)();
    if (!result.succeeded) buffer_ = editOriginal_;
}

bool CourseRailDetailsPanel::CommitPoint(
    const CourseRailDetailsPanelContext& context,
    const RailPathControlPoint& point,
    std::string label) {
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::SetPoint;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.pointGuid = selectedGuid_;
    request.point = point;
    request.label = std::move(label);
    const CourseRailMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded) {
        buffer_ = point;
        syncedRevision_ = result.revision;
    }
    return result.succeeded;
}

void CourseRailDetailsPanel::DrawPoint(
    const CourseRailDetailsPanelContext& context,
    std::string_view guid) {
    SyncPoint(context, guid);
    const CourseRailAuthoringModel* model = context.controller->Model();
    const std::optional<uint32_t> pointIndex = model != nullptr
        ? model->FindPointIndex(guid) : std::nullopt;
    if (!pointIndex.has_value()) {
        ImGui::TextUnformatted("選択した制御点は存在しません。");
        return;
    }
    const bool canMutate = context.canMutateAuthoring &&
        context.controller->State().authoringAllowed;
    ImGui::Text("レール制御点 %u", *pointIndex);
    ImGui::TextDisabled("GUID: %s", selectedGuid_.c_str());
    ImGui::Text("レール全長: %.3f m", model->Length());
    ImGui::Separator();
    ImGui::BeginDisabled(!canMutate);

    const auto continuous = [&](bool changed, const char* label) {
        if (ImGui::IsItemActivated()) BeginContinuousEdit(context);
        if (changed) {
            BeginContinuousEdit(context);
            RefreshPreview(context);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) CommitContinuousEdit(context, label);
    };

    float position[3]{buffer_.position.x, buffer_.position.y, buffer_.position.z};
    const bool positionChanged = ImGui::DragFloat3("位置（X / Y / Z）", position, 0.1f, 0.0f, 0.0f, "%.3f");
    if (positionChanged) buffer_.position = {position[0], position[1], position[2]};
    continuous(positionChanged, "Edit Rail Point Position");

    const bool radiusChanged = ImGui::DragFloat(
        "通路の半径", &buffer_.corridorRadius, 0.1f, 0.01f, 10000.0f, "%.3f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("この制御点付近で、レールを中心とした通路の広さを指定します。");
    buffer_.corridorRadius = (std::max)(buffer_.corridorRadius, 0.01f);
    continuous(radiusChanged, "Edit Rail Corridor Radius");

    const bool speedChanged = ImGui::DragFloat(
        "走行速度", &buffer_.speed, 0.1f, 0.0f, 10000.0f, "%.3f");
    buffer_.speed = (std::max)(buffer_.speed, 0.0f);
    continuous(speedChanged, "Edit Rail Speed");

    int tangentMode = static_cast<int>(buffer_.tangentMode);
    const char* tangentModes[] = {"自動", "対称に連動", "個別に調整"};
    if (ImGui::Combo("接線の種類", &tangentMode, tangentModes, 3)) {
        RailPathControlPoint changed = buffer_;
        const RailPath& path = model->RuntimePath();
        if (buffer_.tangentMode == RailPathTangentMode::Auto && tangentMode != 0) {
            changed.incomingTangent = Subtract(
                path.TangentHandlePosition(*pointIndex, true), buffer_.position);
            changed.outgoingTangent = Subtract(
                path.TangentHandlePosition(*pointIndex, false), buffer_.position);
        }
        changed.tangentMode = static_cast<RailPathTangentMode>(tangentMode);
        if (changed.tangentMode == RailPathTangentMode::Auto) {
            changed.incomingTangent = {};
            changed.outgoingTangent = {};
        } else if (changed.tangentMode == RailPathTangentMode::Mirrored) {
            changed.incomingTangent = {
                -changed.outgoingTangent.x,
                -changed.outgoingTangent.y,
                -changed.outgoingTangent.z};
        }
        CommitPoint(context, changed, "Change Rail Tangent Mode");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "自動: 前後の点から滑らかな接線を作ります。\n対称に連動: 入る側と出る側を同じ長さ・反対方向で連動します。\n個別に調整: 両側の接線を別々に設定します。");

    if (buffer_.tangentMode != RailPathTangentMode::Auto) {
        float incoming[3]{buffer_.incomingTangent.x, buffer_.incomingTangent.y,
            buffer_.incomingTangent.z};
        const bool incomingChanged = ImGui::DragFloat3(
            "入る側の接線", incoming, 0.1f, 0.0f, 0.0f, "%.3f");
        if (incomingChanged) {
            buffer_.incomingTangent = {incoming[0], incoming[1], incoming[2]};
            if (buffer_.tangentMode == RailPathTangentMode::Mirrored) {
                buffer_.outgoingTangent = {-incoming[0], -incoming[1], -incoming[2]};
            }
        }
        continuous(incomingChanged, "Edit Incoming Rail Tangent");

        float outgoing[3]{buffer_.outgoingTangent.x, buffer_.outgoingTangent.y,
            buffer_.outgoingTangent.z};
        const bool outgoingChanged = ImGui::DragFloat3(
            "出る側の接線", outgoing, 0.1f, 0.0f, 0.0f, "%.3f");
        if (outgoingChanged) {
            buffer_.outgoingTangent = {outgoing[0], outgoing[1], outgoing[2]};
            if (buffer_.tangentMode == RailPathTangentMode::Mirrored) {
                buffer_.incomingTangent = {-outgoing[0], -outgoing[1], -outgoing[2]};
            }
        }
        continuous(outgoingChanged, "Edit Outgoing Rail Tangent");
    } else {
        const Vector3 incoming = Subtract(
            model->RuntimePath().TangentHandlePosition(*pointIndex, true), buffer_.position);
        const Vector3 outgoing = Subtract(
            model->RuntimePath().TangentHandlePosition(*pointIndex, false), buffer_.position);
        ImGui::TextDisabled("自動接線・入る側: %.3f, %.3f, %.3f", incoming.x, incoming.y, incoming.z);
        ImGui::TextDisabled("自動接線・出る側: %.3f, %.3f, %.3f", outgoing.x, outgoing.y, outgoing.z);
    }

    ImGui::Separator();
    if (ImGui::Button("隣の区間に点を追加")) InsertAdjacent(context, *pointIndex);
    ImGui::SameLine();
    if (ImGui::Button("制御点を削除")) RemovePoint(context, guid);
    if (ImGui::Button("値をコピー")) {
        clipboard_ = buffer_;
        lastMessage_ = "制御点の値をコピーしました。";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!clipboard_.has_value());
    if (ImGui::Button("値を貼り付け") && clipboard_.has_value()) {
        RailPathControlPoint pasted = *clipboard_;
        pasted.editorGuid = selectedGuid_;
        CommitPoint(context, pasted, "Paste Rail Point Values");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}

void CourseRailDetailsPanel::DrawSegment(
    const CourseRailDetailsPanelContext& context,
    std::string_view guid) {
    const CourseRailAuthoringModel* model = context.controller->Model();
    const CourseRailSegment* segment = model != nullptr ? model->FindSegment(guid) : nullptr;
    if (segment == nullptr) {
        ImGui::TextUnformatted("選択したレール区間は存在しません。");
        return;
    }
    ImGui::Text("レール区間 %u", segment->pointIndex);
    ImGui::TextDisabled("GUID: %s", segment->guid.c_str());
    ImGui::Text("開始距離: %.3f m", segment->startDistance);
    ImGui::Text("区間の長さ: %.3f m", segment->length);
    ImGui::Text("開始の制御点: %s", segment->startPointGuid.c_str());
    ImGui::Text("終了の制御点: %s", segment->endPointGuid.c_str());
    ImGui::BeginDisabled(!context.canMutateAuthoring ||
        !context.controller->State().authoringAllowed);
    if (ImGui::Button("区間の中央に点を追加")) {
        const RailPathSample sample = model->RuntimePath().EvaluateSegmentAt(segment->pointIndex, 0.5f);
        CourseRailMutationRequest request{};
        request.kind = CourseRailMutationKind::InsertPoint;
        request.expectedRevision = context.controller->State().mutationRevision;
        request.segmentGuid = segment->guid;
        request.normalizedT = 0.5f;
        request.point.position = sample.position;
        request.point.corridorRadius = sample.corridorRadius;
        request.point.speed = sample.speed;
        const CourseRailMutationResult result = context.controller->Mutate(request);
        lastMessage_ = result.message;
        if (result.succeeded) SelectPoint(context, result.affectedPointGuid);
    }
    ImGui::EndDisabled();
}

void CourseRailDetailsPanel::DrawTransformSettings(
    const CourseRailDetailsPanelContext& context) {
    if (context.transformGizmo == nullptr) return;
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("レールの移動ハンドル", ImGuiTreeNodeFlags_DefaultOpen)) return;
    CourseRailTransformGizmoSettings settings = context.transformGizmo->Settings();
    int space = settings.space == EditorTransformGizmoSpace::World ? 0 : 1;
    const char* spaces[] = {"ワールド基準", "レール基準"};
    if (ImGui::Combo("座標の基準", &space, spaces, 2)) {
        settings.space = space == 0
            ? EditorTransformGizmoSpace::World : EditorTransformGizmoSpace::Local;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ワールド基準: マップ全体のXYZ軸。\nレール基準: レールの左右・上下・進行方向に沿った軸。");
    ImGui::Checkbox("グリッドに合わせる", &settings.snapEnabled);
    ImGui::DragFloat("グリッド間隔", &settings.gridSize, 0.1f, 0.01f, 1000.0f, "%.3f");
    ImGui::SliderFloat("ハンドルの大きさ", &settings.handleLengthScale, 0.1f, 4.0f, "%.2f");
    ApplyValidatedSettings(*context.transformGizmo, settings);
    if (context.viewportTool != nullptr) {
        CourseRailViewportEditSettings toolSettings = context.viewportTool->Settings();
        toolSettings.gridSnap = settings.snapEnabled;
        toolSettings.gridSize = settings.gridSize;
        ApplyValidatedSettings(*context.viewportTool, toolSettings);
    }
    ImGui::Text("選択中の制御点: %u", context.transformGizmo->State().selectedPointCount);
    ImGui::Text("選択候補: %s / 操作中: %s",
        GizmoHandleLabel(context.transformGizmo->State().hovered),
        GizmoHandleLabel(context.transformGizmo->State().active));
}

void CourseRailDetailsPanel::InsertAdjacent(
    const CourseRailDetailsPanelContext& context,
    uint32_t pointIndex) {
    const CourseRailAuthoringModel* model = context.controller->Model();
    if (model == nullptr || model->Segments().empty()) return;
    const uint32_t segmentIndex = pointIndex < model->Segments().size()
        ? pointIndex : static_cast<uint32_t>(model->Segments().size() - 1);
    const CourseRailSegment& segment = model->Segments()[segmentIndex];
    const RailPathSample sample = model->RuntimePath().EvaluateSegmentAt(segment.pointIndex, 0.5f);
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::InsertPoint;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.segmentGuid = segment.guid;
    request.normalizedT = 0.5f;
    request.point.position = sample.position;
    request.point.corridorRadius = buffer_.corridorRadius;
    request.point.speed = buffer_.speed;
    request.label = "Insert Adjacent Rail Point";
    const CourseRailMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded) SelectPoint(context, result.affectedPointGuid);
}

void CourseRailDetailsPanel::RemovePoint(
    const CourseRailDetailsPanelContext& context,
    std::string_view guid) {
    CourseRailMutationRequest request{};
    request.kind = CourseRailMutationKind::RemovePoint;
    request.expectedRevision = context.controller->State().mutationRevision;
    request.pointGuid = std::string(guid);
    const CourseRailMutationResult result = context.controller->Mutate(request);
    lastMessage_ = result.message;
    if (result.succeeded && context.selection != nullptr) {
        context.selection->Clear();
        selectedGuid_.clear();
        CancelEdit();
    }
}

void CourseRailDetailsPanel::SelectPoint(
    const CourseRailDetailsPanelContext& context,
    std::string_view guid) const {
    if (context.selection == nullptr || context.controller->Model() == nullptr) return;
    const std::optional<uint32_t> index = context.controller->Model()->FindPointIndex(guid);
    if (!index.has_value()) return;
    EditorObjectHandle handle{};
    handle.domain = EditorDomainId::CourseRailControlPoint;
    handle.stableId = std::string(kPointPrefix) + std::string(guid);
    handle.localIndex = *index;
    handle.generation = static_cast<uint32_t>(context.controller->State().mutationRevision);
    handle.displayName = "レール制御点";
    context.selection->SetPrimary(std::move(handle));
}

} // namespace editor
