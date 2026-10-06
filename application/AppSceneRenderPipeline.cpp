#include "AppSceneRenderPipeline.h"
#include "AppLogFile.h"
#include "editor/scene/EditorProductionScenePipeline.h"
#include "editor/geometry/EditorTransientMeshRenderPath.h"
#include "editor/material/EditorProductionMaterialPipeline.h"
#include "editor/texture/EditorProductionTexturePipeline.h"
#include "editor/shader/EditorProductionShaderPipeline.h"
#include "editor/lighting/EditorProductionLightingPipeline.h"
#include "editor/visibility/EditorProductionGpuDrivenPipeline.h"
#include "editor/streaming/EditorWorldPartitionPipeline.h"

#include <Windows.h>

#include "AppFrameGraphBuilder.h"
#include "AppFrameRenderer.h"
#include "AppPipelines.h"
#include "AppRenderResources.h"
#include "AppRuntimeState.h"
#include "AppSceneResources.h"
#include "graphics/RenderGraph.h"
#include "terrain/TerrainChunkManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kSkinningThreadGroupSize = 256;

enum SkinningComputeRootParameter : uint32_t {
    kSkinningRootInputVertices = 0,
    kSkinningRootInfluences = 1,
    kSkinningRootPalette = 2,
    kSkinningRootOutputVertices = 3,
    kSkinningRootInfo = 4,
};

Vector4 TransformToClipSpace(const Vector4& position, const Matrix4x4& matrix) {
    return {
        position.x * matrix.m[0][0] + position.y * matrix.m[1][0] +
            position.z * matrix.m[2][0] + position.w * matrix.m[3][0],
        position.x * matrix.m[0][1] + position.y * matrix.m[1][1] +
            position.z * matrix.m[2][1] + position.w * matrix.m[3][1],
        position.x * matrix.m[0][2] + position.y * matrix.m[1][2] +
            position.z * matrix.m[2][2] + position.w * matrix.m[3][2],
        position.x * matrix.m[0][3] + position.y * matrix.m[1][3] +
            position.z * matrix.m[2][3] + position.w * matrix.m[3][3],
    };
}

void UpdateWeaponScreenBounds(
    const AppManagedModelResource& model,
    const TransformationMatrix& transform,
    const D3D12_VIEWPORT& viewport,
    RuntimeWeaponDrawTelemetry& telemetry) {
    float minimumX = (std::numeric_limits<float>::max)();
    float minimumY = (std::numeric_limits<float>::max)();
    float minimumZ = (std::numeric_limits<float>::max)();
    float maximumX = std::numeric_limits<float>::lowest();
    float maximumY = std::numeric_limits<float>::lowest();
    float maximumZ = std::numeric_limits<float>::lowest();
    bool hasProjectedVertex = false;

    for (const VertexData& vertex : model.model.vertices) {
        const Vector4 clip = TransformToClipSpace(vertex.position, transform.WVP);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
            !std::isfinite(clip.z) || !std::isfinite(clip.w) || clip.w <= 0.00001f) {
            continue;
        }
        const float reciprocalW = 1.0f / clip.w;
        const float x = clip.x * reciprocalW;
        const float y = clip.y * reciprocalW;
        const float z = clip.z * reciprocalW;
        minimumX = (std::min)(minimumX, x);
        minimumY = (std::min)(minimumY, y);
        minimumZ = (std::min)(minimumZ, z);
        maximumX = (std::max)(maximumX, x);
        maximumY = (std::max)(maximumY, y);
        maximumZ = (std::max)(maximumZ, z);
        hasProjectedVertex = true;
    }

    telemetry.screenBoundsVisible = hasProjectedVertex &&
        maximumX >= -1.0f && minimumX <= 1.0f &&
        maximumY >= -1.0f && minimumY <= 1.0f &&
        maximumZ >= 0.0f && minimumZ <= 1.0f;
    if (!hasProjectedVertex) {
        return;
    }
    telemetry.screenMinimum = {
        viewport.TopLeftX + (minimumX * 0.5f + 0.5f) * viewport.Width,
        viewport.TopLeftY + (-maximumY * 0.5f + 0.5f) * viewport.Height,
    };
    telemetry.screenMaximum = {
        viewport.TopLeftX + (maximumX * 0.5f + 0.5f) * viewport.Width,
        viewport.TopLeftY + (-minimumY * 0.5f + 0.5f) * viewport.Height,
    };
    constexpr float kMinimumReadableExtentPixels = 12.0f;
    telemetry.screenBoundsReadable = telemetry.screenBoundsVisible &&
        telemetry.screenMaximum.x - telemetry.screenMinimum.x >=
            kMinimumReadableExtentPixels &&
        telemetry.screenMaximum.y - telemetry.screenMinimum.y >=
            kMinimumReadableExtentPixels;
}

class SkinningGpuTimingProbe {
public:
    void Begin(
        ID3D12GraphicsCommandList* commandList,
        AppRuntimeState* runtimeState,
        const char* path,
        uint32_t vertexCount,
        uint32_t indexCount,
        uint32_t groupCount) {
        if (commandList == nullptr || path == nullptr) {
            return;
        }

        RefreshLogPath();
        if (!EnsureResources(commandList)) {
            return;
        }

        WritePreviousResult();

        pending_ = true;
        currentRuntimeState_ = runtimeState;
        currentPath_ = path;
        currentVertexCount_ = vertexCount;
        currentIndexCount_ = indexCount;
        currentGroupCount_ = groupCount;
        commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }

    void End(ID3D12GraphicsCommandList* commandList) {
        if (!pending_ || commandList == nullptr || queryHeap_ == nullptr || readback_ == nullptr) {
            return;
        }

        commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commandList->ResolveQueryData(
            queryHeap_.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            2,
            readback_.Get(),
            0);
    }

    void Cancel() {
        pending_ = false;
        currentRuntimeState_ = nullptr;
    }

private:
    void RefreshLogPath() {
        char path[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableA("GE3_SKINNING_TIMING_LOG", path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            logEnabled_ = false;
            logPath_.clear();
            headerWritten_ = false;
            return;
        }

        if (logPath_ != path) {
            logPath_ = path;
            headerWritten_ = false;
        }
        logEnabled_ = true;
    }

    bool EnsureResources(ID3D12GraphicsCommandList* commandList) {
        if (queryHeap_ != nullptr && readback_ != nullptr) {
            return true;
        }

        ComPtr<ID3D12Device> device;
        if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device)))) {
            return false;
        }

        D3D12_QUERY_HEAP_DESC queryHeapDesc{};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = 2;
        if (FAILED(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap_)))) {
            return false;
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeof(uint64_t) * 2;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        return SUCCEEDED(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback_)));
    }

    void WritePreviousResult() {
        if (!pending_ || readback_ == nullptr) {
            return;
        }

        uint64_t* timestamps = nullptr;
        D3D12_RANGE readRange{0, sizeof(uint64_t) * 2};
        if (FAILED(readback_->Map(0, &readRange, reinterpret_cast<void**>(&timestamps))) ||
            timestamps == nullptr) {
            return;
        }
        const uint64_t begin = timestamps[0];
        const uint64_t end = timestamps[1];
        const uint64_t ticks = end >= begin ? end - begin : 0;
        D3D12_RANGE writeRange{0, 0};
        readback_->Unmap(0, &writeRange);

        UpdateRuntimeStats(ticks);

        if (!logEnabled_) {
            pending_ = false;
            currentRuntimeState_ = nullptr;
            return;
        }

        std::ofstream log = app::OpenRotatingLog(logPath_);
        if (!log) {
            pending_ = false;
            currentRuntimeState_ = nullptr;
            return;
        }

        if (!headerWritten_) {
            log << "path,vertexCount,indexCount,groupCount,gpuTicks\n";
            headerWritten_ = true;
        }

        log << currentPath_ << ","
            << currentVertexCount_ << ","
            << currentIndexCount_ << ","
            << currentGroupCount_ << ","
            << ticks << "\n";
        pending_ = false;
        currentRuntimeState_ = nullptr;
    }

    RuntimeSkinningTimingPathStats* SelectStatsPath() {
        if (currentRuntimeState_ == nullptr) {
            return nullptr;
        }
        if (currentPath_ == "vertex_shader_total") {
            return &currentRuntimeState_->skinningTiming.vertexShaderTotal;
        }
        if (currentPath_ == "compute_total") {
            return &currentRuntimeState_->skinningTiming.computeTotal;
        }
        if (currentPath_ == "compute_surface_only") {
            return &currentRuntimeState_->skinningTiming.computeSurfaceOnly;
        }
        return nullptr;
    }

    void UpdateRuntimeStats(uint64_t ticks) {
        RuntimeSkinningTimingPathStats* stats = SelectStatsPath();
        if (stats == nullptr) {
            return;
        }

        stats->valid = true;
        stats->lastTicks = ticks;
        if (stats->sampleCount == 0) {
            stats->minTicks = ticks;
            stats->maxTicks = ticks;
            stats->averageTicks = static_cast<double>(ticks);
        } else {
            stats->minTicks = (std::min)(stats->minTicks, ticks);
            stats->maxTicks = (std::max)(stats->maxTicks, ticks);
            stats->averageTicks +=
                (static_cast<double>(ticks) - stats->averageTicks) /
                static_cast<double>(stats->sampleCount + 1);
        }
        ++stats->sampleCount;
    }

    ComPtr<ID3D12QueryHeap> queryHeap_;
    ComPtr<ID3D12Resource> readback_;
    AppRuntimeState* currentRuntimeState_ = nullptr;
    std::string logPath_;
    std::string currentPath_;
    uint32_t currentVertexCount_ = 0;
    uint32_t currentIndexCount_ = 0;
    uint32_t currentGroupCount_ = 0;
    bool pending_ = false;
    bool headerWritten_ = false;
    bool logEnabled_ = false;
};

SkinningGpuTimingProbe& GetSkinningGpuTimingProbe() {
    static SkinningGpuTimingProbe probe;
    return probe;
}

void TransitionResource(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
    if (commandList == nullptr || resource == nullptr || before == after) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

void WriteSkinningComputeProbe(uint32_t vertexCount, uint32_t groupCount) {
    char logPath[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "GE3_SKINNING_COMPUTE_PROBE_LOG",
        logPath,
        MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }

    static bool wrote = false;
    if (wrote) {
        return;
    }
    wrote = true;

    std::ofstream log(logPath, std::ios::out | std::ios::trunc);
    if (!log) {
        return;
    }
    log << "SkinningComputePath=Dispatch\n";
    log << "VertexCount=" << vertexCount << "\n";
    log << "ThreadGroupSize=" << kSkinningThreadGroupSize << "\n";
    log << "GroupCount=" << groupCount << "\n";
}

bool IsComputeSkinningDisabled() {
    return GetEnvironmentVariableA("GE3_DISABLE_COMPUTE_SKINNING", nullptr, 0) > 0;
}

bool RequiresSkinnedSurfaceVfx(const AppRuntimeState* runtimeState) {
    return runtimeState != nullptr && runtimeState->vfx.enableSkinnedSurfaceVfx;
}

bool UploadPaletteIfNeeded(
    ID3D12GraphicsCommandList* commandList,
    SkinCluster& skinCluster) {
    if (!skinCluster.paletteDirty) {
        return true;
    }
    if (commandList == nullptr ||
        skinCluster.paletteResource == nullptr ||
        skinCluster.paletteUploadResource == nullptr ||
        skinCluster.mappedPaletteUpload == nullptr ||
        skinCluster.paletteEntries.empty()) {
        return false;
    }

    const size_t paletteBytes =
        sizeof(JointPaletteEntry) * skinCluster.paletteEntries.size();
    std::memcpy(
        skinCluster.mappedPaletteUpload,
        skinCluster.paletteEntries.data(),
        paletteBytes);

    TransitionResource(
        commandList,
        skinCluster.paletteResource.Get(),
        skinCluster.paletteState,
        D3D12_RESOURCE_STATE_COPY_DEST);
    skinCluster.paletteState = D3D12_RESOURCE_STATE_COPY_DEST;

    commandList->CopyBufferRegion(
        skinCluster.paletteResource.Get(),
        0,
        skinCluster.paletteUploadResource.Get(),
        0,
        paletteBytes);

    TransitionResource(
        commandList,
        skinCluster.paletteResource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    skinCluster.paletteState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    skinCluster.paletteDirty = false;
    return true;
}

bool DispatchSkinningPass(
    const AppFrameGraphBuildContext& ctx,
    ge3::graphics::RenderPassContext& passContext,
    SkinnedModelInstance& instance) {
    SkinCluster& skinCluster = instance.skinCluster;
    if (!instance.loaded ||
        IsComputeSkinningDisabled() ||
        instance.mesh.vertexCount == 0 ||
        ctx.srvDescriptorHeap == nullptr ||
        ctx.appPipelines->GetSkinningComputeRootSignature() == nullptr ||
        ctx.appPipelines->GetSkinningComputePSO() == nullptr ||
        skinCluster.vertexSrvGpu.ptr == 0 ||
        skinCluster.influenceSrvGpu.ptr == 0 ||
        skinCluster.paletteSrvGpu.ptr == 0 ||
        skinCluster.skinnedVertexUavGpu.ptr == 0 ||
        skinCluster.skinningInfoResource == nullptr ||
        skinCluster.skinnedVertexResource == nullptr) {
        return false;
    }

    if (!UploadPaletteIfNeeded(passContext.commandList, skinCluster)) {
        return false;
    }

    TransitionResource(
        passContext.commandList,
        skinCluster.skinnedVertexResource.Get(),
        skinCluster.skinnedVertexState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    ID3D12DescriptorHeap* descriptorHeaps[] = { ctx.srvDescriptorHeap };
    passContext.commandList->SetDescriptorHeaps(1, descriptorHeaps);
    passContext.commandList->SetComputeRootSignature(
        ctx.appPipelines->GetSkinningComputeRootSignature());
    passContext.commandList->SetPipelineState(ctx.appPipelines->GetSkinningComputePSO());
    passContext.commandList->SetComputeRootDescriptorTable(
        kSkinningRootInputVertices,
        skinCluster.vertexSrvGpu);
    passContext.commandList->SetComputeRootDescriptorTable(
        kSkinningRootInfluences,
        skinCluster.influenceSrvGpu);
    passContext.commandList->SetComputeRootDescriptorTable(
        kSkinningRootPalette,
        skinCluster.paletteSrvGpu);
    passContext.commandList->SetComputeRootDescriptorTable(
        kSkinningRootOutputVertices,
        skinCluster.skinnedVertexUavGpu);
    passContext.commandList->SetComputeRootConstantBufferView(
        kSkinningRootInfo,
        skinCluster.skinningInfoResource->GetGPUVirtualAddress());

    const uint32_t groupCount =
        (instance.mesh.vertexCount + kSkinningThreadGroupSize - 1) / kSkinningThreadGroupSize;
    WriteSkinningComputeProbe(instance.mesh.vertexCount, groupCount);
    passContext.commandList->Dispatch(groupCount, 1, 1);

    TransitionResource(
        passContext.commandList,
        skinCluster.skinnedVertexResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    return true;
}

bool DispatchComputeSkinnedSurface(
    const AppFrameGraphBuildContext& ctx,
    ge3::graphics::RenderPassContext& passContext,
    SkinnedModelInstance& instance) {
    if (!instance.loaded || IsComputeSkinningDisabled()) {
        return false;
    }

    const uint32_t groupCount =
        (instance.mesh.vertexCount + kSkinningThreadGroupSize - 1) / kSkinningThreadGroupSize;
    GetSkinningGpuTimingProbe().Begin(
        passContext.commandList,
        ctx.runtimeState,
        "compute_surface_only",
        instance.mesh.vertexCount,
        instance.mesh.indexCount,
        groupCount);

    if (!DispatchSkinningPass(ctx, passContext, instance)) {
        GetSkinningGpuTimingProbe().Cancel();
        return false;
    }

    GetSkinningGpuTimingProbe().End(passContext.commandList);
    return true;
}

const AppGpuMaterialResource* ResolveGpuMaterial(
    const std::vector<AppGpuMaterialResource>& materials,
    uint32_t materialIndex) {
    if (materialIndex >= materials.size()) {
        return materials.empty() ? nullptr : &materials.front();
    }
    return &materials[materialIndex];
}

bool DrawComputeSkinnedModelInstance(
    const AppFrameGraphBuildContext& ctx,
    ge3::graphics::RenderPassContext& passContext,
    SkinnedModelInstance& instance) {
    if (!instance.loaded ||
        IsComputeSkinningDisabled() ||
        !instance.visible ||
        !instance.transformResource ||
        instance.skinCluster.skinnedVertexBufferView.BufferLocation == 0 ||
        ctx.srvDescriptorHeap == nullptr ||
        ctx.appPipelines->GetSkinningComputeRootSignature() == nullptr ||
        ctx.appPipelines->GetSkinningComputePSO() == nullptr ||
        instance.skinCluster.vertexSrvGpu.ptr == 0 ||
        instance.skinCluster.influenceSrvGpu.ptr == 0 ||
        instance.skinCluster.paletteSrvGpu.ptr == 0 ||
        instance.skinCluster.skinnedVertexUavGpu.ptr == 0 ||
        instance.skinCluster.skinningInfoResource == nullptr ||
        instance.skinCluster.skinnedVertexResource == nullptr) {
        return false;
    }

    const uint32_t groupCount =
        (instance.mesh.vertexCount + kSkinningThreadGroupSize - 1) / kSkinningThreadGroupSize;
    GetSkinningGpuTimingProbe().Begin(
        passContext.commandList,
        ctx.runtimeState,
        "compute_total",
        instance.mesh.vertexCount,
        instance.mesh.indexCount,
        groupCount);

    if (!DispatchSkinningPass(ctx, passContext, instance)) {
        GetSkinningGpuTimingProbe().Cancel();
        return false;
    }

    const bool mainReady = ctx.frameRenderer->PrepareMainPass(
        passContext.commandList,
        ctx.srvDescriptorHeap,
        ctx.runtimeState->viewport,
        ctx.runtimeState->scissorRect,
        ctx.appPipelines->GetMainRootSignature(),
        ctx.appPipelines->GetMainPSO());
    if (!mainReady) {
        GetSkinningGpuTimingProbe().Cancel();
        return false;
    }

    for (const SubMeshData& subMesh : instance.model.subMeshes) {
        const AppGpuMaterialResource* material =
            ResolveGpuMaterial(instance.gpuMaterials, subMesh.materialIndex);
        if (material == nullptr || material->constantBuffer == nullptr) {
            continue;
        }
        ctx.frameRenderer->DrawMainModel(
            passContext.commandList,
            instance.skinCluster.skinnedVertexBufferView,
            instance.mesh.ibv,
            material->constantBuffer->GetGPUVirtualAddress(),
            instance.transformResource->GetGPUVirtualAddress(),
            material->albedoTextureGpu,
            material->normalTextureGpu,
            ctx.scene->textureSrvHandleGPU2,
            ctx.scene->skyboxTextureSrvHandleGPU,
            ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
            ctx.scene->cameraResource->GetGPUVirtualAddress(),
            ctx.scene->pointLightResource->GetGPUVirtualAddress(),
            ctx.scene->spotLightResource->GetGPUVirtualAddress(),
            subMesh.indexCount,
            subMesh.indexStart);
    }
    GetSkinningGpuTimingProbe().End(passContext.commandList);
    return true;
}

bool DrawSkinnedModelInstance(
    const AppFrameGraphBuildContext& ctx,
    ge3::graphics::RenderPassContext& passContext,
    SkinnedModelInstance& instance) {
    if (!instance.loaded ||
        !instance.visible ||
        !instance.transformResource ||
        instance.skinCluster.paletteSrvGpu.ptr == 0) {
        return false;
    }

    GetSkinningGpuTimingProbe().Begin(
        passContext.commandList,
        ctx.runtimeState,
        "vertex_shader_total",
        instance.mesh.vertexCount,
        instance.mesh.indexCount,
        0);

    if (!UploadPaletteIfNeeded(passContext.commandList, instance.skinCluster)) {
        GetSkinningGpuTimingProbe().Cancel();
        return false;
    }

    const bool skinnedReady = ctx.frameRenderer->PrepareMainPass(
        passContext.commandList,
        ctx.srvDescriptorHeap,
        ctx.runtimeState->viewport,
        ctx.runtimeState->scissorRect,
        ctx.appPipelines->GetSkinnedRootSignature(),
        ctx.appPipelines->GetSkinnedPSO());
    if (!skinnedReady) {
        GetSkinningGpuTimingProbe().Cancel();
        return false;
    }

    bool submitted = false;
    for (const SubMeshData& subMesh : instance.model.subMeshes) {
        const AppGpuMaterialResource* material =
            ResolveGpuMaterial(instance.gpuMaterials, subMesh.materialIndex);
        if (material == nullptr || material->constantBuffer == nullptr) {
            continue;
        }
        ctx.frameRenderer->DrawSkinnedModel(
            passContext.commandList,
            instance.mesh.vbv,
            instance.skinCluster.influenceBufferView,
            instance.mesh.ibv,
            material->constantBuffer->GetGPUVirtualAddress(),
            instance.transformResource->GetGPUVirtualAddress(),
            material->albedoTextureGpu,
            material->normalTextureGpu,
            ctx.scene->textureSrvHandleGPU2,
            ctx.scene->skyboxTextureSrvHandleGPU,
            instance.skinCluster.paletteSrvGpu,
            ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
            ctx.scene->cameraResource->GetGPUVirtualAddress(),
            ctx.scene->pointLightResource->GetGPUVirtualAddress(),
            ctx.scene->spotLightResource->GetGPUVirtualAddress(),
            subMesh.indexCount,
            subMesh.indexStart);
        submitted = true;
    }
    GetSkinningGpuTimingProbe().End(passContext.commandList);
    return submitted;
}

} // namespace

void AppSceneRenderPipeline::RegisterPasses(const AppFrameGraphBuildContext& ctx) const {
    ctx.renderGraph->DeclarePersistentRenderTarget(
        "SceneColor",
        1.0f,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        ctx.runtimeState != nullptr ? ctx.runtimeState->clearColor : nullptr,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    ctx.renderGraph->DeclarePersistentDepthTarget(
        "SceneDepth",
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        1.0f,
        0,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    const editor::EditorProductionMeshDrawMode productionDrawMode =
        ctx.productionScenePipeline != nullptr
            ? ctx.productionScenePipeline->DrawMode()
            : editor::EditorProductionMeshDrawMode::Auto;
    if (ctx.productionGpuDrivenPipeline != nullptr &&
        productionDrawMode !=
            editor::EditorProductionMeshDrawMode::ForceDirect) {
        ctx.renderGraph->AddPass({
            "Visibility.ProductionGpuDriven",
            ge3::graphics::RenderPassLayer::Geometry,
            {},
            "",
            [ctx](ge3::graphics::RenderPassContext& passContext) {
                D3D12_GPU_DESCRIPTOR_HANDLE hiZ{};
                bool hiZFresh = false;
                if (ctx.terrainChunkManager != nullptr &&
                    ctx.frameState != nullptr) {
                    hiZ =
                        ctx.terrainChunkManager->GetHiZDebugSrv(2);
                    hiZFresh =
                        ctx.terrainChunkManager->IsHiZFresh(
                            ctx.frameState->viewProjectionMatrix);
                }
                ctx.productionGpuDrivenPipeline->DispatchVisibility(
                    passContext.commandList, hiZ, hiZFresh);
            },
            true});
    }

    if (ctx.productionLightingPipeline != nullptr && ctx.productionScenePipeline != nullptr) {
        ctx.renderGraph->AddPass({
            "Lighting.ProductionShadowAtlas",
            ge3::graphics::RenderPassLayer::Geometry,
            {},
            "",
            [ctx](ge3::graphics::RenderPassContext& passContext) {
                ctx.productionLightingPipeline->RenderShadowMaps(
                    passContext.commandList, ctx.productionScenePipeline->RenderPackets());
            },
            true});
    }
    ctx.renderGraph->DeclarePersistentDepthTarget(
        "SceneDepthReadOnly",
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        1.0f,
        0,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    if ((ctx.runtimeState->showSkybox || ctx.runtimeState->showProceduralBackdrop) &&
        ctx.scene->skybox.cbvResource &&
        ctx.scene->skyboxTextureSrvHandleGPU.ptr != 0) {
        ctx.renderGraph->AddPass({
            ctx.runtimeState->showProceduralBackdrop ? "Background.ProceduralBackdrop" : "Background.Skybox",
            ge3::graphics::RenderPassLayer::Geometry,
            {
                {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            },
            "",
            [ctx](ge3::graphics::RenderPassContext& passContext) {
                const bool skyboxReady = ctx.frameRenderer->PrepareMainPass(
                    passContext.commandList,
                    ctx.srvDescriptorHeap,
                    ctx.runtimeState->viewport,
                    ctx.runtimeState->scissorRect,
                    ctx.appPipelines->GetSkyboxRootSignature(),
                    ctx.appPipelines->GetSkyboxPSO());

                if (skyboxReady) {
                    ctx.frameRenderer->DrawSkybox(
                        passContext.commandList,
                        ctx.srvDescriptorHeap,
                        ctx.scene->skybox.vbv,
                        ctx.scene->skybox.cbvResource->GetGPUVirtualAddress(),
                        ctx.scene->skyboxTextureSrvHandleGPU,
                        ctx.scene->skybox.vertexCount);
                }
            }});
    }

    ctx.renderGraph->AddPass({
        "Geometry.Sprite",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::WriteDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            if (!ctx.runtimeState->showSprite) {
                return;
            }
            const bool spritePassReady = ctx.frameRenderer->PrepareMainPass(
                passContext.commandList,
                ctx.srvDescriptorHeap,
                ctx.runtimeState->viewport,
                ctx.runtimeState->scissorRect,
                ctx.appPipelines->GetSpriteRootSignature(),
                ctx.appPipelines->GetSpritePSO());

            if (spritePassReady &&
                ctx.scene->materialResourceSprite &&
                ctx.scene->transformationMatrixResourceSprite) {
                ctx.frameRenderer->DrawSprite(
                    passContext.commandList,
                    ctx.srvDescriptorHeap,
                    ctx.scene->indexBufferViewSprite,
                    ctx.renderResources->SharedSpriteQuadVertexBufferView(),
                    ctx.scene->materialResourceSprite->GetGPUVirtualAddress(),
                    ctx.scene->transformationMatrixResourceSprite->GetGPUVirtualAddress(),
                    ctx.spriteTextureHandle);
            } else {
                OutputDebugStringA("[AppSceneRenderPipeline] Sprite pass skipped because pipeline or resources are not ready.\n");
            }
        }});

    ctx.renderGraph->AddPass({
        "Geometry.MainModel",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::WriteDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            bool mainPassPrepared = false;
            auto prepareMainPass = [&]() {
                if (mainPassPrepared) {
                    return true;
                }
                mainPassPrepared = ctx.frameRenderer->PrepareMainPass(
                    passContext.commandList,
                    ctx.srvDescriptorHeap,
                    ctx.runtimeState->viewport,
                    ctx.runtimeState->scissorRect,
                    ctx.appPipelines->GetMainRootSignature(),
                    ctx.appPipelines->GetMainPSO());
                return mainPassPrepared;
            };
            auto drawManagedModel = [&](const AppManagedModelResource& model,
                                        const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
                                        const D3D12_INDEX_BUFFER_VIEW& indexBuffer,
                                        D3D12_GPU_VIRTUAL_ADDRESS transformAddress,
                                        D3D12_GPU_VIRTUAL_ADDRESS materialOverride = 0) -> uint32_t {
                if (transformAddress == 0 || model.mesh.indexCount == 0 ||
                    !prepareMainPass()) {
                    return 0;
                }
                uint32_t submittedSubMeshCount = 0;
                for (const SubMeshData& subMesh : model.model.subMeshes) {
                    const AppGpuMaterialResource* material =
                        ResolveGpuMaterial(model.gpuMaterials, subMesh.materialIndex);
                    if (material == nullptr || material->constantBuffer == nullptr ||
                        material->albedoTextureGpu.ptr == 0 ||
                        material->normalTextureGpu.ptr == 0) {
                        continue;
                    }
                    ctx.frameRenderer->DrawMainModel(
                        passContext.commandList,
                        vertexBuffer,
                        indexBuffer,
                        materialOverride != 0
                            ? materialOverride
                            : material->constantBuffer->GetGPUVirtualAddress(),
                        transformAddress,
                        material->albedoTextureGpu,
                        material->normalTextureGpu,
                        ctx.scene->textureSrvHandleGPU2,
                        ctx.scene->skyboxTextureSrvHandleGPU,
                        ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
                        ctx.scene->cameraResource->GetGPUVirtualAddress(),
                        ctx.scene->pointLightResource->GetGPUVirtualAddress(),
                        ctx.scene->spotLightResource->GetGPUVirtualAddress(),
                        subMesh.indexCount,
                        subMesh.indexStart);
                    ++submittedSubMeshCount;
                }
                return submittedSubMeshCount;
            };

            if (ctx.runtimeState->useMonsterBall && prepareMainPass()) {
                ctx.frameRenderer->DrawMainModel(
                    passContext.commandList,
                    ctx.scene->modelMesh.vbv,
                    ctx.scene->modelMesh.ibv,
                    ctx.scene->materialResource->GetGPUVirtualAddress(),
                    ctx.scene->sphere.cbvResource->GetGPUVirtualAddress(),
                    ctx.scene->textureSrvHandleGPU2,
                    ctx.scene->textureSrvHandleGPU2,
                    ctx.scene->textureSrvHandleGPU2,
                    ctx.scene->skyboxTextureSrvHandleGPU,
                    ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
                    ctx.scene->cameraResource->GetGPUVirtualAddress(),
                    ctx.scene->pointLightResource->GetGPUVirtualAddress(),
                    ctx.scene->spotLightResource->GetGPUVirtualAddress(),
                    ctx.scene->modelMesh.indexCount);
            }

            if (ctx.runtimeState->showAnimatedCube &&
                ctx.scene->animatedCubeTransformResource &&
                ctx.scene->animatedCubeTextureSrvHandleGPU.ptr != 0 &&
                prepareMainPass()) {
                ctx.frameRenderer->DrawMainModel(
                    passContext.commandList,
                    ctx.scene->animatedCubeMesh.vbv,
                    ctx.scene->animatedCubeMesh.ibv,
                    ctx.scene->materialResource->GetGPUVirtualAddress(),
                    ctx.scene->animatedCubeTransformResource->GetGPUVirtualAddress(),
                    ctx.scene->animatedCubeTextureSrvHandleGPU,
                    ctx.scene->textureSrvHandleGPU2,
                    ctx.scene->textureSrvHandleGPU2,
                    ctx.scene->skyboxTextureSrvHandleGPU,
                    ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
                    ctx.scene->cameraResource->GetGPUVirtualAddress(),
                    ctx.scene->pointLightResource->GetGPUVirtualAddress(),
                    ctx.scene->spotLightResource->GetGPUVirtualAddress(),
                    ctx.scene->animatedCubeMesh.indexCount);
            }

            for (const AppModelObjectInstance& object : ctx.scene->ModelObjectInstances()) {
                const AppManagedModelResource* model =
                    ctx.scene->FindManagedModel(object.modelIndex);
                if (!object.visible ||
                    model == nullptr ||
                    !object.transformResource) {
                    continue;
                }
                drawManagedModel(
                    *model,
                    model->mesh.vbv,
                    model->mesh.ibv,
                    object.transformResource->GetGPUVirtualAddress());
            }

            const AppModelObjectInstance& weapon =
                ctx.scene->WeaponAttachmentObject();
            const AppManagedModelResource* weaponModel =
                ctx.scene->FindManagedModel(weapon.modelIndex);
            RuntimeWeaponDrawTelemetry& weaponDraw =
                ctx.runtimeState->weaponDrawTelemetry;
            weaponDraw = {};
            if (weapon.visible &&
                weaponModel != nullptr &&
                weapon.transformResource) {
                weaponDraw.materialCount =
                    static_cast<uint32_t>(weaponModel->gpuMaterials.size());
                weaponDraw.submittedSubMeshCount = drawManagedModel(
                    *weaponModel,
                    weaponModel->mesh.vbv,
                    weaponModel->mesh.ibv,
                    weapon.transformResource->GetGPUVirtualAddress());
                weaponDraw.submitted = weaponDraw.submittedSubMeshCount != 0;
                if (weapon.transformData != nullptr) {
                    UpdateWeaponScreenBounds(
                        *weaponModel,
                        *weapon.transformData,
                        ctx.runtimeState->viewport,
                        weaponDraw);
                }
            }

            const AppModelObjectInstance& railVehicle =
                ctx.scene->RailVehicleObject();
            const AppManagedModelResource* railVehicleModel =
                ctx.scene->FindManagedModel(railVehicle.modelIndex);
            if (railVehicle.visible &&
                railVehicleModel != nullptr &&
                railVehicle.transformResource) {
                drawManagedModel(
                    *railVehicleModel,
                    railVehicleModel->mesh.vbv,
                    railVehicleModel->mesh.ibv,
                    railVehicle.transformResource->GetGPUVirtualAddress());
            }

            const AppModelObjectInstance& railVehicleOccupant =
                ctx.scene->RailVehicleOccupantObject();
            const AppManagedModelResource* railVehicleOccupantModel =
                ctx.scene->FindManagedModel(railVehicleOccupant.modelIndex);
            if (railVehicleOccupant.visible &&
                railVehicleOccupantModel != nullptr &&
                railVehicleOccupant.transformResource) {
                drawManagedModel(
                    *railVehicleOccupantModel,
                    railVehicleOccupantModel->mesh.vbv,
                    railVehicleOccupantModel->mesh.ibv,
                    railVehicleOccupant.transformResource->GetGPUVirtualAddress());
            }

            for (const CourseMeshRenderItem& item : ctx.scene->CourseMeshes().Items()) {
                const AppManagedModelResource* model =
                    ctx.scene->FindManagedModel(item.modelIndex);
                if (IsTitleLandscapeMesh(item.meshId) || !item.visible ||
                    model == nullptr ||
                    !item.transformResource) {
                    continue;
                }
                drawManagedModel(
                    *model,
                    model->mesh.vbv,
                    model->mesh.ibv,
                    item.transformResource->GetGPUVirtualAddress(),
                    item.useMaterialOverride && item.materialResource
                        ? item.materialResource->GetGPUVirtualAddress()
                        : 0);
            }

            for (const CourseMeshRenderItem& item :
                 ctx.scene->CourseRailTrackMeshes().Items()) {
                const AppManagedModelResource* model =
                    ctx.scene->FindManagedModel(item.modelIndex);
                if (!item.visible || model == nullptr || !item.transformResource) {
                    continue;
                }
                drawManagedModel(
                    *model,
                    model->mesh.vbv,
                    model->mesh.ibv,
                    item.transformResource->GetGPUVirtualAddress(),
                    item.useMaterialOverride && item.materialResource
                        ? item.materialResource->GetGPUVirtualAddress()
                        : 0);
            }

            if (ctx.productionScenePipeline != nullptr) {
                const editor::EditorProductionMeshDrawMode drawMode =
                    ctx.productionScenePipeline->DrawMode();
                const bool gpuPipelineReady =
                    ctx.productionGpuDrivenPipeline != nullptr &&
                    ctx.productionGpuDrivenPipeline->Ready();
                const bool gpuDriven =
                    ctx.productionGpuDrivenPipeline != nullptr &&
                    editor::EditorProductionGpuDrivenPipeline::
                        ShouldSubmitIndirect(
                            drawMode,
                            gpuPipelineReady,
                            ctx.productionGpuDrivenPipeline->Stats()
                                .dispatchSucceeded,
                            ctx.productionGpuDrivenPipeline
                                ->AutoValidationReady());
                if (gpuDriven) {
                    const auto& batches = ctx.productionGpuDrivenPipeline->Batches();
                    for (uint32_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
                        const editor::EditorProductionGpuDrivenBatch& batch = batches[batchIndex];
                        ID3D12PipelineState* productionPso = batch.pipelineState != nullptr
                            ? batch.pipelineState : ctx.appPipelines->GetMainPSO();
                        if (!ctx.frameRenderer->PrepareMainPass(
                                passContext.commandList, ctx.srvDescriptorHeap,
                                ctx.runtimeState->viewport,
                                ctx.runtimeState->scissorRect,
                                ctx.appPipelines->GetMainRootSignature(), productionPso)) continue;
                        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = batch.materialAddress != 0
                            ? batch.materialAddress : ctx.scene->materialResource->GetGPUVirtualAddress();
                        D3D12_GPU_VIRTUAL_ADDRESS directionalAddress =
                            ctx.scene->directionalLightResource->GetGPUVirtualAddress();
                        D3D12_GPU_VIRTUAL_ADDRESS pointAddress =
                            ctx.scene->pointLightResource->GetGPUVirtualAddress();
                        D3D12_GPU_VIRTUAL_ADDRESS spotAddress =
                            ctx.scene->spotLightResource->GetGPUVirtualAddress();
                        if (ctx.productionMaterialPipeline != nullptr) {
                            const auto& lighting = ctx.productionMaterialPipeline->Lighting();
                            if (lighting.directionalAddress != 0) directionalAddress = lighting.directionalAddress;
                            if (lighting.pointAddress != 0) pointAddress = lighting.pointAddress;
                            if (lighting.spotAddress != 0) spotAddress = lighting.spotAddress;
                        }
                        const D3D12_GPU_DESCRIPTOR_HANDLE albedo = batch.albedoHandle.ptr != 0
                            ? batch.albedoHandle : ctx.scene->textureSrvHandleGPU2;
                        const D3D12_GPU_DESCRIPTOR_HANDLE normal = batch.normalHandle.ptr != 0
                            ? batch.normalHandle : ctx.scene->textureSrvHandleGPU2;
                        const bool batchPrepared =
                            ctx.frameRenderer->PrepareIndirectMainBatch(
                                passContext.commandList, batch.representative.vertexBuffer,
                                batch.representative.indexBuffer, materialAddress, albedo, normal,
                                ctx.scene->textureSrvHandleGPU2,
                                ctx.scene->skyboxTextureSrvHandleGPU,
                                directionalAddress,
                                ctx.scene->cameraResource->GetGPUVirtualAddress(),
                                pointAddress, spotAddress,
                                ctx.productionLightingPipeline ? ctx.productionLightingPipeline->LightBufferAddress() : 0,
                                ctx.productionLightingPipeline ? ctx.productionLightingPipeline->ClusterRangeBufferAddress() : 0,
                                ctx.productionLightingPipeline ? ctx.productionLightingPipeline->ClusterIndexBufferAddress() : 0,
                                ctx.productionLightingPipeline ? ctx.productionLightingPipeline->ConstantsAddress() : 0,
                                ctx.productionLightingPipeline ? ctx.productionLightingPipeline->ShadowAtlasHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{});
                        ctx.productionGpuDrivenPipeline->RecordBatchPreparation(
                            batchPrepared);
                        if (batchPrepared) {
                            ctx.productionGpuDrivenPipeline->ExecuteBatch(passContext.commandList, batchIndex);
                        }
                    }
                }
                if (ctx.productionGpuDrivenPipeline != nullptr &&
                    gpuPipelineReady &&
                    drawMode !=
                        editor::EditorProductionMeshDrawMode::ForceDirect &&
                    ctx.productionGpuDrivenPipeline->Stats()
                        .dispatchSucceeded) {
                    ctx.productionGpuDrivenPipeline->RecordReadback(passContext.commandList);
                }
                std::vector<editor::EditorProductionSceneRenderPacket> directPackets;
                if (gpuDriven) {
                    directPackets = ctx.productionGpuDrivenPipeline->CpuFallbackPackets();
                } else if (
                    drawMode !=
                    editor::EditorProductionMeshDrawMode::ForceGpuDriven) {
                    directPackets = ctx.productionScenePipeline->RenderPackets();
                    if (ctx.worldPartitionPipeline != nullptr) {
                        const auto& hlodPackets = ctx.worldPartitionPipeline->HlodPackets();
                        directPackets.insert(directPackets.end(), hlodPackets.begin(), hlodPackets.end());
                    }
                }
                if (ctx.transientMeshRenderPath != nullptr) {
                    const auto& transientPackets = ctx.transientMeshRenderPath->RenderPackets();
                    directPackets.insert(
                        directPackets.end(), transientPackets.begin(), transientPackets.end());
                }
                for (const editor::EditorProductionSceneRenderPacket& packet : directPackets) {
                    if (packet.indexCount == 0 || packet.transformAddress == 0 ||
                        packet.vertexBuffer.BufferLocation == 0 ||
                        packet.indexBuffer.BufferLocation == 0) {
                        continue;
                    }
                    ID3D12PipelineState* productionPso = ctx.appPipelines->GetMainPSO();
                    if (ctx.productionShaderPipeline != nullptr) {
                        const editor::EditorProductionShaderBinding* shaderBinding =
                            ctx.productionShaderPipeline->Resolve(
                                packet.entityGuid, packet.materialSlot);
                        if (shaderBinding != nullptr && shaderBinding->pipelineState != nullptr) {
                            productionPso = shaderBinding->pipelineState;
                        }
                    }
                    if (!ctx.frameRenderer->PrepareMainPass(
                            passContext.commandList,
                            ctx.srvDescriptorHeap,
                            ctx.runtimeState->viewport,
                            ctx.runtimeState->scissorRect,
                            ctx.appPipelines->GetMainRootSignature(),
                            productionPso)) {
                        continue;
                    }
                    D3D12_GPU_VIRTUAL_ADDRESS materialAddress =
                        packet.materialAddressOverride != 0
                            ? packet.materialAddressOverride
                            : ctx.scene->materialResource->GetGPUVirtualAddress();
                    D3D12_GPU_VIRTUAL_ADDRESS directionalAddress =
                        ctx.scene->directionalLightResource->GetGPUVirtualAddress();
                    D3D12_GPU_VIRTUAL_ADDRESS pointAddress =
                        ctx.scene->pointLightResource->GetGPUVirtualAddress();
                    D3D12_GPU_VIRTUAL_ADDRESS spotAddress =
                        ctx.scene->spotLightResource->GetGPUVirtualAddress();
                    D3D12_GPU_DESCRIPTOR_HANDLE albedoTexture =
                        ctx.scene->textureSrvHandleGPU2;
                    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture =
                        ctx.scene->textureSrvHandleGPU2;
                    if (packet.materialAddressOverride == 0 &&
                        ctx.productionMaterialPipeline != nullptr) {
                        const editor::EditorProductionMaterialBinding* binding =
                            ctx.productionMaterialPipeline->Resolve(
                                packet.entityGuid, packet.materialSlot);
                        if (binding != nullptr && !binding->fallback &&
                            binding->materialAddress != 0) {
                            materialAddress = binding->materialAddress;
                        }
                        const editor::EditorProductionSceneLighting& lighting =
                            ctx.productionMaterialPipeline->Lighting();
                        if (lighting.directionalAddress != 0)
                            directionalAddress = lighting.directionalAddress;
                        if (lighting.pointAddress != 0)
                            pointAddress = lighting.pointAddress;
                        if (lighting.spotAddress != 0)
                            spotAddress = lighting.spotAddress;
                    }
                    if (ctx.productionTexturePipeline != nullptr) {
                        const editor::EditorProductionTextureBinding* textureBinding =
                            ctx.productionTexturePipeline->Resolve(
                                packet.entityGuid, packet.materialSlot);
                        if (textureBinding != nullptr) {
                            if (!textureBinding->albedoFallback &&
                                textureBinding->albedoHandle.ptr != 0) {
                                albedoTexture = textureBinding->albedoHandle;
                            }
                            if (!textureBinding->normalFallback &&
                                textureBinding->normalHandle.ptr != 0) {
                                normalTexture = textureBinding->normalHandle;
                            }
                        }
                    }
                    ctx.frameRenderer->DrawMainModel(
                        passContext.commandList,
                        packet.vertexBuffer,
                        packet.indexBuffer,
                        materialAddress,
                        packet.transformAddress,
                        albedoTexture,
                        normalTexture,
                        ctx.scene->textureSrvHandleGPU2,
                        ctx.scene->skyboxTextureSrvHandleGPU,
                        directionalAddress,
                        ctx.scene->cameraResource->GetGPUVirtualAddress(),
                        pointAddress,
                        spotAddress,
                        packet.indexCount,
                        0,
                        ctx.productionLightingPipeline != nullptr
                            ? ctx.productionLightingPipeline->LightBufferAddress() : 0,
                        ctx.productionLightingPipeline != nullptr
                            ? ctx.productionLightingPipeline->ClusterRangeBufferAddress() : 0,
                        ctx.productionLightingPipeline != nullptr
                            ? ctx.productionLightingPipeline->ClusterIndexBufferAddress() : 0,
                        ctx.productionLightingPipeline != nullptr
                            ? ctx.productionLightingPipeline->ConstantsAddress() : 0,
                        ctx.productionLightingPipeline != nullptr
                            ? ctx.productionLightingPipeline->ShadowAtlasHandle()
                            : D3D12_GPU_DESCRIPTOR_HANDLE{});
                }
            }

            ctx.runtimeState->activeSkinningPath =
                RuntimeSkinningPath::Unavailable;
            if (ctx.runtimeState->showSkinnedModel) {
                if (SkinnedModelInstance* activeSkinnedModel =
                        ctx.scene->GetActiveSkinnedModel()) {
                    const bool needsSkinnedSurfaceVfx = RequiresSkinnedSurfaceVfx(ctx.runtimeState);
                    const bool preferComputeSkinning =
                        needsSkinnedSurfaceVfx ||
                        ctx.runtimeState->submissionShowcase.enabled;
                    if (preferComputeSkinning) {
                        if (DrawComputeSkinnedModelInstance(
                                ctx, passContext, *activeSkinnedModel)) {
                            ctx.runtimeState->activeSkinningPath =
                                RuntimeSkinningPath::ComputeShader;
                        } else if (DrawSkinnedModelInstance(
                                       ctx, passContext, *activeSkinnedModel)) {
                            ctx.runtimeState->activeSkinningPath =
                                RuntimeSkinningPath::VertexShader;
                        }
                    } else if (!preferComputeSkinning) {
                        if (DrawSkinnedModelInstance(
                                ctx, passContext, *activeSkinnedModel)) {
                            ctx.runtimeState->activeSkinningPath =
                                RuntimeSkinningPath::VertexShader;
                        }
                    }
                }
            } else if (RequiresSkinnedSurfaceVfx(ctx.runtimeState)) {
                if (SkinnedModelInstance* activeSkinnedModel =
                        ctx.scene->GetActiveSkinnedModel()) {
                    DispatchComputeSkinnedSurface(ctx, passContext, *activeSkinnedModel);
                }
            }
        }});

    ctx.renderGraph->AddPass({
        "Geometry.Terrain",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::WriteDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            const auto& courseItems = ctx.scene->CourseMeshes().Items();
            const bool hasTitleLandscape = std::any_of(courseItems.begin(), courseItems.end(),
                [](const CourseMeshRenderItem& item) {
                    return item.visible && IsTitleLandscapeMesh(item.meshId);
                });
            const bool drawGameplayTerrain = ctx.runtimeState->terrain.enabled &&
                ctx.terrainChunkManager != nullptr;
            if ((!drawGameplayTerrain && !hasTitleLandscape) ||
                ctx.appPipelines->GetTerrainPSO() == nullptr ||
                ctx.scene->terrainMaterialResource == nullptr ||
                ctx.scene->directionalLightResource == nullptr ||
                ctx.scene->cameraResource == nullptr ||
                ctx.scene->pointLightResource == nullptr ||
                ctx.scene->spotLightResource == nullptr) {
                return;
            }

            const std::vector<TerrainRenderChunk> emptyChunks;
            const std::vector<TerrainRenderChunk>& chunks = drawGameplayTerrain
                ? ctx.terrainChunkManager->RenderChunks() : emptyChunks;
            if (chunks.empty() && !hasTitleLandscape) {
                return;
            }

            ID3D12PipelineState* terrainPipeline = ctx.appPipelines->GetTerrainPSO();
            if (!hasTitleLandscape && ctx.runtimeState->terrain.displayMode == TerrainDisplayMode::Wireframe &&
                ctx.appPipelines->GetTerrainWireframePSO() != nullptr) {
                terrainPipeline = ctx.appPipelines->GetTerrainWireframePSO();
            }

            const bool ready = ctx.frameRenderer->PrepareMainPass(
                passContext.commandList,
                ctx.srvDescriptorHeap,
                ctx.runtimeState->viewport,
                ctx.runtimeState->scissorRect,
                ctx.appPipelines->GetMainRootSignature(),
                terrainPipeline);
            if (!ready) {
                return;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE terrainTexture =
                ctx.scene->terrainAlbedoTextureSrvHandleGPU.ptr != 0
                    ? ctx.scene->terrainAlbedoTextureSrvHandleGPU
                    : ctx.scene->textureSrvHandleGPU;
            const D3D12_GPU_DESCRIPTOR_HANDLE terrainDetailCache =
                ctx.scene->terrainDetailCacheTextureSrvHandleGPU.ptr != 0
                    ? ctx.scene->terrainDetailCacheTextureSrvHandleGPU
                    : ctx.scene->textureSrvHandleGPU2;
            const D3D12_GPU_DESCRIPTOR_HANDLE terrainDetailNormalMap =
                ctx.scene->terrainDetailNormalMapTextureSrvHandleGPU.ptr != 0
                    ? ctx.scene->terrainDetailNormalMapTextureSrvHandleGPU
                    : ctx.scene->textureSrvHandleGPU2;
            if (ctx.scene->terrainPbrNormalTextureSrvHandleGPU.ptr != 0 &&
                ctx.scene->terrainPbrMaterialResource != nullptr) {
                passContext.commandList->SetGraphicsRootDescriptorTable(
                    18,
                    ctx.scene->terrainPbrNormalTextureSrvHandleGPU);
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    19,
                    ctx.scene->terrainPbrMaterialResource->GetGPUVirtualAddress());
            }
            if (ctx.scene->cascadeShadowResource != nullptr) {
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    10,
                    ctx.scene->cascadeShadowResource->GetGPUVirtualAddress());
            }
            if (ctx.scene->cascadeShadowSrvTableGpu.ptr != 0) {
                passContext.commandList->SetGraphicsRootDescriptorTable(
                    11,
                    ctx.scene->cascadeShadowSrvTableGpu);
            }
            for (const CourseMeshRenderItem& item : courseItems) {
                if (!item.visible || !IsTitleLandscapeMesh(item.meshId) ||
                    !item.transformResource || !item.materialResource) continue;
                const AppManagedModelResource* model = ctx.scene->FindManagedModel(item.modelIndex);
                if (model == nullptr || model->mesh.indexCount == 0) continue;
                ctx.frameRenderer->DrawMainModel(
                    passContext.commandList, model->mesh.vbv, model->mesh.ibv,
                    item.materialResource->GetGPUVirtualAddress(),
                    item.transformResource->GetGPUVirtualAddress(),
                    terrainTexture, terrainDetailCache, terrainDetailNormalMap,
                    ctx.scene->skyboxTextureSrvHandleGPU,
                    ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
                    ctx.scene->cameraResource->GetGPUVirtualAddress(),
                    ctx.scene->pointLightResource->GetGPUVirtualAddress(),
                    ctx.scene->spotLightResource->GetGPUVirtualAddress(), model->mesh.indexCount);
            }
            for (const TerrainRenderChunk& chunk : chunks) {
                if (chunk.indexCount == 0 || chunk.transformResource == nullptr || chunk.transformGpuAddress == 0) {
                    continue;
                }
                ctx.frameRenderer->DrawMainModel(
                    passContext.commandList,
                    chunk.vbv,
                    chunk.ibv,
                    ctx.scene->terrainMaterialResource->GetGPUVirtualAddress(),
                    chunk.transformGpuAddress,
                    terrainTexture,
                    terrainDetailCache,
                    terrainDetailNormalMap,
                    ctx.scene->skyboxTextureSrvHandleGPU,
                    ctx.scene->directionalLightResource->GetGPUVirtualAddress(),
                    ctx.scene->cameraResource->GetGPUVirtualAddress(),
                    ctx.scene->pointLightResource->GetGPUVirtualAddress(),
                    ctx.scene->spotLightResource->GetGPUVirtualAddress(),
                    chunk.indexCount);
            }

            if (drawGameplayTerrain && !chunks.empty() &&
                ctx.runtimeState->terrain.displayMode != TerrainDisplayMode::Wireframe &&
                ctx.runtimeState->terrain.enableDebrisRendering &&
                ctx.appPipelines->GetTerrainDebrisPSO() != nullptr) {
                if (ctx.appPipelines->GetTerrainDebrisCullRootSignature() != nullptr &&
                    ctx.appPipelines->GetTerrainDebrisCullPSO() != nullptr &&
                    ctx.appPipelines->GetTerrainHiZBuildRootSignature() != nullptr &&
                    ctx.appPipelines->GetTerrainHiZBuildPSO() != nullptr &&
                    ctx.frameState != nullptr &&
                    ctx.terrainChunkManager->ShouldDispatchDebrisCulling(
                        ctx.runtimeState->terrain.debrisOcclusionUpdateInterval)) {
                    const float debrisCullDistance =
                        (std::max)(
                            ctx.runtimeState->terrain.settings.lodFarDistance +
                                ctx.runtimeState->terrain.settings.chunkLength * 2.0f,
                            ctx.runtimeState->terrain.settings.chunkLength * 4.0f);
                    ctx.terrainChunkManager->DispatchDebrisCulling(
                        passContext.commandList,
                        ctx.srvDescriptorHeap,
                        ctx.depthTextureResource,
                        ctx.depthTextureHandle,
                        ctx.appPipelines->GetTerrainHiZBuildRootSignature(),
                        ctx.appPipelines->GetTerrainHiZBuildPSO(),
                        ctx.appPipelines->GetTerrainDebrisCullRootSignature(),
                        ctx.appPipelines->GetTerrainDebrisCullPSO(),
                        ctx.frameState->cameraWorldPosition,
                        ctx.frameState->viewProjectionMatrix,
                        debrisCullDistance,
                        ctx.runtimeState->terrain.debrisOcclusionMip,
                        ctx.runtimeState->terrain.debrisOcclusionStrength,
                        ctx.runtimeState->terrain.debrisOcclusionDepthBias);
                }

                const bool readyForDebris = ctx.frameRenderer->PrepareMainPass(
                    passContext.commandList,
                    ctx.srvDescriptorHeap,
                    ctx.runtimeState->viewport,
                    ctx.runtimeState->scissorRect,
                    ctx.appPipelines->GetMainRootSignature(),
                    ctx.appPipelines->GetTerrainDebrisPSO());
                if (!readyForDebris) {
                    return;
                }
                passContext.commandList->SetPipelineState(ctx.appPipelines->GetTerrainDebrisPSO());
                if (ctx.scene->terrainPbrNormalTextureSrvHandleGPU.ptr != 0 &&
                    ctx.scene->terrainPbrMaterialResource != nullptr) {
                    passContext.commandList->SetGraphicsRootDescriptorTable(
                        18,
                        ctx.scene->terrainPbrNormalTextureSrvHandleGPU);
                    passContext.commandList->SetGraphicsRootConstantBufferView(
                        19,
                        ctx.scene->terrainPbrMaterialResource->GetGPUVirtualAddress());
                }
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    0,
                    ctx.scene->terrainMaterialResource->GetGPUVirtualAddress());
                passContext.commandList->SetGraphicsRootDescriptorTable(2, terrainTexture);
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    3,
                    ctx.scene->directionalLightResource->GetGPUVirtualAddress());
                passContext.commandList->SetGraphicsRootDescriptorTable(4, terrainDetailCache);
                passContext.commandList->SetGraphicsRootDescriptorTable(5, terrainDetailNormalMap);
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    6,
                    ctx.scene->cameraResource->GetGPUVirtualAddress());
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    7,
                    ctx.scene->pointLightResource->GetGPUVirtualAddress());
                passContext.commandList->SetGraphicsRootConstantBufferView(
                    8,
                    ctx.scene->spotLightResource->GetGPUVirtualAddress());
                passContext.commandList->SetGraphicsRootDescriptorTable(9, ctx.scene->skyboxTextureSrvHandleGPU);
                if (ctx.scene->cascadeShadowResource != nullptr) {
                    passContext.commandList->SetGraphicsRootConstantBufferView(
                        10,
                        ctx.scene->cascadeShadowResource->GetGPUVirtualAddress());
                }
                if (ctx.scene->cascadeShadowSrvTableGpu.ptr != 0) {
                    passContext.commandList->SetGraphicsRootDescriptorTable(
                        11,
                        ctx.scene->cascadeShadowSrvTableGpu);
                }

                ctx.terrainChunkManager->DrawDebrisIndirect(passContext.commandList);
            }
        }});

    ctx.renderGraph->AddPass({
        "Debug.Skeleton",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::ReadDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            if (!ctx.runtimeState->showSkeletonDebug ||
                ctx.scene->skeletonDebugVertexCount == 0 ||
                !ctx.scene->skeletonDebugTransformResource) {
                return;
            }

            const bool ready = ctx.frameRenderer->PrepareMainPass(
                passContext.commandList,
                ctx.srvDescriptorHeap,
                ctx.runtimeState->viewport,
                ctx.runtimeState->scissorRect,
                ctx.appPipelines->GetSkeletonDebugRootSignature(),
                ctx.appPipelines->GetSkeletonDebugPSO());
            if (!ready) {
                return;
            }

            ctx.frameRenderer->DrawSkeletonDebugLines(
                passContext.commandList,
                ctx.scene->skeletonDebugVBV,
                ctx.scene->skeletonDebugTransformResource->GetGPUVirtualAddress(),
                ctx.scene->skeletonDebugVertexCount);
        }});

    ctx.renderGraph->AddPass({
        "Gameplay.EnemyThreatVisuals",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::ReadDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            // Hostile projectile and lane readability is a gameplay contract,
            // so this pass is intentionally independent from every Debug.Draw
            // and editor visualization flag.
            if (ctx.scene->enemyThreatVisualDraw.VertexCount() == 0 ||
                !ctx.scene->enemyThreatVisualDraw.IsReady()) {
                return;
            }

            const bool ready = ctx.frameRenderer->PrepareMainPass(
                passContext.commandList,
                ctx.srvDescriptorHeap,
                ctx.runtimeState->viewport,
                ctx.runtimeState->scissorRect,
                ctx.appPipelines->GetSkeletonDebugRootSignature(),
                ctx.appPipelines->GetSkeletonDebugDepthTestPSO() != nullptr
                    ? ctx.appPipelines->GetSkeletonDebugDepthTestPSO()
                    : ctx.appPipelines->GetSkeletonDebugPSO());
            if (!ready) return;

            ctx.frameRenderer->DrawSkeletonDebugLines(
                passContext.commandList,
                ctx.scene->enemyThreatVisualDraw.VertexBufferView(),
                ctx.scene->enemyThreatVisualDraw.TransformBufferAddress(),
                ctx.scene->enemyThreatVisualDraw.VertexCount());
        }});

    ctx.renderGraph->AddPass({
        "Debug.Draw",
        ge3::graphics::RenderPassLayer::Geometry,
        {
            {"SceneColor", ge3::graphics::RenderResourceAccessType::WriteRtv},
            {"SceneDepth", ge3::graphics::RenderResourceAccessType::ReadDepth},
        },
        "SceneDepth",
        [ctx](ge3::graphics::RenderPassContext& passContext) {
            if (!(ctx.runtimeState->terrain.showDebugDraw ||
                  ctx.runtimeState->terrain.showCourseObjectFrame ||
                  ctx.runtimeState->terrain.showCascadeBounds ||
                  ctx.runtimeState->terrain.displayMode == TerrainDisplayMode::Debug) ||
                ctx.scene->debugDraw.VertexCount() == 0 ||
                !ctx.scene->debugDraw.IsReady()) {
                return;
            }

            const bool ready = ctx.frameRenderer->PrepareMainPass(
                passContext.commandList,
                ctx.srvDescriptorHeap,
                ctx.runtimeState->viewport,
                ctx.runtimeState->scissorRect,
                ctx.appPipelines->GetSkeletonDebugRootSignature(),
                ctx.runtimeState->terrain.courseObjectFrameDepthTest &&
                        ctx.appPipelines->GetSkeletonDebugDepthTestPSO() != nullptr
                    ? ctx.appPipelines->GetSkeletonDebugDepthTestPSO()
                    : ctx.appPipelines->GetSkeletonDebugPSO());
            if (!ready) {
                return;
            }

            ctx.frameRenderer->DrawSkeletonDebugLines(
                passContext.commandList,
                ctx.scene->debugDraw.VertexBufferView(),
                ctx.scene->debugDraw.TransformBufferAddress(),
                ctx.scene->debugDraw.VertexCount());
        }});
}
