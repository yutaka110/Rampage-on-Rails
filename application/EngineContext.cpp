#include "EngineContext.h"
#include "AppSceneResources.h"

#include <cassert>
#include <d3d12.h>
#include <dxgi1_6.h>

using Microsoft::WRL::ComPtr;

// AppMain.cpp から移植：DepthStencil 用のテクスチャを作る
static ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClear{};
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}

bool EngineContext::Initialize(HWND hwnd, UINT width, UINT height, bool enableDebugLayer) {
    if (!dev_.Initialize(enableDebugLayer)) {
        return false;
    }

    ID3D12Device* device = dev_.GetDevice();
    assert(device != nullptr);

    // SwapChain
    if (!swapChain_.Create(dev_, hwnd, width, height, /*buffers*/ 3)) {
        return false;
    }

    // CommandListPool（フレーム数分）
    clPool_.Initialize(dev_, /*frameCount*/ swapChain_.BufferCount());

    // Descriptor heaps（AppMain の static g_descHeaps をここへ移す）
    heaps_.Initialize(device, /*rtv*/ 128, /*dsv*/ 32, /*srv*/ 4096);

    // Reserve fixed application descriptors before the first dynamic SRV.
    // In particular ImGui owns slot 0; allocating scene depth there lets font
    // initialization silently replace the depth SRV used by fog and AO.
    heaps_.srv.Reserve(AppSceneResources::kMaterialTextureSrvBaseIndex +
        AppSceneResources::kMaterialTextureSrvCount);
    heaps_.srv.ReserveRange(3200, 896);

    // DepthStencil + DSV
    depthStencil_ = CreateDepthStencilTextureResource(device, (int32_t)width, (int32_t)height);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    auto dsvH = heaps_.dsv.Allocate();
    device->CreateDepthStencilView(depthStencil_.Get(), &dsvDesc, dsvH.cpu);
    mainDsvIndex_ = dsvH.index;

    D3D12_DEPTH_STENCIL_VIEW_DESC readOnlyDsvDesc = dsvDesc;
    readOnlyDsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    auto readOnlyDsv = heaps_.dsv.Allocate();
    device->CreateDepthStencilView(depthStencil_.Get(), &readOnlyDsvDesc, readOnlyDsv.cpu);
    readOnlyDsvIndex_ = readOnlyDsv.index;

    depthSrv_ = heaps_.srv.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depthStencil_.Get(), &depthSrvDesc, depthSrv_.cpu);

    // AppMain が今まで使っていた “メイン用の即席 CommandAllocator/List” も一旦ここへ。
    // （後で clPool に寄せる段階で削除してOK）
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mainCmdAlloc_));
    assert(SUCCEEDED(hr));

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mainCmdAlloc_.Get(), nullptr, IID_PPV_ARGS(&mainCmdList_));
    assert(SUCCEEDED(hr));

    // Fence event（dev_ の fence と組み合わせて AppMain 側の待機で使う）
    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent_ != nullptr);

    fenceValue_ = 0;
    return true;
}

bool EngineContext::ResizeSceneDepth(UINT width, UINT height) {
    if (width == 0 || height == 0 || !dev_.GetDevice() ||
        mainDsvIndex_ == UINT32_MAX || readOnlyDsvIndex_ == UINT32_MAX) return false;
    if (depthStencil_) {
        const auto desc = depthStencil_->GetDesc();
        if (desc.Width == width && desc.Height == height) return true;
    }
    auto depth = CreateDepthStencilTextureResource(dev_.GetDevice(),
        static_cast<int32_t>(width), static_cast<int32_t>(height));
    if (!depth) return false;
    auto* device = dev_.GetDevice();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depth.Get(), &dsv, heaps_.dsv.GetHandle(mainDsvIndex_).cpu);
    dsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    device->CreateDepthStencilView(depth.Get(), &dsv, heaps_.dsv.GetHandle(readOnlyDsvIndex_).cpu);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depth.Get(), &srv, depthSrv_.cpu);
    depthStencil_ = std::move(depth);
    return true;
}

void EngineContext::Shutdown() {
    mainCmdList_.Reset();
    mainCmdAlloc_.Reset();
    depthStencil_.Reset();
    depthSrv_ = {};
    heaps_.Finalize();
    swapChain_ = {};
    clPool_ = {};
    dev_.Shutdown();

    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    mainDsvIndex_ = UINT32_MAX;
    readOnlyDsvIndex_ = UINT32_MAX;
    fenceValue_ = 0;
}
