#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "CourseAsset.h"
#include "TwinShieldDronePose.h"
#include "../terrain/RailPath.h"
#include "utils/math/MathUtils.h"
#include "utils/math/Vector.h"

class CourseSpawnRuntime;
class EnemyCombatPresentationBridge;
class EnemyEncounterReadabilityDirector;

struct CourseMeshModelBinding {
    std::string name;
    Matrix4x4 rootLocal = MakeIdentity4x4();
    bool loaded = false;
};

enum class CourseMeshRenderKind {
    Enemy,
    Obstacle,
    GameplayTerrain,
    HeroLandmark,
    VistaBackground,
    TrackRail,
    TrackSleeper,
    TrackSupport,
    VehicleWheel,
};

// ball and animated_cube are intentionally rejected for authored scenery,
// but remain the packaged fallback presentation for gameplay enemies.
bool IsCourseMeshRenderEligible(
    CourseMeshRenderKind kind,
    const std::string& meshId);

// These authored OBJ surfaces share the gameplay terrain PBR pipeline.
bool IsTitleLandscapeMesh(std::string_view meshId);
Material BuildTitleLandscapePbrMaterial(std::string_view meshId);

struct CourseMeshRenderItem {
    CourseMeshRenderKind kind = CourseMeshRenderKind::Obstacle;
    std::string name;
    std::string meshId;
    CourseTerrainLayer terrainLayer = CourseTerrainLayer::HeroLandmark;
    CourseTerrainCollisionMode collisionMode = CourseTerrainCollisionMode::None;
    uint32_t sourceActorId = 0;
    uint32_t modelIndex = 0;
    float sortDistance = 0.0f;
    bool visible = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> transformResource;
    TransformationMatrix* transformData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;
    bool useMaterialOverride = false;
};

class CourseMeshRenderQueue {
public:
    bool Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device, size_t capacity = 128);
    void Reset();
    void AppendTwinShieldDrone(const TwinShieldDronePose& pose,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix);
    void SyncFromCourseRuntime(
        const CourseSpawnRuntime& runtime,
        const CourseAsset* course,
        float currentDistance,
        const RailPath& railPath,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewMatrix,
        const Matrix4x4& projMatrix,
        const EnemyCombatPresentationBridge* enemyPresentation = nullptr,
        const EnemyEncounterReadabilityDirector* enemyReadability = nullptr);

    const std::vector<CourseMeshRenderItem>& Items() const { return items_; }
    size_t Capacity() const { return items_.size(); }
    size_t VisibleCount() const { return visibleCount_; }

private:
    void WriteTwinShieldDrone(CourseMeshRenderItem& hull,
        const TwinShieldDronePose& pose, std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewProjection);
    void AddObstacleInstances(
        const CourseSpawnRuntime& runtime,
        const RailPath& railPath,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewProjection);
    void AddEnemyInstances(
        const CourseSpawnRuntime& runtime,
        const RailPath& railPath,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const EnemyCombatPresentationBridge* enemyPresentation,
        const EnemyEncounterReadabilityDirector* enemyReadability);
    void AddCourseDebrisInstances(
        const CourseAsset& course,
        float currentDistance,
        const RailPath& railPath,
        std::span<const CourseMeshModelBinding> models,
        const Matrix4x4& viewProjection);
    CourseMeshRenderItem* AllocateItem();
    uint32_t ResolveModelIndex(
        std::span<const CourseMeshModelBinding> models,
        const std::string& meshId,
        const char* fallbackName) const;
    void WriteItemTransform(
        CourseMeshRenderItem& item,
        const Matrix4x4& rootLocal,
        const Vector3& scale,
        const Vector3& rotate,
        const Vector3& translate,
        const Matrix4x4& viewProjection);

    std::vector<CourseMeshRenderItem> items_;
    size_t visibleCount_ = 0;
};
