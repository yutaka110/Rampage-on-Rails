#include "CourseRailTrackRenderer.h"

#include "utils/dx12/BufferHelper.h"

#include <algorithm>
#include <cmath>

bool CourseRailTrackRenderer::Initialize(
    Microsoft::WRL::ComPtr<ID3D12Device> device,
    size_t capacity) {
    if (device == nullptr || capacity < 4) return false;
    items_.clear();
    items_.resize(capacity);
    for (CourseMeshRenderItem& item : items_) {
        item.transformResource = CreateBufferResource(device, sizeof(TransformationMatrix));
        item.materialResource = CreateBufferResource(device, sizeof(Material));
        if (item.transformResource == nullptr || item.materialResource == nullptr) return false;
        item.transformResource->Map(
            0, nullptr, reinterpret_cast<void**>(&item.transformData));
        item.materialResource->Map(
            0, nullptr, reinterpret_cast<void**>(&item.materialData));
        if (item.transformData == nullptr || item.materialData == nullptr) return false;
        item.transformData->WVP = MakeIdentity4x4();
        item.transformData->World = MakeIdentity4x4();
        item.transformData->WorldInverseTranspose = MakeIdentity4x4();
        *item.materialData = {};
        item.materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
        item.materialData->enableLighting = true;
        item.materialData->shininess = 18.0f;
        item.materialData->environmentCoefficient = 0.24f;
        item.materialData->specularMode = 1;
        item.materialData->uvTransform = MakeIdentity4x4();
    }
    Reset();
    return true;
}

void CourseRailTrackRenderer::Reset() {
    visibleCount_ = 0;
    stats_ = {};
    for (CourseMeshRenderItem& item : items_) {
        item.visible = false;
        item.useMaterialOverride = false;
        item.name.clear();
        item.meshId.clear();
    }
}

void CourseRailTrackRenderer::Sync(
    const CourseRailTrackMeshBakeResult& baked,
    float currentDistance,
    const RailVehicleWheelContactPresentationFrame* wheels,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewMatrix,
    const Matrix4x4& projMatrix) {
    Reset();
    stats_.revision = ++revision_;
    stats_.sourceValid = baked.valid;
    stats_.sourceBakeRevision = baked.revision;
    stats_.sourceWheelRevision = wheels != nullptr ? wheels->revision : 0;
    if (models.empty()) return;
    const Matrix4x4 viewProjection = Multiply(viewMatrix, projMatrix);

    if (baked.valid && baked.definition.enabled) {
        const float minimumDistance = currentDistance - baked.definition.renderBehindDistance;
        const float maximumDistance = currentDistance + baked.definition.renderAheadDistance;
        auto found = std::lower_bound(
            baked.instances.begin(), baked.instances.end(), minimumDistance,
            [](const CourseRailTrackBakedInstance& item, float distance) {
                return item.endDistance < distance;
            });
        const size_t staticBudget = wheels != nullptr && wheels->valid && items_.size() >= 4
            ? items_.size() - 4
            : items_.size();
        for (; found != baked.instances.end() &&
               found->startDistance <= maximumDistance; ++found) {
            const float delta = std::abs(found->startDistance-currentDistance);
            const bool detailedPart =
                found->part == CourseRailTrackMeshPart::Sleeper ||
                found->part == CourseRailTrackMeshPart::LeftSupport ||
                found->part == CourseRailTrackMeshPart::RightSupport;
            if (detailedPart && delta > baked.definition.nearDetailDistance &&
                found->detailOrdinal % baked.definition.farDetailStride != 0) {
                ++stats_.lodCulledInstances;
                continue;
            }
            if (visibleCount_ >= staticBudget ||
                visibleCount_ >= baked.definition.maximumVisibleInstances) {
                ++stats_.capacityCulledInstances;
                continue;
            }
            const uint32_t modelIndex = ResolveModelIndex(
                models, found->meshId, "course_rail.track_unit");
            const CourseMeshModelBinding& model = models[modelIndex];
            if (!model.loaded) continue;
            CourseMeshRenderItem* item = AllocateItem();
            if (item == nullptr) {
                ++stats_.capacityCulledInstances;
                continue;
            }
            item->kind = found->part == CourseRailTrackMeshPart::Sleeper
                ? CourseMeshRenderKind::TrackSleeper
                : (found->part == CourseRailTrackMeshPart::LeftSupport ||
                   found->part == CourseRailTrackMeshPart::RightSupport)
                    ? CourseMeshRenderKind::TrackSupport
                    : CourseMeshRenderKind::TrackRail;
            item->name = "course_track";
            item->meshId = found->meshId;
            item->modelIndex = modelIndex;
            item->sortDistance = found->startDistance;
            item->visible = true;
            WriteItem(*item, model, found->worldMatrix, viewProjection, found->color);
            if (item->kind == CourseMeshRenderKind::TrackRail)
                ++stats_.visibleRailInstances;
            else if (item->kind == CourseMeshRenderKind::TrackSleeper)
                ++stats_.visibleSleeperInstances;
            else
                ++stats_.visibleSupportInstances;
        }
    }

    // Wheels are presentation-critical and therefore consume the reserved tail
    // of the fixed queue even if static track detail reached its authored budget.
    if (wheels != nullptr && wheels->valid) {
        for (const RailVehicleWheelContactVisual& wheel : wheels->wheels) {
            if (!wheel.visible || visibleCount_ >= items_.size()) continue;
            const uint32_t modelIndex = ResolveModelIndex(
                models, wheel.meshId, "course_rail.wheel_proxy");
            const CourseMeshModelBinding& model = models[modelIndex];
            if (!model.loaded) continue;
            CourseMeshRenderItem* item = AllocateItem();
            if (item == nullptr) break;
            item->kind = CourseMeshRenderKind::VehicleWheel;
            item->name = "rail_vehicle_wheel";
            item->meshId = wheel.meshId;
            item->modelIndex = modelIndex;
            item->visible = true;
            WriteItem(*item, model, wheel.worldMatrix, viewProjection, wheel.color);
            ++stats_.visibleWheelInstances;
        }
    }
}

CourseMeshRenderItem* CourseRailTrackRenderer::AllocateItem() {
    if (visibleCount_ >= items_.size()) return nullptr;
    CourseMeshRenderItem& item = items_[visibleCount_++];
    item.visible = false;
    item.useMaterialOverride = false;
    return &item;
}

uint32_t CourseRailTrackRenderer::ResolveModelIndex(
    std::span<const CourseMeshModelBinding> models,
    const std::string& meshId,
    const char* fallbackName) const {
    for (uint32_t index = 0; index < models.size(); ++index)
        if (models[index].loaded && models[index].name == meshId) return index;
    for (uint32_t index = 0; fallbackName != nullptr && index < models.size(); ++index)
        if (models[index].loaded && models[index].name == fallbackName) return index;
    for (uint32_t index = 0; index < models.size(); ++index)
        if (models[index].loaded) return index;
    return 0;
}

void CourseRailTrackRenderer::WriteItem(
    CourseMeshRenderItem& item,
    const CourseMeshModelBinding& model,
    const Matrix4x4& authoredWorld,
    const Matrix4x4& viewProjection,
    const Vector4& color) {
    Matrix4x4 world = Multiply(model.rootLocal, authoredWorld);
    item.transformData->World = world;
    item.transformData->WVP = Multiply(world, viewProjection);
    item.transformData->WorldInverseTranspose = Transpose(Inverse(world));
    // Reused slots may have held title-only shadow/iron material modes.
    // Restore the gameplay defaults before applying any per-frame overrides.
    *item.materialData = {};
    item.materialData->color = color;
    item.materialData->enableLighting = true;
    item.materialData->uvTransform = MakeIdentity4x4();
    item.materialData->environmentCoefficient = 0.24f;
    item.materialData->specularMode = 1;
    item.materialData->shininess = item.kind == CourseMeshRenderKind::VehicleWheel ? 10.0f : 22.0f;
    item.useMaterialOverride = true;
}
