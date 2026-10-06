#pragma once
#include "course/RailTitleScene.h"

#include <Windows.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

#include "camera/debugCamera.h"
#include "core/CommandListPool.h"
#include "core/DescriptorHeap.h"
#include "core/Device.h"
#include "diagnostics/DebugDrawSystem.h"
#include "AppFrameState.h"
#include "AppFrameGraphBuilder.h"
#include "AppAudio.h"
#include "AppGamepadInput.h"
#include "AppRuntimeConfig.h"
#include "AppVfxRuntimeState.h"
#include "HandParticleAttachment.h"
#include "WeaponAttachment.h"
#include "graphics/RenderGraph.h"
#include "graphics/SwapChain.h"
#include "resources/ResourceRegistry.h"
#include "course/CourseAsset.h"
#include "course/CourseCollisionSystem.h"
#include "course/CourseEventDispatcher.h"
#include "course/CourseGameplayWaveRuntimeBridge.h"
#include "course/CourseRuntimeProgramAsset.h"
#include "course/GameSessionSystem.h"
#include "course/GameSessionPresentationBridge.h"
#include "course/GameSessionRetryCoordinator.h"
#include "course/RailShooterHudDefinitionAsset.h"
#include "course/RailShooterHudRuntimeModel.h"
#include "course/RailShooterHudPresentationBridge.h"
#include "course/RailShooterHudRenderer.h"
#include "course/RailDodgeSystem.h"
#include "course/RailPlayerMovementSystem.h"
#include "course/RailPlayerVehicleMountSystem.h"
#include "course/RailVehicleMovementSystem.h"
#include "course/RailVehiclePresentationBridge.h"
#include "course/RailVehicleRideDynamicsSystem.h"
#include "course/RailVehicleTrackContactPoseSolver.h"
#include "course/CourseRailTrackDefinitionAsset.h"
#include "course/CourseRailTrackMeshBakePipeline.h"
#include "course/RailVehicleWheelContactPresentationBridge.h"
#include "course/RailVehicleCameraInertiaBridge.h"
#include "course/RailVehicleActor.h"
#include "course/RailVehicleRenderer.h"
#include "course/RailVehicleAudioBridge.h"
#include "course/RailVehicleMountedEvasionSystem.h"
#include "course/RailVehicleMountedDefenseSystem.h"
#include "course/RailVehicleOccupantClearanceSystem.h"
#include "course/RailVehicleEvasionConstraintResolver.h"
#include "course/RailVehicleCombatMountBridge.h"
#include "course/RailVehicleCameraMountBridge.h"
#include "course/RailVehicleMountedEvasionPresentationBridge.h"
#include "course/RailVehicleOccupantActor.h"
#include "course/RailVehicleEvasionFeedbackBridge.h"
#include "course/RailVehicleControlPresetRegistry.h"
#include "course/RailVehicleBodyCollisionSystem.h"
#include "course/RailVehicleDamageCoordinator.h"
#include "course/RailVehicleCollisionFeedbackBridge.h"
#include "course/EnemyAttackTelegraphSystem.h"
#include "course/EnemyAttackLaneTelegraphRenderer.h"
#include "course/EnemyAttackTelegraphFeedbackBridge.h"
#include "course/EnemyAttackDefensePresentationBridge.h"
#include "course/EnemyAttackDefenseResolutionSystem.h"
#include "course/EnemyAttackDefenseOutcomeFeedbackBridge.h"
#include "course/EnemyCombatPresentationBridge.h"
#include "course/EnemyEncounterReadabilityDirector.h"
#include "course/EnemyEncounterPacingDirector.h"
#include "course/EnemyEncounterCameraCompositionBridge.h"
#include "course/EncounterPerformanceScoreSystem.h"
#include "course/EnemyProjectilePresentationBridge.h"
#include "course/EnemyProjectileScreenSpaceReadabilityPolicy.h"
#include "course/EnemyProjectileVfxRenderer.h"
#include "course/EnemyProjectileAudioBridge.h"
#include "course/RailShooterDefensePromptRenderer.h"
#include "course/EnemyAttackDefenseValidationSystem.h"
#include "course/GrazeScoreSystem.h"
#include "course/ThreatResponseDirector.h"
#include "course/EncounterDirector.h"
#include "course/PlayerCombatFeelSystem.h"
#include "course/PlayerDamagePresentationBridge.h"
#include "course/RailAimAssistPresetRegistry.h"
#include "course/RailCameraDirector.h"
#include "course/RailLockOnSystem.h"
#include "course/RailSpeedDirector.h"
#include "course/RailRideDirector.h"
#include "course/RailRideMotionEnvelope.h"
#include "course/RailRideTuningTelemetry.h"
#include "course/RailTrackFeedbackDirector.h"
#include "course/SectionCheckpointSystem.h"
#include "terrain/RailPath.h"
#include "terrain/TerrainChunkManager.h"
#include "terrain/TerrainPresetStore.h"
#include "utils/math/MathUtils.h"
#include "AppSceneState.h"
#include "AppSceneStateManager.h"
#include "runtime/AppFrameCoordinator.h"
#include "VfxEngine.h"
#include "editor/EditorPropertyEditSession.h"
#include "editor/EditorTransactionStack.h"
#include "editor/EditorViewportCameraController.h"
#include "editor/EditorPlaySessionState.h"
#include "editor/EditorViewportAuthoringInputGuard.h"
#include "editor/course/CourseEnemyEditorController.h"
#include "editor/course/CourseEnemyPickingService.h"
#include "editor/course/CourseEnemyViewportRenderer.h"
#include "editor/course/CoursePreviewSimulationSystem.h"
#include "editor/course/CoursePreviewActorRuntimeBridge.h"
#include "editor/course/CourseRuntimeCookPipeline.h"
#include "editor/course/CourseWaveEditorController.h"
#include "editor/course/CourseWavePickingService.h"
#include "editor/course/CourseWaveViewportRenderer.h"
#include "editor/course/CourseRailEditorController.h"
#include "editor/course/CourseRailPickingService.h"
#include "editor/course/CourseRailViewportRenderer.h"
#include "editor/scene/EditorGameplaySpawnRuntimeFactory.h"
#include "editor/scene/EditorBuiltInRuntimeFactoryRegistration.h"
#include "editor/scene/EditorGimmickRuntimeFactory.h"
#include "editor/scene/EditorGimmickPresentationPhysicsAdapter.h"
#include "editor/scene/EditorGimmickRuntimeEventRouter.h"
#include "editor/scene/EditorGimmickRuntimeEventBindingRegistry.h"
#include "editor/scene/EditorGimmickRuntimeDelayedEventScheduler.h"
#include "editor/scene/EditorGimmickRuntimeEventSequenceRegistry.h"
#include "editor/scene/EditorGimmickRuntimeInteractionSystem.h"
#include "editor/scene/EditorGimmickRuntimeTriggerSystem.h"
#include "editor/scene/EditorMeshRendererRuntimeFactory.h"
#include "editor/scene/EditorPatrolRuntimeFactory.h"
#include "editor/scene/EditorSceneRuntimeInstantiation.h"

class AppFrameRenderer;
class AppImGuiLayer;
class AppPipelines;
class AppParticleSystem;
class AppRenderResources;
struct AppRuntimeState;
class AppSceneResources;
class EngineContext;
struct ImDrawList;
namespace editor { class EditorViewportOverlayService; }

class AppRunLoop : private AppSceneHost {
public:
    AppRunLoop(
        DebugCamera& debugCamera,
        AppRuntimeState& runtimeState,
        AppSceneResources& scene,
        AppParticleSystem& particleSystem,
        AppImGuiLayer& imguiLayer,
        AppFrameRenderer& frameRenderer,
        AppPipelines& appPipelines,
        AppRenderResources& renderResources,
        AppAudio& audio,
        graphics::SwapChain& swapChain,
        core::CommandListPool& clPool,
        EngineContext& engineContext,
        ge3::core::DescriptorHeapSet& heaps,
        core::Device& dev,
        HWND hwnd,
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap,
        Matrix4x4* wvpData,
        uint32_t windowWidth,
        uint32_t windowHeight,
        FrameLoopState& frameState,
        ID3D12CommandQueue* commandQueue,
        ID3D12Fence* fence,
        HANDLE fenceEvent,
        AppStartupScene startupScene);

    void InitializeBeam(
        ID3D12Device* device,
        ID3D12DescriptorHeap* srvDescriptorHeap,
        uint32_t descriptorSizeSRV,
        DXGI_FORMAT rtvFormat,
        DXGI_FORMAT dsvFormat);
    void RenderFrame();
    bool HandleTitleScreenMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Shutdown();

private:
    struct CourseObjectEditSnapshot;
    struct CourseObjectDragState;

    void EnterVfxPreviewScene() override;
    void EnterMultiMaterialShowcaseScene() override;
    void EnterRailShooterScene() override;
    void UpdateRailShooterFrame() override;
    void RenderRailShooterFrame() override;
    void UpdateVfxPreviewFrame() override;
    void RenderVfxPreviewFrame() override;
    void UpdateMultiMaterialShowcaseFrame() override;
    void UpdateSubmissionShowcaseInput(float deltaTime);
    void BeginFrameSystems();
    void ApplyEditorViewportRenderTargetForRender();
    bool ResolveEditorViewportClientPoint(
        POINT clientPoint,
        POINT& outViewportPoint,
        uint32_t& outViewportWidth,
        uint32_t& outViewportHeight) const;
    void ProcessCourseObjectViewportEditing();
    CourseObjectEditSnapshot CaptureCourseObjectSnapshot() const;
    std::string BuildCourseObjectSnapshotSummary(const CourseObjectEditSnapshot& snapshot) const;
    void RestoreCourseObjectSnapshot(const CourseObjectEditSnapshot& snapshot);
    bool ApplyCourseObjectGizmoEditThroughServiceIfPossible();
    bool BeginCourseObjectGizmoEditSession();
    bool PreviewCourseObjectGizmoEditSession(std::vector<editor::EditorPropertyEditSessionValue> values);
    bool CancelCourseObjectDragIfNeeded();
    void StageCourseObjectGizmoTransactionIfNeeded();
    bool CommitCourseObjectDragIfNeeded();
    void EnsureCourseObjectHistoryBaseline();
    void CommitCourseObjectHistoryIfNeeded();
    void ProcessCourseObjectUndoRedo();
    void ProcessIceProjectileMouseLaunch();
    void UpdateHandParticleAttachment();
    void UpdateWeaponAttachment();
    void ProcessReleaseShowcaseControls(float deltaTime);
    void PlayShowcaseEffect(AppVfxRuntimeState::ShowcaseEffect effect, bool resetAutoTimer);
    void ClearShowcaseEffects();
    void FireShowcaseIceProjectile();
    void ConfigureShowcasePostProcess();
    void UpdateShowcaseWindowTitle();
    void UpdateTerrainAuthoring(float deltaTime);
    void RenderCascadeShadowMaps(ID3D12GraphicsCommandList* commandList);
    void ConfigureRenderGraphDebugDump();
    void DumpRenderGraphDebugFrame();
    void LoadRailShooterCourse();
    void ApplyRailShooterCourse();
    bool SaveRailShooterCourse(std::string* errorMessage = nullptr);
    void TeleportRailShooterCourse(float distance);
    bool BeginEditorGameplaySpawns(std::string* errorMessage);
    bool ReconcileEditorSceneRuntime(std::string* errorMessage);
    void StopEditorGameplaySpawns();
    void LogCourseEvents(const std::vector<CourseEventMarker>& events);
    void ApplyRailShooterVisualPresets(float distance);
    void DrawRailLockOnDebugPanel();
    void BuildRailVisibilityDebugOverlay(editor::EditorViewportOverlayService& overlay);
    bool EnsureRailLockOnHudAtlas(ID3D12GraphicsCommandList* commandList);
    bool BuildRailLockOnHudAtlasQuads();
    void RegisterRailLockOnHudPass(
        ID3D12GraphicsCommandList* commandList,
        const std::string& targetResourceName);
    bool EnsureSubmissionHudResources(ID3D12GraphicsCommandList* commandList);
    bool BuildSubmissionHudQuads();
    void RegisterSubmissionHudPass(
        ID3D12GraphicsCommandList* commandList,
        const std::string& targetResourceName);
    void StartRailCameraTuningRecording();
    void StopRailCameraTuningRecording();
    void ClearRailCameraTuningRecording();
    bool ExportRailCameraTuningCsv(std::string* outPath = nullptr);
    void RecordRailCameraTuningSample(
        float deltaTime,
        const RailSpeedDirectorFrame& speedFrame,
        const RailCameraDirectorFrame& cameraFrame,
        const CourseCollisionFrameStats& collisionStats);
    int ProcessRailLockOnRelease(const Vector3& muzzlePosition, float deltaTime);
    void QueueRailLockIceProjectile(const Vector3& start, const Vector3& target, int shotIndex);
    bool IsRailShooterSceneActive() const;
    void LogRailShooterRuntimeDiagnostics(const char* reason);
    void LogRailShooterPerfSpike();
    bool EnsureRailGpuTimingResources();
    void ResolveCompletedRailGpuTiming(uint32_t backBufferIndex);
    void BeginRailGpuTiming(ID3D12GraphicsCommandList* commandList, uint32_t backBufferIndex);
    void EndRailGpuTiming(ID3D12GraphicsCommandList* commandList, uint32_t backBufferIndex);
    void CaptureRailGpuTimingCpuMetadata(uint32_t backBufferIndex);
    void ProcessPostProcessShowcaseShortcuts();
    void DispatchRailEnemyAttackFeedback(
        const AppGamepadFrame& gamepad,
        float deltaTime,
        bool gameplayActive);
    void StopRailEnemyAttackFeedback();
    void DispatchGameSessionPresentation(const AppGamepadFrame& gamepad);
    void DispatchPlayerDamagePresentation();
    void DispatchThreatResponse();
    void DispatchEnemyAttackDefenseOutcomeFeedback();
    void StopGameSessionPresentation();
    void DispatchRailVehicleAudio(float deltaTime);
    void DispatchRailTrackFeedback();
    void DispatchRailVehicleEvasionFeedback();
    void DispatchRailVehicleCollisionFeedback();
    bool ApplyRailVehicleControlPreset(
        bool preserveRuntimeState,
        std::string* errorMessage = nullptr);
    void DispatchEnemyCombatPresentation(
        std::span<const EnemyCombatEvent> events,
        float deltaTime,
        bool gameplayActive);
    void DispatchEnemyProjectilePresentation(
        float deltaTime,
        bool gameplayActive,
        const Vector3& cameraPosition,
        const Vector3& cameraRight,
        const Vector3& cameraUp);
    void ResetEnemyProjectilePresentation(bool resetGrazeState = true);
    bool WasKeyPressed(int virtualKey);

    DebugCamera& debugCamera_;
    // The free editor camera and the possessed game-camera view must not
    // share transform state. Ejecting may inspect a frozen runtime without
    // mutating the camera owned by gameplay/course simulation.
    editor::EditorViewportCameraController editorViewportCamera_{};
    editor::EditorViewportCameraController editorGameViewportCamera_{};
    editor::EditorPlaySessionViewportMode lastEditorViewportMode_ =
        editor::EditorPlaySessionViewportMode::EditorFree;
    uint64_t lastEditorViewportSessionSerial_ = 0;
    AppRuntimeState& runtimeState_;
    AppSceneResources& scene_;
    AppParticleSystem& particleSystem_;
    AppImGuiLayer& imguiLayer_;
    AppFrameRenderer& frameRenderer_;
    AppPipelines& appPipelines_;
    AppRenderResources& renderResources_;
    AppAudio& audio_;
    graphics::SwapChain& swapChain_;
    core::CommandListPool& clPool_;
    EngineContext& engineContext_;
    ge3::core::DescriptorHeapSet& heaps_;
    core::Device& dev_;
    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    Matrix4x4* wvpData_;
    uint32_t windowWidth_;
    uint32_t windowHeight_;
    FrameLoopState& frameState_;
    ID3D12CommandQueue* commandQueue_;
    AppFrameCoordinator frameCoordinator_;
    bool editorFramePacingProfilingMode_ = false;
    AppSceneStateManager sceneStateManager_;
    VfxEngine vfxEngine_;
    HandParticleAttachment handParticleAttachment_{};
    HandParticleAttachment leftHandParticleAttachment_{};
    WeaponAttachment weaponAttachment_{};
    AppFrameGraphBuilder frameGraphBuilder_;
    CourseAsset railShooterCourse_;
    CourseRuntime railShooterCourseRuntime_;
    CourseCollisionSystem railShooterCollisionSystem_;
    SectionCheckpointSystem railShooterCheckpointSystem_;
    GameSessionSystem railShooterGameSession_{};
    GameSessionPresentationBridge railShooterSessionPresentation_{};
    GameSessionPresentationSettings railShooterSessionPresentationSettings_{};
    GameSessionRetryCoordinator railShooterRetryCoordinator_{};
    RailShooterHudDefinitionAsset railShooterHudDefinition_ =
        RailShooterHudDefinitionAsset::Defaults();
    RailShooterHudRuntimeModel railShooterHudRuntimeModel_{};
    RailShooterHudPresentationBridge railShooterHudPresentation_{};
    RailShooterHudRenderer railShooterHudRenderer_{};
    RailShooterDefensePromptRenderer railShooterDefensePromptRenderer_{};
    RailShooterDefensePromptRendererSettings
        railShooterDefensePromptRendererSettings_{};
    std::string railShooterHudLoadStatus_;
    RailPlayerMovementSystem railShooterPlayerMovement_{};
    RailPlayerVehicleMountSystem railShooterPlayerVehicleMount_{};
    RailDodgeSystem railShooterPlayerDodge_{};
    RailVehicleMovementSystem railShooterVehicleMovement_{};
    RailVehicleControlPresetRegistry railVehicleControlPresetRegistry_{};
    std::string railVehicleControlPresetId_ =
        RailVehicleControlPresetRegistry::kMineCartStandardPresetId;
    std::string railVehicleControlAppliedPresetId_;
    uint64_t railVehicleControlAppliedRevision_ = 0;
    RailVehiclePresentationBridge railShooterVehiclePresentation_{};
    RailVehiclePresentationSettings railShooterVehiclePresentationSettings_{};
    RailVehicleRideDynamicsSystem railShooterVehicleRideDynamics_{};
    RailVehicleRideDynamicsSettings railShooterVehicleRideDynamicsSettings_{};
    RailVehicleTrackContactPoseSolver railShooterTrackContactPose_{};
    RailVehicleTrackContactPoseSettings railShooterTrackContactPoseSettings_{};
    CourseRailTrackDefinitionAsset railShooterTrackDefinition_ =
        CourseRailTrackDefinitionAsset::MineCartDefaults();
    CourseRailTrackMeshBakePipeline railShooterTrackMeshBake_{};
    RailVehicleWheelContactPresentationBridge
        railShooterWheelContactPresentation_{};
    std::string railShooterTrackLoadStatus_;
    RailVehicleCameraInertiaBridge railShooterCameraInertia_{};
    RailVehicleCameraInertiaSettings railShooterCameraInertiaSettings_{};
    RailVehicleActor railShooterVehicleActor_{};
    RailVehicleRenderer railShooterVehicleRenderer_{};
    RailVehicleAudioBridge railShooterVehicleAudioBridge_{};
    RailVehicleAudioSettings railShooterVehicleAudioSettings_{};
    RailVehicleMountedEvasionSystem railShooterMountedEvasion_{};
    RailVehicleMountedDefenseSystem railShooterMountedDefense_{};
    RailVehicleBodyCollisionSystem railShooterVehicleBodyCollision_{};
    RailVehicleDamageCoordinator railShooterVehicleDamageCoordinator_{};
    RailVehicleCollisionFeedbackBridge
        railShooterVehicleCollisionFeedback_{};
    RailVehicleCollisionFeedbackSettings
        railShooterVehicleCollisionFeedbackSettings_{};
    RailVehicleOccupantClearanceSystem railShooterOccupantClearance_{};
    RailVehicleEvasionConstraintResolver railShooterEvasionConstraintResolver_{};
    RailVehicleCombatMountBridge railShooterCombatMountBridge_{};
    RailVehicleCameraMountBridge railShooterCameraMountBridge_{};
    RailVehicleMountedEvasionPresentationBridge
        railShooterMountedEvasionPresentation_{};
    RailVehicleMountedEvasionPresentationSettings
        railShooterMountedEvasionPresentationSettings_{};
    RailVehicleOccupantActor railShooterOccupantActor_{};
    RailVehicleEvasionFeedbackBridge railShooterEvasionFeedback_{};
    RailVehicleEvasionFeedbackSettings railShooterEvasionFeedbackSettings_{};
    PlayerCombatFeelSystem railShooterCombatFeelSystem_;
    PlayerDamagePresentationBridge railPlayerDamagePresentationBridge_{};
    GrazeScoreSystem railGrazeScoreSystem_{};
    ThreatResponseDirector railThreatResponseDirector_{};
    ThreatResponseSettings railThreatResponseSettings_{};
    CourseEventDispatcher railShooterEventDispatcher_;
    EncounterDirector railShooterEncounterDirector_;
    RailCameraDirector railShooterCameraDirector_;
    RailSpeedDirector railShooterSpeedDirector_;
    RailRideDirector railShooterRideDirector_;
    RailRideMotionEnvelope railShooterRideMotionEnvelope_{};
    RailRideTuningTelemetry railShooterRideTuningTelemetry_{};
    RailTrackFeedbackDirector railShooterTrackFeedbackDirector_{};
    RailTrackFeedbackDirectorSettings railShooterTrackFeedbackSettings_{};
    CourseSpawnRuntime railShooterSpawnRuntime_;
    CourseRuntimeProgramAsset railShooterRuntimeProgram_{};
    editor::CourseRuntimeCookPipeline railShooterRuntimeCookPipeline_{};
    CourseGameplayWaveRuntimeBridge railShooterGameplayWaveBridge_{};
    EnemyAttackTelegraphSystem railEnemyAttackTelegraphSystem_;
    EnemyAttackTelegraphSettings railEnemyAttackTelegraphSettings_{};
    EnemyAttackLaneTelegraphRenderer railEnemyAttackLaneTelegraphRenderer_{};
    EnemyAttackLaneTelegraphRendererSettings
        railEnemyAttackLaneTelegraphRendererSettings_{};
    EnemyAttackTelegraphFeedbackBridge railEnemyAttackTelegraphFeedbackBridge_{};
    EnemyAttackTelegraphFeedbackSettings railEnemyAttackTelegraphFeedbackSettings_{};
    EnemyAttackDefensePresentationBridge
        railEnemyAttackDefensePresentationBridge_{};
    EnemyAttackDefensePresentationSettings
        railEnemyAttackDefensePresentationSettings_{};
    EnemyAttackDefenseResolutionSystem
        railEnemyAttackDefenseResolutionSystem_{};
    EnemyAttackDefenseResolutionSettings
        railEnemyAttackDefenseResolutionSettings_{};
    EnemyAttackDefenseOutcomeFeedbackBridge
        railEnemyAttackDefenseOutcomeFeedbackBridge_{};
    EnemyAttackDefenseOutcomeFeedbackSettings
        railEnemyAttackDefenseOutcomeFeedbackSettings_{};
    CombatLoopDefenseUiProofVariant railDefenseUiProofVariant_ =
        CombatLoopDefenseUiProofVariant::Disabled;
    bool railDefenseUiProofEnabled_ = false;
    float railDefenseUiProofElapsedSeconds_ = 0.0f;
    uint32_t railDefenseUiProofStage_ = 0;
    EnemyCombatPresentationBridge railEnemyCombatPresentationBridge_{};
    EnemyCombatPresentationSettings railEnemyCombatPresentationSettings_{};
    EnemyEncounterReadabilityDirector
        railEnemyEncounterReadabilityDirector_{};
    EnemyEncounterReadabilitySettings
        railEnemyEncounterReadabilitySettings_{};
    EnemyEncounterPacingDirector railEnemyEncounterPacingDirector_{};
    EncounterPerformanceScoreSystem railEncounterPerformanceScoreSystem_{};
    EncounterPerformanceScoreSettings railEncounterPerformanceScoreSettings_{};
    EnemyEncounterCameraCompositionBridge
        railEnemyEncounterCameraCompositionBridge_{};
    EnemyProjectilePresentationBridge railEnemyProjectilePresentationBridge_{};
    EnemyProjectilePresentationSettings railEnemyProjectilePresentationSettings_{};
    EnemyProjectileVfxRenderer railEnemyProjectileVfxRenderer_{};
    EnemyProjectileVfxRendererSettings railEnemyProjectileVfxRendererSettings_{};
    EnemyProjectileScreenSpaceReadabilitySettings
        railEnemyProjectileReadabilitySettings_{};
    EnemyProjectileAudioBridge railEnemyProjectileAudioBridge_{};
    EnemyAttackDefenseValidationSystem
        railEnemyAttackDefenseValidation_{};
    EnemyProjectileAudioSettings railEnemyProjectileAudioSettings_{};
    float railEnemyProjectilePresentationTime_ = 0.0f;
    audio::SoundHandle railTelegraphAcquiredSound_{};
    audio::SoundHandle railTelegraphImminentSound_{};
    audio::SoundHandle railTelegraphFiredSound_{};
    audio::SoundHandle railSessionStartSound_{};
    audio::SoundHandle railSessionCheckpointSound_{};
    audio::SoundHandle railSessionDamageSound_{};
    audio::SoundHandle railSessionVictorySound_{};
    audio::SoundHandle railSessionDefeatSound_{};
    audio::SoundHandle railSessionRetrySound_{};
    audio::SoundHandle railVehicleRollingSound_{};
    audio::SoundHandle railVehicleJointSound_{};
    audio::SoundHandle railVehicleBrakeSound_{};
    audio::SoundHandle railVehicleStopSound_{};
    audio::SoundHandle railVehicleCollisionSound_{};
    audio::SoundHandle railVehicleEvasionStartSound_{};
    audio::SoundHandle railVehicleEvasionRecoverSound_{};
    audio::SoundHandle railVehicleEvasionReadySound_{};
    audio::SoundHandle railEnemySpawnSound_{};
    audio::SoundHandle railEnemyEngageSound_{};
    audio::SoundHandle railEnemyAttackSound_{};
    audio::SoundHandle railEnemyHitReactSound_{};
    audio::SoundHandle railEnemyDeathSound_{};
    audio::SoundHandle railEnemyProjectileLaunchSound_{};
    audio::SoundHandle railEnemyProjectileFlyBySound_{};
    audio::SoundHandle railEnemyProjectileImpactSound_{};
    audio::SoundHandle railGrazeSound_{};
    audio::SoundHandle railGrazeChainSound_{};
    audio::SoundHandle railThreatCriticalSound_{};
    audio::SoundHandle railThreatClearSound_{};
    audio::SoundHandle railDefenseOutcomeSuccessSound_{};
    audio::SoundHandle railDefenseInterruptSound_{};
    audio::SoundHandle railDefenseShootDownSound_{};
    audio::SoundHandle railDefenseEvadeSound_{};
    audio::SoundHandle railDefenseOutcomePerfectSound_{};
    audio::SoundHandle railDefenseOutcomeFailedSound_{};
    uint32_t railTelegraphVibrationController_ = UINT32_MAX;
    uint32_t railSessionVibrationController_ = UINT32_MAX;
    RailLockOnSystem railShooterLockOnSystem_;
    RailAimAssistPresetRegistry railAimAssistPresetRegistry_{};
    struct RailHudAtlasVertex {
        Vector4 position;
        Vector2 texcoord;
        Vector4 color;
    };
    struct RailNormalShotLine {
        Vector2 start{};
        Vector2 end{};
        float age = 0.0f;
        float lifetime = 0.085f;
        float thickness = 2.0f;
        bool hit = false;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> railLockOnHudAtlasTexture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> railLockOnHudAtlasVertexResource_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> railLockOnHudAtlasUploadResources_;
    RailHudAtlasVertex* railLockOnHudAtlasMappedVertices_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE railLockOnHudAtlasSrvGpu_{};
    D3D12_VERTEX_BUFFER_VIEW railLockOnHudAtlasVertexBufferView_{};
    uint32_t railLockOnHudAtlasVertexCount_ = 0;
    uint32_t railTitleDustVertexCount_ = 0;
    uint32_t railTitleTracerVertexCount_ = 0;
    bool railLockOnHudAtlasReady_ = false;
    bool railTitleLogoReady_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> submissionHudVertexResource_;
    RailHudAtlasVertex* submissionHudMappedVertices_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW submissionHudVertexBufferView_{};
    uint32_t submissionHudVertexCount_ = 0;
    struct SubmissionHudGlyph {
        uint32_t codepoint = 0;
        Vector4 uv{};
        Vector2 size{};
        Vector2 offset{};
        float advance = 0.0f;
        bool valid = false;
    };
    std::vector<SubmissionHudGlyph> submissionHudGlyphs_;
    bool submissionHudFontReady_ = false;
    bool submissionHudManuallyHidden_ = false;
    std::vector<RailNormalShotLine> railNormalShotLines_;
    std::string railShooterCoursePath_ = "Resources/courses/CanyonAssaultRoute01.course";
    std::string railShooterCourseLoadStatus_;
    RailPath railPath_;
    editor::CourseEnemyEditorController courseEnemyEditorController_{};
    editor::CourseEnemyPickingService courseEnemyPickingService_{};
    editor::CourseEnemyViewportRenderer courseEnemyViewportRenderer_{
        &courseEnemyEditorController_};
    editor::CourseWaveEditorController courseWaveEditorController_{};
    editor::CourseWavePickingService courseWavePickingService_{};
    editor::CourseWaveViewportRenderer courseWaveViewportRenderer_{
        &courseWaveEditorController_};
    editor::CoursePreviewSimulationSystem coursePreviewSimulationSystem_{};
    editor::CoursePreviewActorRuntimeBridge coursePreviewActorRuntimeBridge_{};
    editor::CourseRailEditorController courseRailEditorController_{};
    editor::CourseRailPickingService courseRailPickingService_{};
    editor::CourseRailViewportRenderer courseRailViewportRenderer_{&courseRailEditorController_};
    TerrainChunkManager terrainChunkManager_;
    TerrainPresetStore terrainPresetStore_;
    ge3::graphics::RenderGraph renderGraph_;
    ge3::resources::ResourceRegistry resourceRegistry_;
    ge3::resources::FrameTransientAllocator frameTransientAllocator_;
    std::string lastRenderGraphDescription_;
    std::string lastRenderGraphError_;
    std::vector<ge3::graphics::RenderPassDebugInfo> lastRenderPassDebugInfo_;
    uint32_t lastTransientTargetCount_ = 0;
    uint32_t lastTransientTargetStorageCount_ = 0;
    uint32_t lastTransientBufferCount_ = 0;
    uint32_t lastTransientBufferStorageCount_ = 0;
    uint32_t vfxTelemetryFrameIndex_ = 0;
    struct RailGpuTimingSlot {
        bool pending = false;
        uint32_t frame = 0;
        float distance = 0.0f;
        std::string section;
        double cpuNoPresentMs = 0.0;
        double waitFrameSlotMs = 0.0;
        double renderGraphExecuteMs = 0.0;
        double endAndExecuteMs = 0.0;
        double presentMs = 0.0;
    };
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> railGpuTimingQueryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> railGpuTimingReadback_;
    std::vector<RailGpuTimingSlot> railGpuTimingSlots_;
    uint64_t railGpuTimestampFrequency_ = 0;
    bool railGpuTimingReady_ = false;
    bool railGpuTimingUnsupportedLogged_ = false;
    bool renderGraphDumpConfigured_ = false;
    bool renderGraphDumpEnabled_ = false;
    uint32_t renderGraphDumpFrameLimit_ = 0;
    uint32_t renderGraphDumpFrameIndex_ = 0;
    std::ofstream renderGraphDump_;
    D3D12_RESOURCE_STATES sceneDepthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    float railShooterDistance_ = 0.0f;
    float railShooterPlayerLateralOffset_ = 0.0f;
    float railShooterPlayerVerticalOffset_ = 4.0f;
    editor::EditorPatrolRuntimeWorld editorPatrolRuntimeWorld_{};
    editor::EditorGimmickRuntimeWorld
        editorGimmickRuntimeWorld_{};
    editor::EditorGimmickPresentationPhysicsAdapter
        editorGimmickRuntimeAdapter_{};
    editor::EditorGimmickRuntimeEventRouter
        editorGimmickRuntimeEventRouter_{};
    editor::EditorGimmickRuntimeEventBindingRegistry
        editorGimmickRuntimeEventBindings_{};
    editor::EditorGimmickRuntimeEventSequenceRegistry
        editorGimmickRuntimeEventSequences_{};
    editor::EditorGimmickRuntimeDelayedEventScheduler
        editorGimmickRuntimeDelayedEvents_{};
    editor::EditorGimmickRuntimeInteractionSystem
        editorGimmickRuntimeInteraction_{};
    editor::EditorGimmickRuntimeTriggerSystem
        editorGimmickRuntimeTriggers_{};
    editor::EditorGimmickDefinitionRuntimeFactoryRegistry
        editorGimmickDefinitionRuntimeFactories_{};
    editor::EditorSceneRuntimeComponentFactoryRegistry
        editorSceneRuntimeFactoryRegistry_{};
    editor::EditorSceneRuntimeInstantiationService
        editorSceneRuntimeInstantiation_{};
    uint64_t editorSceneRuntimeLastReconcileAttemptRevision_ = 0;
    uint32_t railShooterFrameIndex_ = 0;
    float railWeaponHotReloadPollTimer_ = 0.0f;
    std::chrono::steady_clock::time_point railShooterLastUpdateTime_{};
    bool railShooterHasLastUpdateTime_ = false;
    uint64_t railAimAssistAppliedPresetRevision_ = 0;
    std::string railAimAssistAppliedPresetId_;
    bool railShooterInitialized_ = false;
    bool gpuDeviceLost_ = false;
    bool previousLeftMouseDown_ = false;
    struct RailVisibilityDebugOverlaySettings {
        bool enabled = true;
        bool showAimableZone = true;
        bool showActors = true;
        bool showLabels = true;
        bool showThreatCenter = true;
        float aimableZoneWidth = 0.58f;
        float aimableZoneHeight = 0.58f;
        float warningZoneWidth = 0.82f;
        float warningZoneHeight = 0.78f;
    };
    RailVisibilityDebugOverlaySettings railVisibilityDebugOverlay_{};
    struct RailCameraTuningSample {
        uint32_t frame = 0;
        float timeSeconds = 0.0f;
        float distance = 0.0f;
        std::string sectionName;
        std::string speedMode;
        std::string speedReason;
        float baseSpeed = 0.0f;
        float requestedSpeed = 0.0f;
        float actualVehicleSpeed = 0.0f;
        float zoneMultiplier = 1.0f;
        float eventMultiplier = 1.0f;
        std::string cameraMode;
        std::string cameraModeKind;
        std::string comfortReason;
        float fovYDeg = 0.0f;
        float rollDeg = 0.0f;
        float angularVelocityDeg = 0.0f;
        float angularAccelerationDeg = 0.0f;
        float fovChangeRateDeg = 0.0f;
        float linearSpeed = 0.0f;
        float stabilityScore = 1.0f;
        float shakeAmount = 0.0f;
        bool stableForAiming = true;
        bool hardTransition = false;
        bool allowEnemyFire = true;
        float aimFocusBlend = 0.0f;
        float lookAtBlend = 0.0f;
        float compositionRisk = 0.0f;
        float compositionSafetyBlend = 0.0f;
        bool compositionSafe = true;
        bool lineOfSightSafe = true;
        bool cameraCollisionSafe = true;
        bool segmentTransitionActive = false;
        float segmentTransitionBlend = 1.0f;
        bool encounterFramingActive = false;
        float encounterFramingBlend = 0.0f;
        float encounterFramingSpread = 0.0f;
        int encounterFramingEnemyCount = 0;
        int encounterFramingBossCount = 0;
        uint32_t activeEnemies = 0;
        uint32_t activeBullets = 0;
        uint32_t activeObstacles = 0;
        uint32_t lockTokenCount = 0;
        bool lockHeld = false;
        bool normalShotHeld = false;
        uint32_t normalShotsFired = 0;
        uint32_t normalShotHits = 0;
        float playerDamage = 0.0f;
        double updateMs = 0.0;
        double renderMs = 0.0;
        double presentMs = 0.0;
    };
    struct RailCameraTuningRecorderState {
        bool recording = false;
        uint32_t sampleStride = 1;
        uint32_t maxSamples = 7200;
        uint32_t recordedSamples = 0;
        uint32_t droppedSamples = 0;
        float recordingTimeSeconds = 0.0f;
        std::string status = "idle";
        std::string lastExportPath;
        std::vector<RailCameraTuningSample> samples;
    };
    RailCameraTuningRecorderState railCameraTuningRecorder_{};
    struct RailInputRouteDebugState {
        bool railSceneActive = false;
        bool lockHeld = false;
        bool lockPressed = false;
        bool lockReleased = false;
        bool normalShotEnabled = true;
        bool normalShotHeld = false;
        bool normalShotPressed = false;
        bool normalShotBlockedByUi = false;
        uint32_t normalShotsFired = 0;
        uint32_t normalShotHits = 0;
        float normalAimLateral = 0.0f;
        float normalAimVertical = 4.0f;
        bool aimAssistEnabled = true;
        bool releaseFireTriggered = false;
        uint32_t releaseTokenCount = 0;
        int releaseHitCount = 0;
        bool showcaseClickToFireEnabled = false;
        bool showcaseClickBlockedInRail = false;
        bool showcaseClickFired = false;
        bool showcaseClickIgnoredByImgui = false;
        bool leftMouseDown = false;
    };
    RailInputRouteDebugState railInputRouteDebug_{};
    struct CourseObjectEditSnapshot {
        std::vector<CourseTerrainPlacement> terrainPlacements;
        std::vector<CourseRockCluster> rockClusters;
        int selectionType = 0;
        int selectedTerrainPlacement = -1;
        int selectedRockCluster = -1;
        std::vector<int> selectedTerrainPlacements{};
        std::vector<int> selectedRockClusters{};
    };
    struct CourseObjectDragState {
        struct Item {
            int type = -1;
            int index = -1;
            float distance = 0.0f;
            float lateral = 0.0f;
            float vertical = 0.0f;
            float forward = 0.0f;
            Vector3 scale = {1.0f, 1.0f, 1.0f};
            Vector3 rotation = {};
            float minScale = 0.0f;
            float maxScale = 0.0f;
            Vector3 spread = {};
            float clearLaneRadius = 0.0f;
        };
        bool active = false;
        bool changed = false;
        int type = -1;
        int index = -1;
        int axis = -1;
        int gizmoMode = 0;
        POINT startMouse{};
        float startDistance = 0.0f;
        float startLateral = 0.0f;
        float startVertical = 0.0f;
        float startForward = 0.0f;
        Vector3 startScale = {1.0f, 1.0f, 1.0f};
        Vector3 startRotation = {};
        float startMinScale = 0.0f;
        float startMaxScale = 0.0f;
        Vector3 startSpread = {};
        float startClearLaneRadius = 0.0f;
        Vector3 pivotWorld = {};
        Vector3 localAxes[3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}};
        Vector3 handleAxes[3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}};
        Vector3 constraintPlaneNormal = {};
        Vector3 startConstraintPoint = {};
        float startAxisParameter = 0.0f;
        float handleLength = 1.0f;
        bool constraintValid = false;
        std::vector<Item> items{};
    };
    std::vector<CourseObjectEditSnapshot> courseObjectUndoStack_;
    std::vector<CourseObjectEditSnapshot> courseObjectRedoStack_;
    editor::EditorTransactionStack courseObjectTransactions_{};
    CourseObjectEditSnapshot courseObjectHistoryBaseline_{};
    uint32_t courseObjectHistoryRevision_ = 0;
    bool courseObjectHistoryInitialized_ = false;
    CourseObjectDragState courseObjectDrag_{};
    editor::EditorPropertyEditSession courseObjectGizmoEditSession_{};
    bool previousCourseEditorLeftMouseDown_ = false;
    bool releaseShowcaseInitialized_ = false;
    bool railTitleScreenVisible_ = false;
    float railTitleGameplayFade_ = 0.0f;
    DirectionalLight railTitleSavedLight_{};
    PointLight railTitleSavedPointLight_{};
    SpotLight railTitleSavedSpotLight_{};
    std::array<float,4> railTitleSavedClearColor_{};
    bool railTitleSavedSkybox_ = false;
    bool railTitleSavedBackdrop_ = false;
    std::vector<PostProcessPass> railTitleSavedPostProcess_;
    audio::SoundHandle railTitleAmbience_{};
    bool railTitleAmbiencePlaying_ = false;
    float railTitleAudioGain_ = 0.0f;
    float railTitleDustTimer_ = 0.0f;
    std::vector<uint32_t> railTitleDustIds_;
    RailTitleScene railTitleScene_;
    CourseSpawnRuntime railTitleEmptySpawns_;
    int railTitleMenuSelection_ = 0;
    bool railTitleControlsVisible_ = false;
    bool railTitleSavedTerrainEnabled_ = true;
    bool railTitleHasLastUpdate_ = false;
    std::chrono::steady_clock::time_point railTitleLastUpdate_{};
    bool releaseShowcaseTitleDirty_ = true;
    std::array<bool, 256> previousKeyDown_{};
    AppGamepadInput submissionGamepad_{};
    AppGamepadInput railAimGamepad_{};
};
