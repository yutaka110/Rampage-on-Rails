#pragma once
#include <functional>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <Windows.h>
#include <d3d12.h>

struct ImDrawList;
class AppPipelines;
namespace editor {
class CourseEnemyEditorController;
class CourseEnemyPickingService;
class CourseEnemyViewportRenderer;
class CourseWaveEditorController;
class CourseWavePickingService;
class CourseWaveViewportRenderer;
class CoursePreviewSimulationSystem;
class CoursePreviewActorRuntimeBridge;
class CourseRailEditorController;
class CourseRailPickingService;
class CourseRailViewportRenderer;
}

#include "AppTerrainPbrMaterialsPanel.h"
#include "graphics/RenderGraph.h"
#include "editor/EditorAssetSelection.h"
#include "editor/EditorAssetD3D12ThumbnailGpuBackend.h"
#include "editor/EditorAssetThumbnailService.h"
#include "editor/EditorCommandContext.h"
#include "editor/EditorCommandInputRouter.h"
#include "editor/EditorCommandPalette.h"
#include "editor/EditorCommandRegistry.h"
#include "editor/EditorContentDrawerService.h"
#include "editor/EditorContentBrowserState.h"
#include "editor/EditorFramePacingService.h"
#include "editor/CourseDocumentAdapter.h"
#include "editor/EditorDirtyStateService.h"
#include "editor/EditorDetailsViewState.h"
#include "editor/EditorDocumentLifecycleService.h"
#include "editor/EditorLayoutService.h"
#include "editor/EditorLayoutPersistenceService.h"
#include "editor/EditorDetailsSectionProvider.h"
#include "editor/EditorModalConfirmService.h"
#include "editor/EditorNotificationCenter.h"
#include "editor/EditorNotificationsPanel.h"
#include "editor/EditorPanelHost.h"
#include "editor/EditorPanelLayoutService.h"
#include "editor/EditorPanelRegistry.h"
#include "editor/EditorPlaySessionIsolationSnapshot.h"
#include "editor/EditorPlaySessionLifecycleService.h"
#include "editor/EditorPlaySessionRuntimeControlService.h"
#include "editor/EditorPlaySessionState.h"
#include "editor/EditorPropertyEditSession.h"
#include "editor/EditorPropertyEditService.h"
#include "editor/mesh/EditorProductionMeshAsset.h"
#include "editor/EditorPropertyClipboardService.h"
#include "editor/EditorPropertyRegistry.h"
#include "editor/EditorRailRuntimePause.h"
#include "editor/EditorRuntimeInspector.h"
#include "editor/EditorRuntimeAuthoringApplyService.h"
#include "editor/EditorSelection.h"
#include "editor/EditorFontService.h"
#include "editor/EditorFontSettingsPanel.h"
#include "editor/sequencer/EditorSequencer.h"
#include "editor/course/CourseSequencerTrackProvider.h"
#include "editor/course/CourseEnemyDetailsPanel.h"
#include "editor/course/CourseWaveDetailsPanel.h"
#include "editor/course/CourseOverviewMapController.h"
#include "editor/course/CourseOverviewMapPanel.h"
#include "editor/course/CourseOverviewMapDragDropBridge.h"
#include "editor/course/CourseOverviewMapEditTool.h"
#include "editor/course/CourseOverviewMapMultiViewCoordinator.h"
#include "editor/course/CourseOverviewMapSnapService.h"
#include "editor/course/CourseMapEditorWorkspace.h"
#include "editor/course/CourseMapEditorMajorTab.h"
#include "editor/course/CourseMapCartographyBakePipeline.h"
#include "editor/course/CourseMapCartographyRenderer.h"
#include "editor/course/CourseMap3DViewportController.h"
#include "editor/course/CourseTerrainMapBakePipeline.h"
#include "editor/course/CourseTerrainMapRenderer.h"
#include "editor/course/CourseMapHologramRenderer.h"
#include "editor/course/CourseMapHybridCartographyCompositor.h"
#include "editor/course/CourseMapSceneVisualizationPipeline.h"
#include "editor/course/CourseMapVisualBakePipeline.h"
#include "editor/course/CourseRailConstraintValidationSystem.h"
#include "editor/course/CourseRailCurveFitService.h"
#include "editor/course/CourseRailElevationProfileEditor.h"
#include "editor/course/CourseRailSketchTool.h"
#include "editor/course/CourseRailStrokePreviewRenderer.h"
#include "editor/course/CourseSequencerWaveTrackBridge.h"
#include "editor/course/CourseRideSequencerTrackBridge.h"
#include "editor/course/RailRideSpeedBeatAuthoring.h"
#include "editor/course/CourseRailRideEventAuthoring.h"
#include "editor/course/CourseEncounterBeatAuthoring.h"
#include "editor/course/CourseRideProfileDetailsPanel.h"
#include "editor/course/CourseCameraShotDetailsPanel.h"
#include "editor/course/RailRideTuningTelemetryPanel.h"
#include "editor/course/CourseEnemyTransformGizmo.h"
#include "editor/course/CourseEnemyViewportEditTool.h"
#include "editor/course/CourseRailViewportEditTool.h"
#include "editor/course/CourseRailTransformGizmo.h"
#include "editor/course/CourseRailDetailsPanel.h"
#include "editor/prefab/EditorPrefabService.h"
#include "editor/material/EditorMaterialGraph.h"
#include "editor/material/EditorMaterialGraphPanel.h"
#include "editor/material/EditorMaterialGraphDiagnosticsAdapter.h"
#include "editor/vfx/EditorVfxGraph.h"
#include "editor/vfx/EditorVfxGraphPanel.h"
#include "editor/vfx/EditorVfxGraphDiagnosticsAdapter.h"
#include "editor/animation/EditorAnimationStateMachine.h"
#include "editor/animation/EditorAnimationStateMachinePanel.h"
#include "editor/animation/EditorAnimationStateMachineDiagnosticsAdapter.h"
#include "editor/gameplay/EditorGameplayVisualScript.h"
#include "editor/gameplay/EditorGameplayVisualScriptPanel.h"
#include "editor/gameplay/EditorGameplayVisualScriptDiagnosticsAdapter.h"
#include "editor/EditorToolRegistration.h"
#include "editor/EditorTransformGizmoService.h"
#include "editor/EditorTransactionStack.h"
#include "editor/EditorViewportCoordinateService.h"
#include "editor/EditorViewportCameraInput.h"
#include "editor/EditorViewportRealtimePolicy.h"
#include "editor/EditorViewportInteractionService.h"
#include "editor/EditorViewportOverlay.h"
#include "editor/EditorViewportSelectionBridge.h"
#include "editor/EditorViewportRenderTarget.h"
#include "editor/tools/EditorModeRegistry.h"
#include "editor/tools/EditorSplineRouteTool.h"
#include "editor/tools/EditorToolManager.h"
#include "editor/terrain/EditorTerrainBrushTools.h"
#include "editor/terrain/EditorTerrainEditCommand.h"
#include "editor/terrain/EditorTerrainSurfaceQuery.h"
#include "editor/geometry/EditorGeometryTools.h"
#include "editor/geometry/EditorGeometryEditCommand.h"
#include "editor/geometry/EditorGeometryWorkspace.h"
#include "editor/geometry/EditorTransientMeshRenderPath.h"
#include "editor/mesh/EditorCreateEditableCopyTool.h"
#include "editor/mesh/EditorMeshBakeTools.h"
#include "editor/scene/EditorProductionScenePipeline.h"
#include "editor/material/EditorProductionMaterialPipeline.h"
#include "editor/texture/EditorProductionTexturePipeline.h"
#include "editor/shader/EditorProductionShaderPipeline.h"
#include "editor/lighting/EditorProductionLightingPipeline.h"
#include "editor/visibility/EditorProductionGpuDrivenPipeline.h"
#include "editor/streaming/EditorWorldPartitionPipeline.h"
#include "editor/navigation/EditorProductionNavigationPipeline.h"
#include "editor/navigation/EditorProductionNavigationAuthoringPipeline.h"
#include "editor/navigation/EditorProductionNavigationAuthoringPanel.h"
#include "editor/ai/EditorProductionAiPipeline.h"
#include "editor/ai/EditorProductionAiWorldPipeline.h"
#include "editor/ai/EditorProductionAiAuthoringPipeline.h"
#include "editor/ai/EditorProductionAiAuthoringPanel.h"
#include "editor/ai/EditorProductionAiValidationPipeline.h"
#include "editor/ai/EditorProductionAiValidationPanel.h"
#include "editor/core/EditorExecutionContext.h"
#include "editor/documents/EditorAutosaveService.h"
#include "editor/documents/EditorCourseDocumentProvider.h"
#include "editor/documents/EditorSceneDocumentProvider.h"
#include "editor/scene/EditorGimmickDefinitionRegistry.h"
#include "editor/scene/EditorGimmickPresentationPhysicsAdapter.h"
#include "editor/scene/EditorGimmickRuntimeInteractionSystem.h"
#include "editor/scene/EditorGimmickRuntimeEventRouter.h"
#include "editor/scene/EditorGimmickRuntimeTriggerSystem.h"
#include "editor/scene/EditorSceneComponentRegistry.h"
#include "editor/scene/EditorMeshRendererRuntimeFactory.h"
#include "editor/documents/EditorPrefabDocumentProvider.h"
#include "editor/documents/EditorMaterialGraphDocumentProvider.h"
#include "editor/documents/EditorVfxGraphDocumentProvider.h"
#include "editor/documents/EditorAnimationStateMachineDocumentProvider.h"
#include "editor/documents/EditorGameplayVisualScriptDocumentProvider.h"
#include "editor/documents/EditorAiDocumentProviders.h"
#include "editor/documents/EditorNavigationDocumentProvider.h"
#include "editor/documents/EditorDocumentRecoveryService.h"
#include "editor/documents/EditorDocumentSaveService.h"
#include "editor/documents/EditorTextDocumentProvider.h"
#include "editor/world/CourseWorldObjectProvider.h"
#include "editor/world/SceneWorldObjectProvider.h"
#include "editor/world/EditorWorldModel.h"
#include "editor/world/EditorWorldRefreshRevisionGate.h"
#include "editor/world/EditorWorldMutationService.h"
#include "editor/world/EditorWorldOutlinerPanel.h"
#include "editor/world/VfxWorldObjectProvider.h"

struct AppRuntimeState;
struct FrameLoopState;
class AppGpuParticleSystem;
class AppPipelines;
class AppRenderResources;
class AppSceneResources;
class EffectRuntime;
class EffectAuthoringRegistry;
struct LoadedEffectAsset;
class PostProcessStack;
class CourseCollisionSystem;
class CourseSpawnRuntime;
class CourseGameplayWaveRuntimeBridge;
class PlayerCombatFeelSystem;
class SectionCheckpointSystem;
class RailRideTuningTelemetry;
struct CourseAsset;

struct AppImGuiFrameContext {
    AppRuntimeState* runtimeState = nullptr;
    EffectRuntime* effectRuntime = nullptr;
    const EffectAuthoringRegistry& effectAuthoringRegistry;
    const std::vector<LoadedEffectAsset>* loadedEffectAssets = nullptr;
    PostProcessStack* postProcessStack = nullptr;
    const std::string* renderGraphDescription = nullptr;
    const std::string* renderGraphError = nullptr;
    const std::vector<ge3::graphics::RenderPassDebugInfo>* renderPassDebugInfo = nullptr;
    uint32_t transientTargetCount = 0;
    uint32_t transientTargetStorageCount = 0;
    uint32_t transientBufferCount = 0;
    uint32_t transientBufferStorageCount = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColorPreview{};
    D3D12_GPU_DESCRIPTOR_HANDLE vfxAccumulationPreview{};
    D3D12_GPU_DESCRIPTOR_HANDLE postColorPreview{};
    D3D12_GPU_DESCRIPTOR_HANDLE depthPreview{};
    D3D12_GPU_DESCRIPTOR_HANDLE emissivePreview{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainHiZPreview{};
    AppRenderResources* renderResources = nullptr;
    AppSceneResources* scene = nullptr;
    AppPipelines* appPipelines = nullptr;
    AppGpuParticleSystem* gpuParticleSystem = nullptr;
    FrameLoopState* frameState = nullptr;
    ID3D12DescriptorHeap* srvDescriptorHeap = nullptr;
    ID3D12GraphicsCommandList* editorUploadCommandList = nullptr;
    uint64_t editorCompletedFenceValue = 0;
    uint64_t editorScheduledFenceValue = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE vfxTextureHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE depthTextureHandle{};
    CourseAsset* course = nullptr;
    const CourseSpawnRuntime* courseSpawnRuntime = nullptr;
    const CourseCollisionSystem* courseCollisionSystem = nullptr;
    const SectionCheckpointSystem* courseCheckpointSystem = nullptr;
    const PlayerCombatFeelSystem* playerCombatFeelSystem = nullptr;
    const std::string* courseLoadStatus = nullptr;
    const std::string* coursePath = nullptr;
    float courseDistance = 0.0f;
    float courseSpeed = 0.0f;
    float courseRailLength = 0.0f;
    RailRideTuningTelemetry* railRideTuningTelemetry = nullptr;
    std::function<bool(std::string*)> onSaveCourse;
    std::function<void()> onApplyCourse;
    std::function<void()> onReloadCourse;
    std::function<void(float)> onTeleportCourseToDistance;
    std::function<void()> onAddParticle;
    std::function<void()> onDrawRailLockOnDebugPanel;
    std::function<void(editor::EditorViewportOverlayService&)> onBuildEditorViewportOverlay;
    editor::CourseRailEditorController* courseRailEditorController = nullptr;
    const editor::CourseRailPickingService* courseRailPickingService = nullptr;
    editor::CourseRailViewportRenderer* courseRailViewportRenderer = nullptr;
    editor::CourseEnemyEditorController* courseEnemyEditorController = nullptr;
    const editor::CourseEnemyPickingService* courseEnemyPickingService = nullptr;
    editor::CourseEnemyViewportRenderer* courseEnemyViewportRenderer = nullptr;
    editor::CourseWaveEditorController* courseWaveEditorController = nullptr;
    const editor::CourseWavePickingService* courseWavePickingService = nullptr;
    editor::CourseWaveViewportRenderer* courseWaveViewportRenderer = nullptr;
    editor::CoursePreviewSimulationSystem* coursePreviewSimulationSystem = nullptr;
    editor::CoursePreviewActorRuntimeBridge* coursePreviewActorRuntimeBridge = nullptr;
    const CourseGameplayWaveRuntimeBridge* courseGameplayWaveRuntimeBridge = nullptr;
    editor::EditorTransactionStack* editorTransactions = nullptr;
    std::function<bool(std::string*)> onBeginGameplaySpawns;
    std::function<void()> onStopGameplaySpawns;
    bool railGameplayScene = false;
};

class AppImGuiLayer {
public:
    bool Initialize(HWND hwnd, ID3D12Device* device, int bufferCount,
        DXGI_FORMAT rtvFormat, ID3D12DescriptorHeap* srvHeap,
        AppPipelines* appPipelines);
    bool HandleWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void BeginFrame();
    void BuildUi(const AppImGuiFrameContext& context);
    void QueueExternalAssetDrop(std::filesystem::path path);
    void RefreshEditorViewportRenderTargetLayout();
    void EndFrame();

    void Render(ID3D12GraphicsCommandList* cmdList);
    bool IsEnabled() const;
    void SetVisible(bool visible);
    bool IsVisible() const;
    bool WantsDeveloperDiagnostics() const;
    bool ShouldAdvanceEditorRuntimeFrame() const;
    void CompleteEditorRuntimeFrameAdvance(bool advanced);
    editor::EditorFramePacingService& FramePacingService() {
        return editorFramePacing_;
    }
    const editor::EditorFramePacingService& FramePacingService() const {
        return editorFramePacing_;
    }
    editor::EditorViewportRealtimePolicy& ViewportRealtimePolicy() {
        return editorViewportRealtimePolicy_;
    }
    const editor::EditorViewportRealtimePolicy& ViewportRealtimePolicy() const {
        return editorViewportRealtimePolicy_;
    }
    bool EditorPlaySessionActive() const {
        return editorPlaySession_.IsActive();
    }
    const editor::EditorPlaySessionState& EditorPlaySession() const {
        return editorPlaySession_;
    }
    bool EditorViewportUsesFreeCamera() const {
        return editorPlaySession_.UsesEditorFreeCamera();
    }
    editor::EditorPlaySessionViewportMode EditorViewportControlMode() const {
        return editorPlaySession_.ViewportMode();
    }
    bool EditorViewportInteractionActive() const {
        return editorViewportInteraction_.HasAnyCapture();
    }
    bool EditorInteractiveToolActive() const {
        return editorInteractiveTools_.HasActiveTool();
    }
    editor::EditorScene* ActiveEditorScene();
    const editor::EditorScene* ActiveEditorScene() const;
    const editor::EditorSceneComponentRegistry& SceneComponentRegistry() const {
        return editorSceneComponentRegistry_;
    }
    const editor::EditorGimmickDefinitionRegistry&
    GimmickDefinitionRegistry() const {
        return editorGimmickDefinitionRegistry_;
    }
    const editor::EditorAssetRegistry& AssetRegistry() const {
        return editorAssetRegistry_;
    }
    editor::EditorMeshRendererRuntimeWorld& MeshRendererRuntimeWorld() {
        return editorMeshRendererRuntimeWorld_;
    }
    const editor::EditorMeshRendererRuntimeWorld& MeshRendererRuntimeWorld() const {
        return editorMeshRendererRuntimeWorld_;
    }
    void BindEditorGimmickRuntimeDebug(
        const editor::EditorGimmickRuntimeWorld* world,
          const editor::EditorGimmickPresentationPhysicsAdapter*
              adapter,
          const editor::EditorGimmickRuntimeEventRouter*
              eventRouter,
          const editor::EditorGimmickRuntimeInteractionSystem*
              interaction,
        const editor::EditorGimmickRuntimeTriggerSystem*
            triggers) noexcept {
          editorGimmickRuntimeWorld_ = world;
          editorGimmickRuntimeAdapter_ = adapter;
          editorGimmickRuntimeEventRouter_ = eventRouter;
          editorGimmickRuntimeInteraction_ = interaction;
        editorGimmickRuntimeTriggers_ = triggers;
    }
    const editor::EditorViewportRenderTargetState& EditorViewportRenderTargetState() const;
    const editor::EditorViewportCameraInput& EditorViewportCameraFrameInput() const {
        return editorViewportCameraInput_;
    }
    const editor::EditorViewportOverlayService& EditorViewportOverlay() const;
    const editor::EditorProductionScenePipeline& ProductionScenePipeline() const;
    const editor::EditorTransientMeshRenderPath& TransientMeshRenderPath() const;
    const editor::EditorProductionMaterialPipeline& ProductionMaterialPipeline() const;
    const editor::EditorProductionTexturePipeline& ProductionTexturePipeline() const;
    const editor::EditorProductionShaderPipeline& ProductionShaderPipeline() const;
    editor::EditorProductionLightingPipeline& ProductionLightingPipeline();
    const editor::EditorProductionLightingPipeline& ProductionLightingPipeline() const;
    editor::EditorProductionGpuDrivenPipeline& ProductionGpuDrivenPipeline();
    const editor::EditorProductionGpuDrivenPipeline& ProductionGpuDrivenPipeline() const;
    editor::EditorWorldPartitionPipeline& WorldPartitionPipeline();
    const editor::EditorWorldPartitionPipeline& WorldPartitionPipeline() const;
    editor::EditorProductionNavigationPipeline& ProductionNavigationPipeline();
    const editor::EditorProductionNavigationPipeline& ProductionNavigationPipeline() const;
    editor::EditorProductionAiPipeline& ProductionAiPipeline();
    const editor::EditorProductionAiPipeline& ProductionAiPipeline() const;
    editor::EditorProductionAiWorldPipeline& ProductionAiWorldPipeline();
    const editor::EditorProductionAiWorldPipeline& ProductionAiWorldPipeline() const;
    editor::EditorProductionAiAuthoringPipeline& ProductionAiAuthoringPipeline();
    const editor::EditorProductionAiAuthoringPipeline& ProductionAiAuthoringPipeline() const;
    editor::EditorProductionAiValidationPipeline& ProductionAiValidationPipeline();
    const editor::EditorProductionAiValidationPipeline& ProductionAiValidationPipeline() const;

    void Shutdown();

private:
    bool initialized_ = false;
    bool visible_ = true;
    HWND hwnd_ = nullptr;
    bool viewportFocusMode_ = false;
    bool showcasePresentationInitialized_ = false;
    bool showDeveloperTools_ = false;
    bool showcaseLoopCurrent_ = false;
    TerrainPbrMaterialsPanelState terrainPbrMaterialsPanelState_{};
    uint32_t selectedEffectInstanceId_ = 0;
    uint32_t trailMeshStreamProbeHealthyFrames_ = 0;
    uint32_t trailMeshStreamActiveHealthyFrames_ = 0;
    uint32_t trailMeshStreamHealthFrames_ = 0;
    uint32_t trailMeshStreamStartupTelemetryFrames_ = 0;
    uint32_t particleDedicatedProbeTelemetryFrames_ = 0;
    uint32_t particleDedicatedHealthFrames_ = 0;
    uint32_t particleDedicatedProbeStableFrames_ = 0;
    uint32_t particleDedicatedActiveStableFrames_ = 0;
    uint32_t distortionDedicatedHealthFrames_ = 0;
    uint32_t distortionDedicatedTelemetryFrames_ = 0;
    uint32_t distortionDedicatedStableFrames_ = 0;
    uint32_t distortionDedicatedActiveStableFrames_ = 0;
    uint32_t beamDedicatedTelemetryFrames_ = 0;
    uint32_t beamDedicatedHealthFrames_ = 0;
    uint32_t beamDedicatedStableFrames_ = 0;
    uint32_t beamDedicatedActiveStableFrames_ = 0;
    uint32_t hiddenRuntimeTelemetryFrame_ = 0;
    editor::EditorPropertyRegistry editorPropertyRegistry_{};
    editor::EditorSceneComponentRegistry editorSceneComponentRegistry_{
        editor::CreateBuiltInEditorSceneComponentRegistry()};
    editor::EditorGimmickDefinitionRegistry
        editorGimmickDefinitionRegistry_{
            editor::CreateBuiltInEditorGimmickDefinitionRegistry()};
    editor::EditorAssetRegistry editorAssetRegistry_{};
    editor::EditorAssetSelection editorAssetSelection_{};
    editor::EditorAssetD3D12ThumbnailGpuBackend editorAssetThumbnailGpuBackend_{};
    editor::EditorAssetThumbnailService editorAssetThumbnails_{};
    editor::EditorCommandInputRouter editorCommandInputRouter_{};
    editor::EditorCommandPalette editorCommandPalette_{};
    editor::EditorCommandExecutionStatus editorCommandExecutionStatus_{};
    editor::EditorDirtyStateService editorDirtyState_{};
    editor::EditorFontService editorFonts_{};
    editor::EditorDocumentLifecycleService editorDocumentLifecycle_{};
    editor::EditorCourseDocumentProvider editorCourseDocumentProvider_{};
    editor::EditorSceneDocumentProvider editorSceneDocumentProvider_{};
    editor::EditorPrefabDocumentProvider editorPrefabDocumentProvider_{};
    editor::EditorMaterialGraphDocumentProvider editorMaterialGraphDocumentProvider_{};
    editor::EditorVfxGraphDocumentProvider editorVfxGraphDocumentProvider_{};
    editor::EditorAnimationStateMachineDocumentProvider editorAnimationStateMachineDocumentProvider_{};
    editor::EditorGameplayVisualScriptDocumentProvider editorGameplayVisualScriptDocumentProvider_{};
    editor::EditorBehaviorTreeDocumentProvider editorBehaviorTreeDocumentProvider_{};
    editor::EditorEqsDocumentProvider editorEqsDocumentProvider_{};
    editor::EditorNavigationDocumentProvider editorNavigationDocumentProvider_{};
    editor::EditorTextDocumentProvider editorEffectDocumentProvider_{
        std::string(editor::EditorDocumentTypes::Effect), "Effect/VFX", {".effect", ".vfx"}, 1};
    editor::EditorTextDocumentProvider editorRenderPresetDocumentProvider_{
        std::string(editor::EditorDocumentTypes::RenderPreset), "Material/Render Preset",
        {".renderpreset"}, 1};
    editor::EditorTextDocumentProvider editorProjectSettingsDocumentProvider_{
        std::string(editor::EditorDocumentTypes::ProjectSettings), "Project Settings",
        {".settings", ".projectsettings"}, 1};
    editor::EditorDocumentRegistry editorDocumentRegistry_{};
    editor::EditorDocumentManager editorDocumentManager_{editorDocumentRegistry_};
    editor::EditorExternalChangeMonitor editorExternalChangeMonitor_{};
    editor::EditorDocumentSaveService editorDocumentSaveService_{
        editorDocumentManager_, editorExternalChangeMonitor_};
    editor::EditorAutosaveService editorAutosaveService_{editorDocumentManager_};
    editor::EditorDocumentRecoveryService editorDocumentRecoveryService_{
        editorDocumentRegistry_, editorDocumentManager_};
    editor::CourseWorldObjectProvider editorCourseWorldProvider_{};
    editor::SceneWorldObjectProvider editorSceneWorldProvider_{};
    editor::VfxWorldObjectProvider editorVfxWorldProvider_{};
    editor::EditorWorldObjectRegistry editorWorldObjectRegistry_{};
    editor::EditorWorldModel editorWorldModel_{editorWorldObjectRegistry_};
    editor::EditorWorldRefreshRevisionGate editorWorldRefreshRevisionGate_{};
    editor::EditorWorldMutationService editorWorldMutationService_{
        editorWorldObjectRegistry_, editorWorldModel_};
    editor::EditorWorldMutationExecutionService editorWorldMutationExecution_{
        editorWorldObjectRegistry_, &editorWorldModel_};
    editor::EditorWorldOutlinerState editorWorldOutlinerState_{};
    editor::EditorLayoutService editorLayout_{};
    editor::EditorLayoutPersistenceService editorLayoutPersistence_{};
    editor::EditorContentBrowserState editorContentBrowserState_{};
    editor::EditorAssetWorkspaceStatusRegistry editorAssetWorkspaceStatus_{};
    editor::EditorDetailsViewState editorDetailsViewState_{};
    editor::EditorPrefabService editorPrefabs_{};
    editor::EditorMaterialGraphService editorMaterialGraphs_{};
    editor::EditorVfxGraphService editorVfxGraphs_{};
    editor::EditorAnimationStateMachineService editorAnimationStateMachines_{};
    editor::EditorGameplayVisualScriptService editorGameplayVisualScripts_{};
    editor::EditorPanelHost editorPanelHost_{};
    editor::EditorPanelLayoutService editorPanelLayout_{};
    editor::EditorPanelRegistry editorPanelRegistry_{};
    editor::EditorContentDrawerService editorContentDrawer_{};
    editor::EditorFramePacingService editorFramePacing_{};
    editor::EditorViewportRealtimePolicy editorViewportRealtimePolicy_{};
    bool editorContentBrowserMaximized_ = false;
    editor::EditorDetailsSectionProviderRegistry editorDetailsSectionProviders_{};
    editor::EditorViewportCoordinateService editorViewportCoordinates_{};
    editor::EditorViewportInteractionService editorViewportInteraction_{};
    editor::EditorViewportCameraInput editorViewportCameraInput_{};
    editor::EditorModeRegistry editorModeRegistry_{};
    editor::EditorToolManager editorInteractiveTools_{editorModeRegistry_};
    editor::EditorExecutionContext editorInteractiveExecution_{};
    editor::EditorTerrainToolBinding editorTerrainToolBinding_{};
    editor::EditorTerrainEditExecutionService editorTerrainEditExecution_{};
    editor::EditorTerrainSurfaceQueryService editorTerrainSurfaceQuery_{};
    editor::EditorGeometryWorkspace editorGeometryWorkspace_{};
    editor::EditorGeometryToolBinding editorGeometryToolBinding_{};
    editor::EditorGeometryExecutionService editorGeometryExecution_{};
    editor::EditorProductionMeshEditableSourceLoader
        editorProductionMeshEditableSourceLoader_{};
    editor::EditorCreateEditableCopyToolBinding
        editorCreateEditableCopyToolBinding_{};
    editor::EditorMeshBakePipeline editorMeshBakePipeline_{};
    editor::EditorMeshBakeToolBinding editorMeshBakeToolBinding_{};
    editor::EditorMeshBakeExecutionService editorMeshBakeExecution_{};
    editor::EditorMeshAssetChangeTracker editorMeshAssetChangeTracker_{};
    editor::EditorProductionMeshRuntimeCache editorProductionMeshRuntimeCache_{};
    editor::EditorMeshRendererRuntimeWorld editorMeshRendererRuntimeWorld_{};
    const editor::EditorGimmickRuntimeWorld*
        editorGimmickRuntimeWorld_ = nullptr;
      const editor::EditorGimmickPresentationPhysicsAdapter*
          editorGimmickRuntimeAdapter_ = nullptr;
      const editor::EditorGimmickRuntimeEventRouter*
          editorGimmickRuntimeEventRouter_ = nullptr;
      const editor::EditorGimmickRuntimeInteractionSystem*
        editorGimmickRuntimeInteraction_ = nullptr;
    const editor::EditorGimmickRuntimeTriggerSystem*
        editorGimmickRuntimeTriggers_ = nullptr;
    editor::EditorProductionScenePipeline editorProductionScenePipeline_{};
    editor::EditorTransientMeshRenderPath editorTransientMeshRenderPath_{};
    editor::EditorProductionMaterialPipeline editorProductionMaterialPipeline_{};
    editor::EditorProductionTexturePipeline editorProductionTexturePipeline_{};
    editor::EditorProductionShaderPipeline editorProductionShaderPipeline_{};
    editor::EditorProductionLightingPipeline editorProductionLightingPipeline_{};
    editor::EditorProductionGpuDrivenPipeline editorProductionGpuDrivenPipeline_{};
    editor::EditorWorldPartitionPipeline editorWorldPartitionPipeline_{};
    editor::EditorProductionNavigationPipeline editorProductionNavigationPipeline_{};
    editor::EditorProductionNavigationAuthoringPipeline editorProductionNavigationAuthoringPipeline_{};
    editor::EditorProductionAiPipeline editorProductionAiPipeline_{};
    editor::EditorProductionAiWorldPipeline editorProductionAiWorldPipeline_{};
    editor::EditorProductionAiAuthoringPipeline editorProductionAiAuthoringPipeline_{};
    editor::EditorProductionAiValidationPipeline editorProductionAiValidationPipeline_{};
    editor::EditorViewportOverlayService editorViewportOverlay_{};
    editor::EditorViewportSelectionBridge editorViewportSelectionBridge_{};
    editor::EditorViewportRenderTarget editorViewportRenderTarget_{};
    editor::EditorTransformGizmoService editorTransformGizmo_{};
    editor::EditorModalConfirmService editorConfirmService_{};
    editor::EditorNotificationCenter editorNotifications_{};
    editor::EditorNotificationToastState editorNotificationToastState_{};
    editor::EditorPlaySessionIsolationSnapshot editorPlaySessionSnapshot_{};
    editor::EditorPlaySessionLifecycleService editorPlaySessionLifecycle_{};
    editor::EditorPlaySessionRuntimeControlService editorPlaySessionRuntimeControl_{};
    editor::EditorPlaySessionState editorPlaySession_{};
    editor::EditorPropertyEditSession editorDetailsEditSession_{};
    editor::EditorPropertyEditService editorPropertyEditService_{};
    editor::EditorPropertyClipboardService editorPropertyClipboard_{};
    editor::EditorRailRuntimePause editorRailRuntimePause_{};
    editor::EditorRuntimeInspector editorRuntimeInspector_{};
    editor::EditorRuntimeAuthoringApplyService editorRuntimeAuthoringApply_{};
    editor::EditorSelection editorSelection_{};
    editor::CourseEnemyViewportEditTool courseEnemyViewportEditTool_{};
    editor::CourseEnemyTransformGizmo courseEnemyTransformGizmo_{};
    editor::CourseEnemyDetailsPanel courseEnemyDetailsPanel_{};
    editor::CourseWaveDetailsPanel courseWaveDetailsPanel_{};
    editor::CourseOverviewMapController courseOverviewMapController_{};
    editor::CourseMap3DViewportController courseMap3DViewportController_{};
    editor::CourseMapEditorWorkspace courseMapEditorWorkspace_{};
    editor::CourseMapEditorMajorTab courseMapEditorMajorTab_{};
    editor::CourseMapSceneBoundsService courseMapSceneBounds_{};
    editor::CourseMapVisualBakePipeline courseMapVisualBake_{};
    editor::CourseMapCartographyBakePipeline courseMapCartographyBake_{};
    editor::CourseMapCartographyRenderer courseMapCartographyRenderer_{};
    editor::CourseTerrainMapBakePipeline courseTerrainMapBake_{};
    editor::CourseTerrainMapRenderer courseTerrainMapRenderer_{};
    editor::CourseMapHologramRenderer courseMapHologramRenderer_{};
    editor::CourseMapHybridCartographyCompositor courseMapHybridCompositor_{};
    editor::CourseMapSceneVisualizationPipeline courseMapSceneVisualization_{};
    bool courseMapSceneVisualizationSettingsRestored_ = false;
    editor::CourseOverviewMapSnapService courseOverviewMapSnapService_{};
    editor::CourseOverviewMapDragDropBridge courseOverviewMapDragDropBridge_{};
    editor::CourseOverviewMapEditTool courseOverviewMapEditTool_{};
    editor::CourseOverviewMapMultiViewCoordinator courseOverviewMapMultiViewCoordinator_{};
    editor::CourseRailElevationProfileEditor courseRailElevationProfileEditor_{};
    editor::CourseRailConstraintValidationSystem courseRailConstraintValidationSystem_{};
    editor::CourseRailCurveFitService courseRailCurveFitService_{};
    editor::CourseRailSketchTool courseRailSketchTool_{};
    editor::CourseRailStrokePreviewRenderer courseRailStrokePreviewRenderer_{};
    editor::CourseRailViewportEditTool courseRailViewportEditTool_{};
    editor::CourseRailTransformGizmo courseRailTransformGizmo_{};
    editor::CourseRailDetailsPanel courseRailDetailsPanel_{};
    editor::CourseSequencerTrackProvider editorCourseSequencerProvider_{};
    editor::CourseSequencerWaveTrackBridge editorCourseWaveSequencerBridge_{};
    editor::CourseRideSequencerTrackBridge editorCourseRideSequencerBridge_{};
    editor::RailRideSpeedBeatAuthoring editorRailRideSpeedBeatAuthoring_{};
    editor::CourseRailRideEventAuthoring editorCourseRailRideEventAuthoring_{};
    editor::CourseEncounterBeatAuthoring editorCourseEncounterBeatAuthoring_{};
    editor::CourseRideProfileDetailsPanel courseRideProfileDetailsPanel_{};
    editor::CourseCameraShotDetailsPanel courseCameraShotDetailsPanel_{};
    editor::RailRideTuningTelemetryPanel railRideTuningTelemetryPanel_{};
    editor::EditorSequencerService editorSequencer_{};
    editor::EditorToolRegistry editorToolRegistry_{};
    editor::EditorTransactionStack editorTransactions_{};
    std::vector<std::filesystem::path> pendingExternalAssetImportPaths_{};
    bool editorCourseDocumentOpen_ = true;
    std::string editorCourseDocumentPath_;
    std::filesystem::path editorSceneDocumentPath_{"Resources/Scenes/Editor.scene"};
    editor::EditorDocumentId editorSceneDocumentId_{};
    bool editorCourseObjectDirtyRevisionInitialized_ = false;
    uint32_t editorCourseObjectDirtyRevision_ = 0;
    uint64_t editorDocumentServiceFrame_ = 0;
    bool editorDocumentRecoveryScanned_ = false;
    std::vector<editor::EditorDocumentRecoveryCandidate> editorDocumentRecoveryCandidates_{};
    bool editorAssetRegistryInitialized_ = false;
};
