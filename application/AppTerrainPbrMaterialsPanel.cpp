#include "AppTerrainPbrMaterialsPanel.h"

#include "AppSceneResources.h"
#include "editor/EditorNotificationCenter.h"
#include "editor/io/EditorFileTransaction.h"
#include "editor/io/EditorProjectPathPolicy.h"

#include "../../externals/imgui/imgui.h"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

constexpr size_t kPathBufferSize = 768;

enum class TerrainPbrFileDialogKind {
    MaterialInstance,
    Texture,
};

std::optional<std::filesystem::path> OpenTerrainPbrFileDialog(
    HWND owner,
    TerrainPbrFileDialogKind kind,
    const std::filesystem::path& currentPath = {}) {
    std::vector<wchar_t> buffer(32768, L'\0');
    std::wstring initialDirectory;
    std::error_code pathError;
    std::filesystem::path directory = currentPath;
    if (!directory.empty() && !std::filesystem::is_directory(directory, pathError)) {
        directory = directory.parent_path();
    }
    if (directory.empty() || pathError) {
        pathError.clear();
        directory =
            std::filesystem::path("Resources") / "terrain" / "materials";
    }
    directory = std::filesystem::absolute(directory, pathError);
    if (!pathError) {
        initialDirectory = directory.wstring();
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir =
        initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    if (kind == TerrainPbrFileDialogKind::MaterialInstance) {
        dialog.lpstrFilter =
            L"Terrain Material Instance (*.terrainmaterial)\0*.terrainmaterial\0"
            L"All Files\0*.*\0";
        dialog.lpstrTitle = L"Load Terrain PBR Material Instance";
        dialog.lpstrDefExt = L"terrainmaterial";
    } else {
        dialog.lpstrFilter =
            L"PBR Texture (*.dds;*.png;*.jpg;*.jpeg;*.bmp)\0*.dds;*.png;*.jpg;*.jpeg;*.bmp\0"
            L"DDS Texture (*.dds)\0*.dds\0"
            L"PNG Texture (*.png)\0*.png\0"
            L"JPEG Texture (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
            L"Bitmap Texture (*.bmp)\0*.bmp\0"
            L"All Files\0*.*\0";
        dialog.lpstrTitle = L"Select Terrain PBR Texture";
    }
    dialog.nFilterIndex = 1;
    dialog.Flags =
        OFN_EXPLORER |
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR |
        OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

bool ResolveSelectedProjectFile(
    const std::filesystem::path& selected,
    std::filesystem::path& projectPath,
    std::string& error) {
    const editor::EditorProjectPathPolicy pathPolicy(
        std::filesystem::current_path());
    const editor::EditorProjectPathResolution resolved =
        pathPolicy.Resolve(selected);
    if (!resolved.accepted) {
        error =
            "Selected file is outside the project. Import it through the "
            "Content Browser first: " +
            selected.generic_string();
        return false;
    }
    projectPath = resolved.projectRelativePath;
    return true;
}

bool AnyDirty(const TerrainPbrMaterialsPanelState& state) {
    return std::any_of(
        state.dirty.begin(),
        state.dirty.end(),
        [](bool dirty) { return dirty; });
}

void Notify(
    editor::EditorNotificationCenter* notifications,
    editor::EditorNotificationSeverity severity,
    const std::string& message) {
    if (notifications != nullptr) {
        notifications->Push(
            severity,
            "Terrain PBR Materials",
            message);
    }
}

void SyncFromRuntime(
    TerrainPbrMaterialsPanelState& state,
    const AppSceneResources& scene) {
    state.drafts = scene.TerrainMaterials().Layers();
    state.dirty.fill(false);
    state.loadedRevision = scene.TerrainMaterialRevision();
    state.initialized = true;
    state.selectedLayer = std::clamp(
        state.selectedLayer,
        0,
        static_cast<int>(TerrainMaterialLibrary::kLayerCount) - 1);
}

bool InputStringValue(
    const char* label,
    std::string& value,
    size_t bufferSize = 256) {
    std::vector<char> buffer((std::max)(bufferSize, value.size() + 2u), '\0');
    std::memcpy(buffer.data(), value.data(), value.size());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}

bool InputTexturePath(
    const char* label,
    std::filesystem::path& path,
    const char* channelHint,
    HWND owner,
    editor::EditorNotificationCenter* notifications,
    std::string& lastAction,
    const char* emptyStatus = "Missing / fallback") {
    std::string pathText = path.lexically_normal().generic_string();
    std::array<char, kPathBufferSize> buffer{};
    const size_t copied = (std::min)(pathText.size(), buffer.size() - 1u);
    std::memcpy(buffer.data(), pathText.data(), copied);

    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const bool empty = path.empty();
    const bool exists = !empty && std::filesystem::exists(path);
    const char* status = exists
        ? "Ready"
        : empty
            ? emptyStatus
            : "Missing / fallback";
    ImGui::TextColored(
        exists
            ? ImVec4(0.30f, 0.92f, 0.48f, 1.0f)
            : empty
                ? ImVec4(0.82f, 0.78f, 0.42f, 1.0f)
                : ImVec4(1.0f, 0.48f, 0.24f, 1.0f),
        "%s",
        status);
    const float browseWidth =
        ImGui::CalcTextSize("Browse...").x +
        ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(
        (std::max)(80.0f, ImGui::GetContentRegionAvail().x -
            browseWidth - ImGui::GetStyle().ItemSpacing.x));
    bool changed =
        ImGui::InputText("##Path", buffer.data(), buffer.size());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", channelHint);
    }
    if (changed) {
        path = std::filesystem::path(buffer.data()).lexically_normal();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        const std::optional<std::filesystem::path> selected =
            OpenTerrainPbrFileDialog(
                owner,
                TerrainPbrFileDialogKind::Texture,
                path);
        if (selected.has_value()) {
            std::filesystem::path projectPath;
            std::string error;
            if (ResolveSelectedProjectFile(
                    *selected,
                    projectPath,
                    error)) {
                path = std::move(projectPath);
                lastAction =
                    std::string(label) + " selected: " +
                    path.generic_string();
                changed = true;
            } else {
                lastAction = error;
                Notify(
                    notifications,
                    editor::EditorNotificationSeverity::Error,
                    error);
            }
        }
    }
    ImGui::PopID();
    return changed;
}

bool ValidateDefinition(
    const TerrainPbrMaterialDefinition& definition,
    std::string& error) {
    if (definition.sourcePath.empty()) {
        error = "Material definition has no source file.";
        return false;
    }
    if (definition.id.empty()) {
        error = "Material Id cannot be empty.";
        return false;
    }
    const float values[] = {
        definition.baseColorTint.x,
        definition.baseColorTint.y,
        definition.baseColorTint.z,
        definition.worldTileSize,
        definition.normalStrength,
        definition.detailNormalStrength,
        definition.roughnessScale,
        definition.roughnessBias,
        definition.aoStrength,
        definition.heightScale,
        definition.heightBlendSharpness,
        definition.macroVariationStrength,
        definition.wetnessResponse,
    };
    for (float value : values) {
        if (!std::isfinite(value)) {
            error = "Material parameters must contain finite numbers.";
            return false;
        }
    }
    const editor::EditorProjectPathPolicy pathPolicy(
        std::filesystem::current_path());
    const std::filesystem::path texturePaths[] = {
        definition.baseColorPath,
        definition.normalPath,
        definition.ormPath,
        definition.ambientOcclusionPath,
        definition.roughnessPath,
        definition.metallicPath,
        definition.heightPath,
    };
    for (const std::filesystem::path& path : texturePaths) {
        if (!path.empty() && !pathPolicy.IsInsideProject(path)) {
            error =
                "Texture references must stay inside the project: " +
                path.generic_string();
            return false;
        }
    }
    return true;
}

bool SaveDefinitions(
    TerrainPbrMaterialsPanelState& state,
    const std::vector<size_t>& layerIndices,
    std::string& error) {
    if (layerIndices.empty()) {
        error = "No modified terrain material layers to save.";
        return false;
    }

    editor::EditorFileTransaction transaction(
        std::filesystem::current_path());
    for (size_t layerIndex : layerIndices) {
        if (layerIndex >= state.drafts.size()) {
            error = "Terrain material layer index is invalid.";
            return false;
        }
        const TerrainPbrMaterialDefinition& definition =
            state.drafts[layerIndex];
        if (!ValidateDefinition(definition, error)) {
            return false;
        }
        if (!transaction.StageTextWrite(
                definition.sourcePath,
                SerializeTerrainMaterialDefinition(definition),
                {},
                &error)) {
            return false;
        }
    }
    return transaction.Execute(nullptr, &error);
}

void DrawStatus(
    const TerrainPbrMaterialsPanelState& state,
    const AppSceneResources& scene) {
    const std::string& status = scene.TerrainMaterialHotReloadStatus();
    const bool failed = status.find("failed") != std::string::npos;
    const bool pending = scene.TerrainMaterialReloadPending();
    ImGui::TextColored(
        failed
            ? ImVec4(1.0f, 0.32f, 0.25f, 1.0f)
            : pending
                ? ImVec4(1.0f, 0.76f, 0.24f, 1.0f)
                : ImVec4(0.32f, 0.92f, 0.52f, 1.0f),
        "%s: %s",
        pending ? "Pending" : "Runtime",
        status.c_str());
    if (!state.lastAction.empty()) {
        ImGui::TextWrapped("%s", state.lastAction.c_str());
    }
}

} // namespace

void DrawTerrainPbrMaterialsPanel(
    TerrainPbrMaterialsPanelState& state,
    const TerrainPbrMaterialsPanelContext& context) {
    if (context.scene == nullptr) {
        ImGui::TextDisabled("Terrain PBR runtime is unavailable.");
        return;
    }
    AppSceneResources& scene = *context.scene;
    if (!state.initialized ||
        state.loadedRevision != scene.TerrainMaterialRevision()) {
        const bool replacedDirtyDraft = state.initialized && AnyDirty(state);
        SyncFromRuntime(state, scene);
        state.lastAction = replacedDirtyDraft
            ? "Disk reload replaced the local draft."
            : "Material instances synchronized with the runtime.";
    }

    ImGui::TextUnformatted("Terrain PBR Material Instances");
    ImGui::TextDisabled("Parent: M_TerrainRockSurface");
    ImGui::TextDisabled("Shading: GGX / Cook-Torrance + IBL");
    ImGui::TextDisabled(
        "Scalar overrides preview live; texture slots update after Apply.");
    DrawStatus(state, scene);
    ImGui::Separator();

    const char* layerLabels[TerrainMaterialLibrary::kLayerCount] = {};
    for (size_t index = 0; index < state.drafts.size(); ++index) {
        layerLabels[index] = state.drafts[index].id.empty()
            ? "Unnamed Layer"
            : state.drafts[index].id.c_str();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo(
        "##TerrainPbrLayer",
        &state.selectedLayer,
        layerLabels,
        static_cast<int>(TerrainMaterialLibrary::kLayerCount));
    const size_t selected = static_cast<size_t>(std::clamp(
        state.selectedLayer,
        0,
        static_cast<int>(TerrainMaterialLibrary::kLayerCount) - 1));
    TerrainPbrMaterialDefinition& material = state.drafts[selected];

    if (!context.canEdit) {
        ImGui::BeginDisabled();
    }

    bool changed = false;
    if (ImGui::CollapsingHeader(
            "Instance",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= InputStringValue("Material Id", material.id);
        ImGui::TextDisabled(
            "Source: %s",
            material.sourcePath.generic_string().c_str());
        if (ImGui::Button("Load Instance...")) {
            const std::optional<std::filesystem::path> selectedFile =
                OpenTerrainPbrFileDialog(
                    context.ownerWindow,
                    TerrainPbrFileDialogKind::MaterialInstance,
                    material.sourcePath);
            if (selectedFile.has_value()) {
                std::filesystem::path projectPath;
                std::string error;
                if (!ResolveSelectedProjectFile(
                        *selectedFile,
                        projectPath,
                        error)) {
                    state.lastAction = error;
                    Notify(
                        context.notifications,
                        editor::EditorNotificationSeverity::Error,
                        error);
                } else {
                    TerrainPbrMaterialDefinition loaded;
                    if (LoadTerrainMaterialDefinition(
                            projectPath,
                            loaded,
                            &error)) {
                        const std::filesystem::path destinationSource =
                            material.sourcePath;
                        material = std::move(loaded);
                        // Loading an instance copies its overrides into the
                        // selected runtime slot. Apply still writes the slot
                        // referenced by the active three-layer material set.
                        material.sourcePath = destinationSource;
                        state.lastAction =
                            "Loaded instance into layer " +
                            std::to_string(selected) + ": " +
                            projectPath.generic_string();
                        changed = true;
                    } else {
                        state.lastAction = "Load failed: " + error;
                        Notify(
                            context.notifications,
                            editor::EditorNotificationSeverity::Error,
                            state.lastAction);
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Folder")) {
            std::error_code error;
            std::filesystem::path folder =
                std::filesystem::absolute(
                    material.sourcePath.parent_path(),
                    error);
            HINSTANCE result = nullptr;
            if (!error) {
                result = ShellExecuteW(
                    context.ownerWindow,
                    L"open",
                    folder.c_str(),
                    nullptr,
                    nullptr,
                    SW_SHOWNORMAL);
            }
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                state.lastAction =
                    "Could not open material folder: " +
                    folder.generic_string();
                Notify(
                    context.notifications,
                    editor::EditorNotificationSeverity::Error,
                    state.lastAction);
            }
        }
    }

    if (ImGui::CollapsingHeader(
            "Texture Parameters",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= InputTexturePath(
            "Base Color",
            material.baseColorPath,
            "sRGB albedo without baked lighting.",
            context.ownerWindow,
            context.notifications,
            state.lastAction);
        changed |= InputTexturePath(
            "Normal",
            material.normalPath,
            "Linear tangent-space normal map, +Y convention.",
            context.ownerWindow,
            context.notifications,
            state.lastAction);

        int ormInputMode =
            material.ormInputMode == TerrainPbrOrmInputMode::Separate
                ? 1
                : 0;
        constexpr const char* kOrmInputModes[] = {
            "Packed ORM",
            "Separate Maps",
        };
        if (ImGui::Combo(
                "Surface Input",
                &ormInputMode,
                kOrmInputModes,
                static_cast<int>(std::size(kOrmInputModes)))) {
            material.ormInputMode =
                ormInputMode == 1
                    ? TerrainPbrOrmInputMode::Separate
                    : TerrainPbrOrmInputMode::Packed;
            changed = true;
        }
        if (material.ormInputMode == TerrainPbrOrmInputMode::Packed) {
            ImGui::TextDisabled(
                "Packed channels: R=AO, G=Roughness, B=Metallic.");
            changed |= InputTexturePath(
                "ORM",
                material.ormPath,
                "Linear packed map: R=AO, G=roughness, B=metallic.",
                context.ownerWindow,
                context.notifications,
                state.lastAction);
        } else {
            ImGui::TextDisabled(
                "Separate grayscale maps use R; Apply packs them into ORM.");
            changed |= InputTexturePath(
                "Ambient Occlusion",
                material.ambientOcclusionPath,
                "Linear AO map sampled from R. Empty uses 1.0.",
                context.ownerWindow,
                context.notifications,
                state.lastAction,
                "Default 1.0");
            changed |= InputTexturePath(
                "Roughness",
                material.roughnessPath,
                "Linear roughness map sampled from R. Empty uses 1.0.",
                context.ownerWindow,
                context.notifications,
                state.lastAction,
                "Default 1.0");
            changed |= InputTexturePath(
                "Metallic",
                material.metallicPath,
                "Linear metallic map sampled from R. Empty uses 0.0.",
                context.ownerWindow,
                context.notifications,
                state.lastAction,
                "Default 0.0");
        }
        changed |= InputTexturePath(
            "Height",
            material.heightPath,
            "Linear height map sampled from the red channel.",
            context.ownerWindow,
            context.notifications,
            state.lastAction);
    }

    if (ImGui::CollapsingHeader(
            "Base Color & Tiling",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::ColorEdit3(
            "Base Color Tint",
            &material.baseColorTint.x,
            ImGuiColorEditFlags_Float);
        changed |= ImGui::DragFloat(
            "World Tile Size",
            &material.worldTileSize,
            0.10f,
            0.25f,
            512.0f,
            "%.2f m");
        changed |= ImGui::SliderFloat(
            "Macro Variation",
            &material.macroVariationStrength,
            0.0f,
            1.0f,
            "%.3f");
    }

    if (ImGui::CollapsingHeader(
            "Normal",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat(
            "Normal Strength",
            &material.normalStrength,
            0.0f,
            4.0f,
            "%.3f");
        changed |= ImGui::SliderFloat(
            "Detail Normal Strength",
            &material.detailNormalStrength,
            0.0f,
            4.0f,
            "%.3f");
    }

    if (ImGui::CollapsingHeader(
            "Surface",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat(
            "Roughness Scale",
            &material.roughnessScale,
            0.0f,
            4.0f,
            "%.3f");
        changed |= ImGui::SliderFloat(
            "Roughness Bias",
            &material.roughnessBias,
            -1.0f,
            1.0f,
            "%.3f");
        changed |= ImGui::SliderFloat(
            "AO Strength",
            &material.aoStrength,
            0.0f,
            2.0f,
            "%.3f");
        changed |= ImGui::SliderFloat(
            "Wetness Response",
            &material.wetnessResponse,
            0.0f,
            2.0f,
            "%.3f");
    }

    if (ImGui::CollapsingHeader(
            "Height Blend",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat(
            "Height Scale",
            &material.heightScale,
            0.0f,
            0.25f,
            "%.4f");
        changed |= ImGui::SliderFloat(
            "Blend Sharpness",
            &material.heightBlendSharpness,
            0.0f,
            16.0f,
            "%.2f");
    }

    if (changed) {
        material.baseColorTint.x =
            std::clamp(material.baseColorTint.x, 0.0f, 4.0f);
        material.baseColorTint.y =
            std::clamp(material.baseColorTint.y, 0.0f, 4.0f);
        material.baseColorTint.z =
            std::clamp(material.baseColorTint.z, 0.0f, 4.0f);
        material.baseColorTint.w = 1.0f;
        material.worldTileSize =
            std::clamp(material.worldTileSize, 0.25f, 512.0f);
        material.normalStrength =
            std::clamp(material.normalStrength, 0.0f, 4.0f);
        material.detailNormalStrength =
            std::clamp(material.detailNormalStrength, 0.0f, 4.0f);
        material.roughnessScale =
            std::clamp(material.roughnessScale, 0.0f, 4.0f);
        material.roughnessBias =
            std::clamp(material.roughnessBias, -1.0f, 1.0f);
        material.aoStrength =
            std::clamp(material.aoStrength, 0.0f, 2.0f);
        material.heightScale =
            std::clamp(material.heightScale, 0.0f, 0.25f);
        material.heightBlendSharpness =
            std::clamp(material.heightBlendSharpness, 0.0f, 16.0f);
        material.macroVariationStrength =
            std::clamp(material.macroVariationStrength, 0.0f, 1.0f);
        material.wetnessResponse =
            std::clamp(material.wetnessResponse, 0.0f, 2.0f);
        state.dirty[selected] = true;
        state.lastAction =
            "Previewing unsaved changes for " + material.id + ".";
        scene.PreviewTerrainMaterialDefinitions(state.drafts);
    }

    ImGui::Separator();
    const bool selectedDirty = state.dirty[selected];
    if (!selectedDirty) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Apply Selected")) {
        std::string error;
        if (SaveDefinitions(state, {selected}, error)) {
            state.lastAction =
                "Saved " + material.id + "; GPU hot reload is queued.";
            Notify(
                context.notifications,
                editor::EditorNotificationSeverity::Info,
                state.lastAction);
        } else {
            state.lastAction = "Save failed: " + error;
            Notify(
                context.notifications,
                editor::EditorNotificationSeverity::Error,
                state.lastAction);
        }
    }
    if (!selectedDirty) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool anyDirty = AnyDirty(state);
    if (!anyDirty) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Apply All")) {
        std::vector<size_t> dirtyLayers;
        for (size_t index = 0; index < state.dirty.size(); ++index) {
            if (state.dirty[index]) {
                dirtyLayers.push_back(index);
            }
        }
        std::string error;
        if (SaveDefinitions(state, dirtyLayers, error)) {
            state.lastAction =
                "Saved all modified layers; GPU hot reload is queued.";
            Notify(
                context.notifications,
                editor::EditorNotificationSeverity::Info,
                state.lastAction);
        } else {
            state.lastAction = "Save failed: " + error;
            Notify(
                context.notifications,
                editor::EditorNotificationSeverity::Error,
                state.lastAction);
        }
    }
    if (!anyDirty) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!selectedDirty) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Revert")) {
        material = scene.TerrainMaterials().Layers()[selected];
        state.dirty[selected] = false;
        state.lastAction = "Reverted the selected material instance.";
        scene.PreviewTerrainMaterialDefinitions(state.drafts);
    }
    if (!selectedDirty) {
        ImGui::EndDisabled();
    }

    if (!context.canEdit) {
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "Editing is disabled by the current editor mutation policy.");
    } else if (AnyDirty(state)) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.22f, 1.0f),
            "Unsaved instance overrides");
    }
}
