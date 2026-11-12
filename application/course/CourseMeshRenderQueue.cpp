#include "RailGroundBatteryAttack.h"
#include "RailRouteHazards.h"
#include "RailRearPursuitRules.h"
#include "RailRearPanelMaterial.h"
#include "RailVeilModel.h"
#include "RailFangModel.h"
#include "RailEliteFirePresentation.h"
#include "RailTurretGeometry.h"
#include "CourseMeshRenderQueue.h"
#include "../CaveSubjectMaterial.h"

#include "CourseSpawnRuntime.h"
#include "DebrisCompositionSystem.h"
#include "EnemyCombatPresentationBridge.h"
#include "EnemyEncounterReadabilityDirector.h"
#include "RailTitleScene.h"
#include "TwinShieldDroneShading.h"
#include "utils/dx12/BufferHelper.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace {
Vector3 Add(const Vector3& a, const Vector3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(const Vector3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

Vector3 TransformTurretPoint(Vector3 point,const Matrix4x4& world) {
    return {point.x*world.m[0][0]+point.y*world.m[1][0]+point.z*world.m[2][0]+world.m[3][0],
        point.x*world.m[0][1]+point.y*world.m[1][1]+point.z*world.m[2][1]+world.m[3][1],
        point.x*world.m[0][2]+point.y*world.m[1][2]+point.z*world.m[2][2]+world.m[3][2]};
}

template<class Draw>
void DrawHeavyTurretArmor(const Matrix4x4& head,const Matrix4x4& moving,float opening,float ventOpen,Draw draw) {
    for(float side:{-1.0f,1.0f}) {
        const auto offset=MakeAffineMatrix(Vector3{1,1,1},Vector3{},Vector3{side*.18f,0,0});
        const auto hinge=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,side*ventOpen*1.05f},
            Vector3{side*.68f,.76f,.12f});
        draw("carrier_heavy_turret_vent",Multiply(offset,Multiply(hinge,head)));
        const auto flip=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,side<0?3.14159265f:0},Vector3{});
        const auto shutter=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,side*1.28f*opening,0},
            Vector3{side*(.86f+.38f*opening),0,-2.06f});
        draw("carrier_heavy_turret_shutter",Multiply(flip,Multiply(shutter,moving)));
    }
}

Vector3 ResolveRailLocal(
    const RailPath& railPath,
    float spawnDistance,
    float distanceOffset,
    float lateralOffset,
    float verticalOffset) {
    const RailPathSample sample = railPath.Evaluate(spawnDistance + distanceOffset);
    return Add(
        Add(sample.position, Scale(sample.right, lateralOffset)),
        Scale(sample.up, verticalOffset));
}

Vector3 RotationFromRailTangent(const Vector3& tangent) {
    const float yaw = std::atan2(tangent.x, tangent.z);
    const float pitch = std::asin((std::clamp)(-tangent.y, -1.0f, 1.0f));
    return {pitch, yaw, 0.0f};
}

CourseMeshRenderKind RenderKindForTerrainLayer(CourseTerrainLayer layer) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision:
        return CourseMeshRenderKind::GameplayTerrain;
    case CourseTerrainLayer::HeroLandmark:
        return CourseMeshRenderKind::HeroLandmark;
    case CourseTerrainLayer::VistaBackground:
        return CourseMeshRenderKind::VistaBackground;
    }
    return CourseMeshRenderKind::HeroLandmark;
}

float DefaultCullBehind(CourseTerrainLayer layer) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision:
        return 70.0f;
    case CourseTerrainLayer::HeroLandmark:
        return 180.0f;
    case CourseTerrainLayer::VistaBackground:
        return 420.0f;
    }
    return 180.0f;
}

float DefaultCullAhead(CourseTerrainLayer layer) {
    switch (layer) {
    case CourseTerrainLayer::GameplayCollision:
        return 220.0f;
    case CourseTerrainLayer::HeroLandmark:
        return 360.0f;
    case CourseTerrainLayer::VistaBackground:
        return 760.0f;
    }
    return 360.0f;
}

bool ShouldDrawTerrainPlacement(const CourseTerrainPlacement& placement, float currentDistance) {
    const float behind = placement.cullBehindDistance >= 0.0f
        ? placement.cullBehindDistance
        : DefaultCullBehind(placement.layer);
    const float ahead = placement.cullAheadDistance >= 0.0f
        ? placement.cullAheadDistance
        : DefaultCullAhead(placement.layer);
    const float delta = placement.distance - currentDistance;
    return delta >= -behind && delta <= ahead;
}

bool IsPlaceholderCourseMesh(const std::string& meshId) {
    return meshId == "animated_cube" || meshId == "ball";
}

Matrix4x4 RearPanelLocal(size_t index, bool broken) {
    return MakeAffineMatrix(broken ? Vector3{.88f,.86f,.38f} : Vector3{1,1,1},
        Vector3{}, kRearPanelAnchors[index]);
}

void WriteRearPanelMaterial(CourseMeshRenderItem& panel, float alpha, size_t index, bool veil, bool broken) {
    auto& material = *panel.materialData;
    material = {};
    material.color = {1, 1, 1, alpha};
    material.uvTransform = MakeIdentity4x4();
    material.enableLighting = veil || broken;
    material.specularMode = RailRearPanelMaterial::Mode;
    const auto& basis = panel.transformData->WorldInverseTranspose;
    for (size_t axis = 0; axis < 3; ++axis) {
        std::copy_n(basis.m[axis], 4, material.padding2 + RailRearPanelMaterial::WorldToPanel + axis * 4);
    }
    material.padding2[RailRearPanelMaterial::VeilShape] = veil ? 1.0f : 0.0f;
    material.padding2[RailRearPanelMaterial::PanelIndex] =
        static_cast<float>(RailRearPanelMaterial::EncodeIndex(static_cast<int>(index), broken));
    panel.useMaterialOverride = true;
}
} // namespace

bool IsCourseMeshRenderEligible(
    CourseMeshRenderKind kind,
    const std::string& meshId) {
    if (meshId.empty()) {
        return false;
    }
    return kind == CourseMeshRenderKind::Enemy ||
        !IsPlaceholderCourseMesh(meshId);
}

bool IsCarrierBatterySurfaceMesh(std::string_view meshId) {
    return meshId=="gatling_turret_head" || meshId=="gatling_turret_base" || meshId=="gatling_turret_rotor" ||
        meshId=="carrier_heavy_turret_head" || meshId=="carrier_heavy_turret_base" ||
        meshId=="carrier_heavy_turret_barrel" || meshId=="carrier_heavy_turret_vent" ||
        meshId=="carrier_heavy_turret_shutter";
}

Material BuildCarrierBatterySurfaceMaterial(std::string_view meshId,Vector4 tint,float hitFlash,float counter) {
    Material material{};material.color=tint;material.enableLighting=true;material.uvTransform=MakeIdentity4x4();
    material.specularMode=IsCarrierBatterySurfaceMesh(meshId)?16:3;
    if(material.specularMode!=16){material.shininess=5;material.environmentCoefficient=.025f;return material;}
    // Mode 16 decodes per-face paint/steel/ceramic/rubber bands from authored
    // UVs. Shininess is a roughness bias; environmentCoefficient scales metal.
    material.shininess=meshId=="gatling_turret_rotor"?-.03f:meshId=="carrier_heavy_turret_barrel"?-.015f:
        meshId=="gatling_turret_head"?.03f:meshId=="carrier_heavy_turret_head"?.04f:.06f;
    material.environmentCoefficient=1;
    material.padding2[0]=(std::clamp)(hitFlash,0.0f,1.0f);
    material.padding2[1]=(std::clamp)(counter,0.0f,1.0f);
    return material;
}

bool IsTitleLandscapeMesh(std::string_view meshId) {
    return meshId == "title_ground" || meshId == "title_cliff" ||
        meshId == "title_boulder" || meshId == "title_tunnel" || meshId == "title_sand_clod";
}

bool IsGameplayLandscapeMesh(std::string_view meshId) {
    return meshId == "root_spire_column" || meshId == "organic_arch_large" ||
        meshId == "startup_rear_tunnel" || meshId == "startup_rear_floor" || meshId == "startup_rear_cap" ||
        meshId == "rib_tunnel_wall" || meshId == "rib_tunnel_inner" || meshId == "curved_canyon_wall" ||
        meshId == "vista_hole_wall" || meshId == "spire_broken_bridge_arc";
}

Material BuildGameplayLandscapePbrMaterial(std::string_view meshId) {
    Material material=BuildTitleLandscapePbrMaterial("title_cliff");
    material.specularMode=14; // Authored OBJ UVs; world-space terrain PBR.
    material.color={0.72f,0.64f,0.57f,0.30f};
    material.padding[0]=0.40f;
    material.padding[1]=0.35f;
    material.padding[2]=0.40f;
    material.shininess=0.07f;
    material.environmentCoefficient=0.25f;
    material.padding2[0]=0.06f;
    material.padding2[1]=0.35f;
    material.padding2[9]=0.40f;
    if (meshId == "organic_arch_large" || meshId == "rib_tunnel_wall" || meshId == "rib_tunnel_inner") {
        // Match adjacent cliff strata/detail in world space, including the
        // buried aprons and fracture rocks baked into the same solid mesh.
        material.color = {0.90f, 0.84f, 0.77f, 0.55f};
        material.padding[0] = 0.75f;
        material.padding[1] = 0.55f;
        material.padding[2] = 0.35f;
        material.environmentCoefficient = 0.65f;
        material.padding2[1] = 0.65f;
        material.padding2[3] = 1.35f;
        material.padding2[4] = 72.0f;
        material.padding2[5] = 1.75f;
        material.padding2[6] = 0.72f;
        material.padding2[7] = 180.0f;
        material.padding2[9] = 0.65f;
        material.padding2[13] = 0.92f;
    }
    return material;
}

Material BuildRailStartupRearMaterial(const Material& gameplayTerrain) {
    Material material=gameplayTerrain;
    // Preserve the live course palette, detail scale and lighting response.
    // Only the vertex-UV convention differs from the streamed terrain.
    material.specularMode=14;
    return material;
}

void CourseMeshRenderQueue::SyncStartupRearMaterial(const Material& gameplayTerrain) {
    for(size_t i=0;i<visibleCount_;++i) {
        auto& item=items_[i];
        if(item.visible && item.materialData && std::string_view(item.meshId).starts_with("startup_rear_"))
            *item.materialData=BuildRailStartupRearMaterial(gameplayTerrain);
    }
}

float RailStartupRearFloorHeight(float forwardDistance) {
    const float t=(std::clamp)((forwardDistance+12.0f)/52.0f,0.0f,1.0f);
    // Bury the forward end in the much lower streamed cave floor, rather
    // than leaving a raised rectangular platform beside the starting cart.
    return -0.5f-30.0f*t*t*(3.0f-2.0f*t);
}

std::array<RailStartupRearScenery,3> BuildRailStartupRearScenery(const RailPath& path) {
    const auto start=path.Evaluate(0.0f);
    const auto rotation=RotationFromRailTangent(start.tangent);
    auto tunnelRotation=rotation; tunnelRotation.y+=3.14159265359f;
    // The existing sealed, irregular cave mesh is extended behind distance
    // zero. Its mouth overlaps the streamed cave; its cap closes the far view.
    return {{{"startup_rear_tunnel","title_tunnel",
            Add(start.position,Scale(start.tangent,6.0f)),tunnelRotation,{2.2f,2.3f,2.0f}},
        {"startup_rear_floor","course_startup.rear_floor",start.position,rotation,{1,1,1}},
        // Overlap the cap and floor with a solid rock backstop so their
        // different slopes cannot expose sky at the base of the far wall.
        {"startup_rear_cap","course_rail.track_unit",
            Add(Add(start.position,Scale(start.tangent,-142.0f)),Scale(start.up,8.0f)),
            rotation,{64.0f,40.0f,12.0f}}}};
}

Material BuildTitleLandscapePbrMaterial(std::string_view meshId) {
    Material material{};
    material.color = {1.12f, 0.98f, 0.89f, 0.18f}; // Warm canyon tint, macro noise amount.
    material.enableLighting = true;
    material.uvTransform = MakeIdentity4x4();
    material.padding[0] = 0.65f; // Detail normal strength.
    material.padding[1] = 0.30f; // Cavity AO strength.
    material.padding[2] = 1.00f; // Environment sky fill.
    material.shininess = 0.18f; // Terrain specular strength (not a Phong exponent).
    material.environmentCoefficient = 0.15f; // Strata strength.
    material.specularMode = (meshId == "title_ground" || meshId == "title_sand_clod") ? 9 : (meshId == "title_tunnel" ? 7 : 6);
    material.padding2[0] = 0.10f; // Rim strength.
    material.padding2[1] = 0.55f; // Micro detail strength.
    material.padding2[2] = 1.0f; // Shared detail cache.
    material.padding2[3] = 1.0f; // Cache scale.
    material.padding2[4] = 96.0f; // Detail tile world size.
    material.padding2[5] = 1.0f; // Near scale.
    material.padding2[6] = 0.45f; // Far scale.
    material.padding2[7] = 85.0f; // Distance blend.
    material.padding2[8] = 1.0f; // Shared detail normal map.
    material.padding2[9] = 0.60f; // Detail map strength.
    material.padding2[10] = 0.70f; // Hybrid blend.
    material.padding2[13] = 0.55f; // Strata breakup.
    material.padding2[14] = 0.25f; // Floor sand shadow.
    material.padding2[15] = 0.10f; // Backlight rim boost.
    if (meshId == "title_ground" || meshId == "title_sand_clod") {
        // Ground054's normal map supplies the relief; avoid overlaying the
        // old procedural sand ripples / shared rock detail normal on top.
        material.padding[0] = 0.20f;
        material.padding2[1] = 0.20f;
        material.padding2[8] = 0.0f;
        material.padding2[14] = 0.0f;
        material.environmentCoefficient = 0.0f;
    }
    return material;
}

bool CourseMeshRenderQueue::Initialize(
    Microsoft::WRL::ComPtr<ID3D12Device> device,
    size_t capacity) {
    if (device == nullptr || capacity == 0) {
        return false;
    }

    items_.clear();
    items_.resize(capacity);
    visibleCount_ = 0;

    for (CourseMeshRenderItem& item : items_) {
        item.transformResource = CreateBufferResource(device, sizeof(TransformationMatrix));
        if (item.transformResource == nullptr) {
            return false;
        }
        item.transformResource->Map(
            0,
            nullptr,
            reinterpret_cast<void**>(&item.transformData));
        if (item.transformData == nullptr) {
            return false;
        }
        item.transformData->WVP = MakeIdentity4x4();
        item.transformData->World = MakeIdentity4x4();
        item.transformData->WorldInverseTranspose = MakeIdentity4x4();
        item.materialResource = CreateBufferResource(device, sizeof(Material));
        if (item.materialResource == nullptr) {
            return false;
        }
        item.materialResource->Map(
            0,
            nullptr,
            reinterpret_cast<void**>(&item.materialData));
        if (item.materialData == nullptr) {
            return false;
        }
        *item.materialData = {};
        item.materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
        item.materialData->enableLighting = true;
        item.materialData->shininess = 5.0f;
        item.materialData->environmentCoefficient = 0.16f;
        item.materialData->specularMode = 1;
        item.materialData->uvTransform = MakeIdentity4x4();
        item.visible = false;
    }

    return true;
}

void CourseMeshRenderQueue::Reset() {
    visibleCount_ = 0;
    for (CourseMeshRenderItem& item : items_) {
        item.visible = false;
        item.sourceActorId = 0;
        item.useMaterialOverride = false;
        item.name.clear();
        item.meshId.clear();
        item.terrainLayer = CourseTerrainLayer::HeroLandmark;
        item.collisionMode = CourseTerrainCollisionMode::None;
        item.sortDistance = 0.0f;
    }
}

void CourseMeshRenderQueue::SyncFromCourseRuntime(
    const CourseSpawnRuntime& runtime,
    const CourseAsset* course,
    float currentDistance,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewMatrix,
    const Matrix4x4& projMatrix,
    const EnemyCombatPresentationBridge* enemyPresentation,
    const EnemyEncounterReadabilityDirector* enemyReadability,
    bool includeStartupRearScenery,const RailVeilAttackFrame* veilAttack,const RailEliteFirePresentation* eliteFire) {
    Reset();
    if (railPath.Length() <= 0.0f || models.empty()) {
        return;
    }

    const Matrix4x4 viewProjection = Multiply(viewMatrix, projMatrix);
    Matrix4x4 cameraView = viewMatrix;
    const Matrix4x4 cameraWorld = Inverse(cameraView);
    const Vector3 cameraPosition{cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};

    // Gameplay targets are submitted before scenery and decorative debris so
    // a saturated fixed-capacity queue can never make enemies disappear.
    AddEnemyInstances(
        runtime,
        railPath,
        models,
        viewProjection,
        cameraPosition,
        enemyPresentation,
        enemyReadability,veilAttack,eliteFire);

    if(veilAttack && veilAttack->bridgeWreck && veilAttack->bridgePose.visible) {
        if(auto* wreck=AllocateItem())
            WriteVeil(*wreck,veilAttack->bridgePose,*veilAttack,false,true,0,models,viewProjection);
    }

    // Collidable obstacles must not lose their draw slots to scenery.
    AddObstacleInstances(runtime, railPath, models, viewProjection);

    if(includeStartupRearScenery || std::any_of(runtime.Enemies().begin(),runtime.Enemies().end(),
            [](const auto& actor){return actor.startupFlightControlled;}))
        AddStartupRearScenery(railPath,models,viewProjection);

    if (course != nullptr) {
        AppendTerrain(course->terrainPlacements,currentDistance,railPath,models,viewMatrix,projMatrix);
        AddCourseDebrisInstances(
            *course,
            currentDistance,
            railPath,
            models,
            viewProjection);
    }

}

void CourseMeshRenderQueue::AppendTerrain(std::span<const CourseTerrainPlacement> placements,
    float currentDistance,const RailPath& railPath,std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewMatrix,const Matrix4x4& projMatrix) {
    if(models.empty() || railPath.Length()<=0)return;
    const auto viewProjection=Multiply(viewMatrix,projMatrix);
        for (const CourseTerrainPlacement& placement : placements) {
            if (!ShouldDrawTerrainPlacement(placement, currentDistance)) {
                continue;
            }
            const CourseMeshRenderKind renderKind =
                RenderKindForTerrainLayer(placement.layer);
            if (!IsCourseMeshRenderEligible(renderKind, placement.meshId)) {
                continue;
            }

            const RailPathSample sample =
                railPath.Evaluate(placement.distance + placement.forwardOffset);
            const uint32_t modelIndex =
                ResolveModelIndex(models, placement.meshId, "animated_cube");
            const CourseMeshModelBinding& model = models[modelIndex];
            if (!IsCourseMeshRenderEligible(renderKind, model.name)) {
                continue;
            }

            CourseMeshRenderItem* item = AllocateItem();
            if (item == nullptr) {
                break;
            }

            item->kind = renderKind;
            item->terrainLayer = placement.layer;
            item->collisionMode = placement.collisionMode;
            item->name = placement.id;
            item->meshId = placement.meshId;
            item->sourceActorId = 0;
            item->modelIndex = modelIndex;
            item->sortDistance = sample.distance;
            item->visible = model.loaded && item->transformData != nullptr;
            if (!item->visible) {
                continue;
            }

            if (placement.meshId == "rail_hazard_solid" && item->materialData != nullptr) {
                item->materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
                item->materialData->enableLighting = true;
                item->materialData->shininess = 1.0f;
                item->materialData->environmentCoefficient = 0.0f;
                item->materialData->specularMode = 2; // Diffuse-only stone.
                item->useMaterialOverride = true;
            }
            if (IsTitleLandscapeMesh(placement.meshId) && item->materialData != nullptr) {
                *item->materialData = BuildTitleLandscapePbrMaterial(placement.meshId);
                item->useMaterialOverride = true;
            }
            if (IsGameplayLandscapeMesh(placement.meshId) && item->materialData != nullptr) {
                *item->materialData=BuildGameplayLandscapePbrMaterial(placement.meshId);
                item->useMaterialOverride=true;
            }
            if (placement.meshId == "title_stake" && item->materialData != nullptr) {
                Material timber{};
                timber.color={1,1,1,1};
                timber.enableLighting=true;
                timber.uvTransform=MakeIdentity4x4();
                timber.specularMode=2; // Matte timber, using its authored wood albedo.
                *item->materialData=timber;
                item->useMaterialOverride=true;
            }
            if ((placement.meshId == "course_blockout_bridge" || placement.meshId == "rail_speed_marker") && item->materialData != nullptr) {
                Material steel{};
                steel.color={1,1,1,1};
                steel.enableLighting=true;
                steel.uvTransform=MakeIdentity4x4();
                steel.specularMode=2;
                *item->materialData=steel;
                item->useMaterialOverride=true;
            }
            if ((placement.meshId=="course_blockout_bridge" ||
                 std::string_view(placement.meshId).starts_with("bridge_escape_")) && item->materialData) {
                Material steel{};
                steel.color={1,1,1,1};
                steel.enableLighting=true;
                steel.uvTransform=MakeIdentity4x4();
                *item->materialData=WithCourseSurfaceProfile(WithCaveSurfaceLighting(steel,
                    {.74f,.78f,.82f},.66f,.36f,.84f),CourseSurfaceProfile::Structure);
                item->useMaterialOverride=true;
            }
            const Vector3 center = ResolveRailLocal(
                railPath,
                placement.distance,
                placement.forwardOffset,
                placement.lateralOffset,
                placement.verticalOffset);
            WriteItemTransform(
                *item,
                model.rootLocal,
                placement.scale,
                Add(RotationFromRailTangent(sample.tangent), placement.rotation),
                center,
                viewProjection);
        }
}

void CourseMeshRenderQueue::AddStartupRearScenery(const RailPath& path,
        std::span<const CourseMeshModelBinding> models,const Matrix4x4& viewProjection) {
    for(const auto& source:BuildRailStartupRearScenery(path)) {
        const auto index=ResolveModelIndex(models,source.modelId,nullptr);
        if(!models[index].loaded || models[index].name!=source.modelId) continue;
        auto* item=AllocateItem(); if(!item) return;
        item->kind=CourseMeshRenderKind::VistaBackground;
        item->terrainLayer=CourseTerrainLayer::VistaBackground;
        item->collisionMode=CourseTerrainCollisionMode::None;
        item->name=item->meshId=source.meshId;
        item->modelIndex=index;
        item->visible=item->transformData!=nullptr;
        item->sortDistance=-76.0f;
        if(item->materialData) {
            *item->materialData=BuildGameplayLandscapePbrMaterial("rib_tunnel_wall");
            item->useMaterialOverride=true;
        }
        WriteItemTransform(*item,models[index].rootLocal,source.scale,source.rotation,
            source.position,viewProjection);
    }
}

void CourseMeshRenderQueue::AddObstacleInstances(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection) {
    for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
        if(obstacle.age>=obstacle.desc.lifetime)continue;
        if(RailRouteHazards::IsBarricade(obstacle) || RailRouteHazards::IsSlab(obstacle)) {
            using namespace RailRouteHazards;
            const bool shelf=IsShelf(obstacle);
            const float distance=obstacle.desc.spawnDistance+obstacle.desc.distanceOffset;
            const auto track=railPath.Evaluate(distance);
            const auto facing=RotationFromRailTangent(track.tangent);
            const auto draw=[&](const char* mesh,Vector3 rail,Vector3 size,Vector3 tilt,Vector4 ink,bool unlit=false) {
                const uint32_t index=ResolveModelIndex(models,mesh,nullptr);
                if(models[index].name!=mesh || !models[index].loaded)return;
                auto* part=AllocateItem();
                if(!part)return;
                part->kind=CourseMeshRenderKind::Obstacle;part->sourceActorId=obstacle.actorId;
                part->name=obstacle.desc.id;part->meshId=mesh;part->modelIndex=index;
                part->sortDistance=rail.z;part->visible=part->transformData!=nullptr;
                if(part->materialData) {
                    part->materialData->color=ink;
                    const bool barricade=std::string_view(mesh).starts_with("route_barricade_");
                    part->materialData->enableLighting=!unlit && !barricade;
                    // Painted safety colors should remain distinct against both shaded and sunlit rock.
                    if(barricade)part->materialData->environmentCoefficient=0;
                    part->materialData->shininess=5;
                    part->materialData->specularMode=unlit?5:2;part->useMaterialOverride=true;
                }
                WriteItemTransform(*part,models[index].rootLocal,size,Add(facing,tilt),
                    ResolveRailLocal(railPath,rail.z,0,rail.x,rail.y),viewProjection);
                const std::string_view meshId=mesh;
                const bool naturalArch=meshId.starts_with("route_arch_") || meshId.starts_with("route_shelf_");
                if(!unlit && part->materialData && (naturalArch || meshId=="rail_hazard_solid" ||
                    meshId=="rail_hazard_block")) {
                    const auto hit=Strike(obstacle);
                    const auto impact=ResolveRailLocal(railPath,hit.z,0,hit.x,hit.y);
                    const float since=obstacle.age-BoltImpact;
                    const float flash=since<0?0:1-Ease(since,0,.19f);
                    const auto& world=part->transformData->World;
                    const auto axis=[&](int row) {
                        const Vector3 v{world.m[row][0],world.m[row][1],world.m[row][2]};
                        return Scale(v,1/(std::max)(.0001f,std::hypot(v.x,v.y,v.z)));
                    };
                    const auto x=axis(0),y=axis(1);
                    auto& material=*part->materialData;
                    material.specularMode=18;
                    material.padding2[1]=x.x;material.padding2[2]=x.y;material.padding2[3]=x.z;
                    material.padding2[4]=(meshId=="route_arch_shoulder" || meshId=="route_shelf_tip")?
                        2.0f:(meshId=="rail_hazard_block"?1.0f:0.0f);
                    material.padding2[5]=y.x;material.padding2[6]=y.y;material.padding2[7]=y.z;
                    material.padding2[8]=naturalArch ? 1+Ramp(obstacle.age,FractureStart,FractureEnd)*
                        (1-Ease(obstacle.age,SupportBreak,1.49f)) : 0.0f;
                    material.padding2[9]=world.m[3][0];material.padding2[10]=world.m[3][1];material.padding2[11]=world.m[3][2];
                    material.padding2[12]=flash;
                    material.padding2[13]=impact.x;material.padding2[14]=impact.y;material.padding2[15]=impact.z;
                }
            };
            const float age=obstacle.age;
            if(IsBarricade(obstacle)) {
                const auto extents=obstacle.desc.halfExtents;
                const bool open=obstacle.desc.hitPoints<=0;
                const float u=open?Ramp(age,0,BarricadeDebrisSeconds):0;
                const float damage=1-(std::clamp)(obstacle.desc.hitPoints/BarricadeHealth,0.0f,1.0f);
                for(float side:{-1.0f,1.0f}) {
                    const float spread=open?2.58f+2.45f*u:0;
                    draw("route_barricade_panel",{side*(extents.x*.5f+spread),
                        obstacle.desc.verticalOffset+(open?1.8f*u-4.2f*u*u:0),distance+(open?u*1.45f:0)},
                        {extents.x*.5f,extents.y,extents.z},
                        {open?u*.48f:0,side*(open?.32f+1.4f*u:damage*.055f),side*(open?.17f+1.6f*u:damage*.045f)},
                        {.95f-.14f*damage,.95f-.14f*damage,.95f-.14f*damage,1});
                    const float postX=side*(extents.x+BarricadePostOffset+4.0f*u);
                    draw("route_barricade_post",{postX,-6.5f*u*u,distance},
                        {1,1,1},{u*.4f,0,-side*u*.5f},{1,1,1,1});
                    if(!open) {
                        const float pulse=.80f+.20f*std::sin(age*4.4f);
                        draw("route_barricade_lamp",{postX,BarricadeBeaconHeight,distance},
                            {1,1,1},{},{1.62f*pulse,.85f*pulse,.22f*pulse,1},true);
                    }
                }
                if(!open && damage<.55f)
                    draw("route_barricade_panel",{0,obstacle.desc.verticalOffset+.43f,distance-.66f},
                        {.37f,.24f,.12f},{0,0,.16f*damage},{1.0f,.64f,.20f,1});
            } else {
                const Vector4 bedrock{.58f,.47f,.35f,1};
                draw(shelf?"route_shelf_root":"route_arch_roots",{0,0,distance},{1,1,1},{},bedrock);
                for(float side:{-1.0f,1.0f}) {
                    if(shelf && side<0)continue;
                    draw("rail_hazard_solid",{side*10.1f,ArchPlatformTop-1.8f,distance},
                        {5.6f,1.8f,4.3f},{},bedrock);
                    draw("rail_hazard_solid",{side*12.2f,ArchPlatformTop-4.5f,distance+.55f},
                        {3.4f,2.4f,3.25f},{0,0,side*.25f},bedrock);
                }
                const auto slab=VisibleStone(obstacle,age);
                if(age<GroundImpact)draw(shelf?"route_shelf_tip":"route_arch_shoulder",slab.rail,{1,1,1},{},bedrock);
                if(age>=GroundImpact)for(float side:{-1.0f,1.0f})for(int i=0;i<3;++i) {
                    const float spread=Ease(age,GroundImpact,GroundImpact+.35f);
                    const float size=1.3f+.4f*i;
                    const float x=slab.rail.x+side*((shelf?1.7f:4.9f)+2.1f*i+1.5f*spread);
                    draw("rail_hazard_block",{x,size*.52f,distance+(i-1)*1.8f},
                        {size,size*.78f,size*.94f},{.17f*i,side*(.35f+.4f*i),side*.24f},bedrock);
                }
                if(age>=SupportBreak && age<GroundImpact)for(unsigned i=0;i<5;++i) {
                    const float t=age-SupportBreak-.031f*i;
                    if(t<0)continue;
                    const auto strike=Strike(obstacle);
                    const float side=i%2==0?-1.0f:1.0f;
                    const auto fragment=Add(strike,{side*(3.4f*i+t*(.65f+.51f*i)),.18f*i-6.1f*t*t,
                        -.15f-t*(.49f+.31f*i)});
                    if(fragment.y<-5.0f)continue;
                    const float size=.19f+.054f*(i%4);
                    draw("rail_hazard_block",fragment,{size,size*.65f,size*.73f},
                        {t*2.6f,t*1.2f,t*(2.1f+.64f*i)},{.75f,.63f,.49f,1});
                }
                const auto landing=LandingContact(obstacle);
                if(age>=GroundImpact && age<GroundImpact+1.1f)for(unsigned i=0;i<11;++i) {
                    const float t=age-GroundImpact-.009f*(i%5);
                    if(t<0)continue;
                    const float size=.076f+.023f*(i%5);
                    const float angle=.48f+2.39996f*i;
                    const float firstHop=.19f+.024f*(i%5),secondHop=.10f+.014f*(i%4);
                    const float first=Ramp(t,0,firstHop),second=Ramp(t,firstHop,firstHop+secondHop);
                    const float height=(.36f+.11f*(i%5))*4*first*(1-first)+
                        (.09f+.033f*(i%4))*4*second*(1-second);
                    // Lose speed at each impact, then let the chip roll to rest.
                    const float rollOut=Ease(t,firstHop+secondHop,firstHop+secondHop+.21f);
                    const float travel=(.78f+.14f*(i%6))*(first+.36f*second+.14f*rollOut);
                    const float x=landing.x+(i%2==0?-1.0f:1.0f)*(1.3f+.68f*i)+std::cos(angle)*travel;
                    const float z=landing.z-1.10f-.10f*(i%4)+(std::sin(angle)-.20f)*travel*.58f;
                    const float spin=first+.66f*second+.35f*rollOut;
                    draw("rail_hazard_block",{x,landing.y+size+height,z},{size,size*.78f,size*.88f},
                        {spin*(1.85f+.51f*(i%4)),angle+spin*1.57f,spin*(i%2?2.4f:-2.85f)},
                        {.79f,.68f,.53f,1});
                }
                for(unsigned i=0;i<3;++i) {
                    const float t=age-ChipRelease(i);
                    if(t<=0 || t>=ChipFlight)continue;
                    const float size=.23f+.06f*i;
                    draw("rail_hazard_block",CrossingChip(obstacle,i,age),{size,size*.68f,size*.72f},
                        {t*2.8f,t*1.7f,-t*3.7f},{.85f,.74f,.57f,1});
                }
                if(age>BoltImpact && age<2.49f)for(int i=0;i<5;++i) {
                    const float t=(std::max)(0.0f,age-BoltImpact-.053f*i);
                    if(t==0)continue;
                    auto chip=Strike(obstacle);
                    chip.x-=t*(1.1f+.66f*i);chip.y+=.25f*i+t*(1.3f-.26f*i)-4.7f*t*t;
                    chip.z+=(i-2)*.44f+t*(i%2?.92f:-.7f);
                    const float size=.15f+.09f*i;
                    draw("rail_hazard_solid",chip,{size*.9f,size,size*.64f},{t*2.3f,t*1.4f,t*3.6f},{.71f,.61f,.49f,1});
                }
            }
            continue;
        }
        if (obstacle.desc.breakable && obstacle.desc.hitPoints <= 0.0f) continue;
        if (!IsCourseMeshRenderEligible(
                CourseMeshRenderKind::Obstacle,
                obstacle.desc.meshId)) {
            continue;
        }

        const RailPathSample sample =
            railPath.Evaluate(obstacle.desc.spawnDistance + obstacle.desc.distanceOffset);
        const uint32_t modelIndex =
            ResolveModelIndex(models, obstacle.desc.meshId, "animated_cube");
        const CourseMeshModelBinding& model = models[modelIndex];
        if (!IsCourseMeshRenderEligible(
                CourseMeshRenderKind::Obstacle,
                model.name)) {
            continue;
        }

        CourseMeshRenderItem* item = AllocateItem();
        if (item == nullptr) {
            break;
        }

        item->kind = CourseMeshRenderKind::Obstacle;
        item->name = obstacle.desc.id;
        item->meshId = obstacle.desc.meshId;
        item->sourceActorId = obstacle.actorId;
        item->modelIndex = modelIndex;
        item->sortDistance = sample.distance;
        item->visible = model.loaded && item->transformData != nullptr;
        if (!item->visible) {
            continue;
        }

        if (item->materialData != nullptr) {
            item->materialData->color = obstacle.desc.color;
            item->materialData->enableLighting = true;
            const bool stone = obstacle.desc.meshId == "rail_hazard_block" ||
                obstacle.desc.meshId == "rail_hazard_solid";
            item->materialData->shininess = stone ? 1.0f : (obstacle.desc.breakable ? 12.0f : 4.0f);
            item->materialData->environmentCoefficient = stone ? 0.0f : (obstacle.desc.breakable ? 0.28f : 0.12f);
            item->materialData->specularMode = stone ? 2 : (obstacle.desc.breakable ? 1 : 0);
            item->useMaterialOverride = true;
        }

        const Vector3 center = ResolveRailLocal(
            railPath,
            obstacle.desc.spawnDistance,
            obstacle.desc.distanceOffset,
            obstacle.desc.lateralOffset,
            obstacle.desc.verticalOffset);
        WriteItemTransform(
            *item,
            model.rootLocal,
            obstacle.desc.halfExtents,
            RotationFromRailTangent(sample.tangent),
            center,
            viewProjection);
    }

}

bool CourseMeshRenderQueue::WriteFang(CourseMeshRenderItem& hull,const TwinShieldDronePose& pose,
    float recoil,bool exposed,bool damaged,const RailRearArmor* armor,
    std::span<const CourseMeshModelBinding> models,const Matrix4x4& viewProjection) {
    std::array<uint32_t,RailFangModel::Parts.size()> indices{};
    for(size_t i=0;i<indices.size();++i) {
        const auto name=RailFangModel::Mesh(i,exposed,damaged);
        indices[i]=ResolveModelIndex(models,name,nullptr);
        if(!models[indices[i]].loaded || models[indices[i]].name!=name)return false;
    }
    const auto panelIndex=ResolveModelIndex(models,"rear_boost_panel",nullptr);
    const size_t panelCount=armor ? kRearPanelAnchors.size() : 0;
    if(items_.size()-visibleCount_<7u-unsigned(damaged)+panelCount)return false;
    const auto parent=BuildTwinShieldDroneWorldMatrix(pose);
    const auto motion=RailFangModel::FiringPose(recoil);
    const uint32_t owner=hull.sourceActorId;const float sort=hull.sortDistance;
    const auto world=[&](CourseMeshRenderItem& item,const Matrix4x4& local,uint32_t model) {
        item.transformData->World=Multiply(models[model].rootLocal,Multiply(local,parent));
        item.transformData->WVP=Multiply(item.transformData->World,viewProjection);
        item.transformData->WorldInverseTranspose=Transpose(Inverse(item.transformData->World));
    };
    for(size_t i=0;i<indices.size();++i) {
        const auto part=RailFangModel::EvaluatePart(i,motion,damaged);if(!part.visible)continue;
        auto* item=i==0?&hull:AllocateItem();if(!item)break;
        item->kind=CourseMeshRenderKind::Enemy;item->name=RailFangModel::Parts[i].id;
        item->meshId=RailFangModel::Mesh(i,exposed,damaged);item->sourceActorId=owner;item->sortDistance=sort;
        item->modelIndex=indices[i];item->visible=item->transformData && item->materialData;
        if(!item->visible)continue;
        *item->materialData={};auto& material=*item->materialData;
        material.color={1,1,1,pose.alpha};material.uvTransform=MakeIdentity4x4();material.enableLighting=true;
        material.specularMode=i==0 && exposed && !damaged?17:2;
        if(material.specularMode==17)material.padding2[0]=.94f;
        material.shininess=12;material.environmentCoefficient=.04f;item->useMaterialOverride=true;
        world(*item,part.local,indices[i]);
    }
    if(armor && models[panelIndex].loaded && models[panelIndex].name=="rear_boost_panel")
        for(size_t i=0;i<panelCount;++i) {
            const bool broken=!armor->HasPanel(i);
            auto* panel=AllocateItem();if(!panel)break;
            panel->kind=CourseMeshRenderKind::Enemy;panel->name=panel->meshId="rear_boost_panel";
            panel->sourceActorId=owner;panel->sortDistance=sort;panel->modelIndex=panelIndex;
            panel->visible=panel->transformData && panel->materialData;if(!panel->visible)continue;
            world(*panel,RearPanelLocal(i,broken),panelIndex);
            WriteRearPanelMaterial(*panel, pose.alpha, i, false, broken);
        }
    return true;
}

bool CourseMeshRenderQueue::WriteVeil(CourseMeshRenderItem& hull,const TwinShieldDronePose& pose,
    const RailVeilAttackFrame& attack,bool sensorExposed,bool sensorDamaged,const RailRearArmor* armor,
    std::span<const CourseMeshModelBinding> models,const Matrix4x4& viewProjection) {
    std::array<uint32_t,RailVeilModel::Parts.size()> indices{};
    for(size_t i=0;i<indices.size();++i) {
        indices[i]=ResolveModelIndex(models,RailVeilModel::Parts[i].id,nullptr);
        if(!models[indices[i]].loaded || models[indices[i]].name!=RailVeilModel::Parts[i].id)return false;
    }
    const auto parent=BuildTwinShieldDroneWorldMatrix(pose);
    const auto writeWorld=[&](CourseMeshRenderItem& item,Matrix4x4 world) {
        item.transformData->World=world;
        item.transformData->WVP=Multiply(world,viewProjection);
        item.transformData->WorldInverseTranspose=Transpose(Inverse(world));
    };
    const uint32_t owner=hull.sourceActorId;
    const float sort=hull.sortDistance;
    for(size_t i=0;i<indices.size();++i) {
        const auto frame=RailVeilModel::EvaluatePart(i,attack,sensorExposed,sensorDamaged);
        if(!frame.visible)continue;
        auto* item=i==0?&hull:AllocateItem();if(!item)break;
        item->kind=CourseMeshRenderKind::Enemy;item->name=RailVeilModel::Parts[i].id;
        item->meshId=item->name;item->sourceActorId=owner;item->sortDistance=sort;
        item->modelIndex=indices[i];item->visible=item->transformData && item->materialData;
        if(!item->visible)continue;
        *item->materialData={};auto& material=*item->materialData;
        material.color={1,1,1,pose.alpha};material.uvTransform=MakeIdentity4x4();
        material.enableLighting=true;material.specularMode=16;material.shininess=.035f;
        material.environmentCoefficient=1;material.padding2[0]=pose.flash*.4f;
        if(attack.bridgeWreck && (i==0 || i==1 || i==4))
            material.color={.69f,.66f,.62f,pose.alpha};
        if(i==8 || i==9 || (i==6 && sensorExposed)) {
            material.enableLighting=false;material.specularMode=5;
            if(i==8) {
                const float charge=attack.charge;
                material.color={.80f+charge*1.2f,.025f+charge*2.2f,.055f+charge*3.5f,pose.alpha};
                material.color.x+=attack.flash*2.5f;material.color.y+=attack.flash*2.2f;material.color.z+=attack.flash*1.5f;
            } else material.color=i==6?Vector4{2.0f,.18f,.05f,pose.alpha}:
                attack.bridgeWreck?Vector4{.13f,.013f,.008f,pose.alpha}:Vector4{1.2f,.025f,.045f,pose.alpha};
        }
        item->useMaterialOverride=true;
        writeWorld(*item,Multiply(models[indices[i]].rootLocal,Multiply(frame.local,parent)));
    }
    const auto panelIndex=ResolveModelIndex(models,"veil_boost_panel",nullptr);
    if(armor && models[panelIndex].loaded && models[panelIndex].name=="veil_boost_panel")
        for(size_t i=0;i<kRearPanelAnchors.size();++i) {
            const bool broken=!armor->HasPanel(i);
            auto* panel=AllocateItem();if(!panel)break;
            panel->kind=CourseMeshRenderKind::Enemy;panel->name="veil_boost_panel";panel->meshId=panel->name;
            panel->sourceActorId=owner;panel->sortDistance=sort;panel->modelIndex=panelIndex;
            panel->visible=panel->transformData && panel->materialData;if(!panel->visible)continue;
            writeWorld(*panel,Multiply(models[panelIndex].rootLocal,
                Multiply(RearPanelLocal(i,broken),parent)));
            WriteRearPanelMaterial(*panel, pose.alpha, i, true, broken);
        }
    return true;
}

void CourseMeshRenderQueue::AddEnemyInstances(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection,
    const Vector3& cameraPosition,
    const EnemyCombatPresentationBridge* enemyPresentation,
    const EnemyEncounterReadabilityDirector* enemyReadability,const RailVeilAttackFrame* veilAttack,const RailEliteFirePresentation* eliteFire) {
    for (const CourseEnemyActor& enemy : runtime.Enemies()) {
        if(veilAttack && veilAttack->bridgeWreck && enemy.desc.actorAssetId=="rear_veil" && enemy.desc.hitPoints<=0)continue;
        if(RailGroundBattery::IsBattery(enemy.desc.actorAssetId) && !enemy.groundPlacementResolved) continue;
        // A frozen visual wreck owns all four parts after defeat. Do not also
        // draw the shrinking, camera-facing live actor during its retirement.
        if(enemyPresentation && std::any_of(enemyPresentation->Frame().destructionBursts.begin(),
                enemyPresentation->Frame().destructionBursts.end(),[&](const auto& burst) {
                    return (burst.twinShield || burst.groundTurret.Active()) && burst.actorId==enemy.actorId;
                })) continue;

        if (enemy.combatState.initialized &&
            enemy.combatState.phase == EnemyCombatPhase::Retired) {
            continue;
        }
        if (!IsCourseMeshRenderEligible(
                CourseMeshRenderKind::Enemy,
                enemy.desc.meshId)) {
            continue;
        }

        const RailPathSample sample =
            railPath.Evaluate(enemy.desc.spawnDistance + enemy.desc.distanceOffset);
        const uint32_t modelIndex =
            ResolveModelIndex(models, RailTurret::IsMesh(enemy.desc.meshId)
                ? RailTurret::ForMesh(enemy.desc.meshId).headMesh : enemy.desc.meshId, RailTurret::IsMesh(enemy.desc.meshId) ? "combat_turret" : "ball");
        const CourseMeshModelBinding& model = models[modelIndex];
        if (!IsCourseMeshRenderEligible(
                CourseMeshRenderKind::Enemy,
                model.name)) {
            continue;
        }

        CourseMeshRenderItem* item = AllocateItem();
        if (item == nullptr) {
            break;
        }

        item->kind = CourseMeshRenderKind::Enemy;
        item->name = enemy.desc.role;
        item->meshId = enemy.desc.meshId;
        item->sourceActorId = enemy.actorId;
        item->modelIndex = modelIndex;
        item->sortDistance = sample.distance;
        item->visible = model.loaded && item->transformData != nullptr;
        if (!item->visible) {
            continue;
        }

        const EnemyCombatActorPresentation* presentation =
            enemyPresentation != nullptr
            ? enemyPresentation->FindActor(enemy.actorId)
            : nullptr;
        const EnemyEncounterActorReadability* readability =
            enemyReadability != nullptr
            ? enemyReadability->FindActor(enemy.actorId)
            : nullptr;
        if (presentation != nullptr && !presentation->visible) {
            item->visible = false;
            continue;
        }

        Vector3 center = ResolveRailLocal(
            railPath,
            enemy.desc.spawnDistance,
            enemy.desc.distanceOffset,
            enemy.desc.lateralOffset,
            enemy.desc.verticalOffset);
        if(enemy.desc.actorAssetId=="short_mothership_core") {
            *item->materialData={};item->materialData->color={0.34f,0.37f,0.39f,1};
            item->materialData->enableLighting=true;item->materialData->specularMode=3;
            item->materialData->uvTransform=MakeIdentity4x4();item->useMaterialOverride=true;
            const float r=enemy.desc.radius;
            WriteItemTransform(*item,model.rootLocal,{r,r,r},RotationFromRailTangent(sample.tangent),center,viewProjection);
            continue;
        }
        // RailPath clamps before its first control point. The entrance drone
        // genuinely starts behind the cart/camera, beyond that authored start.
        if(enemy.startupFlightControlled)
            center=Add(center,Scale(sample.tangent,(std::min)(0.0f,
                enemy.desc.spawnDistance+enemy.desc.distanceOffset)));
        if (presentation != nullptr) {
            center = Add(center, Scale(sample.tangent, presentation->forwardOffset));
            center = Add(center, Scale(sample.right, presentation->lateralOffset));
            center = Add(center, Scale(sample.up, presentation->verticalOffset));
            if(!presentation->turret && enemy.desc.meshId!="twin_shield_hull")
                center=Add(center,presentation->primaryImpactOffset);
        }
        const float presentationScale = (enemy.desc.meshId=="siege_turret" || RailTurret::ForMesh(enemy.desc.meshId).movingMesh) ? 1.0f : enemy.combatState.initialized
            ? (std::max)(0.0f, enemy.combatState.presentationScale)
            : 1.0f;
        float bridgeScale = presentation != nullptr
            ? (std::max)(0.0f, presentation->scaleMultiplier)
            : 1.0f;
        if (readability != nullptr && !RailTurret::IsMesh(enemy.desc.meshId)) {
            bridgeScale *= (std::max)(
                1.0f, readability->presentationScale);
        }
        const float modelScale = enemy.desc.meshId == "twin_shield_hull"
            ? kTwinShieldGameplayModelScale : 1.0f;
        const float baseScale = (std::max)(0.01f,
            enemy.desc.radius * presentationScale * bridgeScale * modelScale);
        Vector3 rotation = Add(
            RotationFromRailTangent(sample.tangent),
            enemy.desc.localRotation);
        if (presentation != nullptr) {
            rotation = Add(rotation, presentation->rotationOffset);
            if (presentation->turret) rotation = Add(presentation->turretWorldRotation,presentation->rotationOffset);
            else if(enemy.desc.meshId!="twin_shield_hull")
                rotation=Add(rotation,presentation->primaryImpactRotation);
        }
        if (enemy.desc.meshId == "twin_shield_hull" && model.name == "twin_shield_hull") {
            rotation=ResolveTwinShieldDroneFacing(sample.tangent,center,cameraPosition,
                enemy.desc.localRotation,presentation?presentation->rotationOffset:Vector3{},
                enemy.startupFlightControlled);
            const Vector3 scale{baseScale*(std::max)(0.01f,enemy.desc.localScale.x),
                baseScale*(std::max)(0.01f,enemy.desc.localScale.y),
                baseScale*(std::max)(0.01f,enemy.desc.localScale.z)};
            const float alpha = (presentation ? presentation->materialColor.w :
                (enemy.combatState.initialized ? enemy.combatState.presentationAlpha : 1.0f)) *
                (readability ? readability->presentationAlpha : 1.0f);
            TwinShieldDronePose pose;
            pose.visible = true; pose.position = center; pose.rotation = rotation; pose.scale = scale;
            if(enemy.startupFlightControlled) {
                pose=BuildRailReadyDronePose(railPath,enemy.desc.spawnDistance+enemy.desc.distanceOffset,
                    enemy.desc.lateralOffset,enemy.desc.verticalOffset,enemy.desc.localRotation,enemy.desc.radius);
            }
            if(!enemy.startupFlightControlled && enemy.weaponMount.ready && enemy.combatState.phase!=EnemyCombatPhase::Dying &&
                enemy.combatState.phase!=EnemyCombatPhase::Retired) {
                // The launch boundary owns this frame's rigid transform.
                // Charge colour/shields remain procedural; banking and barrel
                // anchors are exactly those used by the real projectile.
                pose=enemy.weaponMount.dronePose;
            }
            if(presentation) {
                pose.position=Add(pose.position,presentation->primaryImpactOffset);
                pose.rotation=Add(pose.rotation,presentation->primaryImpactRotation);
            }
            pose.alpha = alpha;
            pose.charge = presentation ? presentation->weaponCharge : 0.0f;
            pose.flash = presentation ? presentation->flashStrength : 0.0f;
            pose.death = presentation && presentation->animation == EnemyCombatAnimationState::Death
                ? presentation->animationNormalizedTime : 0.0f;
            if(enemy.desc.actorAssetId=="rear_veil") {
                if(veilAttack && veilAttack->bridgePose.visible && enemy.desc.hitPoints>0) {
                    const float alpha=pose.alpha;
                    pose=veilAttack->bridgePose;
                    pose.alpha=alpha;
                }
                if(!enemy.weaponMount.ready) {
                    const float scale=kElitePursuerModelScale*presentationScale;
                    pose.scale={scale,scale,scale};
                }
                const auto* armor=enemy.rearArmor.enabled && enemy.desc.hitPoints>0 && enemy.weaponMount.ready?
                    &enemy.rearArmor:nullptr;
                if(WriteVeil(*item,pose,veilAttack?*veilAttack:RailVeilAttackFrame{},
                    enemy.veilSensorExposed,enemy.veilSensorHitPoints<=0,armor,models,viewProjection))continue;
            }
            if(enemy.desc.actorAssetId=="rear_fang") {
                if(!enemy.weaponMount.ready) {
                    const float scale=kElitePursuerModelScale*presentationScale;pose.scale={scale,scale,scale};
                }
                const auto* armor=enemy.rearArmor.enabled && enemy.desc.hitPoints>0 && enemy.weaponMount.ready?
                    &enemy.rearArmor:nullptr;
                if(WriteFang(*item,pose,eliteFire?eliteFire->FangRecoil(enemy.actorId):0,
                    enemy.fangCannonExposed,enemy.fangCannonHitPoints<=0,armor,models,viewProjection))continue;
            }
            if(IsRailElitePursuer(enemy.desc.actorAssetId)) {
                const bool fang=enemy.desc.actorAssetId=="rear_fang";
                const bool weakPointExposed=fang
                    ? enemy.fangCannonExposed && enemy.fangCannonHitPoints>0
                    : enemy.veilSensorExposed && enemy.veilSensorHitPoints>0;
                std::string eliteMesh=enemy.desc.actorAssetId;
                if(fang) {
                    eliteMesh=enemy.fangCannonHitPoints<=0?"fang_fire_hull_damaged":
                        enemy.fangCannonExposed?"fang_fire_hull_exposed":"fang_fire_hull";
                } else {
                    if(enemy.veilSensorHitPoints<=0) eliteMesh="rear_veil_damaged";
                    else if(enemy.veilSensorExposed) eliteMesh="rear_veil_exposed";
                }
                const uint32_t eliteIndex=ResolveModelIndex(models,eliteMesh,nullptr);
                if(models[eliteIndex].loaded && models[eliteIndex].name==eliteMesh) {
                    item->modelIndex=eliteIndex;
                    item->meshId=models[eliteIndex].name;
                    if(!enemy.weaponMount.ready) {
                        const float scale=kElitePursuerModelScale*presentationScale;
                        pose.scale={scale,scale,scale};
                    }
                    WriteItemTransform(*item,models[eliteIndex].rootLocal,pose.scale,pose.rotation,pose.position,viewProjection);
                    if(item->materialData) {
                        *item->materialData={};
                        item->materialData->color={1,1,1,alpha};
                        item->materialData->uvTransform=MakeIdentity4x4();
                        item->materialData->enableLighting=true;
                        item->materialData->specularMode=weakPointExposed?17:2;
                        if(weakPointExposed)
                            item->materialData->padding2[0]=.94f+.06f*std::sin(enemy.age*12.0f);
                        item->materialData->shininess=12;
                        item->materialData->environmentCoefficient=.04f;
                        item->useMaterialOverride=true;
                    }
                    if(fang) {
                        const std::string barrels=enemy.fangCannonHitPoints<=0?"fang_fire_barrels_damaged":
                            enemy.fangCannonExposed?"fang_fire_barrels_exposed":"fang_fire_barrels";
                        const auto barrelIndex=ResolveModelIndex(models,barrels,nullptr);
                        if(models[barrelIndex].loaded && models[barrelIndex].name==barrels) {
                            const auto parent=BuildTwinShieldDroneWorldMatrix(pose);
                            const float recoil=eliteFire?eliteFire->FangRecoil(enemy.actorId):0;
                            auto hullWorld=Multiply(MakeAffineMatrix(Vector3{1,1,1},Vector3{recoil*.06f,0,0},Vector3{0,0,recoil*.10f}),parent);
                            item->transformData->World=Multiply(models[eliteIndex].rootLocal,hullWorld);
                            item->transformData->WVP=Multiply(item->transformData->World,viewProjection);
                            item->transformData->WorldInverseTranspose=Transpose(Inverse(item->transformData->World));
                            auto* barrel=AllocateItem();
                            if(barrel && barrel->transformData && barrel->materialData) {
                                barrel->visible=true;barrel->kind=CourseMeshRenderKind::Enemy;barrel->sourceActorId=enemy.actorId;
                                barrel->name=barrel->meshId=barrels;barrel->modelIndex=barrelIndex;barrel->sortDistance=item->sortDistance;
                                *barrel->materialData=*item->materialData;barrel->useMaterialOverride=true;
                                const auto local=MakeAffineMatrix(Vector3{1,1,1},Vector3{},Vector3{0,0,recoil});
                                barrel->transformData->World=Multiply(models[barrelIndex].rootLocal,Multiply(local,parent));
                                barrel->transformData->WVP=Multiply(barrel->transformData->World,viewProjection);
                                barrel->transformData->WorldInverseTranspose=Transpose(Inverse(barrel->transformData->World));
                            }
                        }
                    }
                    if(enemy.rearArmor.enabled && enemy.desc.hitPoints>0 && enemy.weaponMount.ready) {
                        const auto panelIndex=ResolveModelIndex(models,"rear_boost_panel",nullptr);
                        if(models[panelIndex].loaded && models[panelIndex].name=="rear_boost_panel") {
                            for(size_t index=0;index<kRearPanelAnchors.size();++index) {
                                const bool broken=!enemy.rearArmor.HasPanel(index);
                                auto* panel=AllocateItem();
                                if(!panel) break;
                                panel->kind=CourseMeshRenderKind::Enemy;
                                panel->name="rear_boost_panel";
                                panel->meshId="rear_boost_panel";
                                panel->sourceActorId=enemy.actorId;
                                panel->sortDistance=item->sortDistance;
                                panel->modelIndex=panelIndex;
                                panel->visible=panel->transformData && panel->materialData;
                                if(!panel->visible) continue;
                                const auto world=Multiply(models[panelIndex].rootLocal,
                                    Multiply(RearPanelLocal(index,broken),BuildTwinShieldDroneWorldMatrix(pose)));
                                panel->transformData->World=world;
                                panel->transformData->WVP=Multiply(world,viewProjection);
                                auto inverseSource=world;
                                panel->transformData->WorldInverseTranspose=Transpose(Inverse(inverseSource));
                                WriteRearPanelMaterial(*panel, alpha, index, false, broken);
                            }
                        }
                    }
                    continue;
                }
            }
            WriteTwinShieldDrone(*item,pose,models,viewProjection,enemy.desc.energyShield.enabled);
            continue;
        }
        if (item->materialData != nullptr) {
            const float alpha = enemy.combatState.initialized
                ? enemy.combatState.presentationAlpha
                : 1.0f;
            // A pooled item may have been an unlit charging muzzle/outline in
            // the previous frame, especially as a guard departs.
            item->materialData->enableLighting = true;
            item->materialData->specularMode = presentation != nullptr &&
                presentation->flashStrength > 0.25f ? 4 : 3;
            Vector4 materialColor = presentation != nullptr
                ? presentation->materialColor
                : Vector4{1.0f, 1.0f, 1.0f, alpha};
            if (readability != nullptr) {
                const float boost = (std::max)(
                    1.0f, readability->colorBoost);
                materialColor.x = (std::clamp)(
                    materialColor.x * boost, 0.0f, 1.0f);
                materialColor.y = (std::clamp)(
                    materialColor.y * boost, 0.0f, 1.0f);
                materialColor.z = (std::clamp)(
                    materialColor.z * boost, 0.0f, 1.0f);
                materialColor.w = (std::clamp)(
                    materialColor.w * readability->presentationAlpha,
                    0.0f, 1.0f);
            }
            item->materialData->color = materialColor;
            item->materialData->shininess = presentation != nullptr &&
                presentation->flashStrength > 0.01f
                ? 18.0f
                : 5.0f + (presentation != nullptr
                    ? presentation->emissiveStrength * 4.0f
                    : 0.0f);
            item->materialData->environmentCoefficient = presentation != nullptr
                ? (std::clamp)(
                    0.16f + presentation->emissiveStrength * 0.055f,
                    0.16f,
                    0.58f)
                : 0.16f;
            if (RailTurret::IsMesh(enemy.desc.meshId)) {
                item->materialData->environmentCoefficient = 0.025f;
                // Enemy rim/fill mode also keeps painted armour matte.
            }
            if(enemy.desc.meshId=="siege_turret") {
                *item->materialData=WithCourseSurfaceProfile(WithCaveSurfaceLighting(*item->materialData,
                    {.74f,.78f,.82f},.67f,.16f,.87f),CourseSurfaceProfile::Turret);
            }
            if(IsCarrierBatterySurfaceMesh(model.name))
                *item->materialData=BuildCarrierBatterySurfaceMaterial(model.name,materialColor,
                    presentation?presentation->flashStrength:0,
                    presentation && presentation->batteryThermal.phase==RailBatteryThermalPhase::Counter?1.0f:0.0f);
            item->useMaterialOverride = true;
        }
        const Vector3 bodyScale = presentation != nullptr
            ? presentation->bodyScale
            : Vector3{1.0f, 1.0f, 1.0f};
        const bool dedicatedAssault =
            enemy.desc.meshId == "combat_assault_hull";
        const bool dedicatedSniper =
            enemy.desc.meshId == "combat_sniper_hull";
        const bool dedicatedInterceptor =
            enemy.desc.meshId == "combat_interceptor_hull";
        const bool dedicatedCombatHull = dedicatedAssault ||
            dedicatedSniper || dedicatedInterceptor;
        const float dedicatedHeightScale = dedicatedAssault
            ? 1.45f
            : (dedicatedSniper ? 1.16f : 1.0f);
        WriteItemTransform(
            *item,
            model.rootLocal,
            {
                baseScale * (std::max)(0.01f, enemy.desc.localScale.x) *
                    (std::max)(0.01f, bodyScale.x),
                baseScale * (std::max)(0.01f, enemy.desc.localScale.y) *
                    (std::max)(0.01f, bodyScale.y) * dedicatedHeightScale,
                baseScale * (std::max)(0.01f, enemy.desc.localScale.z) *
                    (std::max)(0.01f, bodyScale.z),
            },
            rotation,
            center,
            viewProjection);

        if (presentation == nullptr ||
            !presentation->commercialSilhouette ||
            !presentation->visible) {
            continue;
        }

        // A slightly enlarged copy of the bespoke hull sits just behind the
        // actor. Only its thin navy edge remains visible, separating the
        // silhouette from a bright cave exit without showing an artificial
        // screen-space plate.
        // An expanded closed mesh is unsafe for an articulated turret: its
        // barrel/armour surfaces can protrude through the real aiming head.
        // Turrets already use the lit rim/fill material for separation.
        if (!RailTurret::IsMesh(enemy.desc.meshId) &&
            presentation->contrastBackdropStrength > 0.01f) {
            const Vector3 fromCamera{center.x - cameraPosition.x,
                center.y - cameraPosition.y, center.z - cameraPosition.z};
            const float cameraDistance = std::sqrt(fromCamera.x * fromCamera.x +
                fromCamera.y * fromCamera.y + fromCamera.z * fromCamera.z);
            const Vector3 outlineDirection = cameraDistance > 0.001f
                ? Scale(fromCamera, 1.0f / cameraDistance) : sample.tangent;
            CourseMeshRenderItem* backdrop = AllocateItem();
            if (backdrop != nullptr) {
                backdrop->kind = CourseMeshRenderKind::Enemy;
                backdrop->name = enemy.desc.role + "/contrast-outline";
                backdrop->meshId = model.name;
                backdrop->sourceActorId = enemy.actorId;
                backdrop->modelIndex = modelIndex;
                backdrop->sortDistance = sample.distance + baseScale * 0.10f;
                backdrop->visible = model.loaded &&
                    backdrop->transformData != nullptr;
                if (backdrop->visible) {
                    if (backdrop->materialData != nullptr) {
                        const float strength = (std::clamp)(
                            presentation->contrastBackdropStrength,
                            0.0f,
                            1.0f);
                        backdrop->materialData->color = {
                            0.008f,
                            0.018f,
                            0.042f,
                            (std::clamp)(0.70f + strength * 0.22f,
                                         0.70f,
                                         0.92f) * presentation->materialColor.w};
                        backdrop->materialData->enableLighting = false;
                        backdrop->materialData->shininess = 1.0f;
                        backdrop->materialData->environmentCoefficient = 0.02f;
                        backdrop->materialData->specularMode = 0;
                        backdrop->useMaterialOverride = true;
                    }
                    WriteItemTransform(
                        *backdrop,
                        model.rootLocal,
                        {
                            baseScale *
                                (std::max)(0.01f, enemy.desc.localScale.x) *
                                (std::max)(0.01f, bodyScale.x) * 1.06f,
                            baseScale *
                                (std::max)(0.01f, enemy.desc.localScale.y) *
                                (std::max)(0.01f, bodyScale.y) *
                                dedicatedHeightScale * 1.06f,
                            baseScale *
                                (std::max)(0.01f, enemy.desc.localScale.z) *
                                (std::max)(0.01f, bodyScale.z) * 1.04f,
                        },
                        rotation,
                        Add(center, Scale(outlineDirection, baseScale * 0.14f)),
                        viewProjection);
                }
            }
        }

        if (RailTurret::IsMesh(enemy.desc.meshId)) {
            // Keep the pedestal planted while the separate head aims in yaw/pitch.
            const auto geometry=RailTurret::ForMesh(enemy.desc.meshId);
            const bool siege=enemy.desc.meshId=="siege_turret";
            const bool singleSiege=siege && RailGroundBattery::IsPredictive(enemy.desc.actorAssetId) && enemy.desc.bulletCount==1;
            const CourseBulletActor* siegeRound=nullptr;
            float shotAge=RailTurret::ShotAge(enemy.turretMotion,enemy.age);
            if(singleSiege) {
                const auto& rounds=runtime.Bullets();
                const auto shot=std::find_if(rounds.rbegin(),rounds.rend(),[&](const auto& round) {
                    return round.ownerActorId==enemy.actorId && round.active && !round.hitConsumed && round.age<.49f;
                });
                if(shot!=rounds.rend())siegeRound=&*shot;
                if(siegeRound)shotAge=siegeRound->age;
            }
            const float recoil=singleSiege?RailTurret::SiegeVisualRecoil(shotAge):RailTurret::RecoilAtAge(enemy.desc.meshId,shotAge);
            constexpr float siegeFlashSeconds=.094f;
            const bool siegeFlash=siegeRound && siegeRound->age<siegeFlashSeconds;
            const float flashStrength=siegeFlash?1.0f-TwinShieldDestructionSmooth(siegeRound->age,.016f,.094f):0;
            const bool siegeWarning=singleSiege && presentation && presentation->turretMuzzleActive &&
                enemy.behaviorState.attackIntentActive && enemy.attackState.tokenReserved && enemy.behaviorState.telegraphPresented;
            int siegeBarrel=singleSiege?ResolvePredictiveTurretBarrel(enemy):0;
            if(siegeRound) {
                // Recovery may already be tracking the player again. Keep the
                // kick on the tube that owned this round's launch.
                const float launchLateral=siegeRound->lateralOffset-siegeRound->lateralSpeed*siegeRound->age;
                const float launchDistance=siegeRound->spawnDistance+siegeRound->distanceOffset-siegeRound->forwardSpeed*siegeRound->age;
                const float across=(launchDistance-enemy.desc.spawnDistance-enemy.desc.distanceOffset)*siegeRound->lateralSpeed
                    -(launchLateral-enemy.desc.lateralOffset)*siegeRound->forwardSpeed;
                siegeBarrel=across>0?1:0;
            }
            if((siegeWarning && enemy.targetingState.solutionLocked) || siegeFlash) {
                // The visual head finishes tracking at the committed solution.
                // The targeting and projectile simulation retain their own state.
                const float settle=siegeFlash?1.0f-TwinShieldDestructionSmooth(siegeRound->age,.024f,siegeFlashSeconds):
                    TwinShieldDestructionSmooth(presentation->weaponCharge,.18f,.68f);
                const auto tracked=enemy.targetingState.turretAimDirection;
                const auto locked=siegeFlash?Vector3{
                    siegeRound->lockedTargetLateralOffset-enemy.desc.lateralOffset,
                    siegeRound->lockedTargetVerticalOffset-enemy.desc.verticalOffset-geometry.muzzleHeight*enemy.desc.radius,
                    siegeRound->lockedTargetDistance-enemy.desc.spawnDistance-enemy.desc.distanceOffset}:ResolveTurretAimDirection(enemy);
                const float lockedLength=std::hypot(locked.x,locked.y,locked.z);
                const auto aim=Add(Scale(tracked,1-settle),Scale(locked,settle/(std::max)(.001f,lockedLength)));
                const auto forward=Add(Add(Scale(sample.right,aim.x),Scale(sample.up,aim.y)),Scale(sample.tangent,aim.z));
                rotation={std::atan2(forward.y,std::hypot(forward.x,forward.z)),
                    std::atan2(-forward.x,-forward.z),0};
                WriteItemTransform(*item,model.rootLocal,{baseScale,baseScale,baseScale},rotation,center,viewProjection);
            }
            const auto impactRotation=presentation?presentation->primaryImpactRotation:Vector3{};
            if(!siegeFlash && (impactRotation.x!=0 || impactRotation.y!=0 || impactRotation.z!=0)) {
                rotation=Add(rotation,impactRotation);
                WriteItemTransform(*item,model.rootLocal,{baseScale,baseScale,baseScale},rotation,center,viewProjection);
            }
            const auto head=MakeAffineMatrix({baseScale,baseScale,baseScale},rotation,center);
            const float rotorAngle=enemy.desc.meshId=="gatling_turret"?RailTurret::RotorAngle(enemy.turretMotion,enemy.age):0;
            const auto local=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,rotorAngle},
                Vector3{0,siege?0.0f:geometry.muzzleHeight,recoil});
            auto movingWorld=Multiply(local,head);
            if(siegeFlash || siegeWarning) {
                const auto rim=RailTurret::MuzzleLocalAtPhase(enemy.desc.meshId,siegeBarrel,0);
                const auto restMouth=TransformTurretPoint(rim,head);
                const auto mouthRail=siegeFlash?Vector3{
                    siegeRound->lateralOffset-siegeRound->lateralSpeed*siegeRound->age,
                    siegeRound->verticalOffset-siegeRound->verticalSpeed*siegeRound->age,
                    siegeRound->spawnDistance+siegeRound->distanceOffset-siegeRound->forwardSpeed*siegeRound->age}:
                    ResolveTurretMuzzleRailPosition(enemy,siegeBarrel==0?-1.0f:1.0f);
                const auto launch=ResolveEnemyProjectileWorldPosition(railPath,mouthRail);
                // The committed round anchors the rim at ignition. The barrel
                // still recoils along its local axis as the alignment fades.
                const float blend=siegeFlash?1.0f-TwinShieldDestructionSmooth(siegeRound->age,0.0f,siegeFlashSeconds):presentation->weaponCharge;
                movingWorld.m[3][0]+=(launch.x-restMouth.x)*blend;
                movingWorld.m[3][1]+=(launch.y-restMouth.y)*blend;
                movingWorld.m[3][2]+=(launch.z-restMouth.z)*blend;
            }
            const uint32_t baseIndex = ResolveModelIndex(models,geometry.baseMesh,nullptr);
            if (RailMothership::PartIndex(enemy.desc.actorAssetId)<0 &&
                model.name == geometry.headMesh && models[baseIndex].name == geometry.baseMesh) {
                if (CourseMeshRenderItem* base = AllocateItem()) {
                    base->kind = CourseMeshRenderKind::Enemy;
                    base->sourceActorId = enemy.actorId;
                    base->meshId = geometry.baseMesh;
                    base->modelIndex = baseIndex;
                    base->visible = models[baseIndex].loaded && base->transformData != nullptr;
                    if (base->materialData && item->materialData) {
                        *base->materialData = *item->materialData;
                        if(IsCarrierBatterySurfaceMesh(geometry.baseMesh))
                            *base->materialData=BuildCarrierBatterySurfaceMaterial(geometry.baseMesh,item->materialData->color,
                                item->materialData->padding2[0],item->materialData->padding2[1]);
                        base->useMaterialOverride = true;
                    }
                    auto baseRotation=RotationFromRailTangent(sample.tangent);
                    if(siege)baseRotation.y+=RailTurret::SiegePedestalKick(shotAge);
                    WriteItemTransform(*base,models[baseIndex].rootLocal,
                        {baseScale,baseScale,baseScale},baseRotation,center,viewProjection);
                }
            }
            if(geometry.movingMesh) {
                constexpr auto singleMesh="siege_turret_single_barrel";
                const bool split=singleSiege && models[ResolveModelIndex(models,singleMesh,nullptr)].name==singleMesh;
                const auto mesh=split?singleMesh:geometry.movingMesh;
                const uint32_t rotorIndex=ResolveModelIndex(models,mesh,nullptr);
                if(models[rotorIndex].name==mesh)for(int barrel=0;barrel<(split?2:1);++barrel)if(auto* rotor=AllocateItem()) {
                    rotor->kind=CourseMeshRenderKind::Enemy;rotor->sourceActorId=enemy.actorId;
                    rotor->meshId=mesh;rotor->modelIndex=rotorIndex;
                    rotor->visible=models[rotorIndex].loaded && rotor->transformData;
                    if(rotor->materialData && item->materialData) {
                        *rotor->materialData=siege?*item->materialData:
                            BuildCarrierBatterySurfaceMaterial(geometry.movingMesh,item->materialData->color,
                                item->materialData->padding2[0],item->materialData->padding2[1]);
                        rotor->useMaterialOverride=true;
                    }
                    auto parent=movingWorld;
                    if(split) {
                        const auto offset=MakeAffineMatrix(Vector3{1,1,1},Vector3{},
                            Vector3{(barrel==0?-1:1)*geometry.muzzleSide,0,0});
                        parent=Multiply(offset,barrel==siegeBarrel?movingWorld:head);
                    }
                    auto world=Multiply(models[rotorIndex].rootLocal,parent);
                    if(rotor->transformData) {
                        rotor->transformData->World=world;
                        rotor->transformData->WVP=Multiply(world,viewProjection);
                        rotor->transformData->WorldInverseTranspose=Transpose(Inverse(world));
                    }
                }
            }
            if(enemy.desc.meshId=="carrier_heavy_turret" && presentation) {
                const auto drawPart=[&](const char* mesh,const Matrix4x4& parent) {
                    const uint32_t index=ResolveModelIndex(models,mesh,nullptr);
                    if(models[index].name!=mesh || !models[index].loaded)return;
                    auto* part=AllocateItem();
                    if(!part)return;
                    part->kind=CourseMeshRenderKind::Enemy;part->sourceActorId=enemy.actorId;
                    part->meshId=mesh;part->modelIndex=index;
                    part->visible=part->transformData!=nullptr;
                    if(part->materialData && item->materialData){
                        *part->materialData=BuildCarrierBatterySurfaceMaterial(mesh,item->materialData->color,
                            item->materialData->padding2[0],item->materialData->padding2[1]);part->useMaterialOverride=true;
                    }
                    if(part->transformData) {
                        auto world=Multiply(models[index].rootLocal,parent);
                        part->transformData->World=world;part->transformData->WVP=Multiply(world,viewProjection);
                        part->transformData->WorldInverseTranspose=Transpose(Inverse(world));
                    }
                };
                const float opening=HeavyMuzzleArmorOpen(presentation->batteryThermal,shotAge);
                DrawHeavyTurretArmor(head,movingWorld,opening,presentation->batteryThermal.ventOpen,drawPart);
            }
            if (presentation && (singleSiege?(siegeFlash || siegeWarning):presentation->turretMuzzleActive)) {
                const uint32_t glowIndex = ResolveModelIndex(models,"ball",nullptr);
                if (models[glowIndex].name == "ball") {
                    const int barrels=singleSiege?1:enemy.desc.meshId=="gatling_turret"?6:geometry.muzzleSide==0?1:2;
                    for (int barrel=0;barrel<barrels;++barrel) {
                        CourseMeshRenderItem* glow = AllocateItem();
                        if (!glow) break;
                        glow->kind = CourseMeshRenderKind::Enemy;
                        glow->sourceActorId = enemy.actorId;
                        glow->meshId = siegeFlash?"siege_muzzle_flash":"turret_muzzle_glow";
                        glow->modelIndex = glowIndex;
                        glow->visible = models[glowIndex].loaded && glow->transformData != nullptr;
                        const Vector3 p=RailTurret::MuzzleLocalAtPhase(enemy.desc.meshId,singleSiege?siegeBarrel:barrel,rotorAngle,singleSiege?0:recoil);
                        const auto muzzle=TransformTurretPoint(p,singleSiege?movingWorld:head);
                        if (glow->materialData) {
                            *glow->materialData = {};
                            glow->materialData->color = siegeFlash?Vector4{5.2f*flashStrength,4.8f*flashStrength,4.0f*flashStrength,flashStrength}:presentation->coreColor;
                            glow->materialData->enableLighting = false;
                            glow->materialData->specularMode = 5;
                            glow->materialData->uvTransform = MakeIdentity4x4();
                            glow->useMaterialOverride = true;
                        }
                        const float radius = baseScale*(siegeFlash?0.05f+0.29f*flashStrength:0.07f+0.10f*presentation->weaponCharge);
                        WriteItemTransform(*glow,models[glowIndex].rootLocal,{radius,radius,radius},{},muzzle,viewProjection);
                    }
                }
            }
            continue;
        }

        const float spread = baseScale * (std::max)(
            0.45f, presentation->silhouetteSpread);
        const Vector4 bodyColor = presentation->materialColor;
        const Vector4 podColor{
            bodyColor.x * 0.72f,
            bodyColor.y * 0.76f,
            bodyColor.z * 0.88f,
            bodyColor.w};
        auto addDronePart = [&] (
            const char* suffix,
            const Vector3& position,
            const Vector3& scale,
            const Vector3& rotationOffset,
            const Vector4& color,
            float shininess,
            float environmentCoefficient,
            uint32_t partModelIndex) {
            const CourseMeshModelBinding& partModel = models[partModelIndex];
            CourseMeshRenderItem* part = AllocateItem();
            if (part == nullptr) return;
            part->kind = CourseMeshRenderKind::Enemy;
            part->name = enemy.desc.role + suffix;
            part->meshId = partModel.name;
            part->sourceActorId = enemy.actorId;
            part->modelIndex = partModelIndex;
            part->sortDistance = sample.distance;
            part->visible = partModel.loaded && part->transformData != nullptr;
            if (!part->visible) return;
            if (part->materialData != nullptr) {
                part->materialData->color = color;
                // Reset this on every pooled part, including non-pass actors.
                part->materialData->enableLighting =
                    !(enemy.behaviorDefinition.choreographedAttackPass &&
                      std::string_view{suffix} == "/weapon-core");
                part->materialData->shininess = shininess;
                part->materialData->environmentCoefficient =
                    environmentCoefficient;
                part->materialData->specularMode = 1;
                if (enemy.behaviorDefinition.choreographedAttackPass &&
                    std::string_view{suffix} == "/weapon-core") {
                    // The charge must stay readable even with the legacy ball texture.
                    part->materialData->specularMode = 5;
                }
                part->useMaterialOverride = true;
            }
            WriteItemTransform(
                *part,
                partModel.rootLocal,
                scale,
                Add(rotation, rotationOffset),
                position,
                viewProjection);
        };

        // Expansion archetypes retain the proven animated pod/core language,
        // while their dedicated hulls carry the at-a-glance type silhouette.
        const uint32_t podModelIndex = dedicatedCombatHull
            ? ResolveModelIndex(models, "combat_assault_pod", "ball")
            : modelIndex;
        const uint32_t coreModelIndex = dedicatedCombatHull
            ? ResolveModelIndex(models, "combat_assault_core", "ball")
            : modelIndex;

        const Vector3 podAdvance = Scale(sample.tangent, -baseScale * 0.08f);
        const Vector3 podLift = Scale(sample.up, baseScale * 0.08f);
        const Vector3 leftPodPosition = Add(
            Add(center, Scale(sample.right, -spread)),
            Add(podAdvance, podLift));
        const Vector3 rightPodPosition = Add(
            Add(center, Scale(sample.right, spread)),
            Add(podAdvance, podLift));
        const float podRecoil = presentation->weaponCharge * baseScale * 0.14f;
        const Vector3 podScale = dedicatedAssault
            ? Vector3{
                  baseScale * 0.96f,
                  baseScale * 1.10f,
                  baseScale * (0.78f + presentation->weaponCharge * 0.16f)}
            : dedicatedSniper
            ? Vector3{
                  baseScale * 0.34f,
                  baseScale * 0.62f,
                  baseScale * (1.08f + presentation->weaponCharge * 0.18f)}
            : dedicatedInterceptor
            ? Vector3{
                  baseScale * 1.02f,
                  baseScale * 0.48f,
                  baseScale * (0.68f + presentation->weaponCharge * 0.14f)}
            : Vector3{
                  baseScale * 0.42f,
                  baseScale * 0.30f,
                  baseScale * (0.54f + presentation->weaponCharge * 0.12f)};
        addDronePart(
            "/left-pod",
            Add(leftPodPosition, Scale(sample.tangent, podRecoil)),
            podScale,
            {0.0f, 0.0f, -0.16f - presentation->weaponCharge * 0.12f},
            podColor,
            9.0f + presentation->emissiveStrength * 2.0f,
            0.28f,
            podModelIndex);
        addDronePart(
            "/right-pod",
            Add(rightPodPosition, Scale(sample.tangent, podRecoil)),
            podScale,
            {0.0f, 0.0f, 0.16f + presentation->weaponCharge * 0.12f},
            podColor,
            9.0f + presentation->emissiveStrength * 2.0f,
            0.28f,
            podModelIndex);

        const Vector3 corePosition = Add(
            Add(center, Scale(sample.tangent, -baseScale * 0.78f)),
            Scale(sample.up, baseScale * 0.04f));
        const float corePulse = 1.0f + presentation->weaponCharge * 0.48f;
        addDronePart(
            "/weapon-core",
            corePosition,
            {
                baseScale * 0.34f * corePulse,
                baseScale * 0.34f * corePulse,
                baseScale * 0.24f,
            },
            {},
            presentation->coreColor,
            22.0f + presentation->emissiveStrength * 5.0f,
            (std::clamp)(
                0.42f + presentation->emissiveStrength * 0.045f,
                0.42f,
                0.72f),
            coreModelIndex);
    }
    if(enemyPresentation && !models.empty()) for(const auto& burst:enemyPresentation->Frame().destructionBursts) {
        if(burst.groundTurret.Active()) {
            const auto pose=RailGroundTurretCollapse::Evaluate(burst.groundTurret,burst.age);
            const auto geometry=RailTurret::ForMesh(burst.groundTurret.meshId);
            const auto draw=[&](const char* mesh,const Matrix4x4& transform) {
                if(!mesh)return;
                const auto index=ResolveModelIndex(models,mesh,nullptr);
                if(models[index].name!=mesh || !models[index].loaded)return;
                auto* part=AllocateItem();
                if(!part || !part->transformData || !part->materialData)return;
                part->kind=CourseMeshRenderKind::Enemy;
                part->name="battery_wreck";part->meshId=mesh;part->sourceActorId=burst.actorId;
                part->modelIndex=index;part->collisionMode=CourseTerrainCollisionMode::None;
                part->visible=true;part->useMaterialOverride=true;
                auto& material=*part->materialData;
                material={};material.uvTransform=MakeIdentity4x4();material.enableLighting=true;
                const float soot=RailGroundTurretCollapse::Smooth(burst.age,.30f,1.1f);
                material.color={.35f-.19f*soot,.39f-.23f*soot,.42f-.24f*soot,pose.opacity};
                material.shininess=4.0f;
                material.environmentCoefficient=.025f;
                material.specularMode=3;
                material=WithCourseSurfaceProfile(WithCaveSurfaceLighting(material,
                    {.67f,.73f,.80f},.61f,.12f,.78f),CourseSurfaceProfile::Turret);
                auto world=Multiply(models[index].rootLocal,transform);
                part->transformData->World=world;
                part->transformData->WVP=Multiply(world,viewProjection);
                part->transformData->WorldInverseTranspose=Transpose(Inverse(world));
            };
            draw(geometry.baseMesh,pose.base);
            draw(geometry.headMesh,pose.head);
            if(burst.groundTurret.meshId=="siege_turret") {
                for(float side:{-1.0f,1.0f}) {
                    const auto socket=MakeAffineMatrix(Vector3{1,1,1},Vector3{},Vector3{side*geometry.muzzleSide,0,0});
                    draw(geometry.movingMesh,Multiply(socket,pose.barrel));
                }
            } else draw(geometry.movingMesh,pose.barrel);
            continue;
        }
        if(!burst.twinShield) continue;
        auto pose=burst.frozenPose;
        pose.destructionAge=burst.age;
        if(EvaluateTwinShieldDroneBreakup(pose).opacity<=0.001f) continue;
        const uint32_t index=ResolveModelIndex(models,"twin_shield_hull",nullptr);
        if(models[index].name!="twin_shield_hull" || !models[index].loaded) continue;
        auto* item=AllocateItem();
        if(!item) break;
        item->kind=CourseMeshRenderKind::Enemy; item->name="drone_wreck";
        item->meshId="twin_shield_hull"; item->sourceActorId=burst.actorId;
        item->modelIndex=index; item->collisionMode=CourseTerrainCollisionMode::None;
        item->sortDistance=0; item->visible=item->transformData!=nullptr;
        if(item->visible) WriteTwinShieldDrone(*item,pose,models,viewProjection);
    }

}

void CourseMeshRenderQueue::AddCourseDebrisInstances(
    const CourseAsset& course,
    float currentDistance,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection) {
    std::vector<CourseDebrisRenderInstance> debrisInstances;
    DebrisCompositionSystem::BuildVisibleRockInstances(
        course,
        currentDistance,
        railPath,
        debrisInstances);

    for (const CourseDebrisRenderInstance& debris : debrisInstances) {
        const CourseMeshRenderKind renderKind =
            RenderKindForTerrainLayer(debris.layer);
        if (!IsCourseMeshRenderEligible(renderKind, debris.meshId)) {
            continue;
        }

        const uint32_t modelIndex =
            ResolveModelIndex(models, debris.meshId, "curved_canyon_wall");
        const CourseMeshModelBinding& model = models[modelIndex];
        if (!IsCourseMeshRenderEligible(renderKind, model.name)) {
            continue;
        }

        CourseMeshRenderItem* item = AllocateItem();
        if (item == nullptr) {
            break;
        }

        item->kind = renderKind;
        item->terrainLayer = debris.layer;
        item->collisionMode = debris.collisionMode;
        item->name = debris.id;
        item->meshId = debris.meshId;
        item->sourceActorId = 0;
        item->modelIndex = modelIndex;
        item->sortDistance = debris.sortDistance;
        item->visible = model.loaded && item->transformData != nullptr;
        if (!item->visible) {
            continue;
        }

        if(IsGameplayLandscapeMesh(item->meshId) && item->materialData!=nullptr) {
            *item->materialData=BuildGameplayLandscapePbrMaterial(item->meshId);
            item->useMaterialOverride=true;
        }

        WriteItemTransform(
            *item,
            model.rootLocal,
            debris.scale,
            debris.rotation,
            debris.position,
            viewProjection);
    }
}

CourseMeshRenderItem* CourseMeshRenderQueue::AllocateItem() {
    if (visibleCount_ >= items_.size()) {
        return nullptr;
    }
    CourseMeshRenderItem& item = items_[visibleCount_++];
    item.visible = false;
    return &item;
}

uint32_t CourseMeshRenderQueue::ResolveModelIndex(
    std::span<const CourseMeshModelBinding> models,
    const std::string& meshId,
    const char* fallbackName) const {
    for (uint32_t index = 0; index < models.size(); ++index) {
        if (models[index].loaded && models[index].name == meshId) {
            return index;
        }
    }
    if (fallbackName != nullptr) {
        for (uint32_t index = 0; index < models.size(); ++index) {
            if (models[index].loaded && models[index].name == fallbackName) {
                return index;
            }
        }
    }
    for (uint32_t index = 0; index < models.size(); ++index) {
        if (models[index].loaded) {
            return index;
        }
    }
    return 0;
}

void CourseMeshRenderQueue::WriteItemTransform(
    CourseMeshRenderItem& item,
    const Matrix4x4& rootLocal,
    const Vector3& scale,
    const Vector3& rotate,
    const Vector3& translate,
    const Matrix4x4& viewProjection) {
    if (item.transformData == nullptr) {
        return;
    }

    Matrix4x4 world = Multiply(
        rootLocal,
        MakeAffineMatrix(scale, rotate, translate));
    item.transformData->World = world;
    item.transformData->WVP = Multiply(world, viewProjection);
    item.transformData->WorldInverseTranspose = Transpose(Inverse(world));
}

void CourseMeshRenderQueue::AppendMothership(const RailMothership::Frame& carrier,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewMatrix,const Matrix4x4& projMatrix,const RailPath& railPath,Vector3 cartRail,
    float routeShotAge,Vector3 routeShotTarget) {
    if(!carrier.visible || models.empty()) return;
    Matrix4x4 ship=MakeIdentity4x4();
    ship.m[0][0]=carrier.right.x; ship.m[0][1]=carrier.right.y; ship.m[0][2]=carrier.right.z;
    ship.m[1][0]=carrier.up.x; ship.m[1][1]=carrier.up.y; ship.m[1][2]=carrier.up.z;
    ship.m[2][0]=carrier.forward.x; ship.m[2][1]=carrier.forward.y; ship.m[2][2]=carrier.forward.z;
    ship.m[3][0]=carrier.center.x; ship.m[3][1]=carrier.center.y; ship.m[3][2]=carrier.center.z;
    const auto vp=Multiply(viewMatrix,projMatrix);
    const auto append=[&](const char* mesh,const char* name,const Matrix4x4& local,Vector4 color,bool lit) {
        const uint32_t model=ResolveModelIndex(models,mesh,nullptr);
        if(models[model].name!=mesh || !models[model].loaded) return;
        auto* item=AllocateItem(); if(!item) return;
        item->kind=CourseMeshRenderKind::VistaBackground;
        item->name=name; item->meshId=mesh; item->modelIndex=model;
        item->collisionMode=CourseTerrainCollisionMode::None;
        item->visible=item->transformData!=nullptr && item->materialData!=nullptr;
        if(!item->visible) return;
        *item->materialData={};
        item->materialData->color=color;
        item->materialData->enableLighting=lit;
        item->materialData->uvTransform=MakeIdentity4x4();
        // Matte escort backdrop: broad white specular faces compete with the
        // smaller combatants and their charge lights on the ridge approach.
        item->materialData->specularMode=2;
        item->materialData->shininess=1.0f;
        item->materialData->environmentCoefficient=lit?0.025f:0.0f;
        if(std::string_view(mesh)=="carrier_hull") {
            item->materialData->specularMode=16;
            item->materialData->shininess=.08f;
            item->materialData->environmentCoefficient=.74f;
        }
        if(IsCarrierBatterySurfaceMesh(mesh))
            *item->materialData=BuildCarrierBatterySurfaceMaterial(mesh,color,0,0);
        item->useMaterialOverride=true;
        auto world=Multiply(models[model].rootLocal,Multiply(local,ship));
        item->transformData->World=world;
        item->transformData->WVP=Multiply(world,vp);
        item->transformData->WorldInverseTranspose=Transpose(Inverse(world));
    };
    const auto part=[&](const char* mesh,const char* name,Vector3 center,Vector3 scale,Vector4 color,bool lit) {
        append(mesh,name,MakeAffineMatrix(scale,Vector3{},center),color,lit);
    };
    const float dying=carrier.battleDestruction;
    const float distance=cartRail.z;
    part("carrier_hull","carrier/blockout-hull",{},{1,1,1},
        {0.92f+dying*.08f,.98f-dying*.45f,1.08f-dying*.62f,1},true);
    if(carrier.partsBattle || (!carrier.battleActive && !carrier.battleDefeated)) {
        part("carrier_box","carrier/reactor-armour",{0,-10,-65},{12,9,6},{.10f,.14f,.18f,1},true);
        for(int i=0;i<2;++i) {
            const auto mount=RailMothership::WeaponMount(i);
            if(carrier.partsBattle && carrier.partHealth[i]<=0)continue;
            const bool heavy=i==1;
            const float glow=.54f+carrier.partCharge[i]*1.35f;
            part("carrier_box","carrier/weapon-guide",{mount.x,mount.y-3.0f,mount.z+1.7f},{4.6f,.42f,.48f},
                heavy?Vector4{1.4f*glow,.25f*glow,.62f*glow,1}:Vector4{.20f*glow,.72f*glow,1.25f*glow,1},false);
            // The same weapon meshes take over as damageable actors at the
            // encounter boundary. The hull itself owns the permanent sockets.
            if(carrier.partsBattle)continue;
            using namespace RailRouteHazards;
            const std::string_view mesh=heavy?"carrier_heavy_turret":"gatling_turret";
            const auto geometry=RailTurret::ForMesh(mesh);
            const float scale=RailMothership::PartRadius[i];
            const bool routeShot=heavy && routeShotAge>=0;
            const float shotAge=routeShot?routeShotAge-BoltLaunch:-1;
            const float traverse=heavy?RailMothership::Smooth(distance,1725,1815):RailMothership::GatlingPreviewTraverse(distance);
            const auto rotation=routeShot?HeavyPassRotation(carrier,railPath,cartRail,routeShotTarget,routeShotAge):
                Scale(RailMothership::WeaponCartRotation(carrier,railPath,i,cartRail),traverse);
            const auto head=MakeAffineMatrix(Vector3{scale,scale,scale},rotation,mount);
            const float angle=heavy?0:RailMothership::GatlingPreviewAngle(distance);
            const auto motion=MakeAffineMatrix(Vector3{1,1,1},Vector3{0,0,angle},
                Vector3{0,geometry.muzzleHeight,RailTurret::RecoilAtAge(mesh,shotAge)});
            const auto moving=Multiply(motion,head);
            const Vector4 tint{.88f,.92f,.97f,1};
            append(geometry.headMesh,heavy?"carrier/heavy-head":"carrier/gatling-head",head,tint,true);
            append(geometry.movingMesh,heavy?"carrier/heavy-barrel":"carrier/gatling-rotor",moving,tint,true);
            if(heavy) {
                RailBatteryThermalFrame thermal;
                thermal.valid=thermal.heavy=true;
                if(routeShot && shotAge<0) {
                    thermal.phase=RailBatteryThermalPhase::Charging;
                    thermal.charge=HeavyCharge(distance,routeShotAge);
                }
                const float vent=Ease(shotAge,.10f,.32f)*(1-Ease(shotAge,.55f,1.18f));
                DrawHeavyTurretArmor(head,moving,HeavyMuzzleArmorOpen(thermal,shotAge),vent,
                    [&](const char* meshId,const Matrix4x4& pose) {
                        append(meshId,"carrier/heavy-armor",pose,tint,true);
                    });
            }
        }
    } else if(carrier.battleActive || carrier.battleDefeated) {
        const float pulse=0.8f+carrier.battleCharge*2.6f+dying*2;
        const float r=3.5f+carrier.battleCharge*0.65f+dying*12;
        part("ball","carrier/reactor-glow",{0,-11,-72.5f},{r,r,r*0.38f},
            {2.3f*pulse,0.55f*pulse,0.06f*pulse,1},false);
        for(int side:{-1,1}) {
            part("carrier_box","carrier/reactor-strut",{float(side)*8,-11,-67},{2,14,5},{0.22f,0.25f,0.27f,1},true);
            part("carrier_box","carrier/charge-guide",{float(side)*13,-11,-67.5f},{0.6f,12,0.8f},
                {1.2f*pulse,0.35f*pulse,0.03f*pulse,1},false);
        }
    }
    for(int side:{-1,1}) {
        const float x=float(side)*RailMothership::BayLateral;
        const float glow=carrier.launchGlow;
        const Vector4 amber{1.8f*glow,0.65f*glow,0.12f*glow,1};
        for(int edge:{-1,1})
            part("carrier_box","carrier/bay-guide",{x+float(edge)*9.0f,-9.2f,0},
                {0.7f,0.4f,29},amber,false);
        part("carrier_box","carrier/bay-threshold",{x,-9.2f,-14},
            {18.7f,0.4f,0.7f},amber,false);
        part("carrier_box","carrier/aft-engine",{float(side)*58,0,-48.5f},
            {18,3,0.7f},{0.12f,0.75f,1.3f,1},false);
        part("carrier_box","carrier/identification-stripe",{float(side)*65,-8.6f,16},
            {10,0.4f,3},{0.72f,0.47f,0.16f,1},true);
    }
}

void CourseMeshRenderQueue::AppendTwinShieldDrone(const TwinShieldDronePose& pose,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    if (!pose.visible || pose.alpha <= 0.0f || models.empty()) return;
    const uint32_t index = ResolveModelIndex(models,"twin_shield_hull",nullptr);
    if (models[index].name != "twin_shield_hull" || !models[index].loaded) return;
    CourseMeshRenderItem* hull = AllocateItem();
    if (!hull) return;
    hull->kind = CourseMeshRenderKind::Enemy;
    hull->name = "title_pursuer";
    hull->meshId = "twin_shield_hull";
    hull->sourceActorId = 0;
    hull->sortDistance = 0.0f;
    hull->collisionMode = CourseTerrainCollisionMode::None;
    hull->modelIndex = index;
    hull->visible = hull->transformData != nullptr;
    if (hull->visible) WriteTwinShieldDrone(*hull,pose,models,Multiply(viewMatrix,projMatrix));
}

void CourseMeshRenderQueue::WriteTwinShieldDrone(CourseMeshRenderItem& hull,
    const TwinShieldDronePose& pose, std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection,bool energyShield) {
    const bool wreck=pose.destructionAge>=0;
    const auto breakup=wreck ? EvaluateTwinShieldDroneBreakup(pose) : TwinShieldDroneBreakupFrame{};
    auto attachment = [&](float side) {
        const float intact = 1.0f - 0.45f * (std::clamp)(pose.charge, 0.0f, 1.0f);
        const float detach = TwinShieldDroneDetachAge(pose, side);
        return intact * (wreck ? 1.0f - TwinShieldDestructionSmooth(pose.destructionAge, detach,
            detach + (pose.primaryDestruction?0.06f:0.18f)) : 1.0f);
    };
    const float leftAttachment = attachment(-1.0f), rightAttachment = attachment(1.0f);
    if (hull.materialData) {
        *hull.materialData = {};
        hull.materialData->color = {1,1,1,pose.alpha*(wreck?breakup.opacity:1.0f)};
        hull.materialData->uvTransform = MakeIdentity4x4();
        hull.materialData->enableLighting = true;
        hull.materialData->specularMode = 10; // Normal/roughness-mapped industrial metal.
        hull.materialData->environmentCoefficient = 0.72f;
        hull.materialData->padding2[0] = wreck?breakup.hullFlash:pose.flash;
        hull.useMaterialOverride = true;
    }
    const Matrix4x4 actorWorld = wreck?breakup.bodyWorld:BuildTwinShieldDroneWorldMatrix(pose);
    if(hull.transformData) {
        auto world=Multiply(models[hull.modelIndex].rootLocal,actorWorld);
        hull.transformData->World=world;
        hull.transformData->WVP=Multiply(world,viewProjection);
        hull.transformData->WorldInverseTranspose=Transpose(Inverse(world));
        if (hull.materialData)
            SetTwinShieldDroneContactShading(*hull.materialData,
                hull.transformData->WorldInverseTranspose, 0.0f, leftAttachment, rightAttachment);
    }
    auto addReferencePart = [&](const char* id, const char* suffix,Vector3 offset,float yaw,bool core) {
        const uint32_t index = ResolveModelIndex(models,id,nullptr);
        if (models[index].name != id || !models[index].loaded) return;
        CourseMeshRenderItem* part = AllocateItem();
        if (!part) return;
        part->kind = CourseMeshRenderKind::Enemy;
        part->name = hull.name + suffix;
        part->meshId = id;
        part->sourceActorId = hull.sourceActorId;
        part->modelIndex = index;
        part->sortDistance = hull.sortDistance;
        part->visible = part->transformData != nullptr;
        if (part->materialData && hull.materialData) {
            *part->materialData = *hull.materialData;
            if(std::string_view(id)=="energy_shield_emitter")part->materialData->specularMode=3;
            if (core) {
                part->materialData->enableLighting = false;
                part->materialData->specularMode = 11;
                part->materialData->environmentCoefficient = 0.0f;
                part->materialData->padding2[0] = wreck?breakup.coreGain:1.8f+pose.charge*3.2f;
                if(wreck) part->materialData->color.w*=breakup.coreOpacity;
            }
            part->useMaterialOverride = true;
        }
        if (part->transformData) {
            // Compose in actor space, keeping each shield attached during banking.
            const Matrix4x4 local = MakeAffineMatrix(Vector3{1,1,1},Vector3{0,yaw,0},offset);
            Matrix4x4 world = wreck
                ? Multiply(models[index].rootLocal,core?breakup.bodyWorld:(offset.x<0?breakup.leftWorld:breakup.rightWorld))
                : Multiply(Multiply(models[index].rootLocal,local),actorWorld);
            part->transformData->World = world;
            part->transformData->WVP = Multiply(world,viewProjection);
            part->transformData->WorldInverseTranspose = Transpose(Inverse(world));
            if (!core && part->materialData)
                SetTwinShieldDroneContactShading(*part->materialData,
                    part->transformData->WorldInverseTranspose, offset.x < 0 ? -1.0f : 1.0f,
                    leftAttachment, rightAttachment);
        }
    };
    const float spread = 1.09f + pose.charge*0.14f + pose.death*0.40f;
    const float opening = 0.08f + pose.charge*0.42f + pose.death*0.55f;
    addReferencePart("twin_shield_panel","/left-shield",{-spread,-pose.death*0.30f,0},-opening,false);
    addReferencePart("twin_shield_panel","/right-shield",{spread,-pose.death*0.30f,0},opening,false);
    addReferencePart("twin_shield_core","/sensor-core",{},0,true);
    if(energyShield && !wreck) {
        addReferencePart("energy_shield_emitter","/left-emitter",{-1.34f,0,-.76f},0,false);
        addReferencePart("energy_shield_emitter","/right-emitter",{1.34f,0,-.76f},0,false);
    }
}
