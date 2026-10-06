#include "CourseRailDetailsPanel.h"

#include <algorithm>
#include <cmath>

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


constexpr std::string_view kPointPrefix = "course-rail-point:";
constexpr std::string_view kSegmentPrefix = "course-rail-segment:";

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
        ImGui::TextUnformatted("Course Rail details are unavailable.");
        return;
    }
    if (continuousEditActive_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelEdit();
        lastMessage_ = "Rail Details edit cancelled.";
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
        ImGui::TextUnformatted("Selected rail control point no longer exists.");
        return;
    }
    const bool canMutate = context.canMutateAuthoring &&
        context.controller->State().authoringAllowed;
    ImGui::Text("Rail Control Point %u", *pointIndex);
    ImGui::TextDisabled("GUID: %s", selectedGuid_.c_str());
    ImGui::Text("Rail length: %.3f", model->Length());
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
    const bool positionChanged = ImGui::DragFloat3("Position", position, 0.1f, 0.0f, 0.0f, "%.3f");
    if (positionChanged) buffer_.position = {position[0], position[1], position[2]};
    continuous(positionChanged, "Edit Rail Point Position");

    const bool radiusChanged = ImGui::DragFloat(
        "Corridor Radius", &buffer_.corridorRadius, 0.1f, 0.01f, 10000.0f, "%.3f");
    buffer_.corridorRadius = (std::max)(buffer_.corridorRadius, 0.01f);
    continuous(radiusChanged, "Edit Rail Corridor Radius");

    const bool speedChanged = ImGui::DragFloat(
        "Rail Speed", &buffer_.speed, 0.1f, 0.0f, 10000.0f, "%.3f");
    buffer_.speed = (std::max)(buffer_.speed, 0.0f);
    continuous(speedChanged, "Edit Rail Speed");

    int tangentMode = static_cast<int>(buffer_.tangentMode);
    const char* tangentModes[] = {"Auto", "Mirrored", "Broken"};
    if (ImGui::Combo("Tangent Mode", &tangentMode, tangentModes, 3)) {
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

    if (buffer_.tangentMode != RailPathTangentMode::Auto) {
        float incoming[3]{buffer_.incomingTangent.x, buffer_.incomingTangent.y,
            buffer_.incomingTangent.z};
        const bool incomingChanged = ImGui::DragFloat3(
            "Incoming Tangent", incoming, 0.1f, 0.0f, 0.0f, "%.3f");
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
            "Outgoing Tangent", outgoing, 0.1f, 0.0f, 0.0f, "%.3f");
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
        ImGui::TextDisabled("Auto Incoming: %.3f, %.3f, %.3f", incoming.x, incoming.y, incoming.z);
        ImGui::TextDisabled("Auto Outgoing: %.3f, %.3f, %.3f", outgoing.x, outgoing.y, outgoing.z);
    }

    ImGui::Separator();
    if (ImGui::Button("Insert Adjacent")) InsertAdjacent(context, *pointIndex);
    ImGui::SameLine();
    if (ImGui::Button("Delete Point")) RemovePoint(context, guid);
    if (ImGui::Button("Copy Values")) {
        clipboard_ = buffer_;
        lastMessage_ = "Rail point values copied.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!clipboard_.has_value());
    if (ImGui::Button("Paste Values") && clipboard_.has_value()) {
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
        ImGui::TextUnformatted("Selected rail segment no longer exists.");
        return;
    }
    ImGui::Text("Rail Segment %u", segment->pointIndex);
    ImGui::TextDisabled("GUID: %s", segment->guid.c_str());
    ImGui::Text("Start distance: %.3f", segment->startDistance);
    ImGui::Text("Length: %.3f", segment->length);
    ImGui::Text("Start point: %s", segment->startPointGuid.c_str());
    ImGui::Text("End point: %s", segment->endPointGuid.c_str());
    ImGui::BeginDisabled(!context.canMutateAuthoring ||
        !context.controller->State().authoringAllowed);
    if (ImGui::Button("Insert Point At Midpoint")) {
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
    if (!ImGui::CollapsingHeader("Rail Transform Gizmo", ImGuiTreeNodeFlags_DefaultOpen)) return;
    CourseRailTransformGizmoSettings settings = context.transformGizmo->Settings();
    int space = settings.space == EditorTransformGizmoSpace::World ? 0 : 1;
    const char* spaces[] = {"World", "Rail Local"};
    if (ImGui::Combo("Space", &space, spaces, 2)) {
        settings.space = space == 0
            ? EditorTransformGizmoSpace::World : EditorTransformGizmoSpace::Local;
    }
    ImGui::Checkbox("Grid Snap", &settings.snapEnabled);
    ImGui::DragFloat("Grid Size", &settings.gridSize, 0.1f, 0.01f, 1000.0f, "%.3f");
    ImGui::SliderFloat("Handle Scale", &settings.handleLengthScale, 0.1f, 4.0f, "%.2f");
    ApplyValidatedSettings(*context.transformGizmo, settings);
    if (context.viewportTool != nullptr) {
        CourseRailViewportEditSettings toolSettings = context.viewportTool->Settings();
        toolSettings.gridSnap = settings.snapEnabled;
        toolSettings.gridSize = settings.gridSize;
        ApplyValidatedSettings(*context.viewportTool, toolSettings);
    }
    ImGui::Text("Selected points: %u", context.transformGizmo->State().selectedPointCount);
    ImGui::Text("Hover: %s  Active: %s",
        ToString(context.transformGizmo->State().hovered),
        ToString(context.transformGizmo->State().active));
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
    handle.displayName = "Rail Control Point";
    context.selection->SetPrimary(std::move(handle));
}

} // namespace editor
