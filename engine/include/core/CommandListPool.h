#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace core {
    class Device;

    class CommandListPool {
    public:
        CommandListPool() noexcept;
        CommandListPool(const CommandListPool&) = delete;
        CommandListPool& operator=(const CommandListPool&) = delete;

        // frameCount はスワップチェーンのバッファ数に合わせる（例: 2）
        // 再初期化する場合、呼び出し元が既存の GPU 作業の完了を待つ。
        bool Initialize(Device& dev, UINT frameCount);

        // 呼び出し元が既存の GPU 作業の完了を待ってから破棄する。
        void Shutdown() noexcept;

        // Begin: 指定フレームの Allocator を Reset し、リストを Reset して返す
        // pInitialPSO は任意（nullptr可）
        ID3D12GraphicsCommandList* Begin(UINT frameIndex, ID3D12PipelineState* pInitialPSO = nullptr);

        // EndAndExecute: Close→Execute（QueueはDeviceから取得）
        bool EndAndExecute(Device& dev);

        // 必要なら外からAllocator参照
        ID3D12CommandAllocator* Allocator(UINT frameIndex) {
            return frameIndex < allocators_.size() ? allocators_[frameIndex].Get() : nullptr;
        }

    private:
        class State;
        class UnavailableState;
        class ReadyState;
        class RecordingState;

        bool InitializeResources(Device& dev, UINT frameCount);
        void TransitionTo(const State& next) noexcept;

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
        std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators_;
        const State* state_;
    };

}
