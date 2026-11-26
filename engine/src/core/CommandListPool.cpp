#include "core/CommandListPool.h"
#include "core/Device.h"
#include "utils/Logger.h"
#if !defined(GE3_ENGINE_RENDERER) || GE3_ENGINE_RENDERER != 1
#error CommandListPool.cpp must only be compiled by the EngineRenderer module.
#endif
#include <sstream>
#include <utility>

using Microsoft::WRL::ComPtr;
using namespace core;

static std::string HexHR(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << (unsigned)(hr & 0xFFFFFFFF);
    return oss.str();
}

class CommandListPool::State {
public:
    virtual ~State() = default;
    virtual bool Initialize(CommandListPool&, Device&, UINT) const { return false; }
    virtual ID3D12GraphicsCommandList* Begin(
        CommandListPool&, UINT, ID3D12PipelineState*) const { return nullptr; }
    virtual bool EndAndExecute(CommandListPool&, Device&) const { return false; }
};

class CommandListPool::UnavailableState final : public State {
public:
    static const UnavailableState& Instance() noexcept {
        static const UnavailableState state;
        return state;
    }

    bool Initialize(CommandListPool& pool, Device& dev, UINT frameCount) const override;
};

class CommandListPool::ReadyState final : public State {
public:
    static const ReadyState& Instance() noexcept {
        static const ReadyState state;
        return state;
    }

    bool Initialize(CommandListPool& pool, Device& dev, UINT frameCount) const override {
        return pool.InitializeResources(dev, frameCount);
    }

    ID3D12GraphicsCommandList* Begin(
        CommandListPool& pool, UINT frameIndex, ID3D12PipelineState* pso) const override;
};

class CommandListPool::RecordingState final : public State {
public:
    static const RecordingState& Instance() noexcept {
        static const RecordingState state;
        return state;
    }

    bool EndAndExecute(CommandListPool& pool, Device& dev) const override;
};

CommandListPool::CommandListPool() noexcept : state_(&UnavailableState::Instance()) {}

bool CommandListPool::Initialize(Device& dev, UINT frameCount) {
    return state_->Initialize(*this, dev, frameCount);
}

void CommandListPool::Shutdown() noexcept {
    list_.Reset();
    allocators_.clear();
    TransitionTo(UnavailableState::Instance());
}

ID3D12GraphicsCommandList* CommandListPool::Begin(
    UINT frameIndex, ID3D12PipelineState* pso) {
    return state_->Begin(*this, frameIndex, pso);
}

bool CommandListPool::EndAndExecute(Device& dev) {
    return state_->EndAndExecute(*this, dev);
}

void CommandListPool::TransitionTo(const State& next) noexcept {
    state_ = &next;
}

bool CommandListPool::InitializeResources(Device& dev, UINT frameCount) {
    auto* d3d = dev.GetDevice();
    if (d3d == nullptr || frameCount == 0) {
        Logger::Error("[CommandListPool] Initialization requires a device and at least one frame slot.\n");
        return false;
    }

    std::vector<ComPtr<ID3D12CommandAllocator>> allocators(frameCount);
    ComPtr<ID3D12GraphicsCommandList> list;

    for (UINT i = 0; i < frameCount; ++i) {
        const HRESULT hr = d3d->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i]));
        if (FAILED(hr)) {
            std::ostringstream oss;
            oss << "[CommandListPool] CreateCommandAllocator[" << i << "] failed: " << HexHR(hr) << "\n";
            Logger::Error(oss.str());
            return false;
        }
    }

    HRESULT hr = d3d->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocators.front().Get(), nullptr,
        IID_PPV_ARGS(&list));
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[CommandListPool] CreateCommandList failed: " << HexHR(hr) << "\n";
        Logger::Error(oss.str());
        return false;
    }

    hr = list->Close();
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[CommandListPool] Initial Close() failed: " << HexHR(hr) << "\n";
        Logger::Error(oss.str());
        return false;
    }

    list_ = std::move(list);
    allocators_ = std::move(allocators);
    Logger::Info("[CommandListPool] Initialized successfully.\n");
    return true;
}

bool CommandListPool::UnavailableState::Initialize(
    CommandListPool& pool, Device& dev, UINT frameCount) const {
    if (!pool.InitializeResources(dev, frameCount)) return false;
    pool.TransitionTo(ReadyState::Instance());
    return true;
}

ID3D12GraphicsCommandList* CommandListPool::ReadyState::Begin(
    CommandListPool& pool, UINT frameIndex, ID3D12PipelineState* pso) const {
    auto* allocator = pool.Allocator(frameIndex);
    if (allocator == nullptr) {
        Logger::Error("[CommandListPool] Begin called with an invalid frame slot.\n");
        return nullptr;
    }

    HRESULT hr = allocator->Reset();
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[CommandListPool] CommandAllocator[" << frameIndex << "] Reset() failed: " << HexHR(hr) << "\n";
        Logger::Error(oss.str());
        return nullptr;
    }

    hr = pool.list_->Reset(allocator, pso);
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "[CommandListPool] CommandList Reset() failed on frame " << frameIndex << ": " << HexHR(hr) << "\n";
        Logger::Error(oss.str());
        return nullptr;
    }

    pool.TransitionTo(RecordingState::Instance());
    return pool.list_.Get();
}

bool CommandListPool::RecordingState::EndAndExecute(CommandListPool& pool, Device& dev) const {
    auto* queue = dev.GetCommandQueue();
    if (queue == nullptr) return false;

    const HRESULT hr = pool.list_->Close();
    if (FAILED(hr)) {
        // D3D12 cannot reset a list after Close fails. Keep the old resources alive
        // until the caller can safely reinitialize or destroy the pool.
        pool.TransitionTo(UnavailableState::Instance());
        std::ostringstream oss;
        oss << "[CommandListPool] CommandList Close() failed: " << HexHR(hr) << "\n";
        Logger::Error(oss.str());
        return false;
    }

    ID3D12CommandList* lists[] = { pool.list_.Get() };
    queue->ExecuteCommandLists(1, lists);
    pool.TransitionTo(ReadyState::Instance());
    return true;
}
