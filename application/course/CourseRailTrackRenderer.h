#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "CourseMeshRenderQueue.h"
#include "CourseRailTrackMeshBakePipeline.h"
#include "RailVehicleWheelContactPresentationBridge.h"
#include "RailVehicleRenderer.h"

struct CourseRailTrackRenderStats final {
    bool sourceValid = false;
    uint32_t visibleRailInstances = 0;
    uint32_t visibleSleeperInstances = 0;
    uint32_t visibleSupportInstances = 0;
    uint32_t visibleWheelInstances = 0;
    uint32_t lodCulledInstances = 0;
    uint32_t capacityCulledInstances = 0;
    uint64_t sourceBakeRevision = 0;
    uint64_t sourceWheelRevision = 0;
    uint64_t revision = 0;
};

// Bounded D3D12 instance renderer for immutable baked track geometry plus the
// four dynamic wheel-contact proxies. No path tessellation occurs here.
class CourseRailTrackRenderer final {
public:
    bool Initialize(
        Microsoft::WRL::ComPtr<ID3D12Device> device,
        size_t capacity = 512);
    void Reset();
    void Sync(
        const CourseRailTrackMeshBakeResult& baked,
        float currentDistance,
        const RailVehicleWheelContactPresentationFrame* wheels,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix,
        const RailVehicleRenderFrame* detachedPanel=nullptr,
        std::span<const RailVehicleRenderFrame> fractures={});

    const std::vector<CourseMeshRenderItem>& Items() const noexcept { return items_; }
    const CourseRailTrackRenderStats& Stats() const noexcept { return stats_; }
    void AppendVehiclePart(const RailVehicleRenderFrame& frame,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& view,const Matrix4x4& projection,
        const RailVehicleWheelContactVisual* wheel=nullptr);

private:
    CourseMeshRenderItem* AllocateItem();
    uint32_t ResolveModelIndex(
        std::span<const CourseMeshModelBinding> models,
        const std::string& meshId,
        const char* fallbackName) const;
    void WriteItem(
        CourseMeshRenderItem& item,
        const CourseMeshModelBinding& model,
        const Matrix4x4& authoredWorld,
        const Matrix4x4& viewProjection,
        const Vector4& color);

    std::vector<CourseMeshRenderItem> items_;
    size_t visibleCount_ = 0;
    CourseRailTrackRenderStats stats_{};
    uint64_t revision_ = 0;
};
