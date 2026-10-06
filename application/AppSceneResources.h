#pragma once

#include <array>
#include <d3d12.h>
#include <span>
#include <wrl.h>
#include <string>
#include <vector>

#include "../../externals/DirectXTex/DirectXTex.h"
#include "AppRuntimeState.h"
#include "AnimationClip.h"
#include "course/CourseMeshRenderQueue.h"
#include "course/CourseRailTrackRenderer.h"
#include "diagnostics/DebugDrawSystem.h"
#include "Skeleton.h"
#include "terrain/TerrainMaterialLibrary.h"
#include "WeaponAttachment.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"
#include "utils/dx12/BufferHelper.h"
#include "ModelLoaderAssimp.h"

struct RailVehicleRenderFrame;
struct RailVehicleOccupantActorFrame;
class EnemyCombatPresentationBridge;
class EnemyEncounterReadabilityDirector;
struct SphereMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cbvResource;
    TransformationMatrix* mappedCBV = nullptr;
    UINT vertexCount = 0;
};

struct SkyboxMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    Microsoft::WRL::ComPtr<ID3D12Resource> cbvResource;
    TransformationMatrix* mappedCBV = nullptr;
    UINT vertexCount = 0;
};

struct RingMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT vertexCount = 0;
};

struct CylinderMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT vertexCount = 0;
};

struct SpearMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT vertexCount = 0;
};

struct OrbitRibbonMeshData {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT vertexCount = 0;
};

struct GpuMeshResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT vertexCount = 0;
    UINT indexCount = 0;
};

struct AppGpuMaterialResource {
    MaterialData source;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer;
    Material* mappedConstants = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE albedoTextureGpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE normalTextureGpu{};
    bool albedoFallback = true;
    bool normalFallback = true;
};

static constexpr uint32_t kNumMaxInfluence = 4;

struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights{};
    std::array<int32_t, kNumMaxInfluence> jointIndices{};
};

struct JointPaletteEntry {
    Matrix4x4 skeletonSpaceMatrix{};
    Matrix4x4 skeletonSpaceInverseTransposeMatrix{};
};

struct SkinningInformation {
    uint32_t numVertices = 0;
    uint32_t padding[3]{};
};

struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices;
    Matrix4x4 meshRootInverseMatrix{};
    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView{};
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    D3D12_RESOURCE_STATES paletteState = D3D12_RESOURCE_STATE_COMMON;
    std::vector<JointPaletteEntry> paletteEntries;
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteUploadResource;
    JointPaletteEntry* mappedPaletteUpload = nullptr;
    bool paletteDirty = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> skinnedVertexResource;
    D3D12_VERTEX_BUFFER_VIEW skinnedVertexBufferView{};
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningInfoResource;
    SkinningInformation* mappedSkinningInfo = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE vertexSrvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE vertexSrvGpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE influenceSrvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE influenceSrvGpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE paletteSrvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE paletteSrvGpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE skinnedVertexUavCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE skinnedVertexUavGpu{};
    D3D12_RESOURCE_STATES skinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
};

struct SkinnedModelInstance {
    std::string name;
    std::string directory;
    std::string filename;
    ModelData model;
    AnimationClip animation;
    Skeleton skeleton;
    Animator animator;
    GpuMeshResource mesh;
    SkinCluster skinCluster;
    std::vector<AppGpuMaterialResource> gpuMaterials;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource;
    TransformationMatrix* transformData = nullptr;
    Transform transform{};
    bool visible = false;
    bool loaded = false;
};

struct SkeletonDebugLineVertex {
    Vector4 position;
    Vector4 color;
};

struct CameraForGPU {
    Vector3 worldPosition;
    float padding;
    // Object3D b2 extension; the first 16 bytes retain the shared camera ABI.
    // w enables the title rig. Shadows use the actual rolling wheel contacts.
    Vector4 titleSubject{};
    Vector4 titleForward{};
    Vector4 titleHaze{};
    std::array<Vector4, 4> titleWheelContacts{};
};
static_assert(sizeof(CameraForGPU) == 128);

struct CascadeShadowData {
    Matrix4x4 lightViewProjection[4]{};
    Vector4 cascadeSplits{};
    Vector4 parameters{}; // x: depth bias, y: strength, z: enabled, w: texel size
};

struct TerrainPbrLayerGpuConstants {
    Vector4 baseColorTintAndNormalStrength{};
    Vector4 surfaceParameters{};
    Vector4 scaleParameters{};
};

struct TerrainPbrLibraryGpuConstants {
    // Gameplay keeps layers 0..2; slice/layer 3 is the title-only Ground054.
    static constexpr size_t kGpuLayerCount = TerrainMaterialLibrary::kLayerCount + 1;
    std::array<TerrainPbrLayerGpuConstants, kGpuLayerCount> layers{};
    Vector4 blendParameters{}; // x: floor start, y: floor end, z: height blend scale, w: layer count
};
static_assert(sizeof(TerrainPbrLibraryGpuConstants) == 208);

struct AppManagedTextureResource {
    std::string name;
    std::string path;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    uint32_t descriptorIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct AppManagedModelResource {
    std::string name;
    std::string directory;
    std::string filename;
    ModelData model;
    GpuMeshResource mesh;
    D3D12_GPU_DESCRIPTOR_HANDLE textureGpu{};
    bool loaded = false;
    std::vector<AppGpuMaterialResource> gpuMaterials;
};

struct AppModelObjectInstance {
    std::string name;
    uint32_t modelIndex = 0;
    Transform transform{};
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource;
    TransformationMatrix* transformData = nullptr;
    bool visible = true;
};

// Builds the submission sword as a validated, render-ready MultiMaterial model.
// Kept public so the CPU regression suite can verify the exact procedural asset
// that is uploaded by AppSceneResources.
[[nodiscard]] ModelData BuildTrainingSwordModelDataForSubmission();
[[nodiscard]] ModelData BuildRailVehicleModelDataForSubmission();
// Title preview of the original gameplay material under the opening key light.
[[nodiscard]] Material BuildRailVehicleTitleMaterial(
    const MaterialData& source, const Material& gameplayMaterial,
    const DirectionalLight& gameplayLight, Vector3 referenceNormal,
    Vector3 referenceView, float exposureRatio = 1.0f);

class AppSceneResources {
public:
    static constexpr uint32_t kCascadeShadowCount = 4;
    static constexpr uint32_t kCascadeShadowMapSize = 2048;
    static constexpr uint32_t kCascadeShadowSrvBaseIndex = 12;
    static constexpr uint32_t kTerrainPbrSrvBaseIndex = 16;
    static constexpr uint32_t kMaterialTextureSrvBaseIndex = 160;
    static constexpr uint32_t kMaterialTextureSrvCount = 512;

    bool Initialize(
        Microsoft::WRL::ComPtr<ID3D12Device> device,
        ID3D12GraphicsCommandList* uploadCommandList,
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap,
        uint32_t descriptorSizeSRV);
    void ReleaseInitialUploadResources();
    [[nodiscard]] bool PollTerrainMaterialHotReload();
    bool ReloadTerrainMaterialAssets(
        ID3D12GraphicsCommandList* uploadCommandList,
        std::string* errorMessage = nullptr);
    void PreviewTerrainMaterialDefinitions(
        const std::array<TerrainPbrMaterialDefinition, TerrainMaterialLibrary::kLayerCount>&
            definitions);
    void ResetTerrainMaterialPreview();
    [[nodiscard]] uint64_t TerrainMaterialRevision() const noexcept {
        return terrainMaterialRevision_;
    }
    [[nodiscard]] bool TerrainMaterialReloadPending() const noexcept {
        return terrainMaterialPendingSignature_ != 0;
    }
    [[nodiscard]] const std::string& TerrainMaterialHotReloadStatus() const noexcept {
        return terrainMaterialHotReloadStatus_;
    }

    void UpdateCameraWorldPosition(const Vector3& worldPosition);
    void UpdateTransforms(
        const AppRuntimeState& runtimeState,
        Matrix4x4* wvpData,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix,
        uint32_t windowWidth,
        uint32_t windowHeight);
    void SyncRuntimeState(AppRuntimeState& runtimeState, float deltaTime,
        float vehicleToneExposure = 1.0f);
    SkinnedModelInstance* GetActiveSkinnedModel();
    const SkinnedModelInstance* GetActiveSkinnedModel() const;
    const AppManagedModelResource* FindManagedModel(uint32_t modelIndex) const;
    [[nodiscard]] uint32_t TrainingSwordModelIndex() const noexcept {
        return trainingSwordModelIndex;
    }
    [[nodiscard]] uint32_t RailVehicleModelIndex() const noexcept {
        return railVehicleModelIndex;
    }
    [[nodiscard]] const AppModelObjectInstance& WeaponAttachmentObject() const noexcept {
        return weaponAttachmentObject;
    }
    void UpdateWeaponAttachment(
        const WeaponAttachmentTelemetry& telemetry,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix);
    const std::vector<AppManagedModelResource>& ManagedModelLibrary() const { return vfxModelLibrary; }
    const std::vector<AppModelObjectInstance>& ModelObjectInstances() const { return vfxModelObjects; }
    const AppModelObjectInstance& RailVehicleObject() const noexcept {
        return railVehicleObject;
    }
    const AppModelObjectInstance& RailVehicleOccupantObject() const noexcept {
        return railVehicleOccupantObject;
    }
    const CourseMeshRenderQueue& CourseMeshes() const { return courseMeshRenderQueue; }
    const CourseRailTrackRenderer& CourseRailTrackMeshes() const {
        return courseRailTrackRenderer;
    }
    void SyncCourseMeshRenderQueue(
        const CourseSpawnRuntime& courseRuntime,
        const CourseAsset* course,
        float currentDistance,
        const RailPath& railPath,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix,
        const EnemyCombatPresentationBridge* enemyPresentation = nullptr,
        const EnemyEncounterReadabilityDirector* enemyReadability = nullptr,
        const TwinShieldDronePose* titlePursuer = nullptr);
    void SyncRailVehicleRenderFrame(
        const RailVehicleRenderFrame& frame,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix);
    void SyncCourseRailTrackRenderer(
        const CourseRailTrackMeshBakeResult& baked,
        float currentDistance,
        const RailVehicleWheelContactPresentationFrame* wheels,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix);
    void SyncRailVehicleOccupantActorFrame(
        const RailVehicleOccupantActorFrame& frame,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix);

public:
    // Sprite
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
    D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite;
    TransformationMatrix* transformationMatrixDataSprite = nullptr;

    // Material
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainMaterialResource;
    Material* terrainMaterialData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainPbrMaterialResource;
    TerrainPbrLibraryGpuConstants* terrainPbrMaterialData = nullptr;
    TerrainMaterialLibrary terrainMaterialLibrary;
    TerrainPbrMaterialDefinition titleGroundMaterialDefinition;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite;
    Material* materialDataSprite = nullptr;

    // Lights
    DirectionalLight directionalLightData{};
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource;
    DirectionalLight* mappedLight = nullptr;

    PointLight pointLightData{};
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource;
    PointLight* mappedPointLight = nullptr;

    SpotLight spotLight{};
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource;
    SpotLight* mappedSpotLight = nullptr;

    // Camera
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource;
    CameraForGPU* mappedCamera = nullptr;
    DirectionalLight titleVehicleReferenceLight{};
    Vector3 titleVehicleReferenceNormal{0,0,-1};
    Vector3 titleVehicleReferenceView{0,0.32f,-0.95f};

    // Cascaded shadow maps
    Microsoft::WRL::ComPtr<ID3D12Resource> cascadeShadowResource;
    CascadeShadowData* mappedCascadeShadow = nullptr;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kCascadeShadowCount> cascadeShadowDrawResources{};
    std::array<CascadeShadowData*, kCascadeShadowCount> mappedCascadeShadowDraw{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kCascadeShadowCount> cascadeShadowMaps{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kCascadeShadowCount> cascadeShadowDsvCpu{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kCascadeShadowCount> cascadeShadowSrvGpuHandles{};
    D3D12_GPU_DESCRIPTOR_HANDLE cascadeShadowSrvGpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE cascadeShadowSrvTableGpu{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cascadeShadowDsvHeap;
    std::array<D3D12_RESOURCE_STATES, kCascadeShadowCount> cascadeShadowStates{};

    // Texture
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainAlbedoTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainDetailCacheTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainDetailNormalMapTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainPbrNormalTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainPbrOrmTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> terrainPbrHeightTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> circle2TextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> gradationLineTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> skyboxTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> animatedCubeTextureResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> flatNormalTextureResource;
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainAlbedoTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainDetailCacheTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainDetailNormalMapTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainPbrNormalTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainPbrOrmTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE terrainPbrHeightTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE circle2TextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE gradationLineTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE skyboxTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE animatedCubeTextureSrvHandleGPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE flatNormalTextureSrvHandleGPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainAlbedoTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainDetailCacheTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainDetailNormalMapTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainPbrNormalTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainPbrOrmTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE terrainPbrHeightTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE circle2TextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE gradationLineTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE skyboxTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE animatedCubeTextureSrvHandleCPU{};
    D3D12_CPU_DESCRIPTOR_HANDLE flatNormalTextureSrvHandleCPU{};
    std::vector<AppManagedTextureResource> vfxTextureLibrary;
    std::vector<AppManagedModelResource> vfxModelLibrary;
    std::vector<AppModelObjectInstance> vfxModelObjects;
    uint32_t trainingSwordModelIndex = UINT32_MAX;
    uint32_t railVehicleModelIndex = UINT32_MAX;
    AppModelObjectInstance weaponAttachmentObject{};
    AppModelObjectInstance railVehicleObject{};
    AppModelObjectInstance railVehicleOccupantObject{};
    CourseMeshRenderQueue courseMeshRenderQueue;
    CourseRailTrackRenderer courseRailTrackRenderer;

    // Sphere
    SphereMeshData sphere{};

    // Skybox
    SkyboxMeshData skybox{};

    // VFX primitives
    RingMeshData ring{};
    CylinderMeshData cylinder{};
    SpearMeshData spear{};
    OrbitRibbonMeshData orbitRibbon{};

    // Model
    ModelData modelData{};
    GpuMeshResource modelMesh{};

    ModelData animatedCubeData{};
    AnimationClip animatedCubeAnimation{};
    GpuMeshResource animatedCubeMesh{};
    Microsoft::WRL::ComPtr<ID3D12Resource> animatedCubeTransformResource;
    TransformationMatrix* animatedCubeTransformData = nullptr;

    static constexpr uint32_t kSkinnedModelCount = 3;
    std::array<SkinnedModelInstance, kSkinnedModelCount> skinnedModels{};
    uint32_t activeSkinnedModelIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> skeletonDebugVertexResource;
    Microsoft::WRL::ComPtr<ID3D12Resource> skeletonDebugTransformResource;
    D3D12_VERTEX_BUFFER_VIEW skeletonDebugVBV{};
    SkeletonDebugLineVertex* mappedSkeletonDebugLines = nullptr;
    TransformationMatrix* skeletonDebugTransformData = nullptr;
    UINT skeletonDebugVertexCapacity = 0;
    UINT skeletonDebugVertexCount = 0;
    ge3::debug::DebugDrawSystem debugDraw;
    // Dedicated gameplay production buffer. It deliberately does not share
    // Debug.Draw visibility flags so hostile fire remains visible in Release.
    ge3::debug::DebugDrawSystem enemyThreatVisualDraw;

private:
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> initialUploadResources_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> terrainRuntimeUploadResources_;
    Microsoft::WRL::ComPtr<ID3D12Device> terrainPbrDevice_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> terrainPbrSrvHeap_;
    uint32_t terrainPbrDescriptorSize_ = 0;
    uint64_t terrainMaterialWatchSignature_ = 0;
    uint64_t terrainMaterialPendingSignature_ = 0;
    uint64_t terrainMaterialPendingSinceMs_ = 0;
    uint64_t terrainMaterialNextPollMs_ = 0;
    uint64_t terrainMaterialRevision_ = 1;
    std::string terrainMaterialHotReloadStatus_ = "Not initialized";
};
