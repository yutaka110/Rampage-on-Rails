#include "RailNearShadow.h"
#include "AppSceneResources.h"

#include <array>
#include <cstring>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES state) {
    ComPtr<ID3D12Resource> buffer;
    if(!size) return buffer;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type=heapType;
    heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=size;
    desc.Height=1;
    desc.DepthOrArraySize=1;
    desc.MipLevels=1;
    desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(heapType==D3D12_HEAP_TYPE_DEFAULT) desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,
        IID_PPV_ARGS(&buffer)))) return {};
    return buffer;
}

void BuildStructure(ID3D12GraphicsCommandList4* list,
    const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs,
    ID3D12Resource* result, ID3D12Resource* scratch) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs=inputs;
    build.DestAccelerationStructureData=result->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();
    list->BuildRaytracingAccelerationStructure(&build,0,nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource=result;
    list->ResourceBarrier(1,&barrier);
}
}

bool RailNearShadow::Supports(ID3D12Device* device) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_5};
    return device && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options))) &&
        options.RaytracingTier>=D3D12_RAYTRACING_TIER_1_1 &&
        SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader,sizeof(shader))) &&
        shader.HighestShaderModel>=D3D_SHADER_MODEL_6_5;
}

D3D12_GPU_VIRTUAL_ADDRESS RailNearShadow::Build(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
    uint32_t slotIndex, uint32_t slotCount, std::span<const RailShadowCaster> casters) {
    if(casters.empty() || casters.size()>MaxCasters || slotIndex>=slotCount) return 0;
    ComPtr<ID3D12Device5> dxrDevice;
    ComPtr<ID3D12GraphicsCommandList4> list;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxrDevice))) ||
        FAILED(commandList->QueryInterface(IID_PPV_ARGS(&list)))) return 0;

    if(slots_.size()<slotCount) slots_.resize(slotCount);
    auto& slot=slots_[slotIndex];
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
    top.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    top.NumDescs=MaxCasters;
    if(!slot.result) {
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
        dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&top,&sizes);
        auto result=Buffer(device,sizes.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        auto scratch=Buffer(device,sizes.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto upload=Buffer(device,sizeof(D3D12_RAYTRACING_INSTANCE_DESC)*MaxCasters,D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        if(!result || !scratch || !upload) return 0;
        slot={std::move(result),std::move(scratch),std::move(upload)};
    }

    std::array<D3D12_RAYTRACING_INSTANCE_DESC,MaxCasters> instances{};
    uint32_t count=0;
    for(const auto& caster:casters) {
        auto cached=meshes_.find(caster.modelIndex);
        if(cached==meshes_.end()) {
            const auto& mesh=*caster.mesh;
            D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
            geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.VertexCount=mesh.vertexCount;
            geometry.Triangles.VertexBuffer={mesh.vbv.BufferLocation,mesh.vbv.StrideInBytes};
            geometry.Triangles.IndexFormat=mesh.ibv.Format;
            geometry.Triangles.IndexCount=mesh.indexCount;
            geometry.Triangles.IndexBuffer=mesh.ibv.BufferLocation;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
            bottom.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bottom.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
            bottom.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            bottom.NumDescs=1;
            bottom.pGeometryDescs=&geometry;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
            dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottom,&sizes);
            Blas blas;
            blas.result=Buffer(device,sizes.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
            blas.scratch=Buffer(device,sizes.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(!blas.result || !blas.scratch) return 0;
            cached=meshes_.emplace(caster.modelIndex,std::move(blas)).first;
            // Managed mesh indices normally live in INDEX_BUFFER. Add the
            // build input read state only while recording the first BLAS.
            D3D12_RESOURCE_BARRIER indexRead{};
            indexRead.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            indexRead.Transition={mesh.indexResource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                D3D12_RESOURCE_STATE_INDEX_BUFFER,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
            list->ResourceBarrier(1,&indexRead);
            BuildStructure(list.Get(),bottom,cached->second.result.Get(),cached->second.scratch.Get());
            std::swap(indexRead.Transition.StateBefore,indexRead.Transition.StateAfter);
            list->ResourceBarrier(1,&indexRead);
        }
        auto& instance=instances[count++];
        for(uint32_t row=0;row<3;++row) for(uint32_t col=0;col<4;++col)
            instance.Transform[row][col]=caster.world.m[col][row];
        instance.InstanceMask=0xff;
        instance.AccelerationStructure=cached->second.result->GetGPUVirtualAddress();
    }
    const D3D12_RANGE noRead{};
    void* upload=nullptr;
    if(FAILED(slot.instances->Map(0,&noRead,&upload))) return 0;
    std::memcpy(upload,instances.data(),count*sizeof(instances[0]));
    slot.instances->Unmap(0,nullptr);
    top.NumDescs=count;
    top.InstanceDescs=slot.instances->GetGPUVirtualAddress();
    BuildStructure(list.Get(),top,slot.result.Get(),slot.scratch.Get());
    return slot.result->GetGPUVirtualAddress();
}
