#include "AppImGuiLayer.h"
#include "editor/course/CourseEnemyEditorController.h"
#include "editor/course/CourseEnemyDetailsPanel.h"
#include "editor/course/CourseEnemyPickingService.h"
#include "editor/course/CourseEnemyTransformGizmo.h"
#include "editor/course/CourseEnemyViewportEditTool.h"
#include "editor/course/CourseEnemyViewportRenderer.h"
#include "editor/course/CourseWaveEditorController.h"
#include "editor/course/CourseWaveDetailsPanel.h"
#include "editor/course/CourseWavePickingService.h"
#include "editor/course/CourseWaveViewportRenderer.h"
#include "editor/course/CoursePreviewSimulationSystem.h"
#include "editor/course/CoursePreviewActorRuntimeBridge.h"
#include "editor/course/CourseRailEditorController.h"
#include "editor/course/CourseRailPickingService.h"
#include "editor/course/CourseRailViewportRenderer.h"

#if defined(GE3_ENABLE_IMGUI) && GE3_ENABLE_IMGUI

#include "AppLogFile.h"

#include "AppDebugViewsPanel.h"
#include "AppCourseTimelineDebugPanel.h"
#include "AppEditorToolModules.h"
#include "AppEffectAssetEditorPanel.h"
#include "AppEffectInstancePanel.h"
#include "AppPostProcessPanel.h"
#include "AppRenderGraphDebugPanel.h"
#include "RuntimeAuthoringPolicy.h"
#include "AppRuntimeState.h"
#include "AppSceneResources.h"
#include "AppSceneControlsPanel.h"
#include "AppTerrainPbrMaterialsPanel.h"
#include "AppVfxDebugDataBuilder.h"
#include "AppVfxRuntimeQueuesPanel.h"
#include "AppVfxRuntimeStatusPanel.h"
#include "AppVfxTelemetry.h"
#include "AppVfxTelemetryPanel.h"
#include "AppGpuParticleSystem.h"
#include "AppPipelines.h"
#include "AppFrameState.h"
#include "EffectRuntime.h"
#include "EffectAssetLoader.h"
#include "PostProcessStack.h"
#include "course/CourseAsset.h"
#include "editor/CourseMeshAssetAdapter.h"
#include "editor/CourseDocumentAdapter.h"
#include "editor/CourseEditorCommandProvider.h"
#include "editor/CourseObjectPropertyAdapter.h"
#include "editor/EditorCompositePropertyAccessor.h"
#include "editor/CourseObjectValidationAdapter.h"
#include "editor/EffectAssetDiagnosticsAdapter.h"
#include "editor/EditorProductionPropertyAdapter.h"
#include "editor/EditorAssetBrowserPanel.h"
#include "editor/EditorAssetCommandProvider.h"
#include "editor/EditorAssetMutationExecutor.h"
#include "editor/EditorBuiltinCommandProvider.h"
#include "editor/EditorAssetReferenceDiagnosticsAdapter.h"
#include "editor/EditorAssetThumbnailDiagnosticsAdapter.h"
#include "editor/EditorAssetFolderIndexer.h"
#include "editor/EditorCommandContext.h"
#include "editor/EditorContext.h"
#include "editor/EditorCommandInputRouter.h"
#include "editor/EditorCommandPanel.h"
#include "editor/EditorCommandPalette.h"
#include "editor/EditorCommandRegistry.h"
#include "editor/EditorBuiltinDetailsSectionProviders.h"
#include "editor/play/EditorRuntimeChangeSetPanel.h"
#include "editor/EditorDetailsPanel.h"
#include "editor/EditorDiagnosticsPanel.h"
#include "editor/EditorDocumentLifecycleService.h"
#include "editor/EditorDocumentTabs.h"
#include "editor/EditorLayoutService.h"
#include "editor/EditorMenuBar.h"
#include "editor/EditorModalConfirmPanel.h"
#include "editor/EditorModalConfirmService.h"
#include "editor/EditorNotificationsPanel.h"
#include "editor/EditorPanelHost.h"
#include "editor/EditorPanelLayoutService.h"
#include "editor/EditorPanelRegistry.h"
#include "editor/EditorPropertyRegistryPanel.h"
#include "editor/EditorRuntimeInspectorPanel.h"
#include "editor/EditorRuntimeWatchBuilder.h"
#include "editor/EditorSaveApplyPolicy.h"
#include "editor/EditorSelectionPanel.h"
#include "editor/EditorStatusBar.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorTransformGizmoService.h"
#include "editor/EditorTransactionPanel.h"
#include "editor/EditorValidationService.h"
#include "editor/EditorViewportInteractionService.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorViewportSelectionBridge.h"
#include "editor/tools/EditorModePanels.h"
#include "editor/tools/EditorPlacementTools.h"
#include "editor/world/EditorWorldOutlinerPanel.h"
#include "editor/scene/EditorBlenderSceneImportTransaction.h"
#include "editor/scene/EditorGimmickComponent.h"
#include "editor/scene/EditorGimmickEventBindingComponent.h"
#include "editor/scene/EditorGimmickEventSequenceComponent.h"
#include "editor/scene/EditorPatrolComponent.h"
#include "editor/scene/EditorSplineRouteComponent.h"
#include "editor/ExistingFeatureProtection.h"
#include "editor/ExistingFeatureProtectionPanel.h"
#include "vfx/DistortionRenderer.h"
#include "vfx/ParticleRenderer.h"
#include "vfx/TrailRenderer.h"
#include "vfx/VfxResources.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_impl_dx12.h"
#include "../../externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#include <array>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using EditorUiTimingClock = std::chrono::steady_clock;

std::optional<std::filesystem::path> OpenBlenderLevelJsonDialog(HWND owner) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter =
        L"Blender Level JSON (*.json)\0*.json\0"
        L"All Files\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = L"Import Blender Level JSON";
    dialog.lpstrDefExt = L"json";
    dialog.Flags =
        OFN_EXPLORER |
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

double EditorUiElapsedMs(
    EditorUiTimingClock::time_point begin,
    EditorUiTimingClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

struct EditorImguiFrameTiming {
    double assetPipelineMs = 0.0;
    double assetRegistryInitMs = 0.0;
    double assetThumbnailSyncMs = 0.0;
    double assetPreviewJobMs = 0.0;
    double assetGpuSetupMs = 0.0;
    double assetGpuProcessMs = 0.0;
    double worldRefreshMs = 0.0;
    double validationMs = 0.0;
    double panelRegistryMs = 0.0;
    double panelDrawMs = 0.0;
    double layoutPersistenceMs = 0.0;
    double buildUiMs = 0.0;
    uint32_t previewJobs = 0;
    uint32_t gpuThumbnails = 0;
    uint32_t assetRecords = 0;
    uint32_t thumbnailEntries = 0;
    uint32_t previewQueued = 0;
    uint32_t previewRunning = 0;
    uint32_t previewReady = 0;
    uint32_t previewFailed = 0;
    uint32_t previewActiveAsync = 0;
    uint32_t gpuQueued = 0;
    uint32_t gpuRendering = 0;
    uint32_t gpuReady = 0;
    uint32_t gpuFailed = 0;
    uint32_t worldRefreshReasons = 0;
    uint64_t worldRefreshRevision = 0;
    uint64_t worldRefreshCacheHits = 0;
    bool worldRefreshExecuted = false;
    bool developerTools = false;
    bool viewportFocus = false;
};

editor::EditorObjectHandle MakeEditorObjectHandle(
    editor::EditorDomainId domain,
    const std::string& prefix,
    uint64_t index,
    uint32_t generation,
    std::string displayName) {
    editor::EditorObjectHandle handle{};
    handle.domain = domain;
    handle.stableId = editor::BuildStableIndexedId(prefix, index);
    handle.localIndex = index;
    handle.generation = generation;
    handle.displayName = std::move(displayName);
    return handle;
}

editor::EditorObjectHandle MakeNamedEditorObjectHandle(
    editor::EditorDomainId domain,
    const std::string& prefix,
    const std::string& name,
    uint64_t index,
    uint32_t generation,
    std::string displayName) {
    editor::EditorObjectHandle handle{};
    handle.domain = domain;
    handle.stableId = prefix + ":" + name;
    handle.localIndex = index;
    handle.generation = generation;
    handle.displayName = std::move(displayName);
    return handle;
}

std::vector<editor::EditorSelectionPanelTarget> BuildProductionSelectionTargets(
    const EffectRuntime& effectRuntime,
    const PostProcessStack& postProcessStack,
    const AppRuntimeState& runtimeState,
    const CourseAsset* course) {
    std::vector<editor::EditorSelectionPanelTarget> targets;

    uint64_t effectIndex = 0;
    for (const auto& [name, asset] : effectRuntime.Assets()) {
        (void)asset;
        targets.push_back(
            editor::EditorSelectionPanelTarget{
                MakeNamedEditorObjectHandle(
                    editor::EditorDomainId::VfxEffectAsset,
                    "vfx-asset",
                    name,
                    effectIndex++,
                    runtimeState.terrain.courseObjectEditRevision,
                    "VFX Asset: " + name)});
    }

    const std::vector<PostProcessPass>& passes = postProcessStack.Passes();
    for (std::size_t index = 0; index < passes.size(); ++index) {
        targets.push_back(
            editor::EditorSelectionPanelTarget{
                MakeNamedEditorObjectHandle(
                    editor::EditorDomainId::PostProcessPass,
                    "post-process",
                    passes[index].name,
                    static_cast<uint64_t>(index),
                    runtimeState.terrain.courseObjectEditRevision,
                    "PostProcess: " + passes[index].name)});
    }

    if (course != nullptr) {
        for (std::size_t index = 0; index < course->cameraKeys.size(); ++index) {
            targets.push_back(
                editor::EditorSelectionPanelTarget{
                    MakeEditorObjectHandle(
                        editor::EditorDomainId::CourseCameraKey,
                        "course-camera-key",
                        static_cast<uint64_t>(index),
                        runtimeState.terrain.courseObjectEditRevision,
                        "Course Camera Key #" + std::to_string(index))});
        }
        for (std::size_t index = 0; index < course->events.size(); ++index) {
            const CourseEventMarker& event = course->events[index];
            targets.push_back(
                editor::EditorSelectionPanelTarget{
                    MakeEditorObjectHandle(
                        editor::EditorDomainId::CourseEventMarker,
                        "course-event",
                        static_cast<uint64_t>(index),
                        runtimeState.terrain.courseObjectEditRevision,
                        "Course Event: " + (event.id.empty() ? std::to_string(index) : event.id))});
        }
    }

    targets.push_back(
        editor::EditorSelectionPanelTarget{
            MakeEditorObjectHandle(
                editor::EditorDomainId::TerrainGeneration,
                "terrain-generation",
                0,
                runtimeState.terrain.courseObjectEditRevision,
                "Terrain Generation")});
    targets.push_back(
        editor::EditorSelectionPanelTarget{
            MakeEditorObjectHandle(
                editor::EditorDomainId::CameraRig,
                "camera-rig",
                0,
                runtimeState.terrain.courseObjectEditRevision,
                "Camera Rig")});
    targets.push_back(
        editor::EditorSelectionPanelTarget{
            MakeEditorObjectHandle(
                editor::EditorDomainId::GameplayTuning,
                "gameplay-tuning",
                0,
                runtimeState.terrain.courseObjectEditRevision,
                "Gameplay Tuning")});

    return targets;
}

void LogEditorImguiBreakdown(const EditorImguiFrameTiming& timing) {
    if (timing.buildUiMs < 8.0 &&
        timing.assetPipelineMs < 2.0 &&
        timing.worldRefreshMs < 2.0 &&
        timing.validationMs < 2.0 &&
        timing.panelDrawMs < 4.0) {
        return;
    }

    static uint32_t frameCounter = 0;
    ++frameCounter;
    if ((frameCounter % 30u) != 1u) {
        return;
    }

    std::ofstream log = app::OpenRotatingLog("logs/editor_imgui_breakdown.log");
    if (!log.is_open()) {
        return;
    }
    log << "[EditorImguiBreakdown]"
        << " buildUiMs=" << timing.buildUiMs
        << " assetPipelineMs=" << timing.assetPipelineMs
        << " assetRegistryInitMs=" << timing.assetRegistryInitMs
        << " assetThumbnailSyncMs=" << timing.assetThumbnailSyncMs
        << " assetPreviewJobMs=" << timing.assetPreviewJobMs
        << " assetGpuSetupMs=" << timing.assetGpuSetupMs
        << " assetGpuProcessMs=" << timing.assetGpuProcessMs
        << " worldRefreshMs=" << timing.worldRefreshMs
        << " worldRefreshExecuted=" << (timing.worldRefreshExecuted ? 1 : 0)
        << " worldRefreshReasons=" << timing.worldRefreshReasons
        << " worldRefreshRevision=" << timing.worldRefreshRevision
        << " worldRefreshCacheHits=" << timing.worldRefreshCacheHits
        << " previewJobs=" << timing.previewJobs
        << " gpuThumbnails=" << timing.gpuThumbnails
        << " assetRecords=" << timing.assetRecords
        << " thumbnailEntries=" << timing.thumbnailEntries
        << " previewQueued=" << timing.previewQueued
        << " previewRunning=" << timing.previewRunning
        << " previewReady=" << timing.previewReady
        << " previewFailed=" << timing.previewFailed
        << " previewActiveAsync=" << timing.previewActiveAsync
        << " gpuQueued=" << timing.gpuQueued
        << " gpuRendering=" << timing.gpuRendering
        << " gpuReady=" << timing.gpuReady
        << " gpuFailed=" << timing.gpuFailed
        << " validationMs=" << timing.validationMs
        << " panelRegistryMs=" << timing.panelRegistryMs
        << " panelDrawMs=" << timing.panelDrawMs
        << " layoutPersistenceMs=" << timing.layoutPersistenceMs
        << " developerTools=" << (timing.developerTools ? 1 : 0)
        << " viewportFocus=" << (timing.viewportFocus ? 1 : 0)
        << '\n';
}

void LogEditorImguiRenderTiming(double renderMs) {
    if (renderMs < 8.0) {
        return;
    }
    static uint32_t frameCounter = 0;
    ++frameCounter;
    if ((frameCounter % 30u) != 1u) {
        return;
    }
    std::ofstream log = app::OpenRotatingLog("logs/editor_imgui_breakdown.log");
    if (!log.is_open()) {
        return;
    }
    log << "[EditorImguiRender] renderMs=" << renderMs << '\n';
}

void SyncEditorTransactionsFromFrame(
    editor::EditorTransactionStack& transactions,
    const AppRuntimeState& runtimeState) {
    const TerrainAuthoringState& terrain = runtimeState.terrain;
    transactions.SetLegacyMirror(
        "Course Object History",
        terrain.courseObjectUndoDepth,
        terrain.courseObjectRedoDepth,
        terrain.courseObjectEditRevision);
}

float ClampWorkspaceRatio(float value) {
    return (std::clamp)(value, 0.05f, 0.85f);
}

void DrawWorkspaceVerticalSplitter(
    const char* id,
    float x,
    float y,
    float height,
    float contentWidth,
    float direction,
    float& ratio) {
    if (height <= 0.0f || contentWidth <= 0.0f) {
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(x - 3.0f, y));
    ImGui::InvisibleButton(id, ImVec2(6.0f, height));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(x - 1.0f, y),
            ImVec2(x + 1.0f, y + height),
            IM_COL32(120, 155, 190, 180));
    }
    if (ImGui::IsItemActive()) {
        ratio = ClampWorkspaceRatio(ratio + direction * ImGui::GetIO().MouseDelta.x / contentWidth);
    }
}

void DrawWorkspaceHorizontalSplitter(
    const char* id,
    float x,
    float y,
    float width,
    float contentHeight,
    float direction,
    float& ratio) {
    if (width <= 0.0f || contentHeight <= 0.0f) {
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(x, y - 3.0f));
    ImGui::InvisibleButton(id, ImVec2(width, 6.0f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(x, y - 1.0f),
            ImVec2(x + width, y + 1.0f),
            IM_COL32(120, 155, 190, 180));
    }
    if (ImGui::IsItemActive()) {
        ratio = ClampWorkspaceRatio(ratio + direction * ImGui::GetIO().MouseDelta.y / contentHeight);
    }
}

bool DrawEditorWorkspaceSplitters(
    editor::EditorPanelLayoutConfig& config,
    const editor::EditorPanelLayoutService& layout) {
    if (!config.developerToolsVisible || !layout.ContentRect().Valid()) {
        return false;
    }

    const float beforeLeft = config.leftSidebarWidthRatio;
    const float beforeInspector = config.inspectorWidthRatio;
    const float beforeDiagnostics = config.diagnosticsHeightRatio;
    const float beforeContent = config.contentBrowserWidthRatio;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground;

    const editor::EditorPanelRect& content = layout.ContentRect();
    const editor::EditorPanelRect& left = layout.LeftSidebarRect();
    const editor::EditorPanelRect& inspector = layout.InspectorRect();
    const editor::EditorPanelRect& viewportRect = layout.ViewportRect();
    const editor::EditorPanelRect& contentBrowser = layout.ContentBrowserRect();

    ImGui::SetNextWindowPos(ImVec2(left.x + left.width - 3.0f, content.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(6.0f, content.height), ImGuiCond_Always);
    if (ImGui::Begin("Editor Left Splitter", nullptr, flags)) {
        DrawWorkspaceVerticalSplitter(
            "##leftSplitter",
            left.x + left.width,
            content.y,
            content.height,
            content.width,
            1.0f,
            config.leftSidebarWidthRatio);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(inspector.x - 3.0f, content.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(6.0f, content.height), ImGuiCond_Always);
    if (ImGui::Begin("Editor Right Splitter", nullptr, flags)) {
        DrawWorkspaceVerticalSplitter(
            "##rightSplitter",
            inspector.x,
            content.y,
            content.height,
            content.width,
            -1.0f,
            config.inspectorWidthRatio);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(
        ImVec2(viewportRect.x, viewportRect.y + viewportRect.height - 3.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewportRect.width, 6.0f), ImGuiCond_Always);
    if (ImGui::Begin("Editor Bottom Splitter", nullptr, flags)) {
        DrawWorkspaceHorizontalSplitter(
            "##bottomSplitter",
            viewportRect.x,
            viewportRect.y + viewportRect.height,
            viewportRect.width,
            content.height,
            -1.0f,
            config.diagnosticsHeightRatio);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(
        ImVec2(contentBrowser.x + contentBrowser.width - 3.0f, contentBrowser.y),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(6.0f, contentBrowser.height), ImGuiCond_Always);
    if (ImGui::Begin("Editor Content Splitter", nullptr, flags)) {
        DrawWorkspaceVerticalSplitter(
            "##contentDiagnosticsSplitter",
            contentBrowser.x + contentBrowser.width,
            contentBrowser.y,
            contentBrowser.height,
            layout.BottomDockRect().width,
            1.0f,
            config.contentBrowserWidthRatio);
    }
    ImGui::End();

    return beforeLeft != config.leftSidebarWidthRatio ||
        beforeInspector != config.inspectorWidthRatio ||
        beforeDiagnostics != config.diagnosticsHeightRatio ||
        beforeContent != config.contentBrowserWidthRatio;
}

void DrawEditorWorkspacePanel(
    editor::EditorLayoutPersistenceService& persistence,
    editor::CourseMapEditorWorkspace& courseMapWorkspace) {
    static constexpr std::array<const char*, 4> kWorkspacePresets{{
        "Authoring",
        "VFX Debug",
        "Runtime Profiling",
        "Minimal Playtest",
    }};

    const std::string& currentPreset = persistence.WorkspacePreset();
    const char* preview = currentPreset.empty() ? "Authoring" : currentPreset.c_str();
    if (ImGui::BeginCombo("Preset", preview)) {
        for (const char* preset : kWorkspacePresets) {
            const bool selected = currentPreset == preset;
            if (ImGui::Selectable(preset, selected)) {
                persistence.ApplyWorkspacePreset(preset);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    const bool saveLayoutDisabled = !persistence.Dirty();
    if (saveLayoutDisabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save Layout")) {
        persistence.Save();
    }
    if (saveLayoutDisabled) {
        ImGui::EndDisabled();
    }
    ImGui::SeparatorText("Major Editors");
    if (ImGui::Button(
            courseMapWorkspace.IsOpen()
                ? "Focus Course Map Editor"
                : "Open Course Map Editor")) {
        courseMapWorkspace.Open();
    }
    if (courseMapWorkspace.IsOpen()) {
        ImGui::SameLine();
        if (ImGui::Button("Close##CourseMapEditor")) {
            courseMapWorkspace.Close();
        }
    }
    ImGui::TextWrapped("Layout: %s", persistence.StatusMessage().c_str());
}


float ClampUiDimension(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

bool NeedsVfxRuntimeStatusTelemetry(
    const AppVfxRuntimeState& vfx,
    uint32_t hiddenFrameIndex) {
    constexpr uint32_t kHiddenTelemetryHealthInterval = 12;
    const bool healthSampleFrame = (hiddenFrameIndex % kHiddenTelemetryHealthInterval) == 0;
    return vfx.enableTrailMeshStreamStartupTelemetry ||
        vfx.enableParticleDedicatedResourceProbe ||
        vfx.enableParticleDedicatedProbeTelemetry ||
        vfx.enableDistortionDedicatedTelemetry ||
        vfx.enableBeamDedicatedTelemetry ||
        (healthSampleFrame &&
            (vfx.enableTrailMeshStreamAutoFallback ||
                vfx.enableParticleDedicatedAutoFallback ||
                (vfx.enableDistortionDedicatedResources && vfx.enableDistortionDedicatedAutoFallback) ||
                vfx.enableBeamDedicatedAutoFallback));
}

#if defined(_DEBUG) || defined(DEVELOP)
bool DrawDeveloperToolsSwitch(bool& showDeveloperTools) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f,
            viewport->WorkPos.y + 12.0f),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.80f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    bool changed = false;
    if (ImGui::Begin("Developer Tools Switch", nullptr, flags)) {
        changed = ImGui::Checkbox("Developer Tools", &showDeveloperTools);
    }
    ImGui::End();
    return changed;
}
#endif

void DrawViewportFocusStatusBar(bool& viewportFocusMode) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);

    ImGui::SetNextWindowPos(ImVec2(workPos.x + 8.0f, workPos.y + 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    constexpr ImGuiWindowFlags statusBarFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Viewport Focus Status", nullptr, statusBarFlags)) {
        ImGui::TextUnformatted("Viewport Focus");
        ImGui::SameLine();
        if (ImGui::Button("Show Tools")) {
            viewportFocusMode = false;
        }
    }
    ImGui::End();
}

void DrawSkinningTimingRow(const char* label, const RuntimeSkinningTimingPathStats& stats) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    if (!stats.valid) {
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted("-");
        return;
    }

    ImGui::Text("%llu", static_cast<unsigned long long>(stats.lastTicks));
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%.1f", stats.averageTicks);
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%llu / %llu",
        static_cast<unsigned long long>(stats.minTicks),
        static_cast<unsigned long long>(stats.maxTicks));
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("%u", stats.sampleCount);
}

void DrawSkinningTimingCompactRow(const char* label, const RuntimeSkinningTimingPathStats& stats) {
    if (!stats.valid) {
        ImGui::Text("%s: avg - ticks", label);
        return;
    }

    ImGui::Text("%s: avg %.1f ticks", label, stats.averageTicks);
    ImGui::Text("  last %llu  min/max %llu/%llu  n=%u",
        static_cast<unsigned long long>(stats.lastTicks),
        static_cast<unsigned long long>(stats.minTicks),
        static_cast<unsigned long long>(stats.maxTicks),
        stats.sampleCount);
}

const RuntimeSkinningTimingPathStats* GetDisplayedSkinningTimingStats(const AppRuntimeState& runtimeState,
    const char** label) {
    if (runtimeState.showSkinnedModel) {
        if (runtimeState.vfx.enableSkinnedSurfaceVfx) {
            *label = "CS + draw";
            return &runtimeState.skinningTiming.computeTotal;
        }
        *label = "VS draw";
        return &runtimeState.skinningTiming.vertexShaderTotal;
    }

    if (runtimeState.vfx.enableSkinnedSurfaceVfx) {
        *label = "CS surface";
        return &runtimeState.skinningTiming.computeSurfaceOnly;
    }

    *label = "none";
    return nullptr;
}

size_t ShowcaseIndex(AppVfxRuntimeState::ShowcaseEffect effect) {
    return static_cast<size_t>(effect);
}

const char* ShowcaseEffectDisplayName(AppVfxRuntimeState::ShowcaseEffect effect) {
    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        return "Lightning";
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        return "Ice Bullet";
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole:
        return "Black Hole";
    default:
        return "Showcase";
    }
}

const char* ShowcaseEffectCaption(AppVfxRuntimeState::ShowcaseEffect effect) {
    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        return "charge, strike, flash, afterglow";
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        return "launch, trail, impact, frozen burst";
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole:
        return "orbit, lensing, accretion, collapse";
    default:
        return "ready";
    }
}

AppVfxRuntimeState::ShowcaseEffect NextShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect effect) {
    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        return AppVfxRuntimeState::ShowcaseEffect::IceProjectile;
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        return AppVfxRuntimeState::ShowcaseEffect::BlackHole;
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole:
    default:
        return AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike;
    }
}

float ShowcaseDuration(AppVfxRuntimeState::ShowcaseEffect effect) {
    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        return 4.75f;
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        return 8.00f;
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole:
        return 5.20f;
    default:
        return 6.0f;
    }
}

void ApplyShowcaseSceneDefaults(AppRuntimeState& runtimeState) {
    runtimeState.clearColor[0] = 0.78f;
    runtimeState.clearColor[1] = 0.76f;
    runtimeState.clearColor[2] = 0.74f;
    runtimeState.clearColor[3] = 1.0f;

    runtimeState.useMonsterBall = false;
    runtimeState.showAnimatedCube = false;
    runtimeState.showSkinnedModel = false;
    runtimeState.showSkeletonDebug = false;
    runtimeState.showSkybox = false;
    runtimeState.showProceduralBackdrop = true;
    runtimeState.showVfxModelObjects = false;

    runtimeState.directionalLightData.color = {0.55f, 0.7f, 1.0f, 1.0f};
    runtimeState.directionalLightData.direction = {0.25f, -1.0f, 0.2f};
    runtimeState.directionalLightData.intensity = 0.12f;

    runtimeState.pointLightData.color = {0.35f, 0.65f, 1.0f, 1.0f};
    runtimeState.pointLightData.intensity = 0.0f;
    runtimeState.pointLightData.radius = 6.0f;
    runtimeState.pointLightData.decay = 2.0f;

    runtimeState.vfx.showcaseMode = true;
    runtimeState.vfx.autoPlayVfxDemo = false;
    runtimeState.vfx.enableTrailMeshStream = true;
    runtimeState.vfx.enableTrailMeshStreamAutoFallback = false;
    runtimeState.vfx.trailMeshStreamFallbackActive = false;
}

void ResetShowcaseIceProjectiles(AppVfxRuntimeState& vfxState) {
    vfxState.iceProjectilePreviewActive = false;
    vfxState.iceProjectileImpactSpawned = false;
    vfxState.iceProjectileInstanceId = 0;
    vfxState.iceProjectileTimer = 0.0f;
    for (AppVfxRuntimeState::IceProjectileShotState& shot : vfxState.iceProjectileShots) {
        shot = {};
    }
}

void ClearShowcaseEffectState(AppRuntimeState& runtimeState, EffectRuntime& effectRuntime) {
    runtimeState.vfx.enableParticles = false;
    runtimeState.vfx.enableTrails = false;
    runtimeState.vfx.enableBeams = false;
    runtimeState.vfx.enableDistortions = false;
    runtimeState.vfx.enableRings = false;
    runtimeState.vfx.enableCylinders = false;
    runtimeState.vfx.enableElectricOrbStrike = false;
    runtimeState.vfx.electricOrbStrikeActive = false;
    runtimeState.vfx.electricOrbStrikeLoop = false;
    runtimeState.vfx.electricOrbStrikeTimer = 0.0f;
    runtimeState.vfx.showcaseAutoTimer = 0.0f;
    ResetShowcaseIceProjectiles(runtimeState.vfx);
    effectRuntime.ClearInstances();
}

void ConfigureShowcasePostProcess(PostProcessStack& postProcessStack, AppVfxRuntimeState& vfxState) {
    const bool blackHole = vfxState.showcaseEffect == AppVfxRuntimeState::ShowcaseEffect::BlackHole;
    AppVfxRuntimeState::ShowcaseTuning& tuning =
        vfxState.showcaseTuning[ShowcaseIndex(vfxState.showcaseEffect)];

    postProcessStack.SetEnabled("AccretionComposite", blackHole);
    postProcessStack.SetIntensity("AccretionComposite", blackHole ? tuning.param4 : 1.0f);
    postProcessStack.SetIntensity("GlowComposite", blackHole ? (0.92f + tuning.param4 * 0.42f) : 1.0f);
    postProcessStack.SetIntensity("DistortionComposite", blackHole ? (0.85f + tuning.param3 * 0.58f) : 1.0f);

    for (PostProcessPass pass : postProcessStack.Passes()) {
        if (pass.name == "AccretionComposite") {
            pass.parameters.accretionRadius = 0.30f + tuning.param2 * 0.14f;
            pass.parameters.accretionDiskStretch = 1.65f + tuning.param2 * 0.92f;
            pass.parameters.accretionTurbulence = 0.48f + tuning.param1 * 0.52f;
            pass.parameters.accretionChromaticAberration = 0.42f + tuning.param3 * 0.62f;
            pass.parameters.accretionCoreSize = 0.10f + tuning.param1 * 0.075f;
            pass.parameters.accretionCoreDarkness = 0.78f + tuning.param1 * 0.15f;
            pass.parameters.accretionLensStrength = 0.44f + tuning.param3 * 0.82f;
            pass.parameters.accretionGuideOpacity = 0.42f + tuning.param4 * 0.62f;
            pass.parameters.accretionGuideWidth = 0.08f + tuning.param2 * 0.08f;
        } else if (pass.name == "DistortionComposite") {
            pass.parameters.distortionScale = blackHole ? (0.010f + tuning.param3 * 0.026f) : 0.020f;
        }

        (void)postProcessStack.ConfigurePass(pass);
    }
}

void PlayShowcasePresentationEffect(
    AppRuntimeState& runtimeState,
    EffectRuntime& effectRuntime,
    PostProcessStack& postProcessStack,
    AppVfxRuntimeState::ShowcaseEffect effect) {
    ApplyShowcaseSceneDefaults(runtimeState);
    runtimeState.vfx.showcaseEffect = effect;
    runtimeState.vfx.iceProjectileClickToFire =
        effect == AppVfxRuntimeState::ShowcaseEffect::IceProjectile;

    ClearShowcaseEffectState(runtimeState, effectRuntime);

    switch (effect) {
    case AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike:
        runtimeState.vfx.enableParticles = false;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.vfx.enableRings = false;
        runtimeState.vfx.enableCylinders = false;
        runtimeState.vfx.enableElectricOrbStrike = true;
        runtimeState.vfx.electricOrbStrikeActive = true;
        runtimeState.vfx.electricOrbStrikeDuration = 4.25f;
        break;
    case AppVfxRuntimeState::ShowcaseEffect::IceProjectile:
        runtimeState.vfx.enableParticles = true;
        runtimeState.vfx.enableTrails = true;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = false;
        runtimeState.vfx.enableRings = true;
        runtimeState.vfx.enableCylinders = true;
        runtimeState.vfx.enableElectricOrbStrike = false;
        runtimeState.vfx.iceProjectileStart = {-2.15f, -1.28f, -3.05f};
        runtimeState.vfx.iceProjectileTarget = {2.20f, 0.58f, 0.42f};
        runtimeState.vfx.iceProjectilePreviewActive = true;
        break;
    case AppVfxRuntimeState::ShowcaseEffect::BlackHole: {
        runtimeState.vfx.enableParticles = false;
        runtimeState.vfx.enableTrails = false;
        runtimeState.vfx.enableBeams = false;
        runtimeState.vfx.enableDistortions = true;
        runtimeState.vfx.enableRings = false;
        runtimeState.vfx.enableCylinders = false;
        runtimeState.vfx.enableElectricOrbStrike = false;
        break;
    }
    default:
        break;
    }

    ConfigureShowcasePostProcess(postProcessStack, runtimeState.vfx);
    runtimeState.vfx.showcaseAutoTimer = ShowcaseDuration(effect);
}

bool ShowcaseEffectButton(
    const char* label,
    bool active,
    const ImVec2& size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.72f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.86f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.34f, 0.62f, 1.0f));
    }
    const bool pressed = ImGui::Button(label, size);
    if (active) {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

void DrawSubmissionShowcasePanel(
    AppRuntimeState& runtimeState,
    bool& showDeveloperTools) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workSize = viewport ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
    const float panelWidth = ClampUiDimension(workSize.x * 0.34f, 430.0f, 560.0f);
    ImGui::SetNextWindowPos(
        ImVec2(workPos.x + 16.0f, workPos.y + 16.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoResize;

    if (ImGui::Begin("Skinning + MultiMaterial Showcase", nullptr, flags)) {
        ImGui::TextUnformatted("Skinning + MultiMaterial");
        ImGui::SameLine();
        ImGui::TextDisabled("development submission");
        ImGui::Separator();

        const RuntimeSubmissionShowcaseState& showcase =
            runtimeState.submissionShowcase;
        if (showcase.gamepadConnected) {
            ImGui::TextColored(
                ImVec4(0.30f, 0.95f, 0.50f, 1.0f),
                "Gamepad %u connected",
                showcase.controllerIndex + 1u);
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.20f, 1.0f),
                "Connect an XInput gamepad");
        }
        if (showcase.keyboardInputEnabled) {
            ImGui::TextColored(
                ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                showcase.keyboardActive
                    ? "Keyboard ready (active)"
                    : "Keyboard ready");
        } else {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.20f, 1.0f),
                "Focus window / finish text input for keyboard");
        }

        const auto animationName = [](uint32_t modelIndex) {
            return modelIndex == 2u ? "Human Sneak Walk" : "Human Walk";
        };
        const RuntimeSkinnedAnimationBlendState& animationBlend =
            runtimeState.skinnedAnimationBlend;
        if (animationBlend.active) {
            ImGui::TextColored(
                ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                "Animation Blend: %s -> %s",
                animationName(animationBlend.fromModelIndex),
                animationName(animationBlend.toModelIndex));
            ImGui::ProgressBar(
                (std::clamp)(animationBlend.alpha, 0.0f, 1.0f),
                ImVec2(-1.0f, 8.0f),
                "Cross Fade");
            ImGui::TextDisabled(
                "Translation / Scale: Lerp | Rotation: Slerp");
        } else {
            ImGui::Text(
                "Model: %s",
                animationName(runtimeState.selectedSkinnedModelIndex));
        }
        const HandParticleAttachmentTelemetry& handParticle =
            runtimeState.handParticleAttachmentTelemetry;
        const bool handParticleActive =
            handParticle.status == HandParticleAttachmentStatus::Active;
        ImGui::TextColored(
            handParticleActive
                ? ImVec4(0.30f, 0.90f, 1.0f, 1.0f)
                : ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            "Right Hand GPU Particle: %s",
            HandParticleAttachmentStatusName(handParticle.status));
        const HandParticleAttachmentTelemetry& leftHandParticle =
            runtimeState.leftHandParticleAttachmentTelemetry;
        const bool leftHandParticleActive =
            leftHandParticle.status == HandParticleAttachmentStatus::Active;
        ImGui::TextColored(
            leftHandParticleActive
                ? ImVec4(1.0f, 0.22f, 0.12f, 1.0f)
                : ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            "Left Hand GPU Particle: %s",
            HandParticleAttachmentStatusName(leftHandParticle.status));
        const WeaponAttachmentTelemetry& weapon =
            runtimeState.weaponAttachmentTelemetry;
        const bool weaponActive =
            weapon.status == WeaponAttachmentStatus::Active;
        ImGui::TextColored(
            weaponActive
                ? ImVec4(1.0f, 0.82f, 0.30f, 1.0f)
                : ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            "Right Hand Weapon: %s",
            WeaponAttachmentStatusName(weapon.status));
        if (weaponActive) {
            ImGui::TextDisabled(
                "Joint Scale: (%.3f, %.3f, %.3f) -> World (%.2f, %.2f, %.2f)%s",
                weapon.sourceJointScale.x,
                weapon.sourceJointScale.y,
                weapon.sourceJointScale.z,
                weapon.worldScale.x,
                weapon.worldScale.y,
                weapon.worldScale.z,
                weapon.jointScaleRemoved ? " [normalized]" : "");
        }
        const RuntimeWeaponDrawTelemetry& weaponDraw =
            runtimeState.weaponDrawTelemetry;
        const char* weaponDrawStatus = !weaponDraw.submitted
            ? "not submitted"
            : (!weaponDraw.screenBoundsVisible
                ? "offscreen"
                : (weaponDraw.screenBoundsReadable ? "visible" : "too small"));
        ImGui::TextColored(
            weaponDraw.submitted && weaponDraw.screenBoundsReadable
                ? ImVec4(0.35f, 1.0f, 0.45f, 1.0f)
                : ImVec4(1.0f, 0.45f, 0.20f, 1.0f),
            "Weapon Draw: %s | SubMeshes %u / Materials %u",
            weaponDrawStatus,
            weaponDraw.submittedSubMeshCount,
            weaponDraw.materialCount);
        if (weaponDraw.submitted) {
            ImGui::TextDisabled(
                "Weapon Screen Bounds: (%.0f, %.0f) - (%.0f, %.0f)",
                weaponDraw.screenMinimum.x,
                weaponDraw.screenMinimum.y,
                weaponDraw.screenMaximum.x,
                weaponDraw.screenMaximum.y);
        }
        ImGui::ProgressBar(
            (std::clamp)(showcase.moveMagnitude, 0.0f, 1.0f),
            ImVec2(-1.0f, 8.0f),
            "Movement");
        ImGui::TextDisabled("Left Stick  Move / Right Stick  Rotate");
        ImGui::TextDisabled("A  Skeleton / X  Blend Animation / B  Reset");
        ImGui::TextDisabled("WASD  Move / Q E or Arrows  Rotate");
        ImGui::TextDisabled("K  Skeleton / Space  Blend Animation / R  Reset");
        ImGui::Separator();
        ImGui::Checkbox("Skeleton Debug", &runtimeState.showSkeletonDebug);
        ImGui::SameLine();
        ImGui::Checkbox("Developer Tools", &showDeveloperTools);
    }
    ImGui::End();
}

void DrawShowcasePresentationPanel(
    AppRuntimeState& runtimeState,
    EffectRuntime& effectRuntime,
    PostProcessStack& postProcessStack,
    bool& showDeveloperTools,
    bool& loopCurrentEffect) {
    if (runtimeState.vfx.showcaseAutoRotate || loopCurrentEffect) {
        runtimeState.vfx.showcaseAutoTimer -= ImGui::GetIO().DeltaTime;
        if (runtimeState.vfx.showcaseAutoTimer <= 0.0f) {
            PlayShowcasePresentationEffect(
                runtimeState,
                effectRuntime,
                postProcessStack,
                loopCurrentEffect
                    ? runtimeState.vfx.showcaseEffect
                    : NextShowcaseEffect(runtimeState.vfx.showcaseEffect));
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workSize = viewport ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
    const float panelWidth = ClampUiDimension(workSize.x * 0.34f, 430.0f, 560.0f);

    ImGui::SetNextWindowPos(ImVec2(workPos.x + 16.0f, workPos.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.86f);
    constexpr ImGuiWindowFlags showcaseFlags =
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoResize;

    if (ImGui::Begin("VFX Showcase", nullptr, showcaseFlags)) {
        ImGui::TextUnformatted("VFX Showcase");
        ImGui::SameLine();
        ImGui::TextDisabled("development submission");
        ImGui::Separator();

        const AppVfxRuntimeState::ShowcaseEffect currentEffect = runtimeState.vfx.showcaseEffect;
        ImGui::Text("%s", ShowcaseEffectDisplayName(currentEffect));
        ImGui::TextDisabled("%s", ShowcaseEffectCaption(currentEffect));

        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
        const ImVec2 effectButtonSize(buttonWidth, 38.0f);
        if (ShowcaseEffectButton(
                "Lightning",
                currentEffect == AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike,
                effectButtonSize)) {
            loopCurrentEffect = false;
            PlayShowcasePresentationEffect(
                runtimeState,
                effectRuntime,
                postProcessStack,
                AppVfxRuntimeState::ShowcaseEffect::ElectricOrbStrike);
        }
        ImGui::SameLine();
        if (ShowcaseEffectButton(
                "Ice Bullet",
                currentEffect == AppVfxRuntimeState::ShowcaseEffect::IceProjectile,
                effectButtonSize)) {
            loopCurrentEffect = false;
            PlayShowcasePresentationEffect(
                runtimeState,
                effectRuntime,
                postProcessStack,
                AppVfxRuntimeState::ShowcaseEffect::IceProjectile);
        }
        ImGui::SameLine();
        if (ShowcaseEffectButton(
                "Black Hole",
                currentEffect == AppVfxRuntimeState::ShowcaseEffect::BlackHole,
                effectButtonSize)) {
            loopCurrentEffect = false;
            PlayShowcasePresentationEffect(
                runtimeState,
                effectRuntime,
                postProcessStack,
                AppVfxRuntimeState::ShowcaseEffect::BlackHole);
        }

        if (ImGui::Button("Replay", ImVec2(92.0f, 30.0f))) {
            PlayShowcasePresentationEffect(runtimeState, effectRuntime, postProcessStack, currentEffect);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next", ImVec2(92.0f, 30.0f))) {
            loopCurrentEffect = false;
            PlayShowcasePresentationEffect(
                runtimeState,
                effectRuntime,
                postProcessStack,
                NextShowcaseEffect(currentEffect));
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto Sequence", &runtimeState.vfx.showcaseAutoRotate) &&
            runtimeState.vfx.showcaseAutoRotate) {
            loopCurrentEffect = false;
        }

        if (ImGui::Checkbox("Loop Current", &loopCurrentEffect) && loopCurrentEffect) {
            runtimeState.vfx.showcaseAutoRotate = false;
            runtimeState.vfx.showcaseAutoTimer = ShowcaseDuration(currentEffect);
        }

        const float duration = ShowcaseDuration(currentEffect);
        const float remaining = ClampUiDimension(runtimeState.vfx.showcaseAutoTimer, 0.0f, duration);
        const float progress = duration > 0.0f ? 1.0f - (remaining / duration) : 1.0f;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 8.0f), "");

        ImGui::Separator();
        ImGui::Checkbox("Click viewport to fire ice", &runtimeState.vfx.iceProjectileClickToFire);
        ImGui::SameLine();
        ImGui::Checkbox("Developer Tools", &showDeveloperTools);
    }
    ImGui::End();
}

void DrawSkinningTimingPanel(const AppRuntimeState& runtimeState) {
    ImGui::SeparatorText("Skinning GPU Timing");
    const char* activeLabel = "none";
    const RuntimeSkinningTimingPathStats* activeStats = GetDisplayedSkinningTimingStats(runtimeState, &activeLabel);
    if (activeStats && activeStats->valid) {
        ImGui::Text("Active avg ticks: %.1f (%s, n=%u)",
            activeStats->averageTicks,
            activeLabel,
            activeStats->sampleCount);
    } else {
        ImGui::Text("Active avg ticks: - (%s)", activeLabel);
    }
    ImGui::Text("CS Surface VFX: %s", runtimeState.vfx.enableSkinnedSurfaceVfx ? "on" : "off");
    ImGui::Text("Displayed Path: %s",
        runtimeState.showSkinnedModel
            ? (runtimeState.vfx.enableSkinnedSurfaceVfx ? "CS skinning + draw" : "VS skinning")
            : (runtimeState.vfx.enableSkinnedSurfaceVfx ? "CS surface only" : "none"));

    if (ImGui::GetContentRegionAvail().x < 430.0f) {
        DrawSkinningTimingCompactRow("VS draw", runtimeState.skinningTiming.vertexShaderTotal);
        DrawSkinningTimingCompactRow("CS + draw", runtimeState.skinningTiming.computeTotal);
        DrawSkinningTimingCompactRow("CS surface", runtimeState.skinningTiming.computeSurfaceOnly);
        return;
    }

    if (ImGui::BeginTable("SkinningTimingTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Path");
        ImGui::TableSetupColumn("Last");
        ImGui::TableSetupColumn("Average");
        ImGui::TableSetupColumn("Min / Max");
        ImGui::TableSetupColumn("Samples");
        ImGui::TableHeadersRow();
        DrawSkinningTimingRow("VS draw", runtimeState.skinningTiming.vertexShaderTotal);
        DrawSkinningTimingRow("CS + draw", runtimeState.skinningTiming.computeTotal);
        DrawSkinningTimingRow("CS surface", runtimeState.skinningTiming.computeSurfaceOnly);
        ImGui::EndTable();
    }
}
} // namespace

bool AppImGuiLayer::Initialize(HWND hwnd,
    ID3D12Device* device,
    int bufferCount,
    DXGI_FORMAT rtvFormat,
    ID3D12DescriptorHeap* srvHeap,
    AppPipelines* appPipelines) {
    if (initialized_) {
        return true;
    }

    if (!hwnd || !device || !srvHeap || !appPipelines || bufferCount <= 0) {
        return false;
    }
    hwnd_ = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    std::string fontSettingsError;
    editorFonts_.Load(&fontSettingsError);
    if (!editorFonts_.BuildAtlas(*ImGui::GetIO().Fonts, &fontSettingsError)) {
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    ImGui::GetIO().FontDefault = editorFonts_.RegularFont();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(editorFonts_.AppliedSettings().uiScale);

    if (!ImGui_ImplWin32_Init(hwnd)) {
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX12_Init(device,
        bufferCount,
        rtvFormat,
        srvHeap,
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        srvHeap->GetGPUDescriptorHandleForHeapStart())) {
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    if (!fontSettingsError.empty()) {
        editorNotifications_.Push(editor::EditorNotificationSeverity::Warning,
            "Editor Fonts", fontSettingsError);
    }

    const uint32_t descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    constexpr uint32_t kEditorThumbnailSrvBaseIndex = 3200;
    constexpr uint32_t kEditorThumbnailSrvCapacity = 512;
    if (editorAssetThumbnailGpuBackend_.Initialize(
            device,
            srvHeap,
            descriptorSize,
            kEditorThumbnailSrvBaseIndex,
            kEditorThumbnailSrvCapacity)) {
        editor::EditorAssetThumbnailCachePolicy thumbnailCachePolicy{};
        thumbnailCachePolicy.persistentCacheEnabled = true;
        thumbnailCachePolicy.persistentRoot = "logs/editor_thumbnail_cache";
        thumbnailCachePolicy.maxMemoryBytes = 32ull * 1024ull * 1024ull;
        thumbnailCachePolicy.maxRetainedUploadResources = 256;
        editorAssetThumbnailGpuBackend_.ConfigureCache(std::move(thumbnailCachePolicy));
        editorAssetThumbnails_.SetGpuThumbnailBackend(&editorAssetThumbnailGpuBackend_);
    }

    std::string documentProviderError;
    if (editorDocumentRegistry_.Count() == 0) {
        const bool providersRegistered =
            editorDocumentRegistry_.Register(editorCourseDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorSceneDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorPrefabDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorMaterialGraphDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorVfxGraphDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorAnimationStateMachineDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorGameplayVisualScriptDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorBehaviorTreeDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorEqsDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorNavigationDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorEffectDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorRenderPresetDocumentProvider_, &documentProviderError) &&
            editorDocumentRegistry_.Register(editorProjectSettingsDocumentProvider_, &documentProviderError);
        if (!providersRegistered) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Document",
                documentProviderError.empty()
                    ? "Failed to register built-in document providers."
                    : documentProviderError);
        }
    }
    if (!editorSceneDocumentId_.IsValid() &&
        editorDocumentRegistry_.Find(editor::EditorDocumentTypes::Scene) != nullptr) {
        const editor::EditorDocumentOpenResult sceneOpen = editorDocumentManager_.Open(
            editor::EditorDocumentTypes::Scene, editorSceneDocumentPath_);
        if (sceneOpen.succeeded) {
            editorSceneDocumentId_ = sceneOpen.id;
        } else {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Scene Document",
                sceneOpen.message.empty() ? "Failed to open the default Scene document." : sceneOpen.message);
        }
    }
    if (editorWorldObjectRegistry_.Count() == 0) {
        std::string worldProviderError;
        const bool worldProvidersRegistered =
            editorWorldObjectRegistry_.Register(editorSceneWorldProvider_, &worldProviderError) &&
            editorWorldObjectRegistry_.Register(editorCourseWorldProvider_, &worldProviderError) &&
            editorWorldObjectRegistry_.Register(editorVfxWorldProvider_, &worldProviderError);
        if (!worldProvidersRegistered) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Editor World",
                worldProviderError.empty()
                    ? "Failed to register built-in world providers."
                    : worldProviderError);
        }
    }

    if (editorModeRegistry_.ModeCount() == 0) {
        editor::RegisterDefaultEditorModes(editorModeRegistry_);
        editor::RegisterProductionPlacementTools(
            editorModeRegistry_,
            editor::EditorPlacementToolServices{
                &editorWorldMutationService_,
                &editorWorldModel_,
                &editorSceneWorldProvider_,
                &editorSelection_,
                &editorAssetRegistry_,
                &editorAssetSelection_,
                [this](const editor::EditorWorldMutationResult& result) {
                    editorWorldRefreshRevisionGate_.Invalidate();
                    editorDirtyState_.MarkDirty(
                        editor::EditorDirtyDomain::Unknown,
                        "world:" + result.document.assetGuid,
                        result.document.type + " World",
                        result.message,
                        editorDirtyState_.Revision() + 1);
                    if (editor::EditorDocumentRecord* document =
                            editorDocumentManager_.Find(result.document)) {
                        editorDocumentManager_.MarkDirty(document->id, result.message);
                    }
                    editorNotifications_.Push(
                        editor::EditorNotificationSeverity::Info,
                        "Placement Tool",
                        result.message);
                },
                &editorProductionMeshRuntimeCache_,
                [this](std::string_view message) {
                    editorNotifications_.Push(
                        editor::EditorNotificationSeverity::Info,
                        "Placement Asset Preparation",
                        std::string(message));
                }});
        editor::RegisterSplineRouteTools(
            editorModeRegistry_,
            editor::EditorSplineRouteToolServices{
                &editorWorldMutationService_,
                &editorWorldModel_,
                &editorSceneWorldProvider_,
                &editorSelection_,
                [this](const editor::EditorWorldMutationResult& result) {
                    editorWorldRefreshRevisionGate_.Invalidate();
                    editorDirtyState_.MarkDirty(
                        editor::EditorDirtyDomain::Unknown,
                        "world:" + result.document.assetGuid,
                        result.document.type + " World",
                        result.message,
                        editorDirtyState_.Revision() + 1);
                    if (editor::EditorDocumentRecord* document =
                            editorDocumentManager_.Find(
                                result.document)) {
                        editorDocumentManager_.MarkDirty(
                            document->id, result.message);
                    }
                    editorNotifications_.Push(
                        editor::EditorNotificationSeverity::Info,
                        "Spline Tool",
                        result.message);
                }});
        editor::RegisterProductionTerrainBrushTools(
            editorModeRegistry_, &editorTerrainToolBinding_);
        editorGeometryToolBinding_.workspace = &editorGeometryWorkspace_;
        editorGeometryToolBinding_.onCommitted = [this](std::string_view operation) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Info,
                "Modeling Tool",
                std::string(operation) + " committed.");
        };
        editor::RegisterProductionGeometryTools(
            editorModeRegistry_, &editorGeometryToolBinding_);
        editorCreateEditableCopyToolBinding_.workspace =
            &editorGeometryWorkspace_;
        editorCreateEditableCopyToolBinding_.assetRegistry =
            &editorAssetRegistry_;
        editorCreateEditableCopyToolBinding_.sourceLoader =
            &editorProductionMeshEditableSourceLoader_;
        editorCreateEditableCopyToolBinding_.onCommitRequested =
            [this](
                const editor::EditorCreateEditableCopyCommitRequest&
                    request) {
                editorNotifications_.Push(
                    editor::EditorNotificationSeverity::Info,
                    "Modeling Tool",
                    "Editable copy created from Production Mesh " +
                        request.sourceIdentity.assetGuid + ".");
            };
        editor::RegisterProductionCreateEditableCopyTools(
            editorModeRegistry_,
            &editorCreateEditableCopyToolBinding_);
        editorMeshBakeToolBinding_.workspace = &editorGeometryWorkspace_;
        editorMeshBakeToolBinding_.pipeline = &editorMeshBakePipeline_;
        editorMeshBakeToolBinding_.onCommitted = [this](
            std::string_view operation,
            std::string_view assetGuid) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Info,
                "Mesh Asset Pipeline",
                std::string(operation) +
                    (assetGuid.empty() ? std::string(" committed.")
                                       : std::string(" committed: ") + std::string(assetGuid)));
        };
        editor::RegisterProductionMeshBakeTools(
            editorModeRegistry_, &editorMeshBakeToolBinding_);
        editor::EditorError executionError{};
        if (!editorInteractiveExecution_.Register(
                editorWorldMutationExecution_, &executionError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Interactive Tools",
                executionError.message.empty()
                    ? "Failed to register World commit execution service."
                    : executionError.message);
        }
        if (!editorInteractiveExecution_.Register(
                editorTerrainEditExecution_, &executionError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Interactive Tools",
                executionError.message.empty()
                    ? "Failed to register Terrain commit execution service."
                    : executionError.message);
        }
        if (!editorInteractiveExecution_.Register(
                editorGeometryExecution_, &executionError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Interactive Tools",
                executionError.message.empty()
                    ? "Failed to register Geometry commit execution service."
                    : executionError.message);
        }
        if (!editorInteractiveExecution_.Register(
                editorMeshBakeExecution_, &executionError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Interactive Tools",
                executionError.message.empty()
                    ? "Failed to register Production Mesh Bake execution service."
                    : executionError.message);
        }
        std::string modeError;
        if (!editorInteractiveTools_.Initialize("editor.mode.select", &modeError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Interactive Tools",
                modeError.empty() ? "Failed to initialize Editor Mode framework." : modeError);
        }
    }

    std::string productionSceneError;
    if (!editorProductionScenePipeline_.Initialize(device, &productionSceneError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Scene Pipeline",
            productionSceneError.empty()
                ? "Failed to initialize the E-6 D3D12 Scene Instance pipeline."
                : productionSceneError);
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionMaterialError;
    if (!editorProductionMaterialPipeline_.Initialize(device, &productionMaterialError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Material Pipeline",
            productionMaterialError.empty()
                ? "Failed to initialize the E-7 Material and Scene Lighting pipeline."
                : productionMaterialError);
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionTextureError;
    constexpr uint32_t kProductionTextureSrvBaseIndex = 3712;
    constexpr uint32_t kProductionTextureSrvCapacity = 320;
    if (!editorProductionTexturePipeline_.Initialize(
            device, srvHeap, descriptorSize,
            kProductionTextureSrvBaseIndex, kProductionTextureSrvCapacity,
            {}, &productionTextureError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Texture Pipeline",
            productionTextureError.empty()
                ? "Failed to initialize the E-8 Texture Streaming and Descriptor Residency pipeline."
                : productionTextureError);
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionShaderError;
    if (!editorProductionShaderPipeline_.Initialize(
            device,
            appPipelines->GetMainRootSignature(),
            appPipelines->GetMainPSO(),
            {}, &productionShaderError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Shader Pipeline",
            productionShaderError.empty()
                ? "Failed to initialize the E-9 Shader Variant and PSO Cache pipeline."
                : productionShaderError);
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionLightingError;
    constexpr uint32_t kProductionShadowSrvIndex = 4032;
    if (!editorProductionLightingPipeline_.Initialize(
            device,
            srvHeap,
            descriptorSize,
            kProductionShadowSrvIndex,
            appPipelines->GetMainRootSignature(),
            {},
            &productionLightingError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Lighting Pipeline",
            productionLightingError.empty()
                ? "Failed to initialize the E-10 Multi-Light Cluster and Shadow pipeline."
                : productionLightingError);
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionVisibilityError;
    constexpr uint32_t kProductionVisibilityFallbackSrvIndex = 4033;
    if (!editorProductionGpuDrivenPipeline_.Initialize(
            device, srvHeap, descriptorSize, kProductionVisibilityFallbackSrvIndex,
            appPipelines->GetMainRootSignature(), {},
            &productionVisibilityError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production GPU Visibility Pipeline",
            productionVisibilityError.empty()
                ? "Failed to initialize the E-11 GPU visibility and indirect draw pipeline."
                : productionVisibilityError);
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string worldPartitionError;
    if (!editorWorldPartitionPipeline_.Initialize(device, {}, &worldPartitionError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "World Partition Pipeline",
            worldPartitionError.empty()
                ? "Failed to initialize the E-12 World Partition and HLOD pipeline."
                : worldPartitionError);
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string navigationError;
    if (!editorProductionNavigationPipeline_.Initialize({}, &navigationError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Navigation Pipeline",
            navigationError.empty()
                ? "Failed to initialize the E-13 Navigation and AI query pipeline."
                : navigationError);
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionAiError;
    if (!editorProductionAiPipeline_.Initialize({}, &productionAiError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production AI Pipeline",
            productionAiError.empty()
                ? "Failed to initialize the E-14 Behavior Tree, Blackboard, and Perception pipeline."
                : productionAiError);
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionAiWorldError;
    if (!editorProductionAiWorldPipeline_.Initialize({}, &productionAiWorldError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production AI World Pipeline",
            productionAiWorldError.empty()
                ? "Failed to initialize the E-15 EQS, Crowd, and Smart Object pipeline."
                : productionAiWorldError);
        editorProductionAiPipeline_.Shutdown();
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionAiAuthoringError;
    if (!editorProductionAiAuthoringPipeline_.Initialize({}, &productionAiAuthoringError) ||
        !editorViewportOverlay_.RegisterProvider(editorProductionAiAuthoringPipeline_)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production AI Authoring Pipeline",
            productionAiAuthoringError.empty()
                ? "Failed to initialize the E-16 AI Authoring, Debugger, and Simulation pipeline."
                : productionAiAuthoringError);
        editorProductionAiAuthoringPipeline_.Shutdown();
        editorProductionAiWorldPipeline_.Shutdown();
        editorProductionAiPipeline_.Shutdown();
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    if (editorPlaySessionSnapshot_.Registry().Find(
            editor::EditorProductionAiAuthoringPipeline::kPlayIsolationId) == nullptr &&
        !editorPlaySessionSnapshot_.RegisterProvider(
            &editorProductionAiAuthoringPipeline_, &productionAiAuthoringError)) {
        editorNotifications_.Push(editor::EditorNotificationSeverity::Error,
            "Production AI Play Isolation", productionAiAuthoringError);
        editorViewportOverlay_.UnregisterProvider(editorProductionAiAuthoringPipeline_.Id());
        editorProductionAiAuthoringPipeline_.Shutdown();
        editorProductionAiWorldPipeline_.Shutdown();
        editorProductionAiPipeline_.Shutdown();
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string productionAiValidationError;
    if (!editorProductionAiValidationPipeline_.Initialize({}, &productionAiValidationError)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production AI Validation Pipeline",
            productionAiValidationError.empty()
                ? "Failed to initialize the E-17 AI Validation, Batch Simulation, and Telemetry pipeline."
                : productionAiValidationError);
        editorViewportOverlay_.UnregisterProvider(editorProductionAiAuthoringPipeline_.Id());
        editorProductionAiAuthoringPipeline_.Shutdown();
        editorProductionAiWorldPipeline_.Shutdown();
        editorProductionAiPipeline_.Shutdown();
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    std::string navigationAuthoringError;
    if (!editorProductionNavigationAuthoringPipeline_.Initialize({}, &navigationAuthoringError) ||
        !editorViewportOverlay_.RegisterProvider(editorProductionNavigationAuthoringPipeline_)) {
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Error,
            "Production Navigation Authoring Pipeline",
            navigationAuthoringError.empty()
                ? "Failed to initialize the E-18 Navigation Authoring, Off-Mesh Link, and Area Cost pipeline."
                : navigationAuthoringError);
        editorProductionNavigationAuthoringPipeline_.Shutdown();
        editorProductionAiValidationPipeline_.Shutdown();
        editorViewportOverlay_.UnregisterProvider(editorProductionAiAuthoringPipeline_.Id());
        editorProductionAiAuthoringPipeline_.Shutdown();
        editorProductionAiWorldPipeline_.Shutdown();
        editorProductionAiPipeline_.Shutdown();
        editorProductionNavigationPipeline_.Shutdown();
        editorWorldPartitionPipeline_.Shutdown();
        editorProductionGpuDrivenPipeline_.Shutdown();
        editorProductionLightingPipeline_.Shutdown();
        editorProductionShaderPipeline_.Shutdown();
        editorProductionTexturePipeline_.Shutdown();
        editorProductionMaterialPipeline_.Shutdown();
        editorProductionScenePipeline_.Shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        editorFonts_.OnContextDestroyed();
        ImGui::DestroyContext();
        return false;
    }
    initialized_ = true;
    return true;
}

bool AppImGuiLayer::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!initialized_ || !visible_) {
        return false;
    }
    return ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0;
}

void AppImGuiLayer::QueueExternalAssetDrop(std::filesystem::path path) {
    if (path.empty()) {
        return;
    }
    pendingExternalAssetImportPaths_.push_back(std::move(path));
}

void AppImGuiLayer::BeginFrame() {
    if (!initialized_ || !visible_) {
        return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void AppImGuiLayer::RefreshEditorViewportRenderTargetLayout() {
    if (!initialized_ || !visible_) {
        return;
    }

    editorLayoutPersistence_.EnsureLoaded();
    if (!courseMapEditorWorkspace_.IsRestored()) {
        courseMapEditorWorkspace_.Restore(
            editorLayoutPersistence_.MajorWorkspaceLayout(
                editor::CourseMapEditorWorkspace::kWorkspaceId));
    }
    editorLayout_.Configure(
        editor::EditorLayoutConfig{
            showDeveloperTools_,
            true,
            true,
            true,
            editor::EditorToolbarHeight(),
            editor::EditorDocumentTabsHeight(),
            editor::EditorStatusBarHeight()});
    const ImGuiViewport* editorViewport = ImGui::GetMainViewport();
    const ImVec2 editorWorkPos =
        editorViewport ? editorViewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 editorWorkSize =
        editorViewport ? editorViewport->WorkSize : ImGui::GetIO().DisplaySize;
    editor::EditorPanelLayoutConfig editorPanelLayoutConfig{
        showDeveloperTools_,
        editorWorkPos.x,
        editorWorkPos.y,
        editorWorkSize.x,
        editorWorkSize.y,
        editorLayout_.TopReservedHeight(),
        editorLayout_.BottomReservedHeight()};
    editorLayoutPersistence_.Apply(editorPanelLayoutConfig);
    editorPanelLayout_.Configure(editorPanelLayoutConfig);
    editorLayoutPersistence_.CaptureLayout(editorPanelLayoutConfig);
    editorViewportRenderTarget_.Update(
        editor::EditorViewportRenderTargetInput{
            showDeveloperTools_,
            editorPanelLayout_.ViewportRect(),
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.x)),
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.y))});
}

void AppImGuiLayer::BuildUi(const AppImGuiFrameContext& context) {
    const auto buildUiStart = EditorUiTimingClock::now();
    EditorImguiFrameTiming imguiTiming{};
    imguiTiming.developerTools = showDeveloperTools_;
    imguiTiming.viewportFocus = viewportFocusMode_;

    if (!initialized_ || !visible_ ||
        context.runtimeState == nullptr ||
        context.effectRuntime == nullptr ||
        context.postProcessStack == nullptr ||
        context.renderGraphDescription == nullptr ||
        context.renderGraphError == nullptr ||
        context.renderPassDebugInfo == nullptr) {
        return;
    }

    AppRuntimeState& runtimeState = *context.runtimeState;
    EffectRuntime& effectRuntime = *context.effectRuntime;
    PostProcessStack& postProcessStack = *context.postProcessStack;
#if defined(_DEBUG) || defined(DEVELOP)
    if (DrawDeveloperToolsSwitch(showDeveloperTools_)) {
        viewportFocusMode_ = false;
    }
#endif
    // A gameplay screen needs only the switch until its editor is requested.
    // Avoid running showcase controls or changing gameplay VFX in this mode.
    if (context.railGameplayScene && !showDeveloperTools_) {
        imguiTiming.buildUiMs = EditorUiElapsedMs(buildUiStart, EditorUiTimingClock::now());
        LogEditorImguiBreakdown(imguiTiming);
        return;
    }
    editorPlaySession_.TickFrame();
    editor::EditorTransactionStack& editorTransactions =
        context.editorTransactions != nullptr ? *context.editorTransactions : editorTransactions_;
    editor::RunAppEditorStartupToolPipeline(
        editor::AppEditorStartupToolModuleInput{
            &editorToolRegistry_,
            &editorPropertyRegistry_,
            &editorDetailsSectionProviders_});
    const auto assetPipelineStart = EditorUiTimingClock::now();
    if (!editorAssetRegistryInitialized_) {
        const auto registryInitStart = EditorUiTimingClock::now();
        editorAssetRegistry_.ConfigureRedirectStore("Resources/.assetredirects");
        std::string redirectError;
        if (!editorAssetRegistry_.LoadRedirects(&redirectError)) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Warning,
                "Asset Identity",
                redirectError.empty() ? "Failed to load Asset redirect table." : redirectError);
        }
        editorAssetRegistry_.Clear();
        editor::IndexEditorAssetsFromFolder(editorAssetRegistry_, "Resources");
        editor::CourseMeshAssetAdapter courseMeshAssetAdapter;
        courseMeshAssetAdapter.RegisterAssets(editorAssetRegistry_);
        editorAssetRegistry_.ScanDependencies();
        editorAssetRegistryInitialized_ = true;
        imguiTiming.assetRegistryInitMs =
            EditorUiElapsedMs(registryInitStart, EditorUiTimingClock::now());
    }
    const auto thumbnailSyncStart = EditorUiTimingClock::now();
    editorAssetThumbnails_.Sync(editorAssetRegistry_);
    imguiTiming.assetThumbnailSyncMs =
        EditorUiElapsedMs(thumbnailSyncStart, EditorUiTimingClock::now());
    const auto previewJobStart = EditorUiTimingClock::now();
    imguiTiming.previewJobs =
        editorAssetThumbnails_.ProcessPreviewJobs(std::chrono::milliseconds(2), 2);
    imguiTiming.assetPreviewJobMs =
        EditorUiElapsedMs(previewJobStart, EditorUiTimingClock::now());
    const auto gpuSetupStart = EditorUiTimingClock::now();
    editorAssetThumbnailGpuBackend_.SetUploadCommandList(context.editorUploadCommandList);
    editorAssetThumbnailGpuBackend_.SetFrameFenceValues(
        context.editorCompletedFenceValue,
        context.editorScheduledFenceValue);
    imguiTiming.assetGpuSetupMs =
        EditorUiElapsedMs(gpuSetupStart, EditorUiTimingClock::now());
    const auto gpuProcessStart = EditorUiTimingClock::now();
    imguiTiming.gpuThumbnails = editorAssetThumbnails_.ProcessGpuThumbnails(4);
    imguiTiming.assetGpuProcessMs =
        EditorUiElapsedMs(gpuProcessStart, EditorUiTimingClock::now());
    imguiTiming.assetRecords = static_cast<uint32_t>(editorAssetRegistry_.Records().size());
    imguiTiming.thumbnailEntries = static_cast<uint32_t>(editorAssetThumbnails_.Count());
    imguiTiming.previewQueued = static_cast<uint32_t>(
        editorAssetThumbnails_.PreviewJobs().Count(editor::EditorAssetPreviewJobStatus::Queued));
    imguiTiming.previewRunning = static_cast<uint32_t>(
        editorAssetThumbnails_.PreviewJobs().Count(editor::EditorAssetPreviewJobStatus::Running));
    imguiTiming.previewReady = static_cast<uint32_t>(
        editorAssetThumbnails_.PreviewJobs().Count(editor::EditorAssetPreviewJobStatus::Ready));
    imguiTiming.previewFailed = static_cast<uint32_t>(
        editorAssetThumbnails_.PreviewJobs().Count(editor::EditorAssetPreviewJobStatus::Failed));
    imguiTiming.previewActiveAsync = static_cast<uint32_t>(
        editorAssetThumbnails_.PreviewJobs().ActiveAsyncJobCount());
    imguiTiming.gpuQueued = static_cast<uint32_t>(
        editorAssetThumbnails_.GpuThumbnails().Count(editor::EditorAssetGpuThumbnailStatus::Queued));
    imguiTiming.gpuRendering = static_cast<uint32_t>(
        editorAssetThumbnails_.GpuThumbnails().Count(editor::EditorAssetGpuThumbnailStatus::Rendering));
    imguiTiming.gpuReady = static_cast<uint32_t>(
        editorAssetThumbnails_.GpuThumbnails().Count(editor::EditorAssetGpuThumbnailStatus::Ready));
    imguiTiming.gpuFailed = static_cast<uint32_t>(
        editorAssetThumbnails_.GpuThumbnails().Count(editor::EditorAssetGpuThumbnailStatus::Failed));
    imguiTiming.assetPipelineMs =
        EditorUiElapsedMs(assetPipelineStart, EditorUiTimingClock::now());
    editorCourseDocumentProvider_.Bind(context.course);
    bool courseDocumentPathChanged = false;
    if (context.course == nullptr) {
        editorCourseDocumentOpen_ = false;
        editorCourseDocumentPath_.clear();
    } else {
        const std::string currentCoursePath =
            context.coursePath != nullptr ? *context.coursePath : std::string();
        if (!currentCoursePath.empty() && currentCoursePath != editorCourseDocumentPath_) {
            editorCourseDocumentPath_ = currentCoursePath;
            editorCourseDocumentOpen_ = true;
            courseDocumentPathChanged = true;
        } else if (editorCourseDocumentPath_.empty()) {
            editorCourseDocumentPath_ = currentCoursePath;
            courseDocumentPathChanged = !currentCoursePath.empty();
        }
    }
    if (courseDocumentPathChanged && !editorCourseDocumentPath_.empty()) {
        const editor::EditorDocumentOpenResult openResult = editorDocumentManager_.Open(
            editor::EditorDocumentTypes::Course,
            editorCourseDocumentPath_);
        if (!openResult.succeeded) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Document",
                openResult.message);
        }
    }
    CourseAsset* editableCourse =
        editorCourseDocumentOpen_ && context.course != nullptr ? context.course : nullptr;
    editor::EditorDocumentId courseWorldDocument{};
    if (const editor::EditorDocumentRecord* document =
            editorDocumentManager_.FindByPath(editorCourseDocumentPath_)) {
        courseWorldDocument = document->id;
    } else if (context.course != nullptr) {
        const std::filesystem::path identityPath = editorCourseDocumentPath_.empty()
            ? std::filesystem::path(context.course->name + ".course")
            : std::filesystem::path(editorCourseDocumentPath_);
        courseWorldDocument = {
            editor::MakeEditorDocumentGuid(editor::EditorDocumentTypes::Course, identityPath),
            std::string(editor::EditorDocumentTypes::Course)};
    }
    editorCourseWorldProvider_.Bind(editableCourse, courseWorldDocument);
    editorTerrainToolBinding_.course = editableCourse;
    editorTerrainToolBinding_.runtimeTerrain = &runtimeState.terrain;
    editorTerrainToolBinding_.document = courseWorldDocument;
    editorTerrainToolBinding_.transactionTarget = editor::EditorObjectHandle{
        editor::EditorDomainId::TerrainGeneration,
        editor::BuildEditorWorldStableId(
            courseWorldDocument, editorCourseWorldProvider_.ProviderId(), "root"),
        0,
        0,
        "Procedural Terrain"};
    editorTerrainToolBinding_.surfaceQuery = &editorTerrainSurfaceQuery_;
    editorTerrainToolBinding_.onCommitted = [this](
        const editor::EditorTerrainCommitSummary& summary) {
        std::ostringstream message;
        message << ToString(summary.operation) << " stroke committed: "
                << summary.sampleCount << " sample"
                << (summary.sampleCount == 1 ? "" : "s");
        if (summary.operation == TerrainEditOperation::Paint) {
            message << ", "
                    << editor::GetTerrainPaintLayerVisual(summary.materialLayer).label;
        }
        message << ". Undo available.";
        editorNotifications_.Push(
            editor::EditorNotificationSeverity::Info,
            "Terrain Brush",
            message.str());
    };
    if (editableCourse != nullptr && courseWorldDocument.IsValid()) {
        AppRuntimeState* terrainRuntime = &runtimeState;
        editorTerrainEditExecution_.Bind(
            courseWorldDocument.Key(),
            &editableCourse->terrainEditLayer,
            [this, terrainRuntime, courseWorldDocument](
                const TerrainEditDirtyRegion& dirty) {
                if (terrainRuntime != nullptr) {
                    terrainRuntime->terrain.lastEditDirtyRegion = dirty;
                    ++terrainRuntime->terrain.courseObjectEditRevision;
                }
                editorWorldRefreshRevisionGate_.Invalidate();
                editorDirtyState_.MarkDirty(
                    editor::EditorDirtyDomain::CourseAuthoring,
                    "course.terrain_edit_layer",
                    "Terrain Edit Layer",
                    "Procedural Terrain stroke changed.",
                    editorDirtyState_.Revision() + 1);
                if (editor::EditorDocumentRecord* document =
                        editorDocumentManager_.Find(courseWorldDocument)) {
                    editorDocumentManager_.MarkDirty(
                        document->id, "Procedural Terrain stroke changed.");
                }
            });
    } else {
        editorTerrainEditExecution_.Clear();
    }
    const std::size_t assignedWorldIdentities =
        editorCourseWorldProvider_.EnsurePersistentIdentities();
    if (assignedWorldIdentities > 0) {
        editorDirtyState_.MarkDirty(
            editor::EditorDirtyDomain::CourseAuthoring,
            "course.world_identity",
            "Course World Identity",
            "Persistent editor GUIDs were assigned to Course objects.",
            runtimeState.terrain.courseObjectEditRevision);
        if (editor::EditorDocumentRecord* document =
                editorDocumentManager_.Find(courseWorldDocument)) {
            editorDocumentManager_.MarkDirty(
                document->id, "Persistent editor GUIDs were assigned to Course objects.");
        }
    }
    editorCourseSequencerProvider_.Bind(editableCourse);
    editorCourseWaveSequencerBridge_.Bind(context.courseWaveEditorController);
    editorCourseRideSequencerBridge_.Bind(editableCourse);
    editorRailRideSpeedBeatAuthoring_.Bind(editableCourse);
    editorCourseRailRideEventAuthoring_.Bind(editableCourse);
    editorCourseEncounterBeatAuthoring_.Bind(editableCourse);
    editorSequencer_.BeginFrame();
    editorSequencer_.RegisterProvider(editorCourseSequencerProvider_);
    editorSequencer_.RegisterProvider(editorCourseWaveSequencerBridge_);
    editorSequencer_.RegisterProvider(editorCourseRideSequencerBridge_);
    editorSequencer_.RegisterProvider(editorRailRideSpeedBeatAuthoring_);
    editorSequencer_.RegisterProvider(editorCourseRailRideEventAuthoring_);
    editorSequencer_.RegisterProvider(editorCourseEncounterBeatAuthoring_);
    editorSequencer_.SetTransactionStack(&editorTransactions);
    editorSequencer_.SetSequenceRange(0.0, context.courseRailLength);
    editorSequencer_.SetPreviewPositionCallback(
        [teleport = context.onTeleportCourseToDistance](double position) {
            if (teleport) teleport(static_cast<float>(position));
        });
    editorSequencer_.SetMutationCallback(
        [this, &runtimeState, courseWorldDocument](std::string_view reason) {
            ++runtimeState.terrain.courseObjectEditRevision;
            editorWorldRefreshRevisionGate_.Invalidate();
            editorDirtyState_.MarkDirty(
                editor::EditorDirtyDomain::CourseAuthoring,
                "course.sequencer",
                "Course Sequencer",
                std::string(reason),
                runtimeState.terrain.courseObjectEditRevision);
            if (editor::EditorDocumentRecord* document =
                    editorDocumentManager_.Find(courseWorldDocument)) {
                editorDocumentManager_.MarkDirty(document->id, reason);
            }
        });
    editor::EditorDocumentId activeSceneDocument = editorSceneDocumentId_;
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr && activeDocument->open &&
        activeDocument->id.type == editor::EditorDocumentTypes::Scene) {
        activeSceneDocument = activeDocument->id;
    }
    editorSceneWorldProvider_.Bind(
        editorSceneDocumentProvider_.Scene(activeSceneDocument),
        activeSceneDocument,
        &editorSceneComponentRegistry_,
        &editorGimmickDefinitionRegistry_);
    editor::EditorDocumentId activeGeometryDocument{};
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr && activeDocument->id.type == editor::EditorDocumentTypes::Scene) {
        activeGeometryDocument = activeDocument->id;
    }
    editorGeometryWorkspace_.Bind(
        &editorSceneWorldProvider_, &editorSelection_, activeGeometryDocument);
    if (editor::EditorScene* geometryScene =
            editorSceneDocumentProvider_.Scene(editorSceneDocumentId_)) {
        editorMeshBakePipeline_.Bind(
            editorSceneDocumentId_, geometryScene, &editorAssetRegistry_, std::filesystem::current_path());
        editorGeometryExecution_.Bind(
            editorSceneDocumentId_, geometryScene,
            [this](std::string_view) {
                editorWorldRefreshRevisionGate_.Invalidate();
                editorDirtyState_.MarkDirty(
                    editor::EditorDirtyDomain::Unknown,
                    "scene.geometry",
                    "Editable Geometry",
                    "Scene Geometry changed.",
                    editorDirtyState_.Revision() + 1);
                if (editor::EditorDocumentRecord* document =
                        editorDocumentManager_.Find(editorSceneDocumentId_)) {
                    editorDocumentManager_.MarkDirty(
                        document->id, "Scene Geometry changed.");
                }
            });
        editorMeshBakeExecution_.Bind(
            editorSceneDocumentId_, geometryScene, &editorAssetRegistry_,
            &editorProductionMeshRuntimeCache_, std::filesystem::current_path(),
            [this](std::string_view, std::string_view assetGuid) {
                editorWorldRefreshRevisionGate_.Invalidate();
                editorAssetThumbnails_.Sync(editorAssetRegistry_);
                editorDirtyState_.MarkDirty(
                    editor::EditorDirtyDomain::Unknown,
                    "scene.mesh_bake",
                    "Production Mesh Asset",
                    "Scene Mesh Asset bake changed.",
                    editorDirtyState_.Revision() + 1);
                if (editor::EditorDocumentRecord* document =
                        editorDocumentManager_.Find(editorSceneDocumentId_)) {
                    editorDocumentManager_.MarkDirty(
                        document->id, "Scene Mesh Asset bake changed.");
                }
                editorNotifications_.Push(
                    editor::EditorNotificationSeverity::Info,
                    "Mesh Asset Pipeline",
                    assetGuid.empty()
                        ? "Production Mesh Bake reverted."
                        : "Production Mesh artifacts and runtime cache updated.");
            });
        const editor::EditorMeshAssetChangeSet meshAssetChanges =
            editorMeshAssetChangeTracker_.Poll(editorAssetRegistry_);
        const editor::EditorProductionMeshRuntimeReconcileResult meshReconcile =
            editorProductionMeshRuntimeCache_.ReconcileAssets(
                editorAssetRegistry_, meshAssetChanges);
        if (meshReconcile.failed != 0 && !meshReconcile.diagnostics.empty()) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Warning,
                "Mesh Asset Hot Reload",
                meshReconcile.diagnostics.front());
        }
        if (context.frameState != nullptr) {
            const editor::EditorScene& productionMeshScene =
                editorGimmickRuntimeAdapter_ != nullptr &&
                    editorGimmickRuntimeAdapter_->Active()
                ? editorGimmickRuntimeAdapter_->Scene()
                : editorMeshRendererRuntimeWorld_.Active()
                ? editorMeshRendererRuntimeWorld_.Scene()
                : *geometryScene;
            std::string worldPartitionError;
            editorWorldPartitionPipeline_.Sync(
                *geometryScene,
                editorAssetRegistry_,
                editorProductionMeshRuntimeCache_,
                context.frameState->cameraWorldPosition,
                context.frameState->viewProjectionMatrix,
                context.editorUploadCommandList,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &worldPartitionError);
            std::string transientMeshError;
            editorTransientMeshRenderPath_.Sync(
                *geometryScene,
                &editorGeometryWorkspace_,
                context.frameState->viewProjectionMatrix,
                context.editorUploadCommandList,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &transientMeshError,
                &editorWorldPartitionPipeline_.SourceResidentEntities());
            std::string productionSceneError;
            const std::unordered_set<std::string>* productionResidentEntities =
                editorPlaySession_.IsActive()
                    ? &editorWorldPartitionPipeline_.SourceResidentEntities()
                    : nullptr;
            editorProductionScenePipeline_.Sync(
                productionMeshScene,
                editorAssetRegistry_,
                editorProductionMeshRuntimeCache_,
                context.frameState->cameraWorldPosition,
                context.frameState->viewProjectionMatrix,
                context.editorUploadCommandList,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &productionSceneError,
                productionResidentEntities,
                &editorTransientMeshRenderPath_.OverriddenEntities());
            std::string productionNavigationError;
            editorProductionNavigationPipeline_.Sync(
                *geometryScene,
                editorProductionScenePipeline_,
                editorWorldPartitionPipeline_,
                &productionNavigationError);
            if (editorProductionAiAuthoringPipeline_.ConsumeRuntimeAdvance()) {
                std::string productionAiError;
                editorProductionAiPipeline_.Sync(
                    *geometryScene,
                    editorAssetRegistry_,
                    editorProductionScenePipeline_,
                    editorWorldPartitionPipeline_,
                    editorProductionNavigationPipeline_,
                    context.frameState->deltaTime,
                    &productionAiError);
                std::string productionAiWorldError;
                editorProductionAiWorldPipeline_.Sync(
                    *geometryScene,
                    editorWorldPartitionPipeline_,
                    editorProductionAiPipeline_,
                    context.frameState->deltaTime,
                    &productionAiWorldError);
                editorProductionAiAuthoringPipeline_.CaptureRuntimeFrame(
                    editorProductionAiPipeline_, editorProductionAiWorldPipeline_,
                    context.frameState->deltaTime);
            }
            std::string productionMaterialError;
            editorProductionMaterialPipeline_.Sync(
                *geometryScene,
                editorAssetRegistry_,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &productionMaterialError,
                &editorWorldPartitionPipeline_.SourceResidentEntities());
            std::string productionLightingError;
            editorProductionLightingPipeline_.Sync(
                *geometryScene,
                context.frameState->cameraWorldPosition,
                context.frameState->viewMatrix,
                context.frameState->projMatrix,
                context.frameState->viewProjectionMatrix,
                static_cast<uint32_t>((std::max)(1.0f, runtimeState.viewport.Width)),
                static_cast<uint32_t>((std::max)(1.0f, runtimeState.viewport.Height)),
                runtimeState.camera.nearZ,
                runtimeState.camera.farZ,
                &productionLightingError,
                &editorWorldPartitionPipeline_.SourceResidentEntities());
            std::string productionTextureError;
            editorProductionTexturePipeline_.Sync(
                editorProductionMaterialPipeline_,
                editorAssetRegistry_,
                context.editorUploadCommandList,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &productionTextureError);
            std::string productionShaderError;
            editorProductionShaderPipeline_.Sync(
                editorProductionMaterialPipeline_,
                editorProductionTexturePipeline_,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &productionShaderError);
            std::string productionVisibilityError;
            std::vector<editor::EditorProductionSceneRenderPacket> streamingCandidates =
                editorProductionScenePipeline_.GpuDrivenCandidates();
            const auto& hlodPackets = editorWorldPartitionPipeline_.HlodPackets();
            streamingCandidates.insert(
                streamingCandidates.end(), hlodPackets.begin(), hlodPackets.end());
            if (editorProductionScenePipeline_.DrawMode() ==
                editor::EditorProductionMeshDrawMode::ForceDirect) {
                streamingCandidates.clear();
            }
            editorProductionGpuDrivenPipeline_.Sync(
                streamingCandidates,
                editorProductionMaterialPipeline_,
                editorProductionTexturePipeline_,
                editorProductionShaderPipeline_,
                context.frameState->viewProjectionMatrix,
                context.editorCompletedFenceValue,
                context.editorScheduledFenceValue,
                &productionVisibilityError);
            const editor::EditorProductionGpuDrivenStats&
                gpuDrivenStats =
                    editorProductionGpuDrivenPipeline_.Stats();
            editorProductionScenePipeline_.UpdatePresentationRouting(
                editor::EditorProductionMeshGpuRoutingState{
                    editorProductionGpuDrivenPipeline_.Ready(),
                    gpuDrivenStats.dispatchSucceeded,
                    gpuDrivenStats.readbackAvailable,
                    gpuDrivenStats.commandLayoutValidated,
                    editorProductionGpuDrivenPipeline_
                        .AutoValidationReady(),
                    gpuDrivenStats.hiZFresh,
                    gpuDrivenStats.frustumOnlyRetry,
                    gpuDrivenStats.occlusionQuarantined,
                    gpuDrivenStats.gpuVisibleInstances,
                    gpuDrivenStats.frustumRejectedInstances,
                    gpuDrivenStats.hiZRejectedInstances,
                    gpuDrivenStats.invalidBatchInstances,
                    gpuDrivenStats.preparedBatches,
                    gpuDrivenStats.executedBatches,
                    editorProductionGpuDrivenPipeline_
                        .AutoFallbackReason()});
        }
    } else {
        editorGeometryExecution_.Clear();
        editorMeshBakePipeline_.Clear();
        editorMeshBakeExecution_.Clear();
    }
    editorPrefabs_.Bind(
        editorSceneDocumentProvider_.Scene(editorSceneDocumentId_),
        editorSceneDocumentId_,
        &editorPrefabDocumentProvider_,
        &editorTransactions,
        &editorSceneWorldProvider_,
        &editorAssetRegistry_,
        &editorDocumentManager_);
    editorPrefabs_.SetMutationCallback(
        [this](std::string_view reason, std::string_view changedAssetGuid) {
            editorWorldRefreshRevisionGate_.Invalidate();
            editorDirtyState_.MarkDirty(
                editor::EditorDirtyDomain::Unknown,
                "prefab",
                "Prefab",
                std::string(reason),
                editorDirtyState_.Revision() + 1);
            if (editor::EditorDocumentRecord* sceneDocument =
                    editorDocumentManager_.Find(editorSceneDocumentId_)) {
                editorDocumentManager_.MarkDirty(sceneDocument->id, reason);
            }
            if (!changedAssetGuid.empty()) {
                const editor::EditorDocumentId prefabDocument =
                    editorPrefabDocumentProvider_.DocumentForAssetGuid(changedAssetGuid);
                if (editor::EditorDocumentRecord* document =
                        editorDocumentManager_.Find(prefabDocument)) {
                    editorDocumentManager_.MarkDirty(document->id, reason);
                }
            }
        });
    editorMaterialGraphs_.Bind(
        &editorMaterialGraphDocumentProvider_,
        &editorTransactions,
        &editorDocumentManager_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr && activeDocument->id.type == editor::EditorDocumentTypes::MaterialGraph) {
        if (activeDocument->id != editorMaterialGraphs_.ActiveDocument()) {
            editorMaterialGraphs_.SetActiveDocument(activeDocument->id);
        }
    } else if (editorMaterialGraphs_.ActiveDocument().IsValid()) {
        editorMaterialGraphs_.SetActiveDocument({});
    }
    editorMaterialGraphs_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(
                editor::EditorDirtyDomain::Unknown,
                "material-graph:" + document.assetGuid,
                "Material Graph",
                std::string(reason),
                editorDirtyState_.Revision() + 1);
        });
    editorVfxGraphs_.Bind(
        &editorVfxGraphDocumentProvider_,
        &editorTransactions,
        &editorDocumentManager_,
        context.effectRuntime,
        &editorAssetRegistry_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr && activeDocument->id.type == editor::EditorDocumentTypes::VfxGraph) {
        if (activeDocument->id != editorVfxGraphs_.ActiveDocument()) {
            editorVfxGraphs_.SetActiveDocument(activeDocument->id);
        }
    } else if (editorVfxGraphs_.ActiveDocument().IsValid()) {
        editorVfxGraphs_.SetActiveDocument({});
    }
    editorVfxGraphs_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(
                editor::EditorDirtyDomain::Unknown,
                "vfx-graph:" + document.assetGuid,
                "Advanced VFX Graph",
                std::string(reason),
                editorDirtyState_.Revision() + 1);
        });
    editorAnimationStateMachines_.Bind(
        &editorAnimationStateMachineDocumentProvider_, &editorTransactions, &editorDocumentManager_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr &&
        activeDocument->id.type == editor::EditorDocumentTypes::AnimationStateMachine) {
        if (activeDocument->id != editorAnimationStateMachines_.ActiveDocument()) {
            editorAnimationStateMachines_.SetActiveDocument(activeDocument->id);
        }
    } else if (editorAnimationStateMachines_.ActiveDocument().IsValid()) {
        editorAnimationStateMachines_.SetActiveDocument({});
    }
    editorAnimationStateMachines_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(editor::EditorDirtyDomain::Unknown,
                "animation-state-machine:" + document.assetGuid,
                "Animation State Machine", std::string(reason),
                editorDirtyState_.Revision() + 1);
        });
    editorGameplayVisualScripts_.Bind(
        &editorGameplayVisualScriptDocumentProvider_, &editorTransactions, &editorDocumentManager_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr &&
        activeDocument->id.type == editor::EditorDocumentTypes::GameplayVisualScript) {
        if (activeDocument->id != editorGameplayVisualScripts_.ActiveDocument()) {
            editorGameplayVisualScripts_.SetActiveDocument(activeDocument->id);
        }
    } else if (editorGameplayVisualScripts_.ActiveDocument().IsValid()) {
        editorGameplayVisualScripts_.SetActiveDocument({});
    }
    editorGameplayVisualScripts_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(editor::EditorDirtyDomain::Unknown,
                "gameplay-visual-script:" + document.assetGuid,
                "Gameplay Visual Script", std::string(reason),
                editorDirtyState_.Revision() + 1);
        });
    editorProductionAiAuthoringPipeline_.Bind(
        &editorBehaviorTreeDocumentProvider_, &editorEqsDocumentProvider_,
        &editorTransactions, &editorDocumentManager_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr &&
        (activeDocument->id.type == editor::EditorDocumentTypes::BehaviorTree ||
         activeDocument->id.type == editor::EditorDocumentTypes::EnvironmentQuery)) {
        if (activeDocument->id != editorProductionAiAuthoringPipeline_.ActiveDocument())
            editorProductionAiAuthoringPipeline_.SetActiveDocument(activeDocument->id);
    } else if (editorProductionAiAuthoringPipeline_.ActiveDocument().IsValid()) {
        editorProductionAiAuthoringPipeline_.SetActiveDocument({});
    }
    editorProductionAiAuthoringPipeline_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(editor::EditorDirtyDomain::Unknown,
                "ai-authoring:" + document.assetGuid,
                document.type == editor::EditorDocumentTypes::BehaviorTree
                    ? "Behavior Tree" : "Environment Query",
                std::string(reason), editorDirtyState_.Revision() + 1);
        });
    editorProductionNavigationAuthoringPipeline_.Bind(
        &editorNavigationDocumentProvider_, &editorTransactions, &editorDocumentManager_,
        &editorProductionNavigationPipeline_);
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active();
        activeDocument != nullptr &&
        activeDocument->id.type == editor::EditorDocumentTypes::NavigationData) {
        if (activeDocument->id != editorProductionNavigationAuthoringPipeline_.ActiveDocument())
            editorProductionNavigationAuthoringPipeline_.SetActiveDocument(activeDocument->id);
    } else if (editorProductionNavigationAuthoringPipeline_.ActiveDocument().IsValid()) {
        editorProductionNavigationAuthoringPipeline_.SetActiveDocument({});
    }
    editorProductionNavigationAuthoringPipeline_.SetMutationCallback(
        [this](const editor::EditorDocumentId& document, std::string_view reason) {
            editorDirtyState_.MarkDirty(editor::EditorDirtyDomain::Unknown,
                "navigation-authoring:" + document.assetGuid,
                "Navigation Data", std::string(reason), editorDirtyState_.Revision() + 1);
        });
    editorVfxWorldProvider_.Bind(
        context.effectRuntime,
        {editor::MakeEditorDocumentGuid(
             editor::EditorDocumentTypes::Effect, std::filesystem::path("runtime-vfx.effect")),
         std::string(editor::EditorDocumentTypes::Effect)});
    editor::EditorWorldRefreshRevisionKey worldRevisionKey{};
    worldRevisionKey.providerRegistryRevision =
        editorWorldObjectRegistry_.Revision();

    // Provider source identity includes binding changes and the read-only VFX
    // hierarchy. This is intentionally much cheaper than enumerating and
    // rebuilding every Editor World record.
    uint64_t providerSourceRevision = editor::EditorDocumentHash64(
        courseWorldDocument.Key(), 1469598103934665603ull);
    const auto mixProviderSource = [&providerSourceRevision](uint64_t value) {
        providerSourceRevision ^= value;
        providerSourceRevision *= 1099511628211ull;
    };
    mixProviderSource(editor::EditorDocumentHash64(
        activeSceneDocument.Key(), 1099511628211ull));
    mixProviderSource(static_cast<uint64_t>(
        reinterpret_cast<std::uintptr_t>(editableCourse)));
    const editor::EditorScene* worldScene =
        editorSceneDocumentProvider_.Scene(activeSceneDocument);
    mixProviderSource(static_cast<uint64_t>(
        reinterpret_cast<std::uintptr_t>(worldScene)));
    mixProviderSource(context.effectRuntime->Assets().size());
    mixProviderSource(context.effectRuntime->Instances().size());
    mixProviderSource(context.effectRuntime->ParticlePoolResetSerial());
    uint64_t vfxAssetIdentity = 0;
    for (const auto& [name, asset] : context.effectRuntime->Assets()) {
        (void)asset;
        vfxAssetIdentity ^= editor::EditorDocumentHash64(
            name, 1099511628211ull);
    }
    uint64_t vfxInstanceIdentity = 0;
    for (const EffectInstance& instance : context.effectRuntime->Instances()) {
        uint64_t identity = editor::EditorDocumentHash64(
            instance.assetName, 1469598103934665603ull);
        identity ^= static_cast<uint64_t>(instance.id);
        identity *= 1099511628211ull;
        vfxInstanceIdentity ^= identity;
    }
    mixProviderSource(vfxAssetIdentity);
    mixProviderSource(vfxInstanceIdentity);
    worldRevisionKey.providerSourceRevision = providerSourceRevision;
    worldRevisionKey.sceneRevision = worldScene != nullptr
        ? worldScene->revision : 0;

    uint64_t courseRevision = runtimeState.terrain.courseObjectEditRevision;
    const auto mixCourseRevision = [&courseRevision](uint64_t value) {
        courseRevision ^= value;
        courseRevision *= 1099511628211ull;
    };
    if (const editor::EditorDocumentRecord* document =
            editorDocumentManager_.Find(courseWorldDocument)) {
        mixCourseRevision(document->editRevision);
    }
    if (context.course != nullptr) {
        mixCourseRevision(context.course->terrainPlacements.size());
        mixCourseRevision(context.course->rockClusters.size());
        mixCourseRevision(context.course->cameraKeys.size());
        mixCourseRevision(context.course->events.size());
    }
    worldRevisionKey.courseRevision = courseRevision;

    const editor::EditorWorldRefreshDecision worldRefreshDecision =
        editorWorldRefreshRevisionGate_.Evaluate(worldRevisionKey);
    if (worldRefreshDecision.shouldRefresh) {
        const auto worldRefreshStart = EditorUiTimingClock::now();
        const editor::EditorWorldModelRefreshResult worldRefresh = editorWorldModel_.Refresh();
        imguiTiming.worldRefreshMs = EditorUiElapsedMs(
            worldRefreshStart, EditorUiTimingClock::now());
        imguiTiming.worldRefreshExecuted = true;
        imguiTiming.worldRefreshReasons =
            static_cast<uint32_t>(worldRefreshDecision.reasons);
        if (worldRefresh.succeeded) {
            editorWorldRefreshRevisionGate_.Commit(
                worldRevisionKey, worldRefreshDecision);
        } else {
            editorWorldRefreshRevisionGate_.RecordFailure();
            if (editorWorldRefreshRevisionGate_.State().failures == 1u) {
                editorNotifications_.Push(
                    editor::EditorNotificationSeverity::Error,
                    "Editor World",
                    worldRefresh.message);
            }
        }
    }
    imguiTiming.worldRefreshRevision =
        editorWorldRefreshRevisionGate_.State().revision;
    imguiTiming.worldRefreshCacheHits =
        editorWorldRefreshRevisionGate_.State().cacheHits;
    SyncEditorTransactionsFromFrame(editorTransactions, runtimeState);
    editorRailRuntimePause_.Sync(
        editor::EditorRailRuntimePauseInput{
            context.course != nullptr,
            runtimeState.terrain.freezeCourseRuntime ||
                (editorPlaySession_.IsActive() &&
                    editorPlaySession_.RuntimePaused() &&
                    !editorPlaySession_.RuntimeStepRequested()),
            context.courseDistance,
            context.courseSpeed});
    const uint32_t courseObjectRevision = runtimeState.terrain.courseObjectEditRevision;
    if (!editorCourseObjectDirtyRevisionInitialized_) {
        editorCourseObjectDirtyRevisionInitialized_ = true;
        editorCourseObjectDirtyRevision_ = courseObjectRevision;
    } else if (editableCourse != nullptr && courseObjectRevision > editorCourseObjectDirtyRevision_) {
        editorDirtyState_.MarkDirty(
            editor::EditorDirtyDomain::CourseAuthoring,
            "course.authoring",
            "Course Authoring",
            "Course object edit revision changed.",
            courseObjectRevision);
        editorCourseObjectDirtyRevision_ = courseObjectRevision;
        if (editor::EditorDocumentRecord* document =
                editorDocumentManager_.FindByPath(editorCourseDocumentPath_)) {
            editorDocumentManager_.MarkDirty(document->id, "Course object edit revision changed.");
        }
    } else if (courseObjectRevision < editorCourseObjectDirtyRevision_) {
        editorCourseObjectDirtyRevision_ = courseObjectRevision;
    }
    ++editorDocumentServiceFrame_;
    if (!editorDocumentRecoveryScanned_) {
        editorDocumentRecoveryScanned_ = true;
        editor::EditorDocumentRecoveryScanResult recoveryScan =
            editorDocumentRecoveryService_.Scan();
        editorDocumentRecoveryCandidates_ = std::move(recoveryScan.candidates);
    }
    for (auto candidate = editorDocumentRecoveryCandidates_.begin();
         candidate != editorDocumentRecoveryCandidates_.end();) {
        if (editorDocumentManager_.Find(candidate->autosave.id) == nullptr) {
            ++candidate;
            continue;
        }
        if (candidate->sourceChangedSinceAutosave) {
            editorDocumentManager_.SetConflict(
                candidate->autosave.id,
                editor::EditorDocumentConflictState::ExternalModified);
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Warning,
                "Document Recovery",
                candidate->message);
        } else {
            std::string recoveryError;
            if (editorDocumentRecoveryService_.Recover(*candidate, &recoveryError)) {
                editorNotifications_.Push(
                    editor::EditorNotificationSeverity::Info,
                    "Document Recovery",
                    "Recovered autosaved authoring changes for " + candidate->autosave.id.Key());
            } else {
                editorNotifications_.Push(
                    editor::EditorNotificationSeverity::Error,
                    "Document Recovery",
                    recoveryError);
            }
        }
        candidate = editorDocumentRecoveryCandidates_.erase(candidate);
    }
    if (editorDocumentServiceFrame_ % 120u == 0u) {
        editorExternalChangeMonitor_.Poll(editorDocumentManager_);
    }
    if (editorDocumentServiceFrame_ % 600u == 0u) {
        const editor::EditorAutosaveResult autosaveResult =
            editorAutosaveService_.AutosaveDirtyDocuments();
        for (const std::string& autosaveError : autosaveResult.errors) {
            editorNotifications_.Push(
                editor::EditorNotificationSeverity::Error,
                "Document Autosave",
                autosaveError);
        }
    }
    const auto validationStart = EditorUiTimingClock::now();
    editorToolRegistry_.BeginFrame();
    editor::CourseDocumentAdapter courseDocumentAdapter(
        context.course,
        context.coursePath,
        context.courseLoadStatus,
        &editorDirtyState_,
        editableCourse != nullptr);
    editor::CourseObjectPropertyAdapter coursePropertyAdapter(editableCourse, &runtimeState);
    editor::CourseObjectPropertyAdapter coursePreviewPropertyAdapter(editableCourse, &runtimeState, false);
    editor::EditorProductionPropertyAdapter productionPropertyAdapter(
        &effectRuntime,
        &postProcessStack,
        &runtimeState,
        editableCourse);
    editor::EditorProductionPropertyAdapter productionPreviewPropertyAdapter(
        &effectRuntime,
        &postProcessStack,
        &runtimeState,
        editableCourse,
        false);
    editor::EditorCompositePropertyAccessor editorPropertyAccessor;
    editor::EditorCompositePropertyAccessor editorPreviewPropertyAccessor;
    const std::vector<editor::EditorSelectionPanelTarget> productionSelectionTargets =
        BuildProductionSelectionTargets(
            effectRuntime,
            postProcessStack,
            runtimeState,
            editableCourse);
    editor::CourseObjectValidationAdapter courseValidationAdapter(editableCourse, &editorAssetRegistry_);
    editor::EffectAssetDiagnosticsAdapter effectDiagnosticsAdapter(context.loadedEffectAssets);
    editor::EditorAssetReferenceDiagnosticsAdapter assetReferenceDiagnosticsAdapter(&editorAssetRegistry_);
    editor::EditorAssetThumbnailDiagnosticsAdapter assetThumbnailDiagnosticsAdapter(
        &editorAssetRegistry_,
        &editorAssetThumbnails_);
    editor::EditorMaterialGraphDiagnosticsAdapter materialGraphDiagnosticsAdapter(
        &editorMaterialGraphDocumentProvider_,
        &editorDocumentManager_,
        &editorAssetRegistry_);
    editor::EditorVfxGraphDiagnosticsAdapter vfxGraphDiagnosticsAdapter(
        &editorVfxGraphDocumentProvider_,
        &editorDocumentManager_,
        &editorAssetRegistry_);
    editor::EditorAnimationStateMachineDiagnosticsAdapter animationStateMachineDiagnosticsAdapter(
        &editorAnimationStateMachineDocumentProvider_, &editorDocumentManager_, &editorAssetRegistry_);
    editor::EditorGameplayVisualScriptDiagnosticsAdapter gameplayVisualScriptDiagnosticsAdapter(
        &editorGameplayVisualScriptDocumentProvider_, &editorDocumentManager_);
    editor::EditorValidationService editorValidationService;
    editor::RunAppEditorFrameProviderToolPipeline(
        editor::AppEditorFrameProviderToolModuleInput{
            &editorToolRegistry_,
            &editorPropertyAccessor,
            &editorPreviewPropertyAccessor,
            &editorValidationService,
            &coursePropertyAdapter,
            &productionPropertyAdapter,
            &coursePreviewPropertyAdapter,
            &productionPreviewPropertyAdapter,
            &courseValidationAdapter,
            &effectDiagnosticsAdapter,
            &assetReferenceDiagnosticsAdapter,
            &assetThumbnailDiagnosticsAdapter,
            &materialGraphDiagnosticsAdapter,
            &vfxGraphDiagnosticsAdapter,
            &animationStateMachineDiagnosticsAdapter,
            &gameplayVisualScriptDiagnosticsAdapter});
    const editor::EditorValidationReport editorValidationReport = editorValidationService.Validate();
    imguiTiming.validationMs =
        EditorUiElapsedMs(validationStart, EditorUiTimingClock::now());
    const editor::EditorSaveApplyPolicyInput editorSaveApplyPolicy{
        showDeveloperTools_,
        static_cast<bool>(context.onSaveCourse),
        static_cast<bool>(context.onApplyCourse),
        static_cast<bool>(context.onReloadCourse),
        &editorDirtyState_,
        &editorValidationReport,
        &editorPlaySession_};
    editor::EditorCommandRegistry editorCommandRegistry;
    editorCommandRegistry.SetExecutionStatus(&editorCommandExecutionStatus_);
    editorCommandRegistry.SetNotificationCenter(&editorNotifications_);
    editorConfirmService_.SetNotificationCenter(&editorNotifications_);
    editorLayout_.Configure(
        editor::EditorLayoutConfig{
            showDeveloperTools_,
            true,
            true,
            true,
            editor::EditorToolbarHeight(),
            editor::EditorDocumentTabsHeight(),
            editor::EditorStatusBarHeight()});
    const ImGuiViewport* editorViewport = ImGui::GetMainViewport();
    const ImVec2 editorWorkPos =
        editorViewport ? editorViewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 editorWorkSize =
        editorViewport ? editorViewport->WorkSize : ImGui::GetIO().DisplaySize;
    editor::EditorPanelLayoutConfig editorPanelLayoutConfig{
        showDeveloperTools_,
        editorWorkPos.x,
        editorWorkPos.y,
        editorWorkSize.x,
        editorWorkSize.y,
        editorLayout_.TopReservedHeight(),
        editorLayout_.BottomReservedHeight()};
    editorLayoutPersistence_.Apply(editorPanelLayoutConfig);
    editorPanelLayout_.Configure(editorPanelLayoutConfig);
    editorViewportRenderTarget_.Update(
        editor::EditorViewportRenderTargetInput{
            showDeveloperTools_,
            editorPanelLayout_.ViewportRect(),
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.x)),
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.y))});
    const ImGuiIO& imguiIO = ImGui::GetIO();
    if (!showDeveloperTools_ && editorContentDrawer_.IsVisible()) {
        editorContentDrawer_.DockInLayout();
    }
    editorContentDrawer_.Tick(imguiIO.DeltaTime);
    const editor::EditorPanelRect contentDrawerWorkspace =
        editorPanelLayout_.ContentBrowserPresentationRect(true);
    const editor::EditorPanelRect contentDrawerPresentationRect =
        editorContentDrawer_.ResolvePresentationRect(contentDrawerWorkspace);
    const editor::EditorViewportRenderTargetState& editorViewportTarget =
        editorViewportRenderTarget_.State();
    const editor::EditorViewportPanelRenderInput viewportSurfaceInput{
        0,
        static_cast<float>(editorViewportTarget.renderWidth),
        static_cast<float>(editorViewportTarget.renderHeight),
        true,
        {}};
    const editor::EditorPanelRect viewportSurfaceRect =
        editor::ResolveEditorViewportRenderSurfaceRect(
            editorPanelLayout_.ViewportRect(), viewportSurfaceInput);
    editorViewportCoordinates_.Update(
        editor::EditorViewportCoordinateContext{
            viewportSurfaceRect,
            editorViewportTarget.renderWidth,
            editorViewportTarget.renderHeight,
            context.frameState != nullptr
                ? context.frameState->viewProjectionMatrix
                : MakeIdentity4x4()});
    editorLayoutPersistence_.EnsureLoaded();
    if (!courseMapEditorWorkspace_.IsRestored()) {
        courseMapEditorWorkspace_.Restore(
            editorLayoutPersistence_.MajorWorkspaceLayout(
                editor::CourseMapEditorWorkspace::kWorkspaceId));
    }
    if (!courseMapSceneVisualizationSettingsRestored_) {
        editor::CourseMapSceneVisualizationSettings settings =
            courseMapSceneVisualization_.Settings();
        settings.enabled = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.enabled", settings.enabled);
        settings.showTerrain = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.terrain", settings.showTerrain);
        settings.showRockClusters = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.rocks", settings.showRockClusters);
        settings.showSceneStructures = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.structures", settings.showSceneStructures);
        settings.showAuthoredEnemies = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.authored-enemies", settings.showAuthoredEnemies);
        settings.showEncounterPreview = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.encounter-preview", settings.showEncounterPreview);
        settings.showLabels = editorLayoutPersistence_.OverlayOption(
            "course-map.scene.labels", settings.showLabels);
        courseMapSceneVisualization_.SetSettings(settings);
        editor::CourseMapHybridCartographySettings hybridSettings =
            courseMapHybridCompositor_.Settings();
        hybridSettings.enabled = editorLayoutPersistence_.OverlayOption(
            "course-map.hybrid.enabled", hybridSettings.enabled);
        courseMapHybridCompositor_.SetSettings(hybridSettings);
        courseMapSceneVisualizationSettingsRestored_ = true;
    }
    editorViewportOverlay_.SetGameplayVisible(
        editorLayoutPersistence_.OverlayOption("gameplay-visible", true));
    editorViewportOverlay_.SetEditorVisible(
        editorLayoutPersistence_.OverlayOption("editor-visible", true));
    for (size_t index = 0; index < editor::kEditorViewportOverlayLayerCount; ++index) {
        const auto layer = static_cast<editor::EditorViewportOverlayLayerId>(index);
        editor::EditorViewportOverlayLayerSettings settings =
            editorViewportOverlay_.LayerSettings(layer);
        const std::string prefix = std::string(editor::EditorViewportOverlayLayerStableId(layer));
        settings.visible = editorLayoutPersistence_.OverlayOption(
            prefix + ".visible",
            settings.visible);
        settings.selectedOnly = editorLayoutPersistence_.OverlayOption(
            prefix + ".selected-only",
            settings.selectedOnly);
        editorViewportOverlay_.SetLayerSettings(layer, settings);
    }
    editor::EditorViewportRenderTargetState editorViewportOverlayTarget = editorViewportTarget;
    editorViewportOverlayTarget.displayRect = viewportSurfaceRect;
    editorViewportOverlay_.BeginFrame(
        editor::EditorViewportOverlayFrameContext{
            editorViewportOverlayTarget,
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.x)),
            static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.y)),
            &editorViewportCoordinates_,
            context.frameState != nullptr
                ? context.frameState->cameraWorldPosition
                : Vector3{},
            1.0f});
    {
        const bool playSessionActive = editorPlaySession_.IsActive();
        const bool coursePreviewFrozen =
            runtimeState.terrain.freezeCourseRuntime;
        if (playSessionActive || coursePreviewFrozen) {
            std::ostringstream debugState;
            if (playSessionActive) {
                debugState
                    << (editorPlaySession_.RuntimePaused()
                            ? "FROZEN"
                            : "RUNTIME LIVE")
                    << " | "
                    << editor::ToString(editorPlaySession_.ViewportMode())
                    << "\nRuntime frame "
                    << editorPlaySession_.RuntimeFrameCount()
                    << " | completed steps "
                    << editorPlaySession_.RuntimeStepCount();
                if (editorPlaySession_.RuntimeStepRequested()) {
                    debugState << " | STEP QUEUED";
                }
                debugState
                    << "\nF10: Eject/Possess | RMB+WASD: Move | Q/E: Vertical";
                if (editorPlaySession_.ViewportEjected()) {
                    debugState
                        << "\nCamera input: "
                        << (editorViewportInteraction_
                                    .HasViewportCameraCapture()
                                ? "CAPTURED"
                                : "Awaiting RMB in Viewport");
                }
            } else {
                debugState
                    << "COURSE PREVIEW FROZEN | Editor Free Camera"
                    << "\nRMB+WASD: Move | Q/E: Vertical"
                    << "\nCourse simulation is stopped; camera inspection remains live.";
            }
            editor::EditorViewportOverlayItemOptions options{};
            options.selected = true;
            options.iconFallback = false;
            options.background = true;
            options.priority = 1000;
            editorViewportOverlay_
                .Sink(editor::EditorViewportOverlayLayerId::AuthoringHelpers)
                .Label(
                    14.0f,
                    14.0f,
                    debugState.str(),
                    editorPlaySession_.ViewportEjected() ||
                            (!playSessionActive && coursePreviewFrozen)
                        ? 0xff72e6ffu
                        : 0xffffd36bu,
                    options);
        }
    }
    {
        editor::EditorViewportOverlayCommandSink boundsSink =
            editorViewportOverlay_.Sink(editor::EditorViewportOverlayLayerId::AuthoringHelpers);
        editor::EditorViewportOverlayCommandSink labelSink =
            editorViewportOverlay_.Sink(editor::EditorViewportOverlayLayerId::ObjectLabels);
        const editor::EditorObjectHandle* primary = editorSelection_.Primary();
        for (const editor::EditorProductionSceneInstance& instance :
             editorProductionScenePipeline_.Instances()) {
            float minimumX = FLT_MAX;
            float minimumY = FLT_MAX;
            float maximumX = -FLT_MAX;
            float maximumY = -FLT_MAX;
            bool projected = false;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const Vector3 point{
                    (corner & 1u) != 0 ? instance.boundsMax.x : instance.boundsMin.x,
                    (corner & 2u) != 0 ? instance.boundsMax.y : instance.boundsMin.y,
                    (corner & 4u) != 0 ? instance.boundsMax.z : instance.boundsMin.z};
                const editor::EditorViewportProjectedPoint screen =
                    editorViewportCoordinates_.ProjectWorld(point);
                if (!screen.valid || screen.behind) continue;
                minimumX = (std::min)(minimumX, screen.render.x);
                minimumY = (std::min)(minimumY, screen.render.y);
                maximumX = (std::max)(maximumX, screen.render.x);
                maximumY = (std::max)(maximumY, screen.render.y);
                projected = true;
            }
            if (!projected) continue;
            const bool selected = primary != nullptr &&
                primary->domain == editor::EditorDomainId::SceneEntity &&
                primary->stableId.find(instance.entityGuid) != std::string::npos;
            const editor::EditorViewportOverlayItemOptions options{
                selected, true, false, -1.0f, 0.0f,
                (std::numeric_limits<float>::max)(), selected ? 100 : 0};
            boundsSink.Rect(minimumX, minimumY, maximumX, maximumY,
                selected ? 0xff48d9ffu : 0x9077b7ffu, selected ? 2.0f : 1.0f, options);
            labelSink.Label(minimumX, minimumY,
                "LOD" + std::to_string(instance.selectedLod),
                selected ? 0xffb8efffu : 0xff9fc9ffu, options);
        }
    }
    courseRailViewportEditTool_.Bind(
        context.courseRailEditorController,
        context.courseRailPickingService);
    courseRailTransformGizmo_.Bind(context.courseRailEditorController);
    courseEnemyViewportEditTool_.Bind(
        context.courseEnemyEditorController,
        context.courseEnemyPickingService,
        context.courseRailPickingService);
    courseEnemyTransformGizmo_.Bind(context.courseEnemyEditorController);
    const editor::EditorInteractiveToolDescriptor* activeInteractiveDescriptor =
        editorInteractiveTools_.ActiveToolDescriptor();
    const bool interactiveToolConsumesViewport =
        activeInteractiveDescriptor != nullptr &&
        activeInteractiveDescriptor->requiresViewport;
    const bool viewportOverlayUiBlocked =
        editor::EditorViewportOverlayUiContains(
            editorPanelLayout_.ViewportRect(),
            imguiIO.MousePos.x,
            imguiIO.MousePos.y,
            ImGui::GetFrameHeight(),
            (context.courseRailEditorController != nullptr &&
                context.courseRailEditorController->State().bound) ||
                (context.courseEnemyEditorController != nullptr &&
                    context.courseEnemyEditorController->State().bound) ||
                (context.courseWaveEditorController != nullptr &&
                    context.courseWaveEditorController->State().bound),
            courseRailViewportEditTool_.Active() ||
                courseEnemyViewportEditTool_.Active()) ||
        (ImGui::IsAnyItemActive() && !editorViewportInteraction_.HasAnyCapture());
    const bool popupOrModalActive = ImGui::IsPopupOpen(
        nullptr,
        ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool contentDrawerWasVisible = editorContentDrawer_.IsVisible();
    editorContentDrawer_.HandleOutsidePointerPress(
        contentDrawerPresentationRect,
        imguiIO.MousePos.x,
        imguiIO.MousePos.y,
        ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        popupOrModalActive || editorConfirmService_.HasPending());
    const bool contentDrawerBlocksViewport =
        contentDrawerWasVisible &&
        editorContentDrawer_.BlocksViewportPointer(
            contentDrawerPresentationRect,
            imguiIO.MousePos.x,
            imguiIO.MousePos.y,
            ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    const bool contentDrawerEscapeRequested =
        editorContentDrawer_.IsVisible() &&
        !imguiIO.WantTextInput &&
        !popupOrModalActive &&
        !editorConfirmService_.HasPending() &&
        ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (contentDrawerEscapeRequested) {
        editorContentDrawer_.Close();
    }
    const bool cameraCancelRequested =
        editorViewportInteraction_.HasViewportCameraCapture() &&
        !contentDrawerEscapeRequested &&
        !imguiIO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape);
    editor::EditorViewportInteractionInput viewportInteractionInput{};
    viewportInteractionInput.viewportRect =
        editorContentBrowserMaximized_ ? editor::EditorPanelRect{} : viewportSurfaceRect;
    viewportInteractionInput.renderWidth = editorViewportTarget.renderWidth;
    viewportInteractionInput.renderHeight = editorViewportTarget.renderHeight;
    viewportInteractionInput.mouseX = imguiIO.MousePos.x;
    viewportInteractionInput.mouseY = imguiIO.MousePos.y;
    viewportInteractionInput.mouseAvailable =
        imguiIO.MousePos.x > -FLT_MAX && imguiIO.MousePos.y > -FLT_MAX;
    viewportInteractionInput.imguiWantsMouse = imguiIO.WantCaptureMouse;
    viewportInteractionInput.developerToolsVisible = showDeveloperTools_;
    viewportInteractionInput.viewportFocusMode = viewportFocusMode_;
    const editor::EditorDocumentRecord* activeViewportDocument =
        editorDocumentManager_.Active();
    viewportInteractionInput.documentEditable =
        activeViewportDocument != nullptr && activeViewportDocument->open;
    viewportInteractionInput.authoringMutationAllowed = !editorPlaySession_.IsActive();
    viewportInteractionInput.playSessionActive = editorPlaySession_.IsActive();
    viewportInteractionInput.viewportOwnsMouse = !editorContentBrowserMaximized_;
    viewportInteractionInput.viewportUiBlocked =
        editorContentBrowserMaximized_ ||
        contentDrawerBlocksViewport ||
        viewportOverlayUiBlocked;
    viewportInteractionInput.popupOrModalActive = popupOrModalActive;
    viewportInteractionInput.interactiveToolActive =
        interactiveToolConsumesViewport || courseRailViewportEditTool_.Active() ||
        courseEnemyViewportEditTool_.Active();
    viewportInteractionInput.primaryPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    viewportInteractionInput.primaryDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    viewportInteractionInput.primaryReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    viewportInteractionInput.cameraCapturePressed =
        ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    viewportInteractionInput.cameraCaptureDown =
        ImGui::IsMouseDown(ImGuiMouseButton_Right);
    viewportInteractionInput.cameraCaptureReleased =
        ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    viewportInteractionInput.cameraCaptureCancelRequested = cameraCancelRequested;
    viewportInteractionInput.applicationFocused = hwnd_ != nullptr && GetForegroundWindow() == hwnd_;
    editorViewportInteraction_.Update(viewportInteractionInput);

    const editor::EditorViewportInteractionState& viewportInteractionState =
        editorViewportInteraction_.State();
    const bool cameraOwnsInput = editorViewportInteraction_.CanUseViewportCameraInput();
    editorViewportCameraInput_ = {};
    editorViewportCameraInput_.deltaTime = imguiIO.DeltaTime;
    editorViewportCameraInput_.mouseX = imguiIO.MousePos.x;
    editorViewportCameraInput_.mouseY = imguiIO.MousePos.y;
    editorViewportCameraInput_.captureStarted =
        viewportInteractionState.viewportCameraCaptureStarted;
    editorViewportCameraInput_.captureActive =
        editorViewportInteraction_.HasViewportCameraCapture();
    editorViewportCameraInput_.captureReleased =
        viewportInteractionState.viewportCameraCaptureReleased;
    editorViewportCameraInput_.captureCancelled =
        viewportInteractionState.viewportCameraCaptureCancelled;
    editorViewportCameraInput_.cancelPressed = cameraCancelRequested;
    if (cameraOwnsInput) {
        editorViewportCameraInput_.mouseDeltaX = imguiIO.MouseDelta.x;
        editorViewportCameraInput_.mouseDeltaY = imguiIO.MouseDelta.y;
        editorViewportCameraInput_.wheelDelta = imguiIO.MouseWheel;
        editorViewportCameraInput_.orbitModifier = imguiIO.KeyAlt;
        editorViewportCameraInput_.fastModifier = imguiIO.KeyShift;
        editorViewportCameraInput_.slowModifier = imguiIO.KeyCtrl;
        if (!imguiIO.WantTextInput) {
            editorViewportCameraInput_.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
            editorViewportCameraInput_.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
            editorViewportCameraInput_.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
            editorViewportCameraInput_.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
            editorViewportCameraInput_.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
            editorViewportCameraInput_.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
        }
    }
    editorViewportCameraInput_.focusSelectionPressed =
        editorViewportInteraction_.MouseInsideViewport() &&
        !imguiIO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F);
    const bool coursePreviewSimulationActive =
        context.coursePreviewSimulationSystem != nullptr &&
        context.coursePreviewSimulationSystem->IsActive();
    runtimeState.terrain.courseObjectAuthoringInputLocked =
        !app::kRuntimeAuthoringEnabled ||
        editorViewportInteraction_.AuthoringInputLocked() ||
        coursePreviewSimulationActive;
    if (context.courseRailEditorController != nullptr) {
        context.courseRailEditorController->SetAuthoringAllowed(
            app::kRuntimeAuthoringEnabled &&
            editorViewportInteraction_.CanMutateAuthoring() &&
            !coursePreviewSimulationActive);
    }
    if (context.courseEnemyEditorController != nullptr) {
        context.courseEnemyEditorController->SynchronizeExternalChanges(nullptr);
        context.courseEnemyEditorController->SetAuthoringAllowed(
            app::kRuntimeAuthoringEnabled &&
            editorViewportInteraction_.CanMutateAuthoring() &&
            !coursePreviewSimulationActive);
    }
    if (context.courseWaveEditorController != nullptr) {
        context.courseWaveEditorController->SynchronizeExternalChanges(nullptr);
        context.courseWaveEditorController->SetAuthoringAllowed(
            app::kRuntimeAuthoringEnabled &&
            editorViewportInteraction_.CanMutateAuthoring() &&
            !coursePreviewSimulationActive);
    }
    std::vector<editor::EditorViewportPickResult> viewportPickResults;
    editor::CourseRailPickResult courseRailPick{};
    editor::CourseEnemyPickResult courseEnemyPick{};
    editor::CourseWavePickResult courseWavePick{};
    std::string selectedRailPointGuid;
    if (const editor::EditorObjectHandle* primary = editorSelection_.Primary()) {
        constexpr std::string_view pointPrefix = "course-rail-point:";
        if (primary->domain == editor::EditorDomainId::CourseRailControlPoint &&
            primary->stableId.starts_with(pointPrefix)) {
            selectedRailPointGuid = primary->stableId.substr(pointPrefix.size());
        }
    }
    std::vector<std::string> enemyToolSelectionGuids;
    constexpr std::string_view enemySelectionPrefix = "course-enemy-placement:";
    for (const editor::EditorObjectHandle& handle : editorSelection_.Handles()) {
        if (handle.domain == editor::EditorDomainId::CourseEnemyPlacement &&
            handle.stableId.starts_with(enemySelectionPrefix)) {
            enemyToolSelectionGuids.push_back(
                handle.stableId.substr(enemySelectionPrefix.size()));
        }
    }
    courseRailViewportEditTool_.SetSelectedPoint(selectedRailPointGuid);
    courseEnemyViewportEditTool_.SetSelectedPlacements(
        std::move(enemyToolSelectionGuids));
    if (!courseRailViewportEditTool_.Active() &&
        !courseEnemyViewportEditTool_.Active() &&
        context.courseRailEditorController != nullptr &&
        context.courseRailPickingService != nullptr &&
        editorViewportInteraction_.MouseInsideViewport()) {
        const editor::CourseRailAuthoringModel* railModel =
            context.courseRailEditorController->Model();
        if (railModel != nullptr) {
            courseRailPick = context.courseRailPickingService->PickDisplay(
                *railModel,
                editorViewportCoordinates_,
                imguiIO.MousePos.x,
                imguiIO.MousePos.y);
        }
    }
    if (!courseRailViewportEditTool_.Active() &&
        !courseEnemyViewportEditTool_.Active() &&
        context.courseEnemyEditorController != nullptr &&
        context.courseEnemyPickingService != nullptr &&
        editorViewportInteraction_.MouseInsideViewport()) {
        const editor::CourseEnemyAuthoringModel* enemyModel =
            context.courseEnemyEditorController->Model();
        if (enemyModel != nullptr) {
            courseEnemyPick = context.courseEnemyPickingService->PickDisplay(
                *enemyModel,
                editorViewportCoordinates_,
                imguiIO.MousePos.x,
                imguiIO.MousePos.y);
        }
    }
    if (!courseRailViewportEditTool_.Active() &&
        !courseEnemyViewportEditTool_.Active() &&
        context.courseWaveEditorController != nullptr &&
        context.courseWavePickingService != nullptr &&
        context.courseWaveEditorController->Course() != nullptr &&
        editorViewportInteraction_.MouseInsideViewport()) {
        const editor::CourseWaveAuthoringModel* waveModel =
            context.courseWaveEditorController->Model();
        const editor::CourseRailAuthoringModel railModel(
            *context.courseWaveEditorController->Course());
        if (waveModel != nullptr && railModel.IsValid()) {
            courseWavePick = context.courseWavePickingService->PickDisplay(
                *waveModel,
                railModel,
                editorViewportCoordinates_,
                imguiIO.MousePos.x,
                imguiIO.MousePos.y);
        }
    }
    if (courseRailViewportEditTool_.Active()) {
        courseEnemyTransformGizmo_.Tick(editor::CourseEnemyTransformGizmoInput{});
        const bool railToolOwnsInput =
            editorViewportInteraction_.CanUseInteractiveToolInput();
        const bool keyInputAvailable = !imguiIO.WantTextInput &&
            !popupOrModalActive && !editorConfirmService_.HasPending();
        const bool cancelRailInteraction = keyInputAvailable && !cameraCancelRequested &&
            !contentDrawerEscapeRequested && ImGui::IsKeyPressed(ImGuiKey_Escape);
        courseRailTransformGizmo_.Tick(
            editor::CourseRailTransformGizmoInput{
                &editorSelection_,
                &editorViewportCoordinates_,
                courseRailViewportEditTool_.State().mode ==
                    editor::CourseRailEditMode::SelectMove,
                app::kRuntimeAuthoringEnabled &&
                    editorViewportInteraction_.CanMutateAuthoring(),
                imguiIO.MousePos.x,
                imguiIO.MousePos.y,
                railToolOwnsInput && viewportInteractionState.viewportPrimaryPressed,
                railToolOwnsInput && viewportInteractionState.viewportPrimaryDown,
                railToolOwnsInput && viewportInteractionState.viewportPrimaryReleased,
                viewportInteractionState.primaryCaptureCancelled,
                cancelRailInteraction});
        const bool gizmoOwnsPointer = courseRailTransformGizmo_.WantsPointer();
        editor::CourseRailViewportEditInput railInput{};
        railInput.coordinates = &editorViewportCoordinates_;
        railInput.displayX = imguiIO.MousePos.x;
        railInput.displayY = imguiIO.MousePos.y;
        railInput.primaryPressed = railToolOwnsInput && !gizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryPressed;
        railInput.primaryDown = railToolOwnsInput && !gizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryDown;
        railInput.primaryReleased = railToolOwnsInput && !gizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryReleased;
        railInput.primaryCancelled = viewportInteractionState.primaryCaptureCancelled;
        railInput.cancelPressed = cancelRailInteraction;
        railInput.deletePressed = keyInputAvailable && ImGui::IsKeyPressed(ImGuiKey_Delete);
        railInput.undoPressed = keyInputAvailable && imguiIO.KeyCtrl && !imguiIO.KeyShift &&
            ImGui::IsKeyPressed(ImGuiKey_Z);
        railInput.redoPressed = keyInputAvailable && imguiIO.KeyCtrl &&
            (ImGui::IsKeyPressed(ImGuiKey_Y) ||
                (imguiIO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)));
        railInput.toggleSelection = imguiIO.KeyShift;
        courseRailViewportEditTool_.Tick(railInput);
        courseRailPick = courseRailViewportEditTool_.State().hovered;
        if (courseRailViewportEditTool_.ConsumeClearSelectionRequest()) {
            editorSelection_.Clear();
        }
        if (const std::optional<editor::CourseRailPickResult> selection =
                courseRailViewportEditTool_.ConsumeSelectionRequest()) {
            const editor::EditorViewportPickResult viewportPick =
                context.courseRailPickingService->ToViewportPick(
                    *selection,
                    static_cast<uint32_t>(
                        context.courseRailEditorController->State().mutationRevision));
            if (viewportPick.hit) {
                if (imguiIO.KeyShift &&
                    viewportPick.canonicalHandle.domain ==
                        editor::EditorDomainId::CourseRailControlPoint) {
                    editorSelection_.Toggle(viewportPick.canonicalHandle);
                } else {
                    editorSelection_.SetPrimary(viewportPick.canonicalHandle);
                }
            }
        }
    } else if (courseEnemyViewportEditTool_.Active()) {
        courseRailTransformGizmo_.Tick(editor::CourseRailTransformGizmoInput{});
        const bool enemyToolOwnsInput =
            editorViewportInteraction_.CanUseInteractiveToolInput();
        const bool keyInputAvailable = !imguiIO.WantTextInput &&
            !popupOrModalActive && !editorConfirmService_.HasPending();
        const bool cancelEnemyInteraction = keyInputAvailable && !cameraCancelRequested &&
            !contentDrawerEscapeRequested && ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (keyInputAvailable && !imguiIO.KeyCtrl && !imguiIO.KeyAlt) {
            editor::CourseEnemyTransformGizmoSettings gizmoSettings =
                courseEnemyTransformGizmo_.Settings();
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                gizmoSettings.mode = editor::EditorTransformGizmoMode::Translate;
            } else if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                gizmoSettings.mode = editor::EditorTransformGizmoMode::Rotate;
            } else if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                gizmoSettings.mode = editor::EditorTransformGizmoMode::Scale;
            }
            courseEnemyTransformGizmo_.SetSettings(gizmoSettings);
        }
        courseEnemyTransformGizmo_.Tick(
            editor::CourseEnemyTransformGizmoInput{
                &editorSelection_,
                &editorViewportCoordinates_,
                courseEnemyViewportEditTool_.State().mode ==
                    editor::CourseEnemyEditMode::SelectMove,
                app::kRuntimeAuthoringEnabled &&
                    editorViewportInteraction_.CanMutateAuthoring(),
                imguiIO.MousePos.x,
                imguiIO.MousePos.y,
                enemyToolOwnsInput && viewportInteractionState.viewportPrimaryPressed,
                enemyToolOwnsInput && viewportInteractionState.viewportPrimaryDown,
                enemyToolOwnsInput && viewportInteractionState.viewportPrimaryReleased,
                viewportInteractionState.primaryCaptureCancelled,
                cancelEnemyInteraction});
        const bool enemyGizmoOwnsPointer = courseEnemyTransformGizmo_.WantsPointer();
        editor::CourseEnemyViewportEditInput enemyInput{};
        enemyInput.coordinates = &editorViewportCoordinates_;
        enemyInput.displayX = imguiIO.MousePos.x;
        enemyInput.displayY = imguiIO.MousePos.y;
        enemyInput.primaryPressed = enemyToolOwnsInput && !enemyGizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryPressed;
        enemyInput.primaryDown = enemyToolOwnsInput && !enemyGizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryDown;
        enemyInput.primaryReleased = enemyToolOwnsInput && !enemyGizmoOwnsPointer &&
            viewportInteractionState.viewportPrimaryReleased;
        enemyInput.primaryCancelled = viewportInteractionState.primaryCaptureCancelled;
        enemyInput.cancelPressed = cancelEnemyInteraction;
        enemyInput.deletePressed = keyInputAvailable &&
            ImGui::IsKeyPressed(ImGuiKey_Delete);
        enemyInput.duplicatePressed = keyInputAvailable && imguiIO.KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D);
        enemyInput.undoPressed = keyInputAvailable && imguiIO.KeyCtrl &&
            !imguiIO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z);
        enemyInput.redoPressed = keyInputAvailable && imguiIO.KeyCtrl &&
            (ImGui::IsKeyPressed(ImGuiKey_Y) ||
                (imguiIO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)));
        enemyInput.toggleSelection = imguiIO.KeyShift;
        courseEnemyViewportEditTool_.Tick(enemyInput);
        courseEnemyPick = courseEnemyViewportEditTool_.State().hovered;
        if (courseEnemyViewportEditTool_.ConsumeClearSelectionRequest()) {
            editorSelection_.Clear();
        }
        if (const std::optional<editor::CourseEnemyPickResult> selection =
                courseEnemyViewportEditTool_.ConsumeSelectionRequest()) {
            const editor::EditorViewportPickResult viewportPick =
                context.courseEnemyPickingService->ToViewportPick(
                    *selection,
                    static_cast<uint32_t>(
                        context.courseEnemyEditorController->State().mutationRevision));
            if (viewportPick.hit) {
                if (imguiIO.KeyShift) {
                    editorSelection_.Toggle(viewportPick.canonicalHandle);
                } else {
                    editorSelection_.SetPrimary(viewportPick.canonicalHandle);
                }
            }
        }
        if (std::optional<editor::CourseEnemyRangeSelectionRequest> range =
                courseEnemyViewportEditTool_.ConsumeRangeSelectionRequest()) {
            if (!range->additive) editorSelection_.Clear();
            for (const editor::CourseEnemyPickResult& placement :
                 range->placements) {
                const editor::EditorViewportPickResult viewportPick =
                    context.courseEnemyPickingService->ToViewportPick(
                        placement,
                        static_cast<uint32_t>(
                            context.courseEnemyEditorController->State().mutationRevision));
                if (viewportPick.hit) {
                    editorSelection_.Add(std::move(viewportPick.canonicalHandle));
                }
            }
        }
    } else {
        courseRailTransformGizmo_.Tick(editor::CourseRailTransformGizmoInput{});
        courseEnemyTransformGizmo_.Tick(editor::CourseEnemyTransformGizmoInput{});
    }
    if (context.courseRailViewportRenderer != nullptr) {
        context.courseRailViewportRenderer->SetHoveredPoint(
            courseRailPick.kind == editor::CourseRailPickKind::ControlPoint ||
                    courseRailPick.IsTangentHandle()
                ? courseRailPick.guid : std::string{});
        context.courseRailViewportRenderer->SetHoveredSegment(
            courseRailPick.kind == editor::CourseRailPickKind::Segment
                ? courseRailPick.guid : std::string{});
    }
    if (context.courseEnemyViewportRenderer != nullptr) {
        context.courseEnemyViewportRenderer->SetHoveredPlacement(
            courseEnemyPick.hit ? courseEnemyPick.placementGuid : std::string{});
    }
    if (context.courseWaveViewportRenderer != nullptr) {
        context.courseWaveViewportRenderer->SetHoveredWave(
            courseWavePick.hit ? courseWavePick.waveGuid : std::string{});
    }
    const bool scenePickRequested = app::kRuntimeAuthoringEnabled &&
        !courseRailViewportEditTool_.Active() &&
        !courseEnemyViewportEditTool_.Active() &&
        editorViewportInteraction_.CanUseSceneInput() &&
        editorViewportInteraction_.State().viewportPrimaryPressed;
    if (scenePickRequested) {
        if (courseEnemyPick.hit && context.courseEnemyPickingService != nullptr &&
            context.courseEnemyEditorController != nullptr) {
            viewportPickResults.push_back(
                context.courseEnemyPickingService->ToViewportPick(
                    courseEnemyPick,
                    static_cast<uint32_t>(
                        context.courseEnemyEditorController->State().mutationRevision)));
        } else if (courseWavePick.hit && context.courseWavePickingService != nullptr &&
            context.courseWaveEditorController != nullptr) {
            viewportPickResults.push_back(
                context.courseWavePickingService->ToViewportPick(
                    courseWavePick,
                    static_cast<uint32_t>(
                        context.courseWaveEditorController->State().mutationRevision)));
        } else if (courseRailPick.hit && context.courseRailPickingService != nullptr &&
            context.courseRailEditorController != nullptr) {
            viewportPickResults.push_back(
                context.courseRailPickingService->ToViewportPick(
                    courseRailPick,
                    static_cast<uint32_t>(
                        context.courseRailEditorController->State().mutationRevision)));
        } else {
            const editor::EditorViewportWorldRay ray = editorViewportCoordinates_.DisplayToWorldRay(
                imguiIO.MousePos.x, imguiIO.MousePos.y);
            const editor::EditorProductionSceneRayHit hit = ray.valid
                ? editorProductionScenePipeline_.Raycast(ray.origin, ray.direction)
                : editor::EditorProductionSceneRayHit{};
            if (hit.valid) {
            const editor::EditorScene* scene =
                editorSceneDocumentProvider_.Scene(editorSceneDocumentId_);
            const editor::EditorSceneEntity* entity = scene != nullptr
                ? scene->FindEntity(hit.entityGuid) : nullptr;
            editor::EditorViewportPickResult pick = editor::MakeEditorViewportPickResult(
                editor::EditorViewportPickSource::SceneViewport,
                editor::EditorDomainId::SceneEntity,
                "scene-entity",
                0,
                static_cast<uint32_t>(scene != nullptr ? scene->revision : 0),
                entity != nullptr ? entity->name : "Scene Entity");
            pick.canonicalHandle = {
                editor::EditorDomainId::SceneEntity,
                editor::BuildEditorWorldStableId(
                    editorSceneDocumentId_, editorSceneWorldProvider_.ProviderId(), hit.entityGuid),
                0,
                static_cast<uint32_t>(scene != nullptr ? scene->revision : 0),
                entity != nullptr ? entity->name : "Scene Entity"};
            viewportPickResults.push_back(std::move(pick));
            }
        }
    } else if (const editor::EditorObjectHandle* primary = editorSelection_.Primary();
               primary != nullptr && primary->domain == editor::EditorDomainId::SceneEntity) {
        editor::EditorViewportPickResult pick = editor::MakeEditorViewportPickResult(
            editor::EditorViewportPickSource::SceneViewport,
            editor::EditorDomainId::SceneEntity,
            "scene-entity",
            primary->localIndex,
            primary->generation,
            primary->displayName);
        pick.canonicalHandle = *primary;
        viewportPickResults.push_back(std::move(pick));
    }
    const bool courseAuthoringConsumedPrimary = scenePickRequested &&
        (courseEnemyPick.hit || courseWavePick.hit || courseRailPick.hit);
    if (app::kRuntimeAuthoringEnabled && !courseAuthoringConsumedPrimary &&
        runtimeState.terrain.selectedCourseTerrainPlacement >= 0) {
        const uint64_t index =
            static_cast<uint64_t>(runtimeState.terrain.selectedCourseTerrainPlacement);
        editor::EditorViewportPickResult pick = editor::MakeEditorViewportPickResult(
                editor::EditorViewportPickSource::CourseViewport,
                editor::EditorDomainId::CourseTerrainPlacement,
                "course-terrain",
                index,
                runtimeState.terrain.courseObjectEditRevision,
                "Course Terrain #" + std::to_string(index));
        if (const editor::EditorWorldObjectRecord* record =
                editorWorldModel_.FindByDomainIndex(
                    editor::EditorDomainId::CourseTerrainPlacement, index)) {
            pick.canonicalHandle = record->handle;
        }
        viewportPickResults.push_back(std::move(pick));
    }
    if (app::kRuntimeAuthoringEnabled && !courseAuthoringConsumedPrimary &&
        runtimeState.terrain.selectedCourseRockCluster >= 0) {
        const uint64_t index =
            static_cast<uint64_t>(runtimeState.terrain.selectedCourseRockCluster);
        editor::EditorViewportPickResult pick = editor::MakeEditorViewportPickResult(
                editor::EditorViewportPickSource::CourseViewport,
                editor::EditorDomainId::CourseRockCluster,
                "course-rock",
                index,
                runtimeState.terrain.courseObjectEditRevision,
                "Course Rock Cluster #" + std::to_string(index));
        if (const editor::EditorWorldObjectRecord* record =
                editorWorldModel_.FindByDomainIndex(
                    editor::EditorDomainId::CourseRockCluster, index)) {
            pick.canonicalHandle = record->handle;
        }
        viewportPickResults.push_back(std::move(pick));
    }
    if (selectedEffectInstanceId_ != 0) {
        const uint64_t index = static_cast<uint64_t>(selectedEffectInstanceId_);
        editor::EditorViewportPickResult pick = editor::MakeEditorViewportPickResult(
                editor::EditorViewportPickSource::VfxRuntime,
                editor::EditorDomainId::VfxEffectInstance,
                "vfx-instance",
                index,
                0,
                "VFX Instance #" + std::to_string(index));
        if (const editor::EditorWorldObjectRecord* record =
                editorWorldModel_.FindByObjectGuid(
                    editorVfxWorldProvider_.ProviderId(), "instance-" + std::to_string(index))) {
            pick.canonicalHandle = record->handle;
        }
        viewportPickResults.push_back(std::move(pick));
    }
    editorViewportSelectionBridge_.Sync(
        editor::EditorViewportSelectionBridgeInput{
            &editorSelection_,
            &editorViewportInteraction_,
            &viewportPickResults});
    if (context.courseRailViewportRenderer != nullptr) {
        const editor::CourseRailAuthoringModel* previewModel =
            courseRailDetailsPanel_.PreviewModel();
        if (previewModel == nullptr) previewModel = courseRailTransformGizmo_.PreviewModel();
        if (previewModel == nullptr && courseRailViewportEditTool_.Active()) {
            previewModel = courseRailViewportEditTool_.PreviewModel();
        }
        context.courseRailViewportRenderer->SetPreviewModel(previewModel);
        context.courseRailViewportRenderer->SetSelectedPoint({});
        context.courseRailViewportRenderer->SetSelectedSegment({});
        if (const editor::EditorObjectHandle* primary = editorSelection_.Primary()) {
            constexpr std::string_view pointPrefix = "course-rail-point:";
            constexpr std::string_view segmentPrefix = "course-rail-segment:";
            if (primary->domain == editor::EditorDomainId::CourseRailControlPoint &&
                primary->stableId.starts_with(pointPrefix)) {
                context.courseRailViewportRenderer->SetSelectedPoint(
                    primary->stableId.substr(pointPrefix.size()));
            } else if (primary->domain == editor::EditorDomainId::CourseRailSegment &&
                primary->stableId.starts_with(segmentPrefix)) {
                context.courseRailViewportRenderer->SetSelectedSegment(
                    primary->stableId.substr(segmentPrefix.size()));
            }
        }
    }
    if (context.courseEnemyViewportRenderer != nullptr) {
        const editor::CourseEnemyAuthoringModel* previewModel =
            courseEnemyDetailsPanel_.PreviewModel();
        if (previewModel == nullptr) {
            previewModel = courseEnemyTransformGizmo_.PreviewModel();
        }
        if (previewModel == nullptr && courseEnemyViewportEditTool_.Active()) {
            previewModel = courseEnemyViewportEditTool_.PreviewModel();
        }
        context.courseEnemyViewportRenderer->SetPreviewModel(previewModel);
        std::vector<std::string> selectedEnemyGuids;
        constexpr std::string_view enemyPrefix = "course-enemy-placement:";
        for (const editor::EditorObjectHandle& handle : editorSelection_.Handles()) {
            if (handle.domain == editor::EditorDomainId::CourseEnemyPlacement &&
                handle.stableId.starts_with(enemyPrefix)) {
                selectedEnemyGuids.push_back(
                    handle.stableId.substr(enemyPrefix.size()));
            }
        }
        context.courseEnemyViewportRenderer->SetSelectedPlacements(
            selectedEnemyGuids);
    }
    if (context.courseWaveViewportRenderer != nullptr) {
        context.courseWaveViewportRenderer->SetPreviewModel(
            courseWaveDetailsPanel_.PreviewModel());
        std::vector<std::string> selectedWaveGuids;
        constexpr std::string_view wavePrefix = "course-wave:";
        for (const editor::EditorObjectHandle& handle : editorSelection_.Handles()) {
            if (handle.domain == editor::EditorDomainId::CourseWaveDefinition &&
                handle.stableId.starts_with(wavePrefix)) {
                selectedWaveGuids.push_back(
                    handle.stableId.substr(wavePrefix.size()));
            }
        }
        context.courseWaveViewportRenderer->SetSelectedWaves(selectedWaveGuids);
    }
    runtimeState.terrain.selectedCourseTerrainPlacements.clear();
    runtimeState.terrain.selectedCourseRockClusters.clear();
    for (const editor::EditorObjectHandle& handle : editorSelection_.Handles()) {
        if (handle.domain == editor::EditorDomainId::CourseTerrainPlacement) {
            runtimeState.terrain.selectedCourseTerrainPlacements.push_back(
                static_cast<int>(handle.localIndex));
        } else if (handle.domain == editor::EditorDomainId::CourseRockCluster) {
            runtimeState.terrain.selectedCourseRockClusters.push_back(
                static_cast<int>(handle.localIndex));
        }
    }
    if (app::kRuntimeAuthoringEnabled &&
        editorViewportInteraction_.MouseInsideViewport() &&
        editorViewportInteraction_.CanMutateAuthoring() &&
        !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) runtimeState.terrain.courseObjectGizmoMode = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) runtimeState.terrain.courseObjectGizmoMode = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) runtimeState.terrain.courseObjectGizmoMode = 1;
    }
    editorTransformGizmo_.Update(
        editor::EditorTransformGizmoInput{
            &editorSelection_,
            &editorViewportInteraction_,
            &editorViewportCoordinates_,
            &editorViewportSelectionBridge_,
            &editorTransactions,
            editor::EditorTransformGizmoModeFromIndex(runtimeState.terrain.courseObjectGizmoMode),
            editor::EditorTransformGizmoAxisFromIndex(runtimeState.terrain.courseObjectActiveAxis),
            editor::EditorTransformGizmoSpaceFromIndex(runtimeState.terrain.courseObjectGizmoSpace),
            editor::EditorTransformGizmoPivotModeFromIndex(runtimeState.terrain.courseObjectPivotMode),
            runtimeState.terrain.courseObjectSnapEnabled,
            app::kRuntimeAuthoringEnabled});
    runtimeState.terrain.courseObjectGizmoMode =
        editor::ToCourseGizmoMode(editorTransformGizmo_.State().mode);
    editorToolRegistry_.BuildRuntimeWatch(
        editor::EditorRuntimeWatchBuildInput{
            &editorRuntimeInspector_,
            &effectRuntime,
            context.loadedEffectAssets,
            selectedEffectInstanceId_,
            &editorPlaySession_,
            &editorPlaySessionSnapshot_,
            &editorRailRuntimePause_,
            &editorSelection_,
            &runtimeState,
            context.course,
            context.courseSpawnRuntime,
            context.courseCollisionSystem,
            context.courseCheckpointSystem,
            context.playerCombatFeelSystem,
            context.renderGraphDescription,
            context.renderGraphError,
            context.renderPassDebugInfo,
            context.transientTargetCount,
            context.transientTargetStorageCount,
            context.transientBufferCount,
            context.transientBufferStorageCount,
            context.courseDistance,
            context.courseSpeed,
            context.courseRailLength});
    {
        struct FeatureEvidenceCounts {
            size_t entities = 0;
            size_t runtimeDisabledEntities = 0;
            size_t disabledComponents = 0;
            size_t meshes = 0;
            size_t colliders = 0;
            size_t spawnPoints = 0;
            size_t splineRoutes = 0;
            size_t patrols = 0;
            size_t gimmicks = 0;
            size_t eventBindings = 0;
            size_t eventSequences = 0;
        } evidence;

        const editor::EditorScene* evidenceScene = ActiveEditorScene();
        if (evidenceScene != nullptr) {
            evidence.entities = evidenceScene->entities.size();
            for (const editor::EditorSceneEntity& entity : evidenceScene->entities) {
                if (!entity.runtimeEnabled) {
                    ++evidence.runtimeDisabledEntities;
                }
                for (const editor::EditorSceneComponent& component : entity.components) {
                    if (!component.enabled) {
                        ++evidence.disabledComponents;
                    }
                    if (component.typeId == editor::kEditorMeshRendererComponentType) {
                        ++evidence.meshes;
                    } else if (component.typeId == editor::kEditorBoxColliderComponentType) {
                        ++evidence.colliders;
                    } else if (component.typeId ==
                               editor::kEditorGameplaySpawnPointComponentType) {
                        ++evidence.spawnPoints;
                    } else if (component.typeId ==
                               editor::kEditorSplineRouteComponentType) {
                        ++evidence.splineRoutes;
                    } else if (component.typeId == editor::kEditorPatrolComponentType) {
                        ++evidence.patrols;
                    } else if (component.typeId == editor::kEditorGimmickComponentType) {
                        ++evidence.gimmicks;
                    } else if (component.typeId ==
                               editor::kEditorGimmickEventBindingComponentType) {
                        ++evidence.eventBindings;
                    } else if (component.typeId ==
                               editor::kEditorGimmickEventSequenceComponentType) {
                        ++evidence.eventSequences;
                    }
                }
            }
        }

        std::ostringstream authoredDetail;
        authoredDetail
            << "entities=" << evidence.entities
            << " mesh=" << evidence.meshes
            << " collider=" << evidence.colliders
            << " spawn=" << evidence.spawnPoints
            << " spline=" << evidence.splineRoutes
            << " patrol=" << evidence.patrols
            << " gimmick=" << evidence.gimmicks
            << " binding=" << evidence.eventBindings
            << " sequence=" << evidence.eventSequences
            << " runtimeDisabled=" << evidence.runtimeDisabledEntities
            << " componentDisabled=" << evidence.disabledComponents
            << " terrainRevision="
            << runtimeState.terrain.courseObjectEditRevision;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "Submission Evidence",
            "+Alpha Authored Inventory",
            evidenceScene != nullptr ? "Captured" : "No Scene",
            authoredDetail.str(),
            evidenceScene != nullptr ? editor::EditorRuntimeWatchSeverity::Info
                                     : editor::EditorRuntimeWatchSeverity::Warning,
            editorDocumentServiceFrame_});

        std::ostringstream captureDetail;
        captureDetail
            << "runtime="
            << (editorPlaySession_.RuntimePaused() ? "Frozen" : "Live")
            << " stepQueued="
            << (editorPlaySession_.RuntimeStepRequested() ? "true" : "false")
            << " viewport=" << editor::ToString(editorPlaySession_.ViewportMode())
            << " runtimeFrame=" << editorPlaySession_.RuntimeFrameCount()
            << " completedSteps=" << editorPlaySession_.RuntimeStepCount()
            << " ejects=" << editorPlaySession_.EjectCount()
            << " possesses=" << editorPlaySession_.PossessCount()
            << " controls=[Freeze, Step, F10 Eject/Possess]";
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "Submission Evidence",
            "Runtime Debug Capture",
            editorPlaySession_.IsActive()
                ? (editorPlaySession_.RuntimePaused() ? "Frozen" : "Running")
                : "Stopped",
            captureDetail.str(),
            editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionTexturePipelineStats& textureStats =
            editorProductionTexturePipeline_.Stats();
        std::ostringstream detail;
        detail << "resident=" << textureStats.residentTextures
               << " descriptors=" << textureStats.residentDescriptors
               << " mips(full/partial)=" << textureStats.fullMipTextures
               << "/" << textureStats.partialMipTextures
               << " gpuMiB=" << std::fixed << std::setprecision(2)
               << static_cast<double>(textureStats.residentGpuBytes) / (1024.0 * 1024.0)
               << "/" << static_cast<double>(textureStats.gpuBudgetBytes) / (1024.0 * 1024.0)
               << " pending=" << textureStats.pendingGpuRetirements
               << " fallback=" << textureStats.fallbackTextures
               << " hits/misses=" << textureStats.cacheHits << "/" << textureStats.cacheMisses;
        editorRuntimeInspector_.AddRecord(
            editor::EditorRuntimeWatchRecord{
                "RenderGraph",
                "Texture Residency",
                textureStats.fallbackTextures == 0 ? "Resident" : "Fallback",
                detail.str(),
                textureStats.fallbackTextures == 0
                    ? editor::EditorRuntimeWatchSeverity::Info
                    : editor::EditorRuntimeWatchSeverity::Warning,
                editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionShaderPipelineStats& shaderStats =
            editorProductionShaderPipeline_.Stats();
        std::ostringstream detail;
        detail << "variants=" << shaderStats.residentVariants
               << " queued=" << shaderStats.queuedCompiles
               << " ready/fallback/LKG=" << shaderStats.readyBindings
               << "/" << shaderStats.fallbackBindings
               << "/" << shaderStats.lastKnownGoodBindings
               << " normal=" << shaderStats.normalMapBindings
               << " PSO lib hit/miss=" << shaderStats.pipelineLibraryHits
               << "/" << shaderStats.pipelineLibraryMisses
               << " compile ok/fail=" << shaderStats.compilesCompleted
               << "/" << shaderStats.compilesFailed
               << " pending=" << shaderStats.pendingGpuRetirements;
        editorRuntimeInspector_.AddRecord(
            editor::EditorRuntimeWatchRecord{
                "RenderGraph",
                "Shader Variants / PSO Cache",
                shaderStats.failedVariants == 0 ? "Ready" : "Fallback",
                detail.str(),
                shaderStats.failedVariants == 0
                    ? editor::EditorRuntimeWatchSeverity::Info
                    : editor::EditorRuntimeWatchSeverity::Warning,
                editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionLightingStats& lightingStats =
            editorProductionLightingPipeline_.Stats();
        std::ostringstream detail;
        detail << "lights=" << lightingStats.visibleLights
               << "/" << lightingStats.submittedLights
               << " rejected=" << lightingStats.rejectedByLightBudget
               << " clusters=" << lightingStats.clusterCount
               << " indices=" << lightingStats.clusterIndexCount
               << " overflow=" << lightingStats.clusterOverflowCount
               << " shadows=" << lightingStats.residentShadowMaps
               << "/" << lightingStats.shadowRequests
               << " shadowDraws=" << lightingStats.renderedShadowDraws
               << " atlasMiB=" << std::fixed << std::setprecision(2)
               << static_cast<double>(lightingStats.shadowAtlasBytes) / (1024.0 * 1024.0);
        const bool constrained = lightingStats.rejectedByLightBudget != 0 ||
            lightingStats.clusterOverflowCount != 0 ||
            lightingStats.rejectedByShadowBudget != 0;
        editorRuntimeInspector_.AddRecord(
            editor::EditorRuntimeWatchRecord{
                "RenderGraph",
                "Multi-Light Clusters / Shadow Atlas",
                constrained ? "Budget Fallback" : "Resident",
                detail.str(),
                constrained ? editor::EditorRuntimeWatchSeverity::Warning
                            : editor::EditorRuntimeWatchSeverity::Info,
                editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionGpuDrivenStats& visibilityStats =
            editorProductionGpuDrivenPipeline_.Stats();
        std::ostringstream detail;
        detail << "instances=" << visibilityStats.residentInstances
               << "/" << visibilityStats.submittedInstances
               << " visible(readback)=" << visibilityStats.gpuVisibleInstances
               << " batches=" << visibilityStats.batches
               << " fallback=" << visibilityStats.cpuFallbackPackets
               << " rejected(instance/batch)=" << visibilityStats.rejectedByInstanceBudget
               << "/" << visibilityStats.rejectedByBatchBudget
               << " dispatch/readback=" << visibilityStats.dispatches
               << "/" << visibilityStats.readbacks
               << " reject(frustum/hiz/batch)="
               << visibilityStats.frustumRejectedInstances
               << "/" << visibilityStats.hiZRejectedInstances
               << "/" << visibilityStats.invalidBatchInstances
               << " prepare/submit=" << visibilityStats.preparedBatches
               << "/" << visibilityStats.executedBatches
               << " capacity=" << visibilityStats.submittedCommandCapacity
               << " command=" << (visibilityStats.commandLayoutValidated ? "valid" : "pending")
               << " auto=" << (editorProductionGpuDrivenPipeline_.AutoValidationReady()
                                      ? "indirect"
                                      : "direct-fallback")
               << " occlusion=" << (visibilityStats.occlusionEnabled ? "Hi-Z" : "frustum")
               << " hizFresh=" << (visibilityStats.hiZFresh ? "yes" : "no")
               << " retry=" << (visibilityStats.frustumOnlyRetry ? "yes" : "no")
               << " quarantine=" << (visibilityStats.occlusionQuarantined ? "yes" : "no");
        const bool constrained = !visibilityStats.ready ||
            visibilityStats.cpuFallbackPackets != 0 || visibilityStats.ringStalls != 0;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "RenderGraph", "GPU Visibility / Indirect Draw",
            constrained ? "CPU Fallback" : "GPU Driven", detail.str(),
            constrained ? editor::EditorRuntimeWatchSeverity::Warning
                        : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorWorldPartitionStats& partitionStats =
            editorWorldPartitionPipeline_.Stats();
        std::ostringstream detail;
        detail << "cells=" << partitionStats.cells
               << " source=" << partitionStats.sourceResidentCells
               << "/" << partitionStats.sourceResidentEntities
               << " loading=" << partitionStats.loadingCells
               << " HLOD=" << partitionStats.hlodResidentCells
               << " builds=" << partitionStats.queuedHlodBuilds
               << "/" << partitionStats.completedHlodBuilds
               << " refs/pulls=" << partitionStats.crossCellReferences
               << "/" << partitionStats.hardReferencePulls
               << " rejected(cell/entity/HLOD)=" << partitionStats.rejectedByCellBudget
               << "/" << partitionStats.rejectedByEntityBudget
               << "/" << partitionStats.rejectedByHlodBudget
               << " HLOD MiB=" << std::fixed << std::setprecision(2)
               << static_cast<double>(partitionStats.residentHlodGpuBytes) / (1024.0 * 1024.0)
               << " pending=" << partitionStats.pendingGpuRetirements;
        const bool constrained = partitionStats.rejectedByCellBudget != 0 ||
            partitionStats.rejectedByEntityBudget != 0 ||
            partitionStats.rejectedByHlodBudget != 0 ||
            partitionStats.missingEntityReferences != 0;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "RenderGraph", "World Partition / Cell Streaming / HLOD",
            constrained ? "Budget Constrained" : "Streaming", detail.str(),
            constrained ? editor::EditorRuntimeWatchSeverity::Warning
                        : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionNavigationStats& navigationStats =
            editorProductionNavigationPipeline_.Stats();
        std::ostringstream detail;
        detail << "tiles=" << navigationStats.residentTiles
               << "/" << navigationStats.submittedTiles
               << " queued/completed=" << navigationStats.queuedTileBuilds
               << "/" << navigationStats.completedTileBuilds
               << " nodes=" << navigationStats.residentNodes
               << " polygons=" << navigationStats.residentPolygons
               << " areas/profiles/links=" << navigationStats.activeAreas
               << "/" << navigationStats.activeAgentProfiles
               << "/" << navigationStats.activeOffMeshLinks
               << " linkTraversals/rejected=" << navigationStats.offMeshLinkTraversals
               << "/" << navigationStats.rejectedOffMeshLinks
               << " obstacles/updates=" << navigationStats.dynamicObstacles
               << "/" << navigationStats.dynamicObstacleUpdates
               << " dirty=" << navigationStats.dirtyObstacleTiles
               << " paths(ok/fail)=" << navigationStats.successfulPaths
               << "/" << navigationStats.failedPaths
               << " visited=" << navigationStats.lastVisitedNodes
               << " generation=" << navigationStats.snapshotGeneration
               << " rejected(tile/node/obstacle/query)=" << navigationStats.rejectedByTileBudget
               << "/" << navigationStats.rejectedByNodeBudget
               << "/" << navigationStats.rejectedDynamicObstacles
               << "/" << navigationStats.queryBudgetFailures;
        const bool constrained = navigationStats.rejectedByTileBudget != 0 ||
            navigationStats.rejectedByNodeBudget != 0 ||
            navigationStats.rejectedDynamicObstacles != 0 ||
            navigationStats.queryBudgetFailures != 0;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "Navigation Mesh / World Query / Dynamic Obstacles",
            constrained ? "Budget Constrained" : "Streaming", detail.str(),
            constrained ? editor::EditorRuntimeWatchSeverity::Warning
                        : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorNavigationAuthoringStats& stats =
            editorProductionNavigationAuthoringPipeline_.Stats();
        std::ostringstream detail;
        detail << "mutations/compileFailures=" << stats.mutations << "/"
               << stats.compileFailures << " runtimePublishes=" << stats.runtimePublishes
               << " overlay/rejected=" << stats.overlayCommands << "/"
               << stats.overlayBudgetRejected;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "Navigation Authoring / Off-Mesh Links / Area Costs",
            stats.compileFailures == 0 ? "Published" : "Compile Failed",
            detail.str(), stats.compileFailures == 0
                ? editor::EditorRuntimeWatchSeverity::Info
                : editor::EditorRuntimeWatchSeverity::Warning,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorProductionAiStats& aiStats = editorProductionAiPipeline_.Stats();
        std::ostringstream detail;
        detail << "agents=" << aiStats.activeAgents << "/" << aiStats.submittedAgents
               << " stimuli=" << aiStats.submittedStimuli
               << " perception(sight/hearing)=" << aiStats.sightHits
               << "/" << aiStats.hearingHits
               << " ticks(ok/run/fail)=" << aiStats.successfulTicks
               << "/" << aiStats.runningTicks << "/" << aiStats.failedTicks
               << " nav(ok/fail)=" << (aiStats.navigationQueries - aiStats.navigationFailures)
               << "/" << aiStats.navigationFailures
               << " programs/reloads=" << aiStats.loadedPrograms
               << "/" << aiStats.hotReloads
               << " generation=" << aiStats.tickGeneration
               << " rejected(agent/stimulus/budget)=" << aiStats.rejectedAgents
               << "/" << aiStats.rejectedStimuli << "/" << aiStats.budgetFailures;
        const bool constrained = aiStats.rejectedAgents != 0 || aiStats.rejectedStimuli != 0 ||
            aiStats.budgetFailures != 0;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "Behavior Tree / Blackboard / Perception",
            constrained ? "Budget Constrained" : "Running", detail.str(),
            constrained ? editor::EditorRuntimeWatchSeverity::Warning
                        : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
        constexpr std::size_t kMaximumAiDebuggerRecords = 64;
        const auto& snapshots = editorProductionAiPipeline_.DebugSnapshots();
        for (std::size_t index = 0;
             index < (std::min)(snapshots.size(), kMaximumAiDebuggerRecords); ++index) {
            const editor::EditorAiAgentDebugSnapshot& snapshot = snapshots[index];
            std::ostringstream agentDetail;
            agentDetail << "behavior=" << snapshot.behaviorAssetGuid
                        << " tick/perception=" << snapshot.tickGeneration
                        << "/" << snapshot.perceptionGeneration
                        << " nodes=" << snapshot.executedNodes
                        << " perceived=" << snapshot.perceived.size()
                        << " pathPoints=" << snapshot.lastPath.size() << " trace=";
            for (std::size_t traceIndex = 0;
                 traceIndex < snapshot.activeNodeTrace.size(); ++traceIndex) {
                if (traceIndex != 0) agentDetail << '>';
                agentDetail << snapshot.activeNodeTrace[traceIndex];
            }
            agentDetail << " blackboard=";
            for (std::size_t keyIndex = 0; keyIndex < snapshot.blackboard.size(); ++keyIndex) {
                if (keyIndex != 0) agentDetail << ',';
                const auto& key = snapshot.blackboard[keyIndex];
                agentDetail << key.name << '=';
                switch (key.defaultValue.type) {
                case editor::EditorBlackboardValueType::Bool:
                    agentDetail << (key.defaultValue.boolValue ? "true" : "false"); break;
                case editor::EditorBlackboardValueType::Int:
                    agentDetail << key.defaultValue.intValue; break;
                case editor::EditorBlackboardValueType::Float:
                    agentDetail << key.defaultValue.floatValue; break;
                case editor::EditorBlackboardValueType::Vector3:
                    agentDetail << '(' << key.defaultValue.vectorValue.x << ' '
                                << key.defaultValue.vectorValue.y << ' '
                                << key.defaultValue.vectorValue.z << ')'; break;
                case editor::EditorBlackboardValueType::Entity:
                case editor::EditorBlackboardValueType::String:
                    agentDetail << key.defaultValue.textValue; break;
                }
            }
            const bool unhealthy = snapshot.status == editor::EditorBehaviorStatus::BudgetExceeded ||
                snapshot.status == editor::EditorBehaviorStatus::InvalidProgram;
            editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
                "AI Agent", snapshot.entityGuid, editor::ToString(snapshot.status),
                agentDetail.str(), unhealthy ? editor::EditorRuntimeWatchSeverity::Warning
                                             : editor::EditorRuntimeWatchSeverity::Info,
                editorDocumentServiceFrame_});
        }
    }
    {
        const editor::EditorProductionAiWorldStats& aiWorldStats =
            editorProductionAiWorldPipeline_.Stats();
        std::ostringstream detail;
        detail << "EQS queries/candidates/tested/rejected=" << aiWorldStats.eqsQueries
               << "/" << aiWorldStats.eqsGeneratedCandidates
               << "/" << aiWorldStats.eqsTestedCandidates
               << "/" << aiWorldStats.eqsRejectedCandidates
               << " nav/visibility=" << aiWorldStats.eqsNavigationQueries
               << "/" << aiWorldStats.eqsVisibilityQueries
               << " programs/reloads=" << aiWorldStats.loadedEqsPrograms
               << "/" << aiWorldStats.eqsHotReloads
               << " programEvict=" << aiWorldStats.evictedEqsPrograms
               << " crowd=" << aiWorldStats.activeCrowdAgents
               << "/" << aiWorldStats.submittedCrowdAgents
               << " avoidance/neighborBudget=" << aiWorldStats.crowdAvoidanceAdjustments
               << "/" << aiWorldStats.crowdNeighborBudgetHits
               << " smartSlots=" << aiWorldStats.activeSmartObjectSlots
               << "/" << aiWorldStats.submittedSmartObjectSlots
               << " reservations(ok/reject/release/expire)="
               << aiWorldStats.successfulReservations << "/"
               << aiWorldStats.rejectedReservations << "/"
               << aiWorldStats.releasedReservations << "/"
               << aiWorldStats.expiredReservations
               << " generation=" << aiWorldStats.worldGeneration;
        const bool constrained = aiWorldStats.eqsBudgetFailures != 0 ||
            aiWorldStats.rejectedCrowdAgents != 0 ||
            aiWorldStats.crowdNeighborBudgetHits != 0 ||
            aiWorldStats.rejectedSmartObjectSlots != 0;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "EQS / Crowd Steering / Smart Objects",
            constrained ? "Budget Constrained" : "Running", detail.str(),
            constrained ? editor::EditorRuntimeWatchSeverity::Warning
                        : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
        constexpr std::size_t kMaximumCrowdDebuggerRecords = 64;
        const auto& crowd = editorProductionAiWorldPipeline_.CrowdSnapshots();
        for (std::size_t index = 0;
             index < (std::min)(crowd.size(), kMaximumCrowdDebuggerRecords); ++index) {
            const auto& agent = crowd[index];
            std::ostringstream agentDetail;
            agentDetail << "position=(" << agent.position.x << ' ' << agent.position.y
                        << ' ' << agent.position.z << ") preferred=("
                        << agent.preferredVelocity.x << ' ' << agent.preferredVelocity.y
                        << ' ' << agent.preferredVelocity.z << ") steering=("
                        << agent.steeringVelocity.x << ' ' << agent.steeringVelocity.y
                        << ' ' << agent.steeringVelocity.z << ") neighbors="
                        << agent.consideredNeighbors << " radius/speed="
                        << agent.radius << "/" << agent.maximumSpeed;
            editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
                "AI Crowd", agent.entityGuid,
                agent.constrained ? "Avoiding" : "Preferred Velocity",
                agentDetail.str(), editor::EditorRuntimeWatchSeverity::Info,
                editorDocumentServiceFrame_});
        }
    }
    {
        const editor::EditorAiAuthoringStats& stats =
            editorProductionAiAuthoringPipeline_.Stats();
        std::ostringstream detail;
        detail << "state="
               << (editorProductionAiAuthoringPipeline_.Replaying() ? "replay" :
                   (editorProductionAiAuthoringPipeline_.Paused() ? "paused" : "running"))
               << " breakpoints/hits="
               << editorProductionAiAuthoringPipeline_.Breakpoints().size()
               << "/" << stats.breakpointHits
               << " live/recorded/dropped=" << stats.liveFrames << "/"
               << stats.recordedFrames << "/" << stats.droppedRecordingFrames
               << " mutations(behavior/eqs/fail)=" << stats.behaviorMutations << "/"
               << stats.eqsMutations << "/" << stats.compileFailures
               << " replaySteps=" << stats.replaySteps
               << " overlay/rejected=" << stats.overlayCommands << "/"
               << stats.overlayBudgetRejected;
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "Authoring / Debugger / Simulation",
            editorProductionAiAuthoringPipeline_.Paused() ? "Paused" : "Active",
            detail.str(), stats.overlayBudgetRejected != 0
                ? editor::EditorRuntimeWatchSeverity::Warning
                : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    {
        const editor::EditorAiValidationStats& stats =
            editorProductionAiValidationPipeline_.Stats();
        const editor::EditorAiValidationReport& report =
            editorProductionAiValidationPipeline_.Report();
        std::ostringstream detail;
        detail << "suites(started/completed/rejected)=" << stats.suitesStarted << "/"
               << stats.suitesCompleted << "/" << stats.rejectedSuites
               << " runs(pass/fail)=" << stats.runsPassed << "/" << stats.runsFailed
               << " frames=" << stats.framesSimulated
               << " budget/determinism=" << stats.budgetFailures << "/"
               << stats.determinismFailures
               << " reports/repro=" << stats.exportedReports << "/"
               << stats.exportedReproductions
               << " comparisons/regressions=" << stats.comparisons << "/"
               << stats.regressions
               << " last=" << (report.generation == 0 ? "none" :
                   (report.passed ? "passed" : "failed"));
        const bool constrained = stats.budgetFailures != 0 ||
            stats.determinismFailures != 0 || stats.regressions != 0 ||
            (report.generation != 0 && !report.passed);
        editorRuntimeInspector_.AddRecord(editor::EditorRuntimeWatchRecord{
            "AI", "Validation / Batch Simulation / Telemetry",
            report.generation == 0 ? "Idle" : (report.passed ? "Passed" : "Failed"),
            detail.str(), constrained ? editor::EditorRuntimeWatchSeverity::Warning
                                      : editor::EditorRuntimeWatchSeverity::Info,
            editorDocumentServiceFrame_});
    }
    const editor::EditorCommandContext editorCommandContext =
        editor::BuildEditorCommandContext(
            editor::EditorCommandContextInput{
                &editorSelection_,
                &editorAssetSelection_,
                &editorPropertyRegistry_,
                &editorPropertyAccessor,
                &editorTransactions,
                &editorPlaySession_,
                showDeveloperTools_});
    editorDocumentLifecycle_.SetServices(
        editor::EditorDocumentLifecycleServices{
            &courseDocumentAdapter,
            &editorDirtyState_,
            &editorConfirmService_,
            &editorNotifications_,
            &editorSaveApplyPolicy,
            &editorDocumentManager_,
            &editorDocumentSaveService_});
    editor::EditorContext editorContext{
        &editorSelection_,
        &editorTransactions,
        &editorAssetRegistry_,
        &editorAssetSelection_,
        &editorAssetThumbnails_,
        &courseDocumentAdapter,
        &editorPropertyRegistry_,
        &editorPropertyAccessor,
        &editorPropertyEditService_,
        &editorValidationReport,
        &editorDirtyState_,
            &editorDocumentLifecycle_,
            &editorDocumentManager_,
            &editorDocumentSaveService_,
            &editorWorldModel_,
            &editorWorldMutationExecution_,
            &editorLayout_,
        &editorPanelLayout_,
        &editorViewportInteraction_,
        &editorViewportCoordinates_,
        &editorViewportSelectionBridge_,
        &editorTransformGizmo_,
        &editorNotifications_,
        &editorConfirmService_,
        &editorSaveApplyPolicy,
        &editorRuntimeInspector_,
        &editorPlaySession_,
        &editorRailRuntimePause_,
        &editorToolRegistry_,
        &editorCommandRegistry,
        &editorCommandContext,
        &editorCommandInputRouter_,
        &editorCommandPalette_,
        showDeveloperTools_};
    editorContext.coursePreviewVisible = context.course != nullptr;
    editorContext.contentDrawer = &editorContentDrawer_;
    editorContext.framePacing = &editorFramePacing_;
    editorContext.viewportOverlay = &editorViewportOverlay_;
    editorContext.viewportRealtimePolicy = &editorViewportRealtimePolicy_;
    editorContext.layoutPersistence = &editorLayoutPersistence_;
    editorContext.worldMutations = &editorWorldMutationService_;
    editorContext.sceneWorldProvider = &editorSceneWorldProvider_;
    editorContext.assetWorkspaceStatus = &editorAssetWorkspaceStatus_;
    editorContext.sequencer = &editorSequencer_;
    editorContext.prefabs = &editorPrefabs_;
    editorContext.materialGraphs = &editorMaterialGraphs_;
    editorContext.vfxGraphs = &editorVfxGraphs_;
    editorContext.animationStateMachines = &editorAnimationStateMachines_;
    editorContext.gameplayVisualScripts = &editorGameplayVisualScripts_;
    editorContext.aiAuthoring = &editorProductionAiAuthoringPipeline_;
    editorContext.navigationAuthoring = &editorProductionNavigationAuthoringPipeline_;
    editorContext.interactiveTools = &editorInteractiveTools_;
    editorContext.interactiveExecution = &editorInteractiveExecution_;
    editorContext.courseRailEditTool = &courseRailViewportEditTool_;
    editorContext.courseRailTransformGizmo = &courseRailTransformGizmo_;
    editorContext.courseEnemyEditorController = context.courseEnemyEditorController;
    editorContext.courseEnemyEditTool = &courseEnemyViewportEditTool_;
    editorContext.courseEnemyTransformGizmo = &courseEnemyTransformGizmo_;
    editorContext.courseEnemyPickingService = context.courseEnemyPickingService;
    editorContext.courseEnemyViewportRenderer = context.courseEnemyViewportRenderer;
    editorContext.courseWaveEditorController = context.courseWaveEditorController;
    editorContext.courseWavePickingService = context.courseWavePickingService;
    editorContext.courseWaveViewportRenderer = context.courseWaveViewportRenderer;
    editorContext.coursePreviewSimulationSystem =
        context.coursePreviewSimulationSystem;
    editorContext.coursePreviewActorRuntimeBridge =
        context.coursePreviewActorRuntimeBridge;
    auto onBlenderSceneChanged =
        [this](const editor::EditorDocumentId& document, std::string_view) {
            if (document.type == editor::EditorDocumentTypes::Scene) {
                editorSceneDocumentId_ = document;
                editorSceneWorldProvider_.Bind(
                    editorSceneDocumentProvider_.Scene(document),
                    document,
                    &editorSceneComponentRegistry_,
                    &editorGimmickDefinitionRegistry_);
            }
            editorWorldRefreshRevisionGate_.Invalidate();
            editorSelection_.Clear();
        };
    editor::EditorBlenderSceneImportExecutionService
        blenderSceneImportExecution{
            editorSceneDocumentProvider_,
            &editorDocumentManager_,
            &editorDirtyState_,
            onBlenderSceneChanged};
    editor::EditorBlenderSceneImportWorkflow blenderSceneImportWorkflow{
        editorSceneDocumentProvider_,
        editorTransactions,
        &editorAssetRegistry_,
        &editorDocumentManager_,
        &editorDirtyState_,
        onBlenderSceneChanged};
    editorContext.blenderSceneImportExecution =
        &blenderSceneImportExecution;
    editorContext.blenderSceneImportWorkflow =
        &blenderSceneImportWorkflow;
    editorContext.selectBlenderLevelJsonFile = [this]() {
        return OpenBlenderLevelJsonDialog(hwnd_);
    };
    editorContext.onWorldMutated = [&](const editor::EditorWorldMutationResult& result) {
        if (result.document.type == editor::EditorDocumentTypes::Course) {
            ++runtimeState.terrain.courseObjectEditRevision;
        }
        editorWorldRefreshRevisionGate_.Invalidate();
        editorDirtyState_.MarkDirty(
            result.document.type == editor::EditorDocumentTypes::Course
                ? editor::EditorDirtyDomain::CourseAuthoring
                : editor::EditorDirtyDomain::Unknown,
            "world:" + result.document.assetGuid,
            result.document.type + " World",
            result.message,
            editorDirtyState_.Revision() + 1);
        if (editor::EditorDocumentRecord* document = editorDocumentManager_.Find(result.document)) {
            editorDocumentManager_.MarkDirty(document->id, result.message);
        }
    };

    editor::EditorInteractiveToolEnvironment interactiveEnvironment{};
    interactiveEnvironment.selection = &editorSelection_;
    interactiveEnvironment.viewport = &editorViewportInteraction_;
    interactiveEnvironment.coordinates = &editorViewportCoordinates_;
    interactiveEnvironment.execution = &editorInteractiveExecution_;
    interactiveEnvironment.selectionRevision = editorSelection_.Revision();
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active()) {
        interactiveEnvironment.activeDocumentKey = activeDocument->id.Key();
        interactiveEnvironment.documentEditRevision = activeDocument->editRevision;
        interactiveEnvironment.documentGeneration = activeDocument->contentGeneration;
    }
    interactiveEnvironment.playSessionActive = editorPlaySession_.IsActive();
    interactiveEnvironment.canMutateAuthoring = editorCommandContext.canMutateAuthoring;
    interactiveEnvironment.viewportAvailable = editorViewportInteraction_.ViewportAvailable();
    editor::EditorInteractiveToolFrameInput interactiveInput{};
    interactiveInput.mouseX = ImGui::GetIO().MousePos.x;
    interactiveInput.mouseY = ImGui::GetIO().MousePos.y;
    interactiveInput.viewportPrimaryPressed =
        !courseRailViewportEditTool_.Active() && !courseEnemyViewportEditTool_.Active() &&
        editorViewportInteraction_.CanUseInteractiveToolInput() &&
        editorViewportInteraction_.State().viewportPrimaryPressed;
    interactiveInput.viewportPrimaryDown =
        !courseRailViewportEditTool_.Active() && !courseEnemyViewportEditTool_.Active() &&
        editorViewportInteraction_.CanUseInteractiveToolInput() &&
        editorViewportInteraction_.State().viewportPrimaryDown;
    interactiveInput.viewportPrimaryReleased =
        !courseRailViewportEditTool_.Active() && !courseEnemyViewportEditTool_.Active() &&
        editorViewportInteraction_.CanUseInteractiveToolInput() &&
        editorViewportInteraction_.State().viewportPrimaryReleased;
    interactiveInput.viewportPrimaryCancelled =
        editorViewportInteraction_.State().primaryCaptureCancelled;
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
            !cameraCancelRequested &&
            !contentDrawerEscapeRequested) {
            editorInteractiveTools_.RequestCancel();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) editorInteractiveTools_.RequestAccept();
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_1)) {
            editorInteractiveTools_.ActivateMode("editor.mode.select");
        }
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_2)) {
            editorInteractiveTools_.ActivateMode("editor.mode.inspect");
        }
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_3)) {
            editorInteractiveTools_.ActivateMode("editor.mode.place");
        }
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_4)) {
            editorInteractiveTools_.ActivateMode("editor.mode.terrain");
        }
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_5)) {
            editorInteractiveTools_.ActivateMode("editor.mode.modeling");
        }
        if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_6)) {
            editorInteractiveTools_.ActivateMode("editor.mode.paths");
        }
    }
    editorInteractiveTools_.Tick(
        interactiveEnvironment, interactiveInput, editorTransactions);
    AppRuntimeState* runtimeStateForClose = &runtimeState;
    auto closeCourseDocument = [this, runtimeStateForClose]() {
        editorCourseDocumentOpen_ = false;
        if (editor::EditorDocumentRecord* document =
                editorDocumentManager_.FindByPath(editorCourseDocumentPath_)) {
            editorDocumentManager_.Close(document->id, true, nullptr);
        }
        if (runtimeStateForClose != nullptr) {
            runtimeStateForClose->terrain.selectedCourseTerrainPlacement = -1;
            runtimeStateForClose->terrain.selectedCourseRockCluster = -1;
            runtimeStateForClose->terrain.courseObjectSelectionType = 0;
        }
        editorSelection_.Clear();
        if (const editor::EditorAssetHandle* selectedAsset = editorAssetSelection_.Primary()) {
            if (selectedAsset->kind == editor::EditorAssetKind::Course) {
                editorAssetSelection_.Clear();
            }
        }
    };
    auto reopenCourseDocument = [this]() {
        editorCourseDocumentOpen_ = true;
        if (editor::EditorDocumentRecord* document =
                editorDocumentManager_.FindByPath(editorCourseDocumentPath_)) {
            editorDocumentManager_.Reopen(document->id, nullptr);
        }
        editorSelection_.Clear();
    };
    editor::RunAppEditorCommandToolPipeline(
        editor::AppEditorCommandToolModuleInput{
            &editorContext,
            &context,
            &runtimeState,
            &editorPlaySessionLifecycle_,
            &editorPlaySessionRuntimeControl_,
            &editorRuntimeAuthoringApply_,
            &editorPlaySessionSnapshot_,
            std::move(closeCourseDocument),
            std::move(reopenCourseDocument)});
    editorCommandInputRouter_.Dispatch(
        editorContext,
        editor::EditorCommandInputRouterOptions{
            showDeveloperTools_,
            true});
    if (editorContentDrawer_.IsVisible()) {
        editorContentBrowserMaximized_ = false;
    }
    editorCommandPalette_.Draw(editorContext);
    editor::DrawEditorMenuBar(editorContext);
    editor::DrawEditorToolbar(editorContext);
    editor::DrawEditorDocumentTabs(editorContext);
    editor::DrawEditorStatusBar(editorContext);
    editor::DrawEditorNotificationToast(
        editorNotifications_, editorNotificationToastState_);
    editor::DrawEditorModalConfirmPanel(editorConfirmService_);

    const AppVfxDebugDataBuilderContext vfxDebugDataContext{
        context.appPipelines,
        context.renderResources,
        context.scene,
        context.gpuParticleSystem,
        context.frameState,
        context.srvDescriptorHeap,
        context.vfxTextureHandle,
        context.depthTextureHandle,
        context.renderPassDebugInfo
    };
    editorPanelRegistry_.Clear();
    if (editorLayoutPersistence_.ActivePanel(
            editor::EditorPanelHostArea::LeftSidebar).empty()) {
        editorLayoutPersistence_.SetActivePanel(
            editor::EditorPanelHostArea::LeftSidebar,
            "editor.worldOutliner");
    }
    const std::string activeRightInspector = editorLayoutPersistence_.ActivePanel(
        editor::EditorPanelHostArea::RightInspector);
    if (activeRightInspector.empty()) {
        editorLayoutPersistence_.SetActivePanel(
            editor::EditorPanelHostArea::RightInspector,
            "editor.details");
    } else if (activeRightInspector == "vfx.inspector") {
        editorLayoutPersistence_.SetActivePanelFromUser(
            editor::EditorPanelHostArea::RightInspector,
            "editor.details");
    } else if (activeRightInspector == "editor.performance") {
        editorLayoutPersistence_.SetActivePanelFromUser(
            editor::EditorPanelHostArea::RightInspector,
            "editor.details");
    }
    if (editorLayoutPersistence_.ActivePanel(
            editor::EditorPanelHostArea::BottomDock).empty()) {
        editorLayoutPersistence_.SetActivePanel(
            editor::EditorPanelHostArea::BottomDock,
            "editor.diagnostics");
    }
    const auto panelVisible = [this](const char* panelId, bool fallback = true) {
        return editorLayoutPersistence_.IsPanelVisible(panelId, fallback);
    };
    const auto registerPanel =
        [this, &editorValidationReport](editor::EditorPanelDescriptor descriptor) {
        if (descriptor.id == "editor.performance") {
            descriptor.area = editor::EditorPanelHostArea::BottomDock;
        }
        if (descriptor.area == editor::EditorPanelHostArea::BottomDock) {
            if (descriptor.id == "vfx.runtimeStatus" ||
                descriptor.id == "vfx.runtimeQueues" ||
                descriptor.id == "render.graph" ||
                descriptor.id == "editor.performance") {
                descriptor.bottomDockGroup = editor::EditorBottomDockGroup::Profiling;
            } else if (descriptor.id == "course.timeline" ||
                       descriptor.id == "course.overviewMap" ||
                       descriptor.id == "editor.transactions" ||
                       descriptor.id == "material.graph") {
                descriptor.bottomDockGroup = editor::EditorBottomDockGroup::Authoring;
            } else if (descriptor.id == "editor.featureGuard" ||
                       descriptor.id == "editor.properties" ||
                       descriptor.id == "gameplay.railLockOn") {
                descriptor.bottomDockGroup = editor::EditorBottomDockGroup::Developer;
            } else {
                descriptor.bottomDockGroup = editor::EditorBottomDockGroup::Output;
            }
        }
        if (descriptor.id == "editor.diagnostics") {
            descriptor.badge = [&editorValidationReport]() {
                return editor::EditorPanelBadge{
                    editorValidationReport.warningCount,
                    editorValidationReport.errorCount};
            };
        } else if (descriptor.id == "editor.notifications") {
            descriptor.badge = [this]() {
                editor::EditorPanelBadge badge{};
                for (const editor::EditorNotification& notification :
                     editorNotifications_.Notifications()) {
                    if (notification.severity == editor::EditorNotificationSeverity::Error) {
                        ++badge.errorCount;
                    } else if (notification.severity ==
                               editor::EditorNotificationSeverity::Warning) {
                        ++badge.warningCount;
                    }
                }
                return badge;
            };
        }
        return editorToolRegistry_.RegisterPanel(
                editor::EditorPanelRegistrationDescriptor{
                    {},
                    descriptor.id,
                    std::move(descriptor),
                    true},
                editorPanelRegistry_);
    };
    if (context.onDrawRailLockOnDebugPanel) {
        registerPanel(
            editor::EditorPanelDescriptor{
                "gameplay.railLockOn",
                "Rail Lock-On",
                "Gameplay",
                editor::EditorPanelHostArea::BottomDock,
                panelVisible("gameplay.railLockOn"),
                context.onDrawRailLockOnDebugPanel});
    }
    const AppVfxRuntimeStatusPanelInput runtimeStatusInput{
        &runtimeState,
        &effectRuntime,
        &vfxDebugDataContext,
        &trailMeshStreamProbeHealthyFrames_,
        &trailMeshStreamActiveHealthyFrames_,
        &trailMeshStreamHealthFrames_,
        &trailMeshStreamStartupTelemetryFrames_,
        &particleDedicatedProbeTelemetryFrames_,
        &particleDedicatedHealthFrames_,
        &particleDedicatedProbeStableFrames_,
        &particleDedicatedActiveStableFrames_,
        &distortionDedicatedHealthFrames_,
        &distortionDedicatedTelemetryFrames_,
        &distortionDedicatedStableFrames_,
        &distortionDedicatedActiveStableFrames_,
        &beamDedicatedTelemetryFrames_,
        &beamDedicatedHealthFrames_,
        &beamDedicatedStableFrames_,
        &beamDedicatedActiveStableFrames_};

    if (!context.railGameplayScene && !showcasePresentationInitialized_) {
        showcasePresentationInitialized_ = true;
        runtimeState.vfx.showcaseAutoRotate = false;
        runtimeState.vfx.showcaseHudVisible = true;
        runtimeState.vfx.showcaseTuningVisible = false;
        // The submission scene owns its startup VFX configuration. Clearing the
        // preview showcase here used to undo handParticleAttachment.enabled and
        // vfx.enableParticles immediately after scene entry, so the persistent
        // right-hand emitter never reached the GPU Particle render pass.
        if (!runtimeState.submissionShowcase.enabled) {
            ClearShowcaseEffectState(runtimeState, effectRuntime);
        }
        ConfigureShowcasePostProcess(postProcessStack, runtimeState.vfx);
    }

    if (viewportFocusMode_) {
        if (NeedsVfxRuntimeStatusTelemetry(runtimeState.vfx, hiddenRuntimeTelemetryFrame_++)) {
            UpdateVfxRuntimeStatusTelemetry(runtimeStatusInput);
        }
        DrawViewportFocusStatusBar(viewportFocusMode_);
        imguiTiming.buildUiMs = EditorUiElapsedMs(buildUiStart, EditorUiTimingClock::now());
        LogEditorImguiBreakdown(imguiTiming);
        return;
    }

    if (!context.railGameplayScene) {
        if (runtimeState.submissionShowcase.enabled) {
            DrawSubmissionShowcasePanel(runtimeState, showDeveloperTools_);
        } else {
            DrawShowcasePresentationPanel(
                runtimeState,
                effectRuntime,
                postProcessStack,
                showDeveloperTools_,
                showcaseLoopCurrent_);
        }
    }
    if (!showDeveloperTools_) {
        if (NeedsVfxRuntimeStatusTelemetry(runtimeState.vfx, hiddenRuntimeTelemetryFrame_++)) {
            UpdateVfxRuntimeStatusTelemetry(runtimeStatusInput);
        }
        imguiTiming.buildUiMs = EditorUiElapsedMs(buildUiStart, EditorUiTimingClock::now());
        LogEditorImguiBreakdown(imguiTiming);
        return;
    }

    const auto panelRegistryStart = EditorUiTimingClock::now();
    const D3D12_GPU_DESCRIPTOR_HANDLE editorViewportPreview =
        context.postColorPreview.ptr != 0 ? context.postColorPreview : context.sceneColorPreview;
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.viewport",
            "Viewport",
            "Editor",
            editor::EditorPanelHostArea::Viewport,
            panelVisible("editor.viewport"),
            [&]() {
                editor::DrawEditorViewportPanelContent(
                    editorContext,
                    editorPanelLayout_.ViewportRect(),
                    editor::EditorViewportPanelRenderInput{
                        editorViewportPreview.ptr,
                        static_cast<float>(editorViewportTarget.renderWidth),
                        static_cast<float>(editorViewportTarget.renderHeight),
                        true,
                        context.onBuildEditorViewportOverlay});
            }});

    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.workspace",
            "Workspace",
            "Editor",
            editor::EditorPanelHostArea::LeftSidebar,
            panelVisible("editor.workspace"),
            [&]() {
                DrawEditorWorkspacePanel(
                    editorLayoutPersistence_, courseMapEditorWorkspace_);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.toolPalette",
            "Tool Palette",
            "Editor",
            editor::EditorPanelHostArea::LeftSidebar,
            panelVisible("editor.toolPalette"),
            [&]() { editor::DrawEditorModePalettePanel(editorContext); }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.details",
            "Details",
            "Editor",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("editor.details"),
            [&]() {
                if (courseCameraShotDetailsPanel_.HandlesSelection(
                        &editorSequencer_, &editorCourseSequencerProvider_)) {
                    courseCameraShotDetailsPanel_.Draw(
                        editor::CourseCameraShotDetailsPanelContext{
                            editableCourse,
                            &editorCourseSequencerProvider_,
                            &editorSequencer_,
                            context.courseRailLength,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                if (courseRideProfileDetailsPanel_.HandlesSelection(
                        &editorSequencer_, &editorCourseRideSequencerBridge_)) {
                    courseRideProfileDetailsPanel_.Draw(
                        editor::CourseRideProfileDetailsPanelContext{
                            editableCourse,
                            &editorCourseRideSequencerBridge_,
                            &editorSequencer_,
                            context.courseRailLength,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                if (editorSelection_.Empty() &&
                    context.courseWaveEditorController != nullptr &&
                    context.courseWaveEditorController->State().bound) {
                    courseWaveDetailsPanel_.Draw(
                        editor::CourseWaveDetailsPanelContext{
                            context.courseWaveEditorController,
                            &editorSelection_,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                if (courseWaveDetailsPanel_.HandlesSelection(&editorSelection_)) {
                    courseWaveDetailsPanel_.Draw(
                        editor::CourseWaveDetailsPanelContext{
                            context.courseWaveEditorController,
                            &editorSelection_,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                if (courseEnemyDetailsPanel_.HandlesSelection(&editorSelection_)) {
                    courseEnemyDetailsPanel_.Draw(
                        editor::CourseEnemyDetailsPanelContext{
                            context.courseEnemyEditorController,
                            &editorSelection_,
                            &courseEnemyTransformGizmo_,
                            &courseEnemyViewportEditTool_,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                if (courseRailDetailsPanel_.HandlesSelection(&editorSelection_)) {
                    courseRailDetailsPanel_.Draw(
                        editor::CourseRailDetailsPanelContext{
                            context.courseRailEditorController,
                            &editorSelection_,
                            &courseRailTransformGizmo_,
                            &courseRailViewportEditTool_,
                            editorCommandContext.canMutateAuthoring &&
                                app::kRuntimeAuthoringEnabled});
                    return;
                }
                editor::DrawEditorDetailsPanel(
                    editor::EditorDetailsPanelContext{
                        &editorSelection_,
                        &editorSceneComponentRegistry_,
                        &editorGimmickDefinitionRegistry_,
                        editorGimmickRuntimeWorld_,
                        editorGimmickRuntimeAdapter_,
                        editorGimmickRuntimeEventRouter_,
                        editorGimmickRuntimeInteraction_,
                        editorGimmickRuntimeTriggers_,
                        &editorPropertyRegistry_,
                        &editorPropertyAccessor,
                        &editorPreviewPropertyAccessor,
                        &editorDetailsEditSession_,
                        &editorTransactions,
                        &editorDirtyState_,
                        &editorNotifications_,
                        &editorPropertyClipboard_,
                        &editorAssetRegistry_,
                        &editorAssetSelection_,
                        &editorValidationReport,
                        &editorDetailsSectionProviders_,
                        &editorDetailsViewState_,
                        &editorPrefabs_,
                        &editorPrefabs_,
                        &editorWorldModel_,
                        editorCommandContext.canMutateAuthoring,
                        &editorWorldMutationService_,
                        &editorSceneWorldProvider_,
                        editorContext.onWorldMutated,
                        &editorProductionScenePipeline_});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.toolProperties",
            "Tool Properties",
            "Editor",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("editor.toolProperties"),
            [&]() { editor::DrawEditorToolPropertiesPanel(editorContext); }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.details",
            "VFX Details",
            "VFX",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("vfx.details"),
            [&]() {
                if (ImGui::Button("Viewport Focus")) {
                    viewportFocusMode_ = true;
                }
                ImGui::Separator();
                DrawEffectInstancePanel(
                    EffectInstancePanelInput{
                        &effectRuntime,
                        &selectedEffectInstanceId_,
                        context.loadedEffectAssets,
                        context.effectAuthoringRegistry});
                ImGui::Separator();
                DrawMaterialSettingsControlsPanel(runtimeState, context.onAddParticle);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.runtimeInspector",
            "VFX Runtime",
            "VFX",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("vfx.runtimeInspector"),
            [&]() {
                DrawVfxRuntimeControlsPanel(
                    VfxRuntimeControlsPanelInput{
                        &runtimeState,
                        &effectRuntime,
                        &trailMeshStreamStartupTelemetryFrames_});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "scene.lighting",
            "Scene Lighting",
            "Scene",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("scene.lighting"),
            [&]() { DrawSceneLightingControlsPanel(runtimeState); }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "terrain.pbrMaterials",
            "Terrain PBR Materials",
            "Terrain",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("terrain.pbrMaterials"),
            [&]() {
                DrawTerrainPbrMaterialsPanel(
                    terrainPbrMaterialsPanelState_,
                    TerrainPbrMaterialsPanelContext{
                        context.scene,
                        &editorNotifications_,
                        hwnd_,
                        editorCommandContext.canMutateAuthoring});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "postprocess.inspector",
            "Post Process",
            "Render",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("postprocess.inspector"),
            [&]() { DrawPostProcessPanel(postProcessStack); }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "render.debugViews",
            "Render Debug Views",
            "Render",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("render.debugViews"),
            [&]() { DrawDebugViewsPanel(runtimeState); }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.performance",
            "Performance",
            "Editor",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("editor.performance"),
            [&]() {
                DrawSkinningTimingPanel(runtimeState);
                ImGui::Separator();
                ImGui::TextDisabled("GPU/CPU timing and VFX runtime telemetry are read-only in this panel.");
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.runtimeWatch",
            "Runtime Watch",
            "Editor",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("editor.runtimeWatch"),
            [&]() {
                editor::DrawEditorRuntimeInspectorPanel(editorRuntimeInspector_);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.runtimeChanges",
            "Runtime Changes",
            "Editor",
            editor::EditorPanelHostArea::RightInspector,
            panelVisible("editor.runtimeChanges"),
            [&]() {
                editor::DrawEditorRuntimeChangeSetPanel(
                    editorPlaySessionSnapshot_,
                    editorPlaySession_,
                    context.course,
                    &runtimeState,
                    &effectRuntime,
                    &postProcessStack,
                    editorCommandRegistry);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.worldOutliner",
            "World Outliner",
            "Editor",
            editor::EditorPanelHostArea::LeftSidebar,
            panelVisible("editor.worldOutliner"),
            [&]() {
                editor::DrawEditorWorldOutlinerPanel(
                    editorWorldOutlinerState_,
                    editor::EditorWorldOutlinerPanelContext{
                        &editorWorldModel_,
                        &editorSelection_,
                        &editorWorldMutationService_,
                        &editorTransactions,
                        &editorNotifications_,
                        editorCommandContext.canMutateAuthoring,
                        [&](const editor::EditorWorldMutationResult& result) {
                            if (result.document.type == editor::EditorDocumentTypes::Course) {
                                ++runtimeState.terrain.courseObjectEditRevision;
                            }
                            editorWorldRefreshRevisionGate_.Invalidate();
                            editorDirtyState_.MarkDirty(
                                result.document.type == editor::EditorDocumentTypes::Course
                                    ? editor::EditorDirtyDomain::CourseAuthoring
                                    : editor::EditorDirtyDomain::Unknown,
                                "world:" + result.document.assetGuid,
                                result.document.type + " World",
                                result.message,
                                editorDirtyState_.Revision() + 1);
                            if (editor::EditorDocumentRecord* document =
                                    editorDocumentManager_.Find(result.document)) {
                                editorDocumentManager_.MarkDirty(document->id, result.message);
                            }
                        },
                        [&](const editor::EditorObjectHandle& handle) {
                            if (handle.domain == editor::EditorDomainId::CourseTerrainPlacement) {
                                runtimeState.terrain.selectedCourseTerrainPlacement =
                                    static_cast<int>(handle.localIndex);
                                runtimeState.terrain.selectedCourseRockCluster = -1;
                                runtimeState.terrain.courseObjectSelectionType = 1;
                            } else if (handle.domain == editor::EditorDomainId::CourseRockCluster) {
                                runtimeState.terrain.selectedCourseTerrainPlacement = -1;
                                runtimeState.terrain.selectedCourseRockCluster =
                                    static_cast<int>(handle.localIndex);
                                runtimeState.terrain.courseObjectSelectionType = 2;
                            } else if (handle.domain == editor::EditorDomainId::VfxEffectInstance) {
                                if (const editor::EditorWorldObjectRecord* record =
                                        editorWorldModel_.Resolve(handle)) {
                                    constexpr std::string_view prefix = "instance-";
                                    if (record->objectGuid.rfind(prefix, 0) == 0) {
                                        selectedEffectInstanceId_ = static_cast<uint32_t>(
                                            std::stoul(record->objectGuid.substr(prefix.size())));
                                    }
                                }
                            } else {
                                runtimeState.terrain.selectedCourseTerrainPlacement = -1;
                                runtimeState.terrain.selectedCourseRockCluster = -1;
                                runtimeState.terrain.courseObjectSelectionType = 0;
                                selectedEffectInstanceId_ = 0;
                                editorViewportSelectionBridge_.SuppressNextRequest();
                            }
                        }});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.selection",
            "Selection",
            "Editor",
            editor::EditorPanelHostArea::LeftSidebar,
            panelVisible("editor.selection"),
            [&]() {
                editor::DrawEditorSelectionPanel(editorSelection_, productionSelectionTargets);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.commands",
            "Commands",
            "Editor",
            editor::EditorPanelHostArea::LeftSidebar,
            panelVisible("editor.commands"),
            [&]() {
                editor::DrawEditorCommandPanel(editorContext);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.assets",
            "Assets",
            "Editor",
            editor::EditorPanelHostArea::ContentBrowser,
            panelVisible("editor.assets"),
            [&]() {
                editor::DrawEditorAssetBrowserPanel(
                    editor::EditorAssetBrowserPanelContext{
                        &editorAssetRegistry_,
                        &editorAssetSelection_,
                        &editorAssetThumbnails_,
                        &editorTransactions,
                        &editorNotifications_,
                        &editorContentBrowserState_,
                        &editorAssetWorkspaceStatus_,
                        hwnd_,
                        &pendingExternalAssetImportPaths_,
                        &editorProductionMeshRuntimeCache_});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.effectAssets",
            "Effect Assets",
            "VFX",
            editor::EditorPanelHostArea::ContentBrowser,
            panelVisible("vfx.effectAssets"),
            [&]() {
                DrawEffectAssetEditorPanel(
                    effectRuntime,
                    context.effectAuthoringRegistry,
                    context.loadedEffectAssets);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "render.targets",
            "Render Targets",
            "Render",
            editor::EditorPanelHostArea::ContentBrowser,
            panelVisible("render.targets"),
            [&]() {
                DrawRenderTargetPreviewPanel(
                    RenderTargetPreviewPanelInput{
                        context.sceneColorPreview,
                        context.vfxAccumulationPreview,
                        context.postColorPreview,
                        context.depthPreview,
                        context.emissivePreview,
                        context.terrainHiZPreview,
                        context.scene != nullptr ? context.scene->cascadeShadowSrvGpuHandles : std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4>{},
                        runtimeState.terrain.shadowDebugCascade,
                        runtimeState.terrain.showShadowDebugView});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.featureGuard",
            "Feature Guard",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.featureGuard"),
            [&]() {
                const editor::ExistingFeatureProtectionReport protectionReport =
                    editor::BuildExistingFeatureProtectionReport(
                        editor::ExistingFeatureProtectionInput{
                            &runtimeState,
                            &effectRuntime,
                            &editorPropertyRegistry_,
                            &coursePropertyAdapter,
                            &editorPropertyEditService_,
                            &editorAssetRegistry_,
                            &editorAssetSelection_,
                            &courseDocumentAdapter,
                            &editorRuntimeInspector_,
                            &editorPlaySession_,
                            &editorRailRuntimePause_,
                            &editorSelection_,
                            &editorTransactions,
                            &editorValidationReport,
                            &editorDirtyState_,
                            &editorDocumentLifecycle_,
                            &editorLayout_,
                            &editorLayoutPersistence_,
                            &editorPanelLayout_,
                            &editorPanelRegistry_,
                            &editorViewportInteraction_,
                            &editorViewportSelectionBridge_,
                            &editorTransformGizmo_,
                            &editorNotifications_,
                            &editorConfirmService_,
                            &editorSaveApplyPolicy,
                            context.loadedEffectAssets,
                            context.renderGraphDescription,
                            context.renderGraphError,
                            context.renderPassDebugInfo,
                            editableCourse,
                            context.courseLoadStatus,
                            context.coursePath,
                            context.courseRailLength,
                            static_cast<bool>(context.onSaveCourse),
                            static_cast<bool>(context.onApplyCourse),
                            static_cast<bool>(context.onReloadCourse),
                            static_cast<bool>(context.onTeleportCourseToDistance),
                            true,
                            &editorAssetThumbnails_});
                editor::DrawExistingFeatureProtectionPanel(protectionReport);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.diagnostics",
            "Diagnostics",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.diagnostics"),
            [&]() {
                editor::DrawEditorDiagnosticsPanel(
                    editor::EditorDiagnosticsPanelContext{
                        &editorValidationReport,
                        &editorSelection_,
                        &editorAssetRegistry_,
                        &editorAssetSelection_,
                        &editorWorldModel_});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.notifications",
            "Notifications",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.notifications"),
            [&]() {
                editor::DrawEditorNotificationsPanel(editorNotifications_);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.properties",
            "Properties",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.properties"),
            [&]() {
                editor::DrawEditorPropertyRegistryPanel(editorPropertyRegistry_, &editorTransactions);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "material.graph",
            "Material Graph",
            "Material",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("material.graph"),
            [&]() {
                editor::DrawEditorMaterialGraphPanel(
                    editor::EditorMaterialGraphPanelContext{
                        &editorMaterialGraphs_,
                        &editorDocumentManager_,
                        &editorAssetSelection_,
                        &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.graph",
            "Advanced VFX Graph",
            "VFX",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("vfx.graph"),
            [&]() {
                editor::DrawEditorVfxGraphPanel(
                    editor::EditorVfxGraphPanelContext{
                        &editorVfxGraphs_,
                        &editorDocumentManager_,
                        &editorAssetSelection_,
                        &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "animation.stateMachine",
            "Animation State Machine",
            "Animation",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("animation.stateMachine"),
            [&]() {
                editor::DrawEditorAnimationStateMachinePanel(
                    editor::EditorAnimationStateMachinePanelContext{
                        &editorAnimationStateMachines_, &editorDocumentManager_,
                        &editorAssetSelection_, &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "gameplay.visualScript",
            "Gameplay Visual Script",
            "Gameplay",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("gameplay.visualScript"),
            [&]() {
                editor::DrawEditorGameplayVisualScriptPanel(
                    editor::EditorGameplayVisualScriptPanelContext{
                        &editorGameplayVisualScripts_, &editorDocumentManager_,
                        &editorAssetSelection_, &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "navigation.productionAuthoring",
            "Navigation Authoring",
            "Navigation",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("navigation.productionAuthoring"),
            [&]() {
                editor::DrawEditorProductionNavigationAuthoringPanel(
                    editor::EditorProductionNavigationAuthoringPanelContext{
                        &editorProductionNavigationAuthoringPipeline_, &editorDocumentManager_,
                        &editorAssetSelection_, &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            },
            editor::EditorBottomDockGroup::Authoring});
    registerPanel(
        editor::EditorPanelDescriptor{
            "ai.productionAuthoring",
            "AI Authoring / Debugger",
            "AI",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("ai.productionAuthoring"),
            [&]() {
                editor::DrawEditorProductionAiAuthoringPanel(
                    editor::EditorProductionAiAuthoringPanelContext{
                        &editorProductionAiAuthoringPipeline_, &editorDocumentManager_,
                        &editorAssetSelection_, &editorNotifications_,
                        editorCommandContext.canMutateAuthoring});
            },
            editor::EditorBottomDockGroup::Authoring});
    registerPanel(
        editor::EditorPanelDescriptor{
            "ai.productionValidation",
            "AI Validation / Batch",
            "AI",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("ai.productionValidation"),
            [&]() {
                editor::DrawEditorProductionAiValidationPanel(
                    editor::EditorProductionAiValidationPanelContext{
                        &editorProductionAiValidationPipeline_,
                        &editorProductionAiAuthoringPipeline_, &editorNotifications_});
            },
            editor::EditorBottomDockGroup::Profiling});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.fontSettings",
            "Editor Fonts",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.fontSettings"),
            [&]() {
                editor::DrawEditorFontSettingsPanel(editorFonts_, &editorNotifications_);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "editor.transactions",
            "Transactions",
            "Editor",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("editor.transactions"),
            [&]() {
                editor::DrawEditorTransactionPanel(editorTransactions);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.runtimeStatus",
            "Runtime Status",
            "VFX",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("vfx.runtimeStatus"),
            [&]() {
                DrawVfxRuntimeStatusPanel(runtimeStatusInput);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "vfx.runtimeQueues",
            "Runtime Queues",
            "VFX",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("vfx.runtimeQueues"),
            [&]() {
                DrawVfxRuntimeQueuesPanel(
                    VfxRuntimeQueuesPanelInput{
                        &runtimeState,
                        &effectRuntime,
                        &vfxDebugDataContext,
                        &particleDedicatedProbeTelemetryFrames_,
                        trailMeshStreamHealthFrames_,
                        trailMeshStreamProbeHealthyFrames_,
                        trailMeshStreamActiveHealthyFrames_});
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "render.graph",
            "RenderGraph",
            "Render",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("render.graph"),
            [&]() {
                DrawRenderGraphDebugPanel(
                    RenderGraphDebugPanelInput{
                        context.renderGraphDescription,
                        context.renderGraphError,
                        context.renderPassDebugInfo,
                        context.transientTargetCount,
                        context.transientTargetStorageCount,
                        context.transientBufferCount,
                        context.transientBufferStorageCount});
            }});
    const editor::CourseOverviewMapPanelContext courseMapPanelContext{
        context.courseRailEditorController,
        context.courseEnemyEditorController,
        context.courseWaveEditorController,
        &editorSelection_,
        context.coursePreviewSimulationSystem,
        &courseMap3DViewportController_,
        &courseOverviewMapEditTool_,
        &courseOverviewMapDragDropBridge_,
        &courseOverviewMapSnapService_,
        &courseRailCurveFitService_,
        &courseRailSketchTool_,
        &courseRailStrokePreviewRenderer_,
        &courseOverviewMapMultiViewCoordinator_,
        &courseRailElevationProfileEditor_,
        &courseRailConstraintValidationSystem_,
        &editorAssetRegistry_,
        &editorAssetSelection_,
        context.courseDistance,
        &courseMapSceneBounds_,
        &courseMapVisualBake_,
        &courseMapCartographyBake_,
        &courseMapCartographyRenderer_,
        &courseTerrainMapBake_,
        &courseTerrainMapRenderer_,
        &runtimeState.terrain.settings,
        &courseMapHologramRenderer_,
        &courseMapHybridCompositor_,
        &courseMapSceneVisualization_,
        ActiveEditorScene()};
    registerPanel(
        editor::EditorPanelDescriptor{
            "course.overviewMap",
            "Course Overview Map",
            "Course",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("course.overviewMap"),
            [&]() {
                courseMapEditorMajorTab_.DrawCompactEntry(
                    courseMapEditorWorkspace_);
                if (!courseMapEditorWorkspace_.IsOpen()) {
                    ImGui::Separator();
                    editor::DrawCourseOverviewMapPanel(
                        courseOverviewMapController_, courseMapPanelContext);
                }
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "course.rideTuningTelemetry",
            "Ride Tuning Telemetry",
            "Course",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("course.rideTuningTelemetry"),
            [&]() {
                railRideTuningTelemetryPanel_.Draw(
                    context.railRideTuningTelemetry);
            }});
    registerPanel(
        editor::EditorPanelDescriptor{
            "course.timeline",
            "Course Timeline",
            "Course",
            editor::EditorPanelHostArea::BottomDock,
            panelVisible("course.timeline"),
            [&]() {
                DrawCourseTimelineDebugPanel(
                    CourseTimelineDebugPanelInput{
                        editableCourse,
                        context.courseSpawnRuntime,
                        context.courseCollisionSystem,
                        context.courseCheckpointSystem,
                        context.playerCombatFeelSystem,
                        context.runtimeState,
                        context.courseLoadStatus,
                        context.coursePath,
                        context.courseDistance,
                        context.courseRailLength,
                        context.onSaveCourse,
                        context.onApplyCourse,
                        context.onReloadCourse,
                        context.onTeleportCourseToDistance,
                        &editorTransactions,
                        &editorDirtyState_,
                        &editorDocumentLifecycle_,
                        &editorConfirmService_,
                        editorCommandContext.canMutateAuthoring &&
                            !coursePreviewSimulationActive,
                        &editorSequencer_,
                        context.coursePreviewSimulationSystem,
                        context.coursePreviewActorRuntimeBridge,
                        context.courseGameplayWaveRuntimeBridge});
            }});

    imguiTiming.panelRegistryMs =
        EditorUiElapsedMs(panelRegistryStart, EditorUiTimingClock::now());

    const auto panelDrawStart = EditorUiTimingClock::now();
    editorLayoutPersistence_.CaptureRegistryDefaults(editorPanelRegistry_);
    editorLayoutPersistence_.ValidateActivePanels(editorPanelRegistry_);
    const bool courseMapOpenBeforePanels = courseMapEditorWorkspace_.IsOpen();
    const bool contentBrowserMaximized =
        showDeveloperTools_ && editorContentBrowserMaximized_ &&
        !courseMapOpenBeforePanels;
    if (!contentBrowserMaximized && !courseMapOpenBeforePanels) {
        editorPanelHost_.DrawArea(
            editorPanelRegistry_,
            editor::EditorPanelHostArea::Viewport,
            editorPanelLayout_.ViewportRect(),
            "Editor Viewport Host",
            &editorLayoutPersistence_);
    }
    editorPanelHost_.DrawArea(
        editorPanelRegistry_,
        editor::EditorPanelHostArea::LeftSidebar,
        editorPanelLayout_.LeftSidebarRect(),
        "Editor Left Sidebar",
        &editorLayoutPersistence_);
    editorPanelHost_.DrawArea(
        editorPanelRegistry_,
        editor::EditorPanelHostArea::RightInspector,
        editorPanelLayout_.InspectorRect(),
        "Editor Right Inspector",
        &editorLayoutPersistence_);
    const bool courseMapOpen = courseMapEditorWorkspace_.IsOpen();
    const bool courseMapFillsCenter =
        courseMapOpen && courseMapEditorWorkspace_.IsMaximized();
    const std::vector<editor::EditorPanelHostAction>
        contentBrowserLayoutActions{{
            contentBrowserMaximized ? "Restore" : "Maximize",
            contentBrowserMaximized
                ? "Restore the Viewport and Bottom Dock."
                : "Expand Content Browser across the central editor workspace.",
            [this]() {
                if (!editorContentBrowserMaximized_) {
                    editorContentDrawer_.DockInLayout();
                }
                editorContentBrowserMaximized_ =
                    !editorContentBrowserMaximized_;
            }}};
    if (!courseMapFillsCenter) {
        editorPanelHost_.DrawArea(
            editorPanelRegistry_,
            editor::EditorPanelHostArea::ContentBrowser,
            editorPanelLayout_.ContentBrowserPresentationRect(contentBrowserMaximized),
            "Editor Content Browser",
            &editorLayoutPersistence_,
            &contentBrowserLayoutActions);
    }
    if (!contentBrowserMaximized && !courseMapFillsCenter) {
        editorPanelHost_.DrawArea(
            editorPanelRegistry_,
            editor::EditorPanelHostArea::BottomDock,
            editorPanelLayout_.DiagnosticsRect(),
            "Editor Bottom Dock",
            &editorLayoutPersistence_);
    }
    if (courseMapEditorWorkspace_.IsOpen()) {
        courseMapEditorMajorTab_.Draw(
            courseMapEditorWorkspace_,
            courseOverviewMapController_,
            courseMapPanelContext,
            editor::CourseMapEditorMajorTab::ResolvePresentationRect(
                editorPanelLayout_, courseMapEditorWorkspace_.IsMaximized()));
    }
    if (showDeveloperTools_ &&
        contentDrawerPresentationRect.Valid() &&
        editorContentDrawer_.IsVisible()) {
        if (editorContentDrawer_.ConsumeFocusRequest()) {
            editorInteractiveTools_.RequestCancel();
            ImGui::SetNextWindowFocus();
        }
        const std::vector<editor::EditorPanelHostAction>
            contentDrawerActions{
                {
                    editorContentDrawer_.IsPinned() ? "Unpin" : "Pin",
                    editorContentDrawer_.IsPinned()
                        ? "Return to transient Content Drawer behavior."
                        : "Keep the Content Drawer open while working elsewhere.",
                    [this]() {
                        editorContentDrawer_.SetPinned(
                            !editorContentDrawer_.IsPinned());
                    },
                },
                {
                    "Dock in Layout",
                    "Close the Drawer and focus the docked Content Browser.",
                    [this]() {
                        editorLayoutPersistence_.SetPanelVisible(
                            "editor.assets", true);
                        editorLayoutPersistence_.SetActivePanelFromUser(
                            editor::EditorPanelHostArea::ContentBrowser,
                            "editor.assets");
                        editorContentDrawer_.DockInLayout();
                    },
                },
                {
                    "Close",
                    "Close Content Drawer (Ctrl+Space or Escape).",
                    [this]() {
                        editorContentDrawer_.Close();
                    },
                }};
        editorPanelHost_.DrawArea(
            editorPanelRegistry_,
            editor::EditorPanelHostArea::ContentBrowser,
            contentDrawerPresentationRect,
            "Editor Content Drawer",
            &editorLayoutPersistence_,
            &contentDrawerActions);
    }
    interactiveEnvironment.selectionRevision = editorSelection_.Revision();
    interactiveEnvironment.documentEditRevision = 0;
    interactiveEnvironment.documentGeneration = 0;
    interactiveEnvironment.activeDocumentKey.clear();
    if (const editor::EditorDocumentRecord* activeDocument = editorDocumentManager_.Active()) {
        interactiveEnvironment.activeDocumentKey = activeDocument->id.Key();
        interactiveEnvironment.documentEditRevision = activeDocument->editRevision;
        interactiveEnvironment.documentGeneration = activeDocument->contentGeneration;
    }
    interactiveEnvironment.playSessionActive = editorPlaySession_.IsActive();
    interactiveEnvironment.canMutateAuthoring =
        !interactiveEnvironment.playSessionActive &&
        editorViewportInteraction_.CanMutateAuthoring();
    interactiveEnvironment.viewportAvailable = editorViewportInteraction_.ViewportAvailable();
    editorInteractiveTools_.Reconcile(interactiveEnvironment, editorTransactions);
    imguiTiming.panelDrawMs = EditorUiElapsedMs(panelDrawStart, EditorUiTimingClock::now());

    const auto layoutPersistenceStart = EditorUiTimingClock::now();
    if (!contentBrowserMaximized && !courseMapEditorWorkspace_.IsOpen() &&
        DrawEditorWorkspaceSplitters(editorPanelLayoutConfig, editorPanelLayout_)) {
        editorPanelLayout_.Configure(editorPanelLayoutConfig);
        editorLayoutPersistence_.CaptureLayout(editorPanelLayoutConfig);
        editorViewportRenderTarget_.Update(
            editor::EditorViewportRenderTargetInput{
                showDeveloperTools_,
                editorPanelLayout_.ViewportRect(),
                static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.x)),
                static_cast<uint32_t>((std::max)(1.0f, editorWorkSize.y))});
    } else {
        editorLayoutPersistence_.CaptureLayout(editorPanelLayoutConfig);
    }
    editorLayoutPersistence_.SetMajorWorkspaceLayout(
        editor::CourseMapEditorWorkspace::kWorkspaceId,
        courseMapEditorWorkspace_.CaptureLayout());
    const editor::CourseMapSceneVisualizationSettings courseMapSceneSettings =
        courseMapSceneVisualization_.Settings();
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.enabled", courseMapSceneSettings.enabled);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.terrain", courseMapSceneSettings.showTerrain);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.rocks", courseMapSceneSettings.showRockClusters);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.structures", courseMapSceneSettings.showSceneStructures);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.authored-enemies", courseMapSceneSettings.showAuthoredEnemies);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.encounter-preview", courseMapSceneSettings.showEncounterPreview);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.scene.labels", courseMapSceneSettings.showLabels);
    editorLayoutPersistence_.SetOverlayOption(
        "course-map.hybrid.enabled", courseMapHybridCompositor_.Settings().enabled);
    editorLayoutPersistence_.SaveIfDirty();
    editorContentBrowserState_.SaveIfDirty();
    editorDetailsViewState_.SaveIfDirty();
    imguiTiming.layoutPersistenceMs =
        EditorUiElapsedMs(layoutPersistenceStart, EditorUiTimingClock::now());
    imguiTiming.buildUiMs = EditorUiElapsedMs(buildUiStart, EditorUiTimingClock::now());
    LogEditorImguiBreakdown(imguiTiming);
}

void AppImGuiLayer::EndFrame() {
    if (!initialized_ || !visible_) {
        return;
    }

    const auto renderStart = EditorUiTimingClock::now();
    ImGui::Render();
    LogEditorImguiRenderTiming(EditorUiElapsedMs(renderStart, EditorUiTimingClock::now()));
}

void AppImGuiLayer::Render(ID3D12GraphicsCommandList* cmdList) {
    if (!initialized_ || !visible_ || !cmdList) {
        return;
    }

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

void AppImGuiLayer::Shutdown() {
    if (!initialized_) {
        return;
    }

    if (courseMapEditorWorkspace_.IsRestored()) {
        editorLayoutPersistence_.SetMajorWorkspaceLayout(
            editor::CourseMapEditorWorkspace::kWorkspaceId,
            courseMapEditorWorkspace_.CaptureLayout());
    }
    if (editorLayoutPersistence_.Dirty()) {
        editorLayoutPersistence_.Save();
    }

    editorInteractiveTools_.Shutdown();
    editorTerrainEditExecution_.Clear();
    editorGeometryExecution_.Clear();
    editorGeometryWorkspace_.Clear();
    editorMeshBakeExecution_.Clear();
    editorMeshBakePipeline_.Clear();
    editorMeshAssetChangeTracker_.Reset();
    editorProductionMeshRuntimeCache_.Clear();
    editorMeshRendererRuntimeWorld_.Clear();
    editorViewportOverlay_.UnregisterProvider(editorProductionNavigationAuthoringPipeline_.Id());
    editorProductionNavigationAuthoringPipeline_.Shutdown();
    editorProductionAiValidationPipeline_.Shutdown();
    editorViewportOverlay_.UnregisterProvider(editorProductionAiAuthoringPipeline_.Id());
    editorProductionAiAuthoringPipeline_.Shutdown();
    editorProductionAiWorldPipeline_.Shutdown();
    editorProductionAiPipeline_.Shutdown();
    editorProductionNavigationPipeline_.Shutdown();
    editorWorldPartitionPipeline_.Shutdown();
    editorProductionGpuDrivenPipeline_.Shutdown();
    editorProductionLightingPipeline_.Shutdown();
    editorProductionShaderPipeline_.Shutdown();
    editorProductionTexturePipeline_.Shutdown();
    editorProductionMaterialPipeline_.Shutdown();
    editorTransientMeshRenderPath_.Shutdown();
    editorProductionScenePipeline_.Shutdown();
    editorInteractiveExecution_.Clear();
    editorAssetThumbnails_.Clear();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    editorFonts_.OnContextDestroyed();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool AppImGuiLayer::IsEnabled() const {
    return initialized_;
}

void AppImGuiLayer::SetVisible(bool visible) {
    visible_ = visible;
}

bool AppImGuiLayer::IsVisible() const {
    return initialized_ && visible_;
}

bool AppImGuiLayer::WantsDeveloperDiagnostics() const {
    return initialized_ && visible_ && showDeveloperTools_ && !viewportFocusMode_;
}

bool AppImGuiLayer::ShouldAdvanceEditorRuntimeFrame() const {
    // A UI-less presentation build has no Play button. Treat it as a running
    // executable so animation, attachments, and GPU particles keep advancing.
    return !visible_ || editorPlaySession_.ShouldAdvanceRuntimeFrame();
}

void AppImGuiLayer::CompleteEditorRuntimeFrameAdvance(bool advanced) {
    if (advanced) {
        editorPlaySession_.CompleteRuntimeFrameAdvance();
    }
}

editor::EditorScene* AppImGuiLayer::ActiveEditorScene() {
    if (const editor::EditorDocumentRecord* active = editorDocumentManager_.Active();
        active != nullptr && active->id.type == editor::EditorDocumentTypes::Scene) {
        return editorSceneDocumentProvider_.Scene(active->id);
    }
    return editorSceneDocumentProvider_.Scene(editorSceneDocumentId_);
}

const editor::EditorScene* AppImGuiLayer::ActiveEditorScene() const {
    if (const editor::EditorDocumentRecord* active = editorDocumentManager_.Active();
        active != nullptr && active->id.type == editor::EditorDocumentTypes::Scene) {
        return editorSceneDocumentProvider_.Scene(active->id);
    }
    return editorSceneDocumentProvider_.Scene(editorSceneDocumentId_);
}

const editor::EditorViewportRenderTargetState& AppImGuiLayer::EditorViewportRenderTargetState() const {
    return editorViewportRenderTarget_.State();
}

const editor::EditorViewportOverlayService& AppImGuiLayer::EditorViewportOverlay() const {
    return editorViewportOverlay_;
}

const editor::EditorProductionScenePipeline& AppImGuiLayer::ProductionScenePipeline() const {
    return editorProductionScenePipeline_;
}

const editor::EditorTransientMeshRenderPath& AppImGuiLayer::TransientMeshRenderPath() const {
    return editorTransientMeshRenderPath_;
}

const editor::EditorProductionMaterialPipeline& AppImGuiLayer::ProductionMaterialPipeline() const {
    return editorProductionMaterialPipeline_;
}

const editor::EditorProductionTexturePipeline& AppImGuiLayer::ProductionTexturePipeline() const {
    return editorProductionTexturePipeline_;
}

const editor::EditorProductionShaderPipeline& AppImGuiLayer::ProductionShaderPipeline() const {
    return editorProductionShaderPipeline_;
}

editor::EditorProductionLightingPipeline& AppImGuiLayer::ProductionLightingPipeline() {
    return editorProductionLightingPipeline_;
}

const editor::EditorProductionLightingPipeline& AppImGuiLayer::ProductionLightingPipeline() const {
    return editorProductionLightingPipeline_;
}

editor::EditorProductionGpuDrivenPipeline& AppImGuiLayer::ProductionGpuDrivenPipeline() {
    return editorProductionGpuDrivenPipeline_;
}

const editor::EditorProductionGpuDrivenPipeline& AppImGuiLayer::ProductionGpuDrivenPipeline() const {
    return editorProductionGpuDrivenPipeline_;
}

editor::EditorWorldPartitionPipeline& AppImGuiLayer::WorldPartitionPipeline() {
    return editorWorldPartitionPipeline_;
}

const editor::EditorWorldPartitionPipeline& AppImGuiLayer::WorldPartitionPipeline() const {
    return editorWorldPartitionPipeline_;
}

editor::EditorProductionNavigationPipeline& AppImGuiLayer::ProductionNavigationPipeline() {
    return editorProductionNavigationPipeline_;
}

const editor::EditorProductionNavigationPipeline&
AppImGuiLayer::ProductionNavigationPipeline() const {
    return editorProductionNavigationPipeline_;
}

editor::EditorProductionAiPipeline& AppImGuiLayer::ProductionAiPipeline() {
    return editorProductionAiPipeline_;
}

const editor::EditorProductionAiPipeline& AppImGuiLayer::ProductionAiPipeline() const {
    return editorProductionAiPipeline_;
}

editor::EditorProductionAiWorldPipeline& AppImGuiLayer::ProductionAiWorldPipeline() {
    return editorProductionAiWorldPipeline_;
}

const editor::EditorProductionAiWorldPipeline&
AppImGuiLayer::ProductionAiWorldPipeline() const {
    return editorProductionAiWorldPipeline_;
}

editor::EditorProductionAiAuthoringPipeline&
AppImGuiLayer::ProductionAiAuthoringPipeline() {
    return editorProductionAiAuthoringPipeline_;
}

const editor::EditorProductionAiAuthoringPipeline&
AppImGuiLayer::ProductionAiAuthoringPipeline() const {
    return editorProductionAiAuthoringPipeline_;
}

editor::EditorProductionAiValidationPipeline&
AppImGuiLayer::ProductionAiValidationPipeline() {
    return editorProductionAiValidationPipeline_;
}

const editor::EditorProductionAiValidationPipeline&
AppImGuiLayer::ProductionAiValidationPipeline() const {
    return editorProductionAiValidationPipeline_;
}

#else

bool AppImGuiLayer::Initialize(HWND hwnd,
    ID3D12Device* device,
    int bufferCount,
    DXGI_FORMAT rtvFormat,
    ID3D12DescriptorHeap* srvHeap,
    AppPipelines* appPipelines) {
    (void)hwnd;
    (void)device;
    (void)bufferCount;
    (void)rtvFormat;
    (void)srvHeap;
    (void)appPipelines;
    initialized_ = false;
    return true;
}

bool AppImGuiLayer::HandleWindowMessage(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)hwnd;
    (void)message;
    (void)wParam;
    (void)lParam;
    return false;
}

void AppImGuiLayer::BeginFrame() {
}

void AppImGuiLayer::BuildUi(const AppImGuiFrameContext& context) {
    (void)context;
}

void AppImGuiLayer::QueueExternalAssetDrop(std::filesystem::path path) {
    (void)path;
}

void AppImGuiLayer::RefreshEditorViewportRenderTargetLayout() {
}

void AppImGuiLayer::EndFrame() {
}

void AppImGuiLayer::Render(ID3D12GraphicsCommandList* cmdList) {
    (void)cmdList;
}

bool AppImGuiLayer::IsEnabled() const {
    return false;
}

void AppImGuiLayer::SetVisible(bool visible) {
    visible_ = visible;
}

bool AppImGuiLayer::IsVisible() const {
    return false;
}

bool AppImGuiLayer::WantsDeveloperDiagnostics() const {
    return false;
}

bool AppImGuiLayer::ShouldAdvanceEditorRuntimeFrame() const {
    return true;
}

void AppImGuiLayer::CompleteEditorRuntimeFrameAdvance(bool advanced) {
    (void)advanced;
}

editor::EditorScene* AppImGuiLayer::ActiveEditorScene() {
    return editorSceneDocumentProvider_.Scene(editorSceneDocumentId_);
}

const editor::EditorScene* AppImGuiLayer::ActiveEditorScene() const {
    return editorSceneDocumentProvider_.Scene(editorSceneDocumentId_);
}

const editor::EditorViewportRenderTargetState& AppImGuiLayer::EditorViewportRenderTargetState() const {
    return editorViewportRenderTarget_.State();
}

const editor::EditorViewportOverlayService& AppImGuiLayer::EditorViewportOverlay() const {
    return editorViewportOverlay_;
}

const editor::EditorProductionScenePipeline& AppImGuiLayer::ProductionScenePipeline() const {
    return editorProductionScenePipeline_;
}

const editor::EditorTransientMeshRenderPath& AppImGuiLayer::TransientMeshRenderPath() const {
    return editorTransientMeshRenderPath_;
}

const editor::EditorProductionMaterialPipeline& AppImGuiLayer::ProductionMaterialPipeline() const {
    return editorProductionMaterialPipeline_;
}

const editor::EditorProductionTexturePipeline& AppImGuiLayer::ProductionTexturePipeline() const {
    return editorProductionTexturePipeline_;
}

const editor::EditorProductionShaderPipeline& AppImGuiLayer::ProductionShaderPipeline() const {
    return editorProductionShaderPipeline_;
}

editor::EditorProductionLightingPipeline& AppImGuiLayer::ProductionLightingPipeline() {
    return editorProductionLightingPipeline_;
}

const editor::EditorProductionLightingPipeline& AppImGuiLayer::ProductionLightingPipeline() const {
    return editorProductionLightingPipeline_;
}

editor::EditorProductionGpuDrivenPipeline& AppImGuiLayer::ProductionGpuDrivenPipeline() {
    return editorProductionGpuDrivenPipeline_;
}

const editor::EditorProductionGpuDrivenPipeline& AppImGuiLayer::ProductionGpuDrivenPipeline() const {
    return editorProductionGpuDrivenPipeline_;
}

editor::EditorWorldPartitionPipeline& AppImGuiLayer::WorldPartitionPipeline() {
    return editorWorldPartitionPipeline_;
}

const editor::EditorWorldPartitionPipeline& AppImGuiLayer::WorldPartitionPipeline() const {
    return editorWorldPartitionPipeline_;
}

editor::EditorProductionNavigationPipeline& AppImGuiLayer::ProductionNavigationPipeline() {
    return editorProductionNavigationPipeline_;
}

const editor::EditorProductionNavigationPipeline&
AppImGuiLayer::ProductionNavigationPipeline() const {
    return editorProductionNavigationPipeline_;
}

editor::EditorProductionAiPipeline& AppImGuiLayer::ProductionAiPipeline() {
    return editorProductionAiPipeline_;
}

const editor::EditorProductionAiPipeline& AppImGuiLayer::ProductionAiPipeline() const {
    return editorProductionAiPipeline_;
}

editor::EditorProductionAiWorldPipeline& AppImGuiLayer::ProductionAiWorldPipeline() {
    return editorProductionAiWorldPipeline_;
}

const editor::EditorProductionAiWorldPipeline&
AppImGuiLayer::ProductionAiWorldPipeline() const {
    return editorProductionAiWorldPipeline_;
}

editor::EditorProductionAiAuthoringPipeline&
AppImGuiLayer::ProductionAiAuthoringPipeline() {
    return editorProductionAiAuthoringPipeline_;
}

const editor::EditorProductionAiAuthoringPipeline&
AppImGuiLayer::ProductionAiAuthoringPipeline() const {
    return editorProductionAiAuthoringPipeline_;
}

editor::EditorProductionAiValidationPipeline&
AppImGuiLayer::ProductionAiValidationPipeline() {
    return editorProductionAiValidationPipeline_;
}

const editor::EditorProductionAiValidationPipeline&
AppImGuiLayer::ProductionAiValidationPipeline() const {
    return editorProductionAiValidationPipeline_;
}

void AppImGuiLayer::Shutdown() {
    initialized_ = false;
}

#endif
