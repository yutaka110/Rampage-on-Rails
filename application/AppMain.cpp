#include "camera/debugCamera.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"
#include <wrl/client.h>
#include <Windows.h>
#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <filesystem>
#include <numbers>
#include <string>

#include "../TextureHelper.h"
#include "core/Device.h"
#include "graphics/SwapChain.h"
#include "core/CommandListPool.h"
#include "core/DescriptorHeap.h"
#include "utils/dx12/BufferHelper.h"
#include "AppAudio.h"
#include "AppBootstrap.h"
#include "AppImGuiLayer.h"
#include "AppFrameRenderer.h"
#include "AppPipelines.h"
#include "AppParticleSystem.h"
#include "AppRenderResources.h"
#include "AppRunLoop.h"
#include "AppRuntimeConfig.h"
#include "AppRuntimeState.h"
#include "AppRuntimeUtils.h"
#include "AppSceneResources.h"
#include "EngineContext.h"
#include "AppMain.h"

using namespace DirectX;

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

using namespace Microsoft::WRL;

namespace {
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {

		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};
} // namespace


bool AppMain::Initialize(HINSTANCE hInstance) {
	hInstance_ = hInstance;
	return true;
}

void AppMain::Finalize() {
}

int AppMain::Run() {
	D3DResourceLeakChecker leakCheck;

	AppBootstrap bootstrap;
	if (!bootstrap.Initialize(hInstance_)) {
		return -1;
	}

	const HWND hwnd = bootstrap.Handle();
	const uint32_t windowWidth = bootstrap.Width();
	const uint32_t windowHeight = bootstrap.Height();

	HRESULT hr = S_OK;

	EngineContext engineContext;
	if (!engineContext.Initialize(hwnd, windowWidth, windowHeight, /*enableDebugLayer=*/true)) {
		return -1;
	}

	core::Device& dev = engineContext.GetDevice();

	Microsoft::WRL::ComPtr<ID3D12Device> device = dev.GetDevice();
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = dev.GetCommandQueue();
	Microsoft::WRL::ComPtr<ID3D12Fence>        fence = dev.GetFence();

	auto& swapChain = engineContext.GetSwapChain();
	auto& clPool = engineContext.GetCommandListPool();

	auto& heaps = engineContext.GetHeaps();
	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = heaps.srv.GetHeap();
	// EngineContext reserves fixed SRV ranges before allocating scene depth.

	uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	assert(SUCCEEDED(hr));


	constexpr DXGI_FORMAT kRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	uint64_t fenceValue = engineContext.GetFenceValue();
	HANDLE fenceEvent = engineContext.GetFenceEvent();

	ComPtr<ID3D12Resource> wvpResource =
		CreateBufferResource(device, sizeof(Matrix4x4));

	Matrix4x4* wvpData = nullptr;

	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	*wvpData = MakeIdentity4x4();

AppSceneResources scene;
AppPipelines appPipelines;
if (!appPipelines.Initialize(device.Get())) {
	OutputDebugStringA("[AppMain] AppPipelines initialization failed.\n");
	return 1;
}
ID3D12GraphicsCommandList* initialUploadCommandList = engineContext.GetMainCommandList();
if (!scene.Initialize(device, initialUploadCommandList, srvDescriptorHeap, descriptorSizeSRV)) {
	OutputDebugStringA("[AppMain] AppSceneResources initialization failed.\n");
	return 1;
}
hr = initialUploadCommandList->Close();
assert(SUCCEEDED(hr));
ID3D12CommandList* initialUploadCommandLists[] = { initialUploadCommandList };
commandQueue->ExecuteCommandLists(1, initialUploadCommandLists);
fenceValue += 1;
hr = commandQueue->Signal(fence.Get(), fenceValue);
assert(SUCCEEDED(hr));
if (fence->GetCompletedValue() < fenceValue) {
	hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
	assert(SUCCEEDED(hr));
	WaitForSingleObject(fenceEvent, INFINITE);
}
engineContext.SetFenceValue(fenceValue);
scene.ReleaseInitialUploadResources();

AppRuntimeState runtimeState{};
AppParticleSystem particleSystem;
runtimeState.transform.scale = { 6.0f, 6.0f, 6.0f };
runtimeState.transform.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.transform.translate = { 0.0f, 0.0f, 0.0f };

runtimeState.animatedCubeTransform.scale = { 0.4f, 0.4f, 0.4f };
runtimeState.animatedCubeTransform.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.animatedCubeTransform.translate = { -0.9f, 0.0f, -6.3f };

runtimeState.skinnedModelTransform.scale = { 0.45f, 0.45f, 0.45f };
runtimeState.skinnedModelTransform.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.skinnedModelTransform.translate = { 0.0f, -0.4f, -6.3f };

runtimeState.camera.transform.scale = { 1.0f, 1.0f, 1.0f };
runtimeState.camera.transform.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.camera.transform.translate = { 0.0f, 0.0f, -8.0f };
runtimeState.camera.fovY = 0.25f * std::numbers::pi_v<float>;
runtimeState.camera.nearZ = 0.1f;
runtimeState.camera.farZ = 1000.0f;

	runtimeState.vfxModelObjects[0].modelIndex = 0;
	runtimeState.vfxModelObjects[0].transform.scale = { 0.9f, 0.9f, 0.9f };
	runtimeState.vfxModelObjects[0].transform.rotate = { 0.28f, 0.55f, 0.0f };
	runtimeState.vfxModelObjects[0].transform.translate = { 0.0f, 0.0f, -3.5f };
	for (uint32_t modelIndex = 0;
		 modelIndex < scene.ManagedModelLibrary().size();
		 ++modelIndex) {
		if (scene.ManagedModelLibrary()[modelIndex].name == "multi_material_demo") {
			runtimeState.vfxModelObjects[0].modelIndex = modelIndex;
			break;
		}
	}

	runtimeState.vfxModelObjects[1].modelIndex = 1;
	runtimeState.vfxModelObjects[1].visible = false;
runtimeState.vfxModelObjects[1].transform.scale = { 0.45f, 0.45f, 0.45f };
runtimeState.vfxModelObjects[1].transform.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.vfxModelObjects[1].transform.translate = { 0.0f, -0.2f, -5.8f };

	runtimeState.vfxModelObjects[2].modelIndex = 0;
	runtimeState.vfxModelObjects[2].visible = false;
runtimeState.vfxModelObjects[2].transform.scale = { 0.85f, 0.85f, 0.85f };
runtimeState.vfxModelObjects[2].transform.rotate = { 0.0f, 0.6f, 0.0f };
runtimeState.vfxModelObjects[2].transform.translate = { 2.0f, -0.35f, -5.8f };

runtimeState.transformSprite.scale = { 256.0f, 256.0f, 1.0f };
runtimeState.transformSprite.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.transformSprite.translate = { 640.0f, 360.0f, 0.0f };

runtimeState.uvTransformSprite.scale = { 1.0f, 1.0f, 1.0f };
runtimeState.uvTransformSprite.rotate = { 0.0f, 0.0f, 0.0f };
runtimeState.uvTransformSprite.translate = { 0.0f, 0.0f, 0.0f };

	runtimeState.showAnimatedCube = false;
	ApplyEnvironmentRuntimeConfig(runtimeState);

runtimeState.viewport.Width = 1280.0f;
runtimeState.viewport.Height = 720.0f;
runtimeState.viewport.TopLeftX = 0.0f;
runtimeState.viewport.TopLeftY = 0.0f;
runtimeState.viewport.MinDepth = 0.0f;
runtimeState.viewport.MaxDepth = 1.0f;

runtimeState.scissorRect.left = 0;
runtimeState.scissorRect.top = 0;
runtimeState.scissorRect.right = 1280;
runtimeState.scissorRect.bottom = 720;

runtimeState.directionalLightData = scene.directionalLightData;
runtimeState.pointLightData = scene.pointLightData;
runtimeState.spotLight = scene.spotLight;
runtimeState.cameraWorldPosition = scene.mappedCamera->worldPosition;
runtimeState.materialData = *scene.materialData;

AppImGuiLayer imguiLayer;
imguiLayer.Initialize(
	hwnd,
	device.Get(),
	static_cast<int>(swapChain.BufferCount()),
	kRtvFormat,
	srvDescriptorHeap.Get(),
	&appPipelines);
#if defined(GE3_RELEASE_PRESENTATION) && GE3_RELEASE_PRESENTATION
imguiLayer.SetVisible(false);
#endif
if (imguiLayer.IsVisible()) {
	bootstrap.SetMessageCallback(
		[&imguiLayer](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
			return imguiLayer.HandleWindowMessage(hwnd, message, wParam, lParam);
		});
	bootstrap.SetDropCallback(
		[&imguiLayer](const std::wstring& path) {
			imguiLayer.QueueExternalAssetDrop(std::filesystem::path(path));
		});
}


AppRenderResources renderResources;
assert(renderResources.InitializeSharedSpriteQuad(device));
AppFrameRenderer frameRenderer;

	constexpr uint32_t kInstancingSrvIndex = 10;
	ComPtr<ID3D12Resource> instancingResource =
		particleSystem.CreateInstancingResource(device.Get());
	particleSystem.InitializeInstancingBuffer(
		instancingResource.Get(),
		particleSystem.MaxInstances());

	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvCPU =

		AppRenderResources::GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, kInstancingSrvIndex);

	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvGPU =
		AppRenderResources::GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, kInstancingSrvIndex);

	particleSystem.CreateInstancingSrv(
		device.Get(),
		instancingResource.Get(),
		instancingSrvCPU,
		instancingSrvGPU);
	const UINT texWidth = 1280;
	const UINT texHeight = 720;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ComPtr<ID3D12Resource> texture =
		CreateTextureResourceResolution(device, texWidth, texHeight, format);



	D3D12_CPU_DESCRIPTOR_HANDLE receivedSrvHandleCPU =
		AppRenderResources::GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 4);
	CreateTextureSRV(device.Get(), texture.Get(),
		receivedSrvHandleCPU);

	AppAudio audio;
	audio.Initialize();


    // No classroom alarm sample on startup; gameplay audio owns playback.
	DebugCamera debugCamera;
	debugCamera.Initialize();

	MSG msg{};

	FrameLoopState frameState{};
	const AppStartupScene startupScene = ResolveAppStartupSceneFromCommandLine();
	AppRunLoop runLoop(
		debugCamera,
		runtimeState,
		scene,
		particleSystem,
		imguiLayer,
		frameRenderer,
		appPipelines,
		renderResources,
		audio,
		swapChain,
		clPool,
		engineContext,
		heaps,
		dev,
		hwnd,
		srvDescriptorHeap,
		wvpData,
		windowWidth,
		windowHeight,
		frameState,
		commandQueue.Get(),
		fence.Get(),
		fenceEvent,
		startupScene);
	runLoop.InitializeBeam(
		device.Get(),
		srvDescriptorHeap.Get(),
		descriptorSizeSRV,
		kRtvFormat,
		DXGI_FORMAT_D24_UNORM_S8_UINT);
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.hwnd == hwnd &&
				runLoop.HandleTitleScreenMessage(msg.message, msg.wParam, msg.lParam)) {
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			audio.Update();
			runLoop.RenderFrame();
		}

	}

	audio.Finalize();
	bootstrap.SetMessageCallback({});
	bootstrap.SetDropCallback({});
	imguiLayer.Shutdown();

	runLoop.Shutdown();

	engineContext.Shutdown();

#if defined(_DEBUG) || defined(DEVELOP)
	ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();

		debugController->SetEnableGPUBasedValidation(TRUE);
	}
	debugController.Reset();
#endif
	bootstrap.Shutdown();
	return 0;
}
