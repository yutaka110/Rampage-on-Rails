#include "EditorAssetBrowserPanel.h"

#include "EditorAssetMutationExecutor.h"
#include "EditorAssetMutationSafety.h"
#include "EditorAssetImportService.h"
#include "EditorAssetThumbnailService.h"
#include "EditorNotificationCenter.h"
#include "mesh/EditorObjProductionImportBridge.h"

#include "../../externals/imgui/imgui.h"

#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

namespace editor {
namespace {

constexpr EditorAssetKind kAssetKindFilters[] = {
    EditorAssetKind::Unknown,
    EditorAssetKind::Mesh,
    EditorAssetKind::Effect,
    EditorAssetKind::Course,
    EditorAssetKind::Prefab,
    EditorAssetKind::MaterialGraph,
    EditorAssetKind::MaterialInstance,
    EditorAssetKind::VfxGraph,
    EditorAssetKind::AnimationStateMachine,
    EditorAssetKind::GameplayVisualScript,
    EditorAssetKind::BehaviorTree,
    EditorAssetKind::EnvironmentQuery,
    EditorAssetKind::NavigationData,
    EditorAssetKind::Texture,
    EditorAssetKind::Audio,
};

const char* FilterLabel(EditorAssetKind kind) {
    return kind == EditorAssetKind::Unknown ? "All" : ToString(kind);
}

std::string ShortGuid(const std::string& guid) {
    if (guid.size() <= 13) {
        return guid.empty() ? std::string("-") : guid;
    }
    return guid.substr(0, 13);
}

const char* MetadataLabel(const EditorAssetRecord& record) {
    if (record.hasMetadata) {
        return "meta";
    }
    return record.provisionalGuid ? "auto" : "-";
}

std::string AssetLabel(const EditorAssetRecord& record) {
    return std::string(ToString(record.kind)) + ":" + record.id;
}

const char* ThumbnailLabel(const EditorAssetThumbnailEntry& thumbnail) {
    if (thumbnail.status == EditorAssetThumbnailStatus::Unsupported) {
        return "Icon";
    }
    return ToString(thumbnail.status);
}

std::string PreviewSummary(const EditorAssetThumbnailEntry& thumbnail) {
    std::string summary = ToString(thumbnail.previewKind);
    if (!thumbnail.previewFormat.empty()) {
        summary += " ";
        summary += thumbnail.previewFormat;
    }
    if (thumbnail.width > 0 && thumbnail.height > 0) {
        summary += " ";
        summary += std::to_string(thumbnail.width);
        summary += "x";
        summary += std::to_string(thumbnail.height);
    } else if (thumbnail.vertexCount > 0 || thumbnail.faceCount > 0) {
        summary += " ";
        summary += std::to_string(thumbnail.vertexCount);
        summary += "v/";
        summary += std::to_string(thumbnail.faceCount);
        summary += "f";
    } else if (thumbnail.lineCount > 0) {
        summary += " ";
        summary += std::to_string(thumbnail.lineCount);
        summary += " lines";
    }
    if (thumbnail.byteSize > 0) {
        summary += " ";
        summary += std::to_string(static_cast<unsigned long long>(thumbnail.byteSize));
        summary += " B";
    }
    if (thumbnail.materialTextureCount > 0) {
        summary += " tex:";
        summary += std::to_string(thumbnail.materialTextureCount);
    }
    return summary;
}

std::string GpuThumbnailSummary(const EditorAssetThumbnailEntry& thumbnail) {
    if (thumbnail.gpuStatus == EditorAssetGpuThumbnailStatus::NotRequested ||
        thumbnail.gpuStatus == EditorAssetGpuThumbnailStatus::Cancelled) {
        return ToString(thumbnail.gpuStatus);
    }
    std::string summary = ToString(thumbnail.gpuStatus);
    if (thumbnail.gpuWidth > 0 && thumbnail.gpuHeight > 0) {
        summary += " ";
        summary += std::to_string(thumbnail.gpuWidth);
        summary += "x";
        summary += std::to_string(thumbnail.gpuHeight);
    }
    if (thumbnail.gpuHandleToken != 0) {
        summary += " GPU#";
        summary += std::to_string(static_cast<unsigned long long>(thumbnail.gpuHandleToken));
    }
    if (thumbnail.gpuDescriptorIndex != UINT32_MAX) {
        summary += " SRV[";
        summary += std::to_string(thumbnail.gpuDescriptorIndex);
        summary += "]";
    }
    return summary;
}

ImVec4 ThumbnailStatusColor(EditorAssetThumbnailStatus status) {
    switch (status) {
    case EditorAssetThumbnailStatus::Ready:
        return ImVec4(0.42f, 0.86f, 0.58f, 1.0f);
    case EditorAssetThumbnailStatus::Unsupported:
        return ImVec4(0.58f, 0.68f, 0.84f, 1.0f);
    case EditorAssetThumbnailStatus::Pending:
        return ImVec4(1.0f, 0.72f, 0.22f, 1.0f);
    case EditorAssetThumbnailStatus::Missing:
    case EditorAssetThumbnailStatus::Failed:
        return ImVec4(1.0f, 0.25f, 0.22f, 1.0f);
    }
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

ImVec4 GpuThumbnailStatusColor(EditorAssetGpuThumbnailStatus status) {
    switch (status) {
    case EditorAssetGpuThumbnailStatus::Ready:
        return ImVec4(0.42f, 0.86f, 0.58f, 1.0f);
    case EditorAssetGpuThumbnailStatus::Queued:
    case EditorAssetGpuThumbnailStatus::Rendering:
        return ImVec4(1.0f, 0.72f, 0.22f, 1.0f);
    case EditorAssetGpuThumbnailStatus::Failed:
    case EditorAssetGpuThumbnailStatus::Stale:
        return ImVec4(1.0f, 0.25f, 0.22f, 1.0f);
    case EditorAssetGpuThumbnailStatus::Cancelled:
    case EditorAssetGpuThumbnailStatus::NotRequested:
        return ImVec4(0.58f, 0.68f, 0.84f, 1.0f);
    }
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

EditorAssetThumbnailEntry ResolveThumbnail(
    const EditorAssetRecord& record,
    const EditorAssetThumbnailService* thumbnails) {
    if (thumbnails != nullptr) {
        return thumbnails->Resolve(record);
    }
    EditorAssetThumbnailEntry entry{};
    entry.key = BuildEditorAssetThumbnailKey(record);
    entry.kind = record.kind;
    entry.assetId = record.id;
    entry.label = ToString(record.kind);
    entry.detail = "Thumbnail service is unavailable.";
    entry.previewKind = EditorAssetPreviewKind::Icon;
    entry.status = EditorAssetThumbnailStatus::Pending;
    entry.fallbackIcon = true;
    return entry;
}

ImVec4 SafetyColor(EditorAssetMutationRisk risk) {
    switch (risk) {
    case EditorAssetMutationRisk::Allowed:
        return ImVec4(0.42f, 0.86f, 0.58f, 1.0f);
    case EditorAssetMutationRisk::Warning:
        return ImVec4(1.0f, 0.72f, 0.22f, 1.0f);
    case EditorAssetMutationRisk::Blocked:
        return ImVec4(1.0f, 0.25f, 0.22f, 1.0f);
    }
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

void PushAssetMutationNotification(
    EditorNotificationCenter* notifications,
    const EditorAssetMutationResult& result);
void PushAssetImportNotification(
    EditorNotificationCenter* notifications,
    const EditorAssetImportResult& result);

void SelectAssetRecord(
    const EditorAssetRecord& record,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorContentBrowserState* browserState = nullptr) {
    if (browserState != nullptr) browserState->SetSelectedAssetGuid(record.guid);
    if (assetSelection == nullptr) {
        return;
    }
    assetSelection->SetPrimary(MakeEditorAssetHandle(record, registry.Revision()));
}

void DrawReferenceRow(
    const char* relationship,
    const EditorAssetRecord& record,
    const char* state,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(relationship);
    ImGui::TableNextColumn();
    const std::string label = AssetLabel(record);
    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
        SelectAssetRecord(record, registry, assetSelection);
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(state);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(record.sourcePath.empty() ? "-" : record.sourcePath.c_str());
}

void DrawReferenceTokenRow(
    const char* relationship,
    const std::string& token,
    const char* state) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(relationship);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(token.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(state);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("-");
}

void DrawReferenceInspector(
    const EditorAssetRecord& selectedRecord,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    const EditorAssetMutationSafetyReport& renameSafety,
    const EditorAssetMutationSafetyReport& moveSafety,
    const EditorAssetMutationSafetyReport& deleteSafety) {
    const std::vector<const EditorAssetRecord*> dependencies =
        registry.FindDependencies(selectedRecord);
    const std::vector<const EditorAssetRecord*> dependents =
        registry.FindDependents(selectedRecord);
    const std::vector<std::string> missingTokens =
        registry.FindMissingDependencyTokens(selectedRecord);
    const std::vector<std::string> malformedTokens =
        registry.FindMalformedDependencyTokens(selectedRecord);

    ImGui::Separator();
    ImGui::Text(
        "Reference Inspector  Token %s  Direct %u  Referenced By %u  Missing %u",
        BuildEditorAssetDependencyToken(selectedRecord).c_str(),
        static_cast<unsigned int>(dependencies.size()),
        static_cast<unsigned int>(dependents.size()),
        static_cast<unsigned int>(missingTokens.size() + malformedTokens.size()));
    ImGui::Text(
        "Mutation impact  Rename %s  Move %s  Delete %s",
        ToString(renameSafety.risk),
        ToString(moveSafety.risk),
        ToString(deleteSafety.risk));

    if (!ImGui::BeginTable(
            "EditorAssetReferenceInspector",
            4,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable)) {
        return;
    }

    ImGui::TableSetupColumn("Relation", ImGuiTableColumnFlags_WidthFixed, 112.0f);
    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 230.0f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Source");
    ImGui::TableHeadersRow();

    bool anyRow = false;
    for (const EditorAssetRecord* dependency : dependencies) {
        if (dependency == nullptr) {
            continue;
        }
        anyRow = true;
        DrawReferenceRow(
            "Depends On",
            *dependency,
            dependency->missing ? "missing file" : "ok",
            registry,
            assetSelection);
    }
    for (const EditorAssetRecord* dependent : dependents) {
        if (dependent == nullptr) {
            continue;
        }
        anyRow = true;
        DrawReferenceRow(
            "Referenced By",
            *dependent,
            dependent->missing ? "missing file" : "ok",
            registry,
            assetSelection);
    }
    for (const std::string& token : missingTokens) {
        anyRow = true;
        DrawReferenceTokenRow("Missing", token, "unregistered");
    }
    for (const std::string& token : malformedTokens) {
        anyRow = true;
        DrawReferenceTokenRow("Malformed", token, "invalid token");
    }

    if (!anyRow) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("No indexed asset references.");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("-");
    }

    ImGui::EndTable();
}

void PushAssetMutationNotification(
    EditorNotificationCenter* notifications,
    const EditorAssetMutationResult& result) {
    if (notifications == nullptr || result.message.empty()) {
        return;
    }
    notifications->Push(
        result.succeeded
            ? (result.warning ? EditorNotificationSeverity::Warning : EditorNotificationSeverity::Info)
            : EditorNotificationSeverity::Error,
        "Asset",
        result.message);
}

void PushAssetImportNotification(
    EditorNotificationCenter* notifications,
    const EditorAssetImportResult& result) {
    if (notifications == nullptr || result.message.empty()) {
        return;
    }
    notifications->Push(
        result.succeeded
            ? (result.warning ? EditorNotificationSeverity::Warning : EditorNotificationSeverity::Info)
            : EditorNotificationSeverity::Error,
        "Asset",
        result.message);
}

EditorAssetExternalImportCollisionPolicy CollisionPolicyFromIndex(int index) {
    switch (index) {
    case 0:
        return EditorAssetExternalImportCollisionPolicy::Rename;
    case 1:
        return EditorAssetExternalImportCollisionPolicy::Skip;
    case 2:
        return EditorAssetExternalImportCollisionPolicy::Overwrite;
    }
    return EditorAssetExternalImportCollisionPolicy::Rename;
}

const char* CollisionPolicyLabel(int index) {
    return ToString(CollisionPolicyFromIndex(index));
}

std::vector<std::filesystem::path> ParseOpenFileNameBuffer(const std::vector<wchar_t>& buffer) {
    std::vector<std::filesystem::path> paths;
    if (buffer.empty() || buffer.front() == L'\0') {
        return paths;
    }

    const wchar_t* cursor = buffer.data();
    std::filesystem::path first(cursor);
    cursor += std::wcslen(cursor) + 1;
    if (*cursor == L'\0') {
        paths.push_back(std::move(first));
        return paths;
    }

    while (*cursor != L'\0') {
        paths.push_back(first / cursor);
        cursor += std::wcslen(cursor) + 1;
    }
    return paths;
}

std::vector<std::filesystem::path> OpenProductionAssetImportDialog(HWND owner) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter =
        L"Engine Assets\0*.mesh;*.obj;*.gltf;*.glb;*.fbx;*.effect;*.course;*.json;*.png;*.bmp;*.dds;*.jpg;*.jpeg;*.tga;*.wav;*.mp3;*.ogg;*.flac\0"
        L"All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags =
        OFN_EXPLORER |
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_ALLOWMULTISELECT |
        OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return ParseOpenFileNameBuffer(buffer);
}

EditorAssetExternalImportPolicy MakeExternalImportPolicy(
    const std::array<char, 128>& destinationFolderBuffer,
    int collisionIndex) {
    EditorAssetExternalImportPolicy policy{};
    policy.destinationFolder =
        destinationFolderBuffer[0] != '\0'
            ? std::filesystem::path(destinationFolderBuffer.data())
            : std::filesystem::path("Imported");
    policy.collisionPolicy = CollisionPolicyFromIndex(collisionIndex);
    return policy;
}

void ApplyExternalImportResult(
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorNotificationCenter* notifications,
    const EditorAssetImportResult& result) {
    PushAssetImportNotification(notifications, result);
    if (result.succeeded && result.importedCount > 0 && assetSelection != nullptr) {
        assetSelection->SetPrimary(MakeEditorAssetHandle(result.record, registry.Revision()));
    }
}

void ImportExternalPaths(
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorAssetThumbnailService* thumbnails,
    EditorNotificationCenter* notifications,
    const std::vector<std::filesystem::path>& paths,
    const EditorAssetExternalImportPolicy& policy) {
    if (paths.empty()) {
        return;
    }
    EditorAssetImportService importService(registry, thumbnails);
    const EditorAssetImportResult result =
        paths.size() == 1
            ? importService.ImportExternal(paths.front(), policy)
            : importService.ImportExternalBatch(paths, policy);
    ApplyExternalImportResult(registry, assetSelection, notifications, result);
}

void DrawProductionImportControls(
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorAssetThumbnailService* thumbnails,
    EditorNotificationCenter* notifications,
    EditorProductionMeshRuntimeCache* runtimeCache,
    HWND nativeDialogOwner,
    std::vector<std::filesystem::path>* pendingExternalImportPaths,
    const EditorAssetRecord* selectedRecord) {
    static std::array<char, 260> externalSourceBuffer{};
    static std::array<char, 128> destinationFolderBuffer{};
    static int collisionIndex = 0;
    static bool initialized = false;
    static std::string lastObjBakeSummary;
    if (!initialized) {
        std::snprintf(destinationFolderBuffer.data(), destinationFolderBuffer.size(), "%s", "Imported");
        initialized = true;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Production Import");
    if (pendingExternalImportPaths != nullptr && !pendingExternalImportPaths->empty()) {
        const std::vector<std::filesystem::path> droppedPaths = std::move(*pendingExternalImportPaths);
        pendingExternalImportPaths->clear();
        ImportExternalPaths(
            registry,
            assetSelection,
            thumbnails,
            notifications,
            droppedPaths,
            MakeExternalImportPolicy(destinationFolderBuffer, collisionIndex));
    }

    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("External Source", externalSourceBuffer.data(), externalSourceBuffer.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Collision", CollisionPolicyLabel(collisionIndex))) {
        for (int index = 0; index < 3; ++index) {
            const bool selected = index == collisionIndex;
            if (ImGui::Selectable(CollisionPolicyLabel(index), selected)) {
                collisionIndex = index;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Destination", destinationFolderBuffer.data(), destinationFolderBuffer.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Import...")) {
        const std::vector<std::filesystem::path> selectedPaths =
            OpenProductionAssetImportDialog(nativeDialogOwner);
        ImportExternalPaths(
            registry,
            assetSelection,
            thumbnails,
            notifications,
            selectedPaths,
            MakeExternalImportPolicy(destinationFolderBuffer, collisionIndex));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Opens a native file picker and imports selected files through the formal asset pipeline.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Import")) {
        ImportExternalPaths(
            registry,
            assetSelection,
            thumbnails,
            notifications,
            std::vector<std::filesystem::path>{std::filesystem::path(externalSourceBuffer.data())},
            MakeExternalImportPolicy(destinationFolderBuffer, collisionIndex));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copies an external file into Resources/<Destination>/ and imports it through the formal asset pipeline.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Batch Meta")) {
        EditorAssetImportService importService(registry, thumbnails);
        const EditorAssetImportResult result = importService.BatchMigrateMetadata();
        PushAssetImportNotification(notifications, result);
        if (assetSelection != nullptr) {
            if (const EditorAssetHandle* selected = assetSelection->Primary()) {
                const EditorAssetHandle refreshed = RefreshEditorAssetHandle(registry, *selected);
                if (refreshed.Valid()) {
                    assetSelection->SetPrimary(refreshed);
                }
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Creates durable .meta files for legacy/provisional assets.");
    }
    ImGui::SameLine();
    const bool canReimport = selectedRecord != nullptr && !selectedRecord->runtimeOnly;
    if (!canReimport) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Reimport")) {
        EditorAssetImportService importService(registry, thumbnails);
        const EditorAssetImportResult result =
            importService.Reimport(selectedRecord->kind, selectedRecord->id);
        PushAssetImportNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->SetPrimary(MakeEditorAssetHandle(result.record, registry.Revision()));
        }
    }
    if (!canReimport) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reimports the selected asset while preserving its GUID.");
    }

    const bool canBakeSourceMesh =
        selectedRecord != nullptr &&
        EditorObjProductionImportBridge::CanImport(*selectedRecord);
    if (!canBakeSourceMesh) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Import & Bake Mesh")) {
        EditorObjProductionImportBridge bridge(
            registry,
            runtimeCache,
            std::filesystem::current_path());
        EditorObjProductionImportRequest request{};
        request.sourceAssetGuid = selectedRecord->guid;
        request.outputAssetName =
            EditorObjProductionImportBridge::DefaultOutputAssetName(
                *selectedRecord);
        const EditorObjProductionImportResult result =
            bridge.ImportAndBake(request);
        lastObjBakeSummary = result.message;
        if (notifications != nullptr) {
            notifications->Push(
                result.succeeded
                    ? EditorNotificationSeverity::Info
                    : EditorNotificationSeverity::Error,
                "Mesh Production Import",
                result.message);
        }
        if (result.succeeded) {
            if (assetSelection != nullptr) {
                assetSelection->SetPrimary(
                    MakeEditorAssetHandle(
                        result.record,
                        registry.Revision()));
            }
            if (thumbnails != nullptr) {
                thumbnails->Sync(registry);
            }
        }
    }
    if (!canBakeSourceMesh) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Converts the selected OBJ, glTF, GLB, or FBX into a durable Production .mesh, "
            "cooks Renderer LODs and Collision, then selects the result.");
    }
    if (!lastObjBakeSummary.empty()) {
        ImGui::TextWrapped("OBJ Bake: %s", lastObjBakeSummary.c_str());
    }
}

void DrawMutationControls(
    const EditorAssetRecord& selectedRecord,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorTransactionStack* transactions,
    EditorNotificationCenter* notifications) {
    static EditorAssetKind editedKind = EditorAssetKind::Unknown;
    static std::string editedId;
    static std::array<char, 128> renameBuffer{};
    static std::array<char, 260> moveBuffer{};

    if (editedKind != selectedRecord.kind || editedId != selectedRecord.id) {
        editedKind = selectedRecord.kind;
        editedId = selectedRecord.id;
        std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", selectedRecord.id.c_str());
        std::snprintf(moveBuffer.data(), moveBuffer.size(), "%s", selectedRecord.sourcePath.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Mutation");
    if (ImGui::SmallButton("Duplicate")) {
        EditorAssetMutationExecutor executor(registry);
        const EditorAssetMutationResult result = executor.Execute(
            EditorAssetMutationRequest{
                EditorAssetMutationKind::Duplicate,
                selectedRecord.kind,
                selectedRecord.id,
                {},
                {},
                transactions});
        PushAssetMutationNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->SetPrimary(
                MakeEditorAssetHandle(result.updatedRecord, registry.Revision()));
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copies source and creates a new durable GUID .meta atomically. Undoable.");
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Rename Id", renameBuffer.data(), renameBuffer.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Rename")) {
        EditorAssetMutationExecutor executor(registry);
        const EditorAssetMutationResult result =
            executor.Execute(
                EditorAssetMutationRequest{
                    EditorAssetMutationKind::Rename,
                    selectedRecord.kind,
                    selectedRecord.id,
                    renameBuffer.data(),
                    {},
                    transactions});
        PushAssetMutationNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->SetPrimary(
                MakeEditorAssetHandle(result.updatedRecord, registry.Revision()));
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Renames the asset file stem, .meta file, registry id, and logical path.");
    }

    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Move Path", moveBuffer.data(), moveBuffer.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Move")) {
        EditorAssetMutationExecutor executor(registry);
        const EditorAssetMutationResult result =
            executor.Execute(
                EditorAssetMutationRequest{
                    EditorAssetMutationKind::Move,
                    selectedRecord.kind,
                    selectedRecord.id,
                    {},
                    moveBuffer.data(),
                    transactions});
        PushAssetMutationNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->SetPrimary(
                MakeEditorAssetHandle(result.updatedRecord, registry.Revision()));
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Moves the asset and .meta file. Destination must stay under Resources/.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        EditorAssetMutationExecutor executor(registry);
        const EditorAssetMutationResult result =
            executor.Execute(
                EditorAssetMutationRequest{
                    EditorAssetMutationKind::Delete,
                    selectedRecord.kind,
                    selectedRecord.id,
                    {},
                    {},
                    transactions});
        PushAssetMutationNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->Clear();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Deletes the asset and .meta file only when no indexed dependents exist.");
    }
    ImGui::BeginDisabled(selectedRecord.pathOnlyReferences.empty());
    if (ImGui::SmallButton("Repair Path Refs")) {
        EditorAssetMutationExecutor executor(registry);
        const EditorAssetMutationResult result = executor.Execute(
            EditorAssetMutationRequest{
                EditorAssetMutationKind::RepairReferences,
                selectedRecord.kind,
                selectedRecord.id,
                {},
                {},
                transactions});
        PushAssetMutationNotification(notifications, result);
        if (result.succeeded && assetSelection != nullptr) {
            assetSelection->SetPrimary(
                MakeEditorAssetHandle(result.updatedRecord, registry.Revision()));
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Converts resolvable path-only references to durable Asset GUID references. Undoable.");
    }
}

void DrawAssetDragSource(const EditorAssetRecord& record) {
    if (!record.referenceable || record.missing || record.guid.empty() ||
        !ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) return;
    EditorAssetDragDropPayload payload{};
    payload.kind = record.kind;
    std::snprintf(payload.guid.data(), payload.guid.size(), "%s", record.guid.c_str());
    std::snprintf(payload.id.data(), payload.id.size(), "%s", record.id.c_str());
    std::snprintf(payload.displayName.data(), payload.displayName.size(), "%s",
        record.displayName.empty() ? record.id.c_str() : record.displayName.c_str());
    ImGui::SetDragDropPayload(kEditorAssetDragDropPayloadId, &payload, sizeof(payload));
    ImGui::Text("%s:%s", ToString(record.kind), record.id.c_str());
    ImGui::TextDisabled("Drop into Viewport or compatible Details AssetRef.");
    ImGui::EndDragDropSource();
}

void DrawAssetContextMenu(
    const EditorAssetRecord& record,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorContentBrowserState* browserState) {
    if (!ImGui::BeginPopupContextItem("AssetContext")) return;
    const EditorAssetRecord snapshot = record;
    SelectAssetRecord(snapshot, registry, assetSelection, browserState);
    if (ImGui::MenuItem(
            browserState != nullptr && browserState->IsFavorite(snapshot.guid)
                ? "Remove Favorite" : "Add Favorite")) {
        if (browserState != nullptr) browserState->ToggleFavorite(snapshot.guid);
    }
    if (browserState != nullptr && ImGui::BeginMenu("Collections")) {
        if (browserState->Collections().empty()) ImGui::TextDisabled("No collections");
        for (const auto& pair : browserState->Collections()) {
            const bool member = browserState->IsInCollection(pair.first, snapshot.guid);
            if (ImGui::MenuItem(pair.first.c_str(), nullptr, member)) {
                if (member) browserState->RemoveFromCollection(pair.first, snapshot.guid);
                else browserState->AddToCollection(pair.first, snapshot.guid);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate") && browserState != nullptr) {
        browserState->QueueAssetContextAction(EditorAssetContextAction::Duplicate, snapshot);
    }
    if (ImGui::MenuItem("Reimport", nullptr, false, !snapshot.runtimeOnly) && browserState != nullptr) {
        browserState->QueueAssetContextAction(EditorAssetContextAction::Reimport, snapshot);
    }
    if (ImGui::MenuItem("Rename / Move...")) {
        SelectAssetRecord(snapshot, registry, assetSelection, browserState);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Selects this Asset for the transaction-backed Rename/Move controls above.");
    }
    const EditorAssetMutationSafetyReport deleteSafety =
        EvaluateEditorAssetMutationSafety(registry, snapshot, EditorAssetMutationKind::Delete);
    if (ImGui::MenuItem("Delete", nullptr, false, !deleteSafety.Blocked()) && browserState != nullptr) {
        browserState->QueueAssetContextAction(EditorAssetContextAction::Delete, snapshot);
    }
    ImGui::EndPopup();
}

void ProcessPendingAssetContextRequest(
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorAssetThumbnailService* thumbnails,
    EditorTransactionStack* transactions,
    EditorNotificationCenter* notifications,
    EditorContentBrowserState& browserState) {
    const auto pending = browserState.TakeAssetContextRequest();
    if (!pending) return;
    const auto& request = *pending;
    const EditorAssetRecord* target = registry.Find(request.kind, request.id);
    if (target == nullptr) return;
    if (request.action == EditorAssetContextAction::Reimport) {
        EditorAssetImportService service(registry, thumbnails);
        const EditorAssetImportResult result = service.Reimport(request.kind, request.id);
        PushAssetImportNotification(notifications, result);
        if (result.succeeded) SelectAssetRecord(result.record, registry, assetSelection, &browserState);
        return;
    }
    const EditorAssetMutationKind kind = request.action == EditorAssetContextAction::Duplicate
        ? EditorAssetMutationKind::Duplicate : EditorAssetMutationKind::Delete;
    EditorAssetMutationExecutor executor(registry);
    const EditorAssetMutationResult result = executor.Execute(
        EditorAssetMutationRequest{kind, request.kind, request.id, {}, {}, transactions});
    PushAssetMutationNotification(notifications, result);
    if (result.succeeded && kind == EditorAssetMutationKind::Duplicate) {
        SelectAssetRecord(result.updatedRecord, registry, assetSelection, &browserState);
    } else if (result.succeeded) {
        if (assetSelection != nullptr) assetSelection->Clear();
        browserState.SetSelectedAssetGuid({});
    }
    if (result.succeeded && thumbnails != nullptr) thumbnails->Sync(registry);
}

void DrawThumbnailPreviewLine(
    const EditorAssetRecord& record,
    const EditorAssetThumbnailEntry& thumbnail) {
    ImGui::TextUnformatted("Preview");
    ImGui::SameLine();
    ImGui::TextColored(
        ThumbnailStatusColor(thumbnail.status),
        "%s",
        ThumbnailLabel(thumbnail));
    ImGui::SameLine();
    const std::string summary = PreviewSummary(thumbnail);
    ImGui::TextDisabled("%s", summary.c_str());
    ImGui::SameLine();
    ImGui::TextColored(
        GpuThumbnailStatusColor(thumbnail.gpuStatus),
        "GPU %s",
        ToString(thumbnail.gpuStatus));
    ImGui::SameLine();
    ImGui::Text(
        "Job %s  Key %s  Gen %u  Stamp %llu",
        ToString(thumbnail.jobStatus),
        thumbnail.key.empty() ? "-" : thumbnail.key.c_str(),
        thumbnail.generation,
        static_cast<unsigned long long>(thumbnail.sourceTimestamp));
    if (ImGui::IsItemHovered()) {
        const std::string gpuSummary = GpuThumbnailSummary(thumbnail);
        ImGui::SetTooltip(
            "%s\n%s\nJob: %s attempts=%u\nGPU: %s revision=%u swatch=0x%08x texture=%llu\nSource: %s\nRecord key: %s",
            thumbnail.detail.c_str(),
            summary.c_str(),
            ToString(thumbnail.jobStatus),
            thumbnail.jobAttempts,
            gpuSummary.c_str(),
            thumbnail.gpuRenderRevision,
            thumbnail.gpuSwatchRgba,
            static_cast<unsigned long long>(thumbnail.gpuDisplayTextureId),
            record.sourcePath.empty() ? "-" : record.sourcePath.c_str(),
            record.thumbnailKey.empty() ? "-" : record.thumbnailKey.c_str());
    }
}

void DrawThumbnailTile(
    const EditorAssetRecord& record,
    bool selected,
    const EditorAssetThumbnailEntry& thumbnail,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    EditorContentBrowserState* browserState) {
    ImGui::PushID((std::string(ToString(record.kind)) + ":" + record.id).c_str());
    const ImVec4 color = ThumbnailStatusColor(thumbnail.status);
    const bool hasGpuTexture =
        thumbnail.gpuStatus == EditorAssetGpuThumbnailStatus::Ready &&
        thumbnail.gpuShaderResourceView &&
        thumbnail.gpuDisplayTextureId != 0;
    bool clicked = false;
    if (hasGpuTexture) {
        const ImTextureID textureId =
            reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(thumbnail.gpuDisplayTextureId));
        clicked = ImGui::ImageButton(
            "thumbnailGpuSrv",
            textureId,
            ImVec2(92.0f, 58.0f),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            ImVec4(color.x * 0.18f, color.y * 0.18f, color.z * 0.18f, 1.0f),
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(
            min,
            max,
            ImGui::ColorConvertFloat4ToU32(selected ? ImVec4(0.42f, 0.86f, 0.58f, 1.0f) : color),
            3.0f,
            0,
            selected ? 2.0f : 1.0f);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.32f, color.y * 0.32f, color.z * 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.46f, color.y * 0.46f, color.z * 0.46f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.58f, color.y * 0.58f, color.z * 0.58f, 1.0f));
        clicked = ImGui::Button(
            thumbnail.label.empty() ? ToString(record.kind) : thumbnail.label.c_str(),
            ImVec2(92.0f, 58.0f));
        ImGui::PopStyleColor(3);
    }
    if (clicked) {
        SelectAssetRecord(record, registry, assetSelection, browserState);
    }
    DrawAssetDragSource(record);
    DrawAssetContextMenu(
        record, registry, assetSelection, browserState);
    if (ImGui::IsItemHovered()) {
        const std::string summary = PreviewSummary(thumbnail);
        const std::string gpuSummary = GpuThumbnailSummary(thumbnail);
        ImGui::SetTooltip(
            "%s\n%s\n%s\nGPU: %s\n%s",
            AssetLabel(record).c_str(),
            ThumbnailLabel(thumbnail),
            summary.c_str(),
            gpuSummary.c_str(),
            thumbnail.detail.c_str());
    }
    if (selected) {
        ImGui::TextColored(ImVec4(0.42f, 0.86f, 0.58f, 1.0f), "%s", record.id.c_str());
    } else {
        ImGui::TextUnformatted(record.id.c_str());
    }
    ImGui::TextDisabled(
        "%s  %s  GPU %s",
        ToString(record.kind),
        ThumbnailLabel(thumbnail),
        ToString(thumbnail.gpuStatus));
    ImGui::PopID();
}

void DrawAssetGrid(
    const std::vector<const EditorAssetRecord*>& visibleAssets,
    EditorAssetRegistry& registry,
    EditorAssetSelection* assetSelection,
    const EditorAssetHandle* selectedAsset,
    EditorAssetThumbnailService* thumbnails,
    EditorContentBrowserState* browserState,
    float height) {
    const float tileWidth = 124.0f;
    const float availableWidth = (std::max)(tileWidth, ImGui::GetContentRegionAvail().x);
    const int columns = (std::max)(1, static_cast<int>(availableWidth / tileWidth));
    if (!ImGui::BeginTable(
            "EditorAssetBrowserGrid",
            columns,
            ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, height))) {
        return;
    }

    int column = 0;
    for (const EditorAssetRecord* record : visibleAssets) {
        if (record == nullptr) {
            continue;
        }
        if (column == 0) {
            ImGui::TableNextRow();
        }
        ImGui::TableSetColumnIndex(column);
        const bool selected =
            selectedAsset != nullptr &&
            selectedAsset->kind == record->kind &&
            selectedAsset->id == record->id;
        DrawThumbnailTile(
            *record,
            selected,
            ResolveThumbnail(*record, thumbnails),
            registry,
            assetSelection,
            browserState);
        column = (column + 1) % columns;
    }

    ImGui::EndTable();
}

void DrawContentBrowserNavigation(
    EditorContentBrowserState& state,
    const EditorAssetRegistry& registry) {
    ImGui::TextUnformatted("Folders");
    for (const std::string& folder : state.BuildFolders(registry)) {
        const int depth = static_cast<int>(std::count(folder.begin(), folder.end(), '/'));
        ImGui::Indent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
        const std::string label = std::filesystem::path(folder).filename().string().empty()
            ? folder : std::filesystem::path(folder).filename().string();
        if (ImGui::Selectable(
                (label + "##folder:" + folder).c_str(),
                state.SelectedFolder() == folder)) {
            state.SetSelectedFolder(folder);
        }
        ImGui::Unindent(static_cast<float>((std::max)(0, depth - 1)) * 10.0f);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Collections");
    if (ImGui::Selectable("All Assets##collection", state.ActiveCollection().empty())) {
        state.SetActiveCollection({});
        state.SetFavoritesOnly(false);
    }
    if (ImGui::Selectable("Favorites", state.FavoritesOnly())) {
        state.SetFavoritesOnly(true);
        state.SetActiveCollection({});
    }
    std::vector<std::string> collectionNames;
    for (const auto& pair : state.Collections()) collectionNames.push_back(pair.first);
    std::sort(collectionNames.begin(), collectionNames.end());
    for (const std::string& name : collectionNames) {
        const bool selected = state.ActiveCollection() == name;
        if (ImGui::Selectable((name + "##collection").c_str(), selected)) {
            state.SetFavoritesOnly(false);
            state.SetActiveCollection(name);
        }
        if (ImGui::BeginPopupContextItem(("CollectionContext:" + name).c_str())) {
            if (ImGui::MenuItem("Delete Collection")) state.RemoveCollection(name);
            ImGui::EndPopup();
        }
    }
    static std::array<char, 64> collectionBuffer{};
    ImGui::SetNextItemWidth(130.0f);
    ImGui::InputText("##newCollection", collectionBuffer.data(), collectionBuffer.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        if (state.CreateCollection(collectionBuffer.data())) collectionBuffer.fill('\0');
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create Collection");
}

} // namespace

float ResolveEditorAssetViewHeight(float availableHeight) {
    constexpr float kMinimumAssetViewHeight = 180.0f;
    return (std::max)(kMinimumAssetViewHeight, availableHeight);
}

void DrawEditorAssetBrowserPanel(const EditorAssetBrowserPanelContext& context) {
    if (context.registry == nullptr) {
        ImGui::TextUnformatted("Asset registry unavailable.");
        return;
    }

    EditorAssetRegistry& registry = *context.registry;
    static EditorContentBrowserState fallbackState;
    EditorContentBrowserState& browserState =
        context.browserState != nullptr ? *context.browserState : fallbackState;
    browserState.EnsureLoaded();
    ProcessPendingAssetContextRequest(
        registry,
        context.assetSelection,
        context.thumbnails,
        context.transactions,
        context.notifications,
        browserState);

    if (context.assetSelection != nullptr && !context.assetSelection->HasPrimary() &&
        !browserState.SelectedAssetGuid().empty()) {
        if (const EditorAssetRecord* restored =
                registry.FindByGuid(browserState.SelectedAssetGuid())) {
            context.assetSelection->SetPrimary(MakeEditorAssetHandle(*restored, registry.Revision()));
        }
    }

    EditorAssetThumbnailService* thumbnails = context.thumbnails;
    const EditorAssetHandle* selectedAsset =
        context.assetSelection != nullptr ? context.assetSelection->Primary() : nullptr;
    if (selectedAsset != nullptr && !selectedAsset->guid.empty() &&
        browserState.SelectedAssetGuid() != selectedAsset->guid) {
        browserState.SetSelectedAssetGuid(selectedAsset->guid);
    }
    if (selectedAsset != nullptr) {
        ImGui::Text(
            "Selected  %s:%s  GUID %s",
            ToString(selectedAsset->kind),
            selectedAsset->id.c_str(),
            selectedAsset->guid.empty() ? "-" : selectedAsset->guid.c_str());
    } else {
        ImGui::TextUnformatted("Selected  none");
    }

    if (ImGui::CollapsingHeader("Asset Diagnostics & Operations")) {
    ImGui::Text(
        "Registry  Assets %u  Mesh %u  Durable %u/%u (%.1f%%)  Redirects %u  Auto GUID %u  Deps %u  Missing %u  Revision %u",
        static_cast<unsigned int>(registry.Count()),
        static_cast<unsigned int>(registry.Count(EditorAssetKind::Mesh)),
        static_cast<unsigned int>(registry.CountDurableAssets()),
        static_cast<unsigned int>(registry.CountMetadataEligibleAssets()),
        registry.MetadataCoveragePercent(),
        static_cast<unsigned int>(registry.Redirects().size()),
        static_cast<unsigned int>(registry.CountWithProvisionalGuid()),
        static_cast<unsigned int>(registry.CountWithDependencies()),
        static_cast<unsigned int>(registry.CountMissing()),
        registry.Revision());
    if (thumbnails != nullptr) {
        ImGui::Text(
            "Preview Cache  Items %u  Ready %u  Pending %u  Failed %u  Missing %u  Revision %u",
            static_cast<unsigned int>(thumbnails->Count()),
            static_cast<unsigned int>(thumbnails->Count(EditorAssetThumbnailStatus::Ready)),
            static_cast<unsigned int>(thumbnails->Count(EditorAssetThumbnailStatus::Pending)),
            static_cast<unsigned int>(thumbnails->Count(EditorAssetThumbnailStatus::Failed)),
            static_cast<unsigned int>(thumbnails->Count(EditorAssetThumbnailStatus::Missing)),
            thumbnails->Revision());
        ImGui::TextDisabled(
            "Preview Jobs  Queued %u  Running %u  Ready %u  Failed %u  Revision %u",
            static_cast<unsigned int>(thumbnails->PreviewJobs().Count(EditorAssetPreviewJobStatus::Queued)),
            static_cast<unsigned int>(thumbnails->PreviewJobs().Count(EditorAssetPreviewJobStatus::Running)),
            static_cast<unsigned int>(thumbnails->PreviewJobs().Count(EditorAssetPreviewJobStatus::Ready)),
            static_cast<unsigned int>(thumbnails->PreviewJobs().Count(EditorAssetPreviewJobStatus::Failed)),
            thumbnails->PreviewJobs().Revision());
        ImGui::TextDisabled(
            "GPU Thumbnails  Queued %u  Rendering %u  Ready %u  Failed %u  Revision %u",
            static_cast<unsigned int>(thumbnails->GpuThumbnails().Count(EditorAssetGpuThumbnailStatus::Queued)),
            static_cast<unsigned int>(thumbnails->GpuThumbnails().Count(EditorAssetGpuThumbnailStatus::Rendering)),
            static_cast<unsigned int>(thumbnails->GpuThumbnails().Count(EditorAssetGpuThumbnailStatus::Ready)),
            static_cast<unsigned int>(thumbnails->GpuThumbnails().Count(EditorAssetGpuThumbnailStatus::Failed)),
            thumbnails->GpuThumbnails().Revision());
        const EditorAssetGpuThumbnailBackendTelemetry gpuTelemetry =
            thumbnails->GpuThumbnails().BackendTelemetry();
        ImGui::TextDisabled(
            "Thumbnail Lifetime  Resident %u/%u  Pending Uploads %u (%.2f MB)  Uploaded %.2f MB  Retired %.2f MB  Cache H/M %llu/%llu  Store %llu  Evict %llu  Fallback %llu",
            static_cast<unsigned int>(gpuTelemetry.residentCount),
            static_cast<unsigned int>(gpuTelemetry.descriptorCapacity),
            static_cast<unsigned int>(gpuTelemetry.pendingUploadCount),
            static_cast<double>(gpuTelemetry.pendingUploadBytes) / (1024.0 * 1024.0),
            static_cast<double>(gpuTelemetry.uploadBytes) / (1024.0 * 1024.0),
            static_cast<double>(gpuTelemetry.retiredUploadBytes) / (1024.0 * 1024.0),
            static_cast<unsigned long long>(gpuTelemetry.cacheHits),
            static_cast<unsigned long long>(gpuTelemetry.cacheMisses),
            static_cast<unsigned long long>(gpuTelemetry.cacheStores),
            static_cast<unsigned long long>(gpuTelemetry.cacheEvictions),
            static_cast<unsigned long long>(gpuTelemetry.fallbackUploads));
        ImGui::TextDisabled(
            "Preview Scene  Direct %llu  Fallback %llu  Draw %llu  Loader %llu  Procedural %llu  Proxy %llu  Material %llu  Tex %llu/%llu  SRV %llu/%llu D%u  Table %llu  PBR %llu N/R/M %llu/%llu/%llu  MatCache H/M %llu/%llu  RT Reuse %llu  RT Resize %llu",
            static_cast<unsigned long long>(gpuTelemetry.previewSceneRendered),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneFallback),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneRendererDraws),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneProductionMeshDraws),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneProceduralFallback),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneProxyGeometry),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialTextureBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialTextureFallback),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialTextureSrvBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialTextureSrvFallback),
            static_cast<unsigned int>(gpuTelemetry.previewSceneMaterialTextureSrvDescriptors),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialTextureTables),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialPbrPreviews),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialNormalMapBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialRoughnessMapBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneMaterialMetallicMapBound),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneProductionMaterialCacheHits),
            static_cast<unsigned long long>(gpuTelemetry.previewSceneProductionMaterialCacheMisses),
            static_cast<unsigned long long>(gpuTelemetry.previewRenderTargetReused),
            static_cast<unsigned long long>(gpuTelemetry.previewRenderTargetResized));
    } else {
        ImGui::TextUnformatted("Preview Cache  unavailable");
    }

    const EditorAssetRecord* activeSelectedRecord = nullptr;
    if (selectedAsset != nullptr) {
        const EditorAssetHandleResolveResult selectedResolve =
            ResolveEditorAssetHandle(registry, *selectedAsset);
        ImGui::Text(
            "Selected  %s:%s  GUID %s  Source %s",
            ToString(selectedAsset->kind),
            selectedAsset->id.c_str(),
            selectedAsset->guid.empty() ? "-" : selectedAsset->guid.c_str(),
            selectedAsset->sourcePath.empty() ? "-" : selectedAsset->sourcePath.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s / %s / %s",
            selectedAsset->referenceable ? "Referenceable" : "Index only",
            selectedAsset->hasMetadata ? "meta" : "auto-guid",
            selectedResolve.Current() ? "current" : (selectedResolve.found ? "stale" : "missing"));
        if (const EditorAssetRecord* selectedRecord = selectedResolve.record) {
            activeSelectedRecord = selectedRecord;
            const EditorAssetMutationSafetyReport renameSafety =
                EvaluateEditorAssetMutationSafety(
                    registry,
                    *selectedRecord,
                    EditorAssetMutationKind::Rename);
            const EditorAssetMutationSafetyReport moveSafety =
                EvaluateEditorAssetMutationSafety(
                    registry,
                    *selectedRecord,
                    EditorAssetMutationKind::Move);
            const EditorAssetMutationSafetyReport deleteSafety =
                EvaluateEditorAssetMutationSafety(
                    registry,
                    *selectedRecord,
                    EditorAssetMutationKind::Delete);
            ImGui::TextUnformatted("Safety");
            ImGui::SameLine();
            ImGui::TextColored(
                SafetyColor(renameSafety.risk),
                "Rename %s",
                ToString(renameSafety.risk));
            if (ImGui::IsItemHovered()) {
                const std::string report = FormatEditorAssetMutationSafetyReport(renameSafety);
                ImGui::SetTooltip("%s", report.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(
                SafetyColor(moveSafety.risk),
                "Move %s",
                ToString(moveSafety.risk));
            if (ImGui::IsItemHovered()) {
                const std::string report = FormatEditorAssetMutationSafetyReport(moveSafety);
                ImGui::SetTooltip("%s", report.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(
                SafetyColor(deleteSafety.risk),
                "Delete %s",
                ToString(deleteSafety.risk));
            if (ImGui::IsItemHovered()) {
                const std::string report = FormatEditorAssetMutationSafetyReport(deleteSafety);
                ImGui::SetTooltip("%s", report.c_str());
            }
            DrawThumbnailPreviewLine(
                *selectedRecord,
                ResolveThumbnail(*selectedRecord, thumbnails));
            if (context.thumbnails != nullptr) {
                const EditorAssetThumbnailEntry selectedThumbnail =
                    context.thumbnails->Resolve(*selectedRecord);
                if (selectedThumbnail.jobStatus == EditorAssetPreviewJobStatus::Failed) {
                    if (ImGui::SmallButton("Retry Preview")) {
                        context.thumbnails->RetryPreview(selectedThumbnail.key);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Queues the selected asset preview job again.");
                    }
                }
                if (selectedThumbnail.gpuStatus == EditorAssetGpuThumbnailStatus::Failed) {
                    if (ImGui::SmallButton("Retry GPU Thumbnail")) {
                        context.thumbnails->RetryGpuThumbnail(selectedThumbnail.key);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Queues the selected GPU thumbnail render again.");
                    }
                }
            }
            DrawReferenceInspector(
                *selectedRecord,
                registry,
                context.assetSelection,
                renameSafety,
                moveSafety,
                deleteSafety);
            DrawMutationControls(
                *selectedRecord,
                registry,
                context.assetSelection,
                context.transactions,
                context.notifications);
        }
    } else {
        ImGui::TextUnformatted("Selected  none");
    }

    DrawProductionImportControls(
        registry,
        context.assetSelection,
        context.thumbnails,
        context.notifications,
        context.productionMeshRuntimeCache,
        context.nativeDialogOwner,
        context.pendingExternalImportPaths,
        activeSelectedRecord);
    }

    const float minimumAssetViewHeight = ResolveEditorAssetViewHeight(0.0f);
    constexpr float kMinimumNavigationHeight = 112.0f;
    constexpr float kMaximumNavigationHeight = 190.0f;
    const float availableBeforeNavigation =
        (std::max)(0.0f, ImGui::GetContentRegionAvail().y);
    const float navigationHeight = (std::clamp)(
        availableBeforeNavigation - minimumAssetViewHeight - ImGui::GetFrameHeightWithSpacing(),
        kMinimumNavigationHeight,
        kMaximumNavigationHeight);

    ImGui::BeginChild(
        "ContentBrowserNavigation",
        ImVec2(210.0f, navigationHeight),
        true);
    DrawContentBrowserNavigation(browserState, registry);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    std::array<char, 128> textFilter{};
    std::snprintf(textFilter.data(), textFilter.size(), "%s", browserState.SearchText().c_str());
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::InputText("Search", textFilter.data(), textFilter.size())) {
        browserState.SetSearchText(textFilter.data());
    }

    EditorAssetKind kindFilter = browserState.KindFilter();
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Kind", FilterLabel(kindFilter))) {
        for (EditorAssetKind candidate : kAssetKindFilters) {
            const bool selected = candidate == kindFilter;
            if (ImGui::Selectable(FilterLabel(candidate), selected)) {
                kindFilter = candidate;
                browserState.SetKindFilter(candidate);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const std::vector<std::string> tags = browserState.BuildTags(registry);
    ImGui::SetNextItemWidth(180.0f);
    const char* tagPreview = browserState.TagFilter().empty()
        ? "All Tags" : browserState.TagFilter().c_str();
    if (ImGui::BeginCombo("Tag", tagPreview)) {
        if (ImGui::Selectable("All Tags", browserState.TagFilter().empty())) {
            browserState.SetTagFilter({});
        }
        for (const std::string& tag : tags) {
            if (ImGui::Selectable(tag.c_str(), browserState.TagFilter() == tag)) {
                browserState.SetTagFilter(tag);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button(
            browserState.ViewMode() == EditorContentBrowserViewMode::Grid
                ? "Grid [active]" : "Grid")) {
        browserState.SetViewMode(EditorContentBrowserViewMode::Grid);
    }
    ImGui::SameLine();
    if (ImGui::Button(
            browserState.ViewMode() == EditorContentBrowserViewMode::List
                ? "List [active]" : "List")) {
        browserState.SetViewMode(EditorContentBrowserViewMode::List);
    }
    ImGui::TextDisabled(
        "Folder: %s  Collection: %s  Favorites: %s  State r%u",
        browserState.SelectedFolder().c_str(),
        browserState.ActiveCollection().empty() ? "All" : browserState.ActiveCollection().c_str(),
        browserState.FavoritesOnly() ? "only" : "all",
        browserState.Revision());
    ImGui::EndGroup();
    ImGui::Separator();

    const std::vector<const EditorAssetRecord*> visibleAssets =
        browserState.FilterAssets(registry);
    const float assetViewHeight =
        ResolveEditorAssetViewHeight(ImGui::GetContentRegionAvail().y);
    if (browserState.ViewMode() == EditorContentBrowserViewMode::Grid) {
        DrawAssetGrid(
            visibleAssets,
            registry,
            context.assetSelection,
            selectedAsset,
            thumbnails,
            &browserState,
            assetViewHeight);
        return;
    }

    if (!ImGui::BeginTable(
            "EditorAssetBrowserTable",
            14,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, assetViewHeight))) {
        return;
    }

    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("Thumb", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 122.0f);
    ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthFixed, 210.0f);
    ImGui::TableSetupColumn("Meta", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("SCM", ImGuiTableColumnFlags_WidthFixed, 82.0f);
    ImGui::TableSetupColumn("Dirty/Cook", ImGuiTableColumnFlags_WidthFixed, 104.0f);
    ImGui::TableSetupColumn("Missing", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("Ref", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Deps", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Source");
    ImGui::TableHeadersRow();

    for (const EditorAssetRecord* recordPtr : visibleAssets) {
        if (recordPtr == nullptr) {
            continue;
        }
        const EditorAssetRecord& record = *recordPtr;
        const std::string rowIdentity = !record.guid.empty()
            ? record.guid
            : record.logicalPath;
        ImGui::PushID(rowIdentity.c_str());

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToString(record.kind));
        ImGui::TableNextColumn();
        const bool selected =
            selectedAsset != nullptr &&
            selectedAsset->kind == record.kind &&
            selectedAsset->id == record.id;
        if (ImGui::Selectable(
                record.id.c_str(),
                selected,
                ImGuiSelectableFlags_SpanAllColumns)) {
            SelectAssetRecord(record, registry, context.assetSelection, &browserState);
        }
        DrawAssetDragSource(record);
        DrawAssetContextMenu(
            record,
            registry,
            context.assetSelection,
            &browserState);
        ImGui::TableNextColumn();
        const EditorAssetThumbnailEntry thumbnail = ResolveThumbnail(record, thumbnails);
        ImGui::TextColored(
            ThumbnailStatusColor(thumbnail.status),
            "%s",
            ThumbnailLabel(thumbnail));
        if (ImGui::IsItemHovered()) {
            const std::string summary = PreviewSummary(thumbnail);
            ImGui::SetTooltip(
                "%s\n%s\nKey: %s\nGeneration: %u",
                thumbnail.detail.c_str(),
                summary.c_str(),
                thumbnail.key.empty() ? "-" : thumbnail.key.c_str(),
                thumbnail.generation);
        }
        ImGui::TableNextColumn();
        const std::string shortGuid = ShortGuid(record.guid);
        ImGui::TextUnformatted(shortGuid.c_str());
        if (ImGui::IsItemHovered() && !record.guid.empty()) {
            ImGui::SetTooltip(
                "GUID: %s\nLogical: %s\nMeta: %s",
                record.guid.c_str(),
                record.logicalPath.empty() ? "-" : record.logicalPath.c_str(),
                record.metadataPath.empty() ? "-" : record.metadataPath.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.displayName.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(MetadataLabel(record));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(browserState.IsFavorite(record.guid) ? "*" : "-");
        const EditorAssetWorkspaceStatus workspaceStatus = context.workspaceStatus != nullptr
            ? context.workspaceStatus->QueryStatus(record)
            : EditorAssetWorkspaceStatus{};
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToString(workspaceStatus.sourceControl));
        if (ImGui::IsItemHovered() && !workspaceStatus.detail.empty()) {
            ImGui::SetTooltip("%s", workspaceStatus.detail.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::Text(
            "%s / %s",
            workspaceStatus.dirty ? "Dirty" : "Clean",
            ToString(workspaceStatus.cook));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.missing ? "yes" : "no");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.referenceable ? "yes" : "no");
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned int>(record.dependencies.size()));
        if (ImGui::IsItemHovered() && !record.dependencies.empty()) {
            std::string tooltip = "Dependencies";
            for (const std::string& dependency : record.dependencies) {
                tooltip += "\n";
                tooltip += dependency;
            }
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
        ImGui::TableNextColumn();
        const std::size_t dependentCount = registry.CountDependents(record);
        ImGui::Text("%u", static_cast<unsigned int>(dependentCount));
        if (ImGui::IsItemHovered() && dependentCount > 0) {
            std::string tooltip = "Referenced By";
            for (const EditorAssetRecord* dependent : registry.FindDependents(record)) {
                if (dependent != nullptr) {
                    tooltip += "\n";
                    tooltip += AssetLabel(*dependent);
                }
            }
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.sourcePath.c_str());
        ImGui::PopID();
    }

    ImGui::EndTable();
}

} // namespace editor
