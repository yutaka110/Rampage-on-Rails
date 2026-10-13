#include "CourseMeshRenderQueue.h"

#include "CourseSpawnRuntime.h"
#include "DebrisCompositionSystem.h"
#include "EnemyCombatPresentationBridge.h"
#include "EnemyEncounterReadabilityDirector.h"
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

bool IsTitleLandscapeMesh(std::string_view meshId) {
    return meshId == "title_ground" || meshId == "title_cliff" ||
        meshId == "title_boulder" || meshId == "title_tunnel";
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
    material.specularMode = meshId == "title_ground" ? 9 : (meshId == "title_tunnel" ? 7 : 6);
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
    if (meshId == "title_ground") {
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
    const EnemyEncounterReadabilityDirector* enemyReadability) {
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
        enemyReadability);

    // Collidable obstacles must not lose their draw slots to scenery.
    AddObstacleInstances(runtime, railPath, models, viewProjection);

    if (course != nullptr) {
        for (const CourseTerrainPlacement& placement : course->terrainPlacements) {
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
            if (placement.meshId == "title_stake" && item->materialData != nullptr) {
                Material timber{};
                timber.color={1,1,1,1};
                timber.enableLighting=true;
                timber.uvTransform=MakeIdentity4x4();
                timber.specularMode=2; // Matte timber, using its authored wood albedo.
                *item->materialData=timber;
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
        AddCourseDebrisInstances(
            *course,
            currentDistance,
            railPath,
            models,
            viewProjection);
    }

}

void CourseMeshRenderQueue::AddObstacleInstances(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection) {
    for (const CourseObstacleActor& obstacle : runtime.Obstacles()) {
        if (obstacle.age >= obstacle.desc.lifetime ||
            (obstacle.desc.breakable && obstacle.desc.hitPoints <= 0.0f)) continue;
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

void CourseMeshRenderQueue::AddEnemyInstances(
    const CourseSpawnRuntime& runtime,
    const RailPath& railPath,
    std::span<const CourseMeshModelBinding> models,
    const Matrix4x4& viewProjection,
    const Vector3& cameraPosition,
    const EnemyCombatPresentationBridge* enemyPresentation,
    const EnemyEncounterReadabilityDirector* enemyReadability) {
    for (const CourseEnemyActor& enemy : runtime.Enemies()) {
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
            ResolveModelIndex(models, enemy.desc.meshId == "combat_turret"
                ? "combat_turret_head" : enemy.desc.meshId, enemy.desc.meshId == "combat_turret" ? "combat_turret" : "ball");
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
        if (presentation != nullptr) {
            center = Add(center, Scale(sample.tangent, presentation->forwardOffset));
            center = Add(center, Scale(sample.right, presentation->lateralOffset));
            center = Add(center, Scale(sample.up, presentation->verticalOffset));
        }
        const float presentationScale = enemy.combatState.initialized
            ? (std::max)(0.0f, enemy.combatState.presentationScale)
            : 1.0f;
        float bridgeScale = presentation != nullptr
            ? (std::max)(0.0f, presentation->scaleMultiplier)
            : 1.0f;
        if (readability != nullptr && enemy.desc.meshId != "combat_turret") {
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
        }
        if (enemy.desc.meshId == "twin_shield_hull" && model.name == "twin_shield_hull") {
            // Rigid reference geometry: no legacy sphere/pod scaling or squash.
            // Model forward is -Z after Assimp's right-handed -> LH conversion.
            const Vector3 toCamera{cameraPosition.x-center.x,cameraPosition.y-center.y,cameraPosition.z-center.z};
            const float distance = std::sqrt(toCamera.x*toCamera.x+toCamera.y*toCamera.y+toCamera.z*toCamera.z);
            if (distance > 0.001f) {
                rotation = Add({std::asin((std::clamp)(toCamera.y/distance,-1.0f,1.0f)),
                    std::atan2(-toCamera.x,-toCamera.z),0.0f},enemy.desc.localRotation);
                if (presentation) rotation = Add(rotation,presentation->rotationOffset);
            }
            const Vector3 scale{baseScale*(std::max)(0.01f,enemy.desc.localScale.x),
                baseScale*(std::max)(0.01f,enemy.desc.localScale.y),
                baseScale*(std::max)(0.01f,enemy.desc.localScale.z)};
            const float alpha = (presentation ? presentation->materialColor.w :
                (enemy.combatState.initialized ? enemy.combatState.presentationAlpha : 1.0f)) *
                (readability ? readability->presentationAlpha : 1.0f);
            TwinShieldDronePose pose;
            pose.visible = true; pose.position = center; pose.rotation = rotation; pose.scale = scale;
            if(enemy.weaponMount.ready && enemy.combatState.phase!=EnemyCombatPhase::Dying &&
                enemy.combatState.phase!=EnemyCombatPhase::Retired) {
                // The launch boundary owns this frame's rigid transform.
                // Charge colour/shields remain procedural; banking and barrel
                // anchors are exactly those used by the real projectile.
                pose=enemy.weaponMount.dronePose;
            }
            pose.alpha = alpha;
            pose.charge = presentation ? presentation->weaponCharge : 0.0f;
            pose.flash = presentation ? presentation->flashStrength : 0.0f;
            pose.death = presentation && presentation->animation == EnemyCombatAnimationState::Death
                ? presentation->animationNormalizedTime : 0.0f;
            WriteTwinShieldDrone(*item,pose,models,viewProjection);
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
            if (enemy.desc.meshId == "combat_turret") {
                item->materialData->environmentCoefficient = 0.025f;
                // Enemy rim/fill mode also keeps painted armour matte.
            }
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
        if (presentation->contrastBackdropStrength > 0.01f) {
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

        if (enemy.desc.meshId == "combat_turret") {
            // Keep the pedestal planted while the separate head aims in yaw/pitch.
            const uint32_t baseIndex = ResolveModelIndex(models,"combat_turret_base",nullptr);
            if (model.name == "combat_turret_head" && models[baseIndex].name == "combat_turret_base") {
                if (CourseMeshRenderItem* base = AllocateItem()) {
                    base->kind = CourseMeshRenderKind::Enemy;
                    base->sourceActorId = enemy.actorId;
                    base->meshId = "combat_turret_base";
                    base->modelIndex = baseIndex;
                    base->visible = models[baseIndex].loaded && base->transformData != nullptr;
                    if (base->materialData && item->materialData) {
                        *base->materialData = *item->materialData;
                        base->useMaterialOverride = true;
                    }
                    WriteItemTransform(*base,models[baseIndex].rootLocal,
                        {baseScale,baseScale,baseScale},RotationFromRailTangent(sample.tangent),center,viewProjection);
                }
            }
            if (presentation->turretMuzzleActive) {
                const uint32_t glowIndex = ResolveModelIndex(models,"ball",nullptr);
                if (models[glowIndex].name == "ball") {
                    const Matrix4x4 head = MakeAffineMatrix({baseScale,baseScale,baseScale},rotation,center);
                    for (float side : {-1.0f,1.0f}) {
                        CourseMeshRenderItem* glow = AllocateItem();
                        if (!glow) break;
                        glow->kind = CourseMeshRenderKind::Enemy;
                        glow->sourceActorId = enemy.actorId;
                        glow->meshId = "turret_muzzle_glow";
                        glow->modelIndex = glowIndex;
                        glow->visible = models[glowIndex].loaded && glow->transformData != nullptr;
                        const Vector3 p{side*0.2538f,0.1598f,-0.8272f};
                        const Vector3 muzzle{p.x*head.m[0][0]+p.y*head.m[1][0]+p.z*head.m[2][0]+head.m[3][0],
                            p.x*head.m[0][1]+p.y*head.m[1][1]+p.z*head.m[2][1]+head.m[3][1],
                            p.x*head.m[0][2]+p.y*head.m[1][2]+p.z*head.m[2][2]+head.m[3][2]};
                        if (glow->materialData) {
                            *glow->materialData = {};
                            glow->materialData->color = presentation->coreColor;
                            glow->materialData->enableLighting = false;
                            glow->materialData->specularMode = 5;
                            glow->materialData->uvTransform = MakeIdentity4x4();
                            glow->useMaterialOverride = true;
                        }
                        const float radius = baseScale*(0.09f+0.14f*presentation->weaponCharge);
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
    const Matrix4x4& viewProjection) {
    if (hull.materialData) {
        *hull.materialData = {};
        hull.materialData->color = {1,1,1,pose.alpha};
        hull.materialData->uvTransform = MakeIdentity4x4();
        hull.materialData->enableLighting = true;
        hull.materialData->specularMode = 10; // Normal/roughness-mapped industrial metal.
        hull.materialData->environmentCoefficient = 0.72f;
        hull.materialData->padding2[0] = pose.flash;
        hull.useMaterialOverride = true;
    }
    WriteItemTransform(hull,models[hull.modelIndex].rootLocal,pose.scale,pose.rotation,pose.position,viewProjection);
    const Matrix4x4 actorWorld = MakeAffineMatrix(pose.scale,pose.rotation,pose.position);
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
            if (core) {
                part->materialData->enableLighting = false;
                part->materialData->specularMode = 11;
                part->materialData->environmentCoefficient = 0.0f;
                part->materialData->padding2[0] = 1.8f+pose.charge*3.2f;
            }
            part->useMaterialOverride = true;
        }
        if (part->transformData) {
            // Compose in actor space, keeping each shield attached during banking.
            const Matrix4x4 local = MakeAffineMatrix(Vector3{1,1,1},Vector3{0,yaw,0},offset);
            Matrix4x4 world = Multiply(Multiply(models[index].rootLocal,local),actorWorld);
            part->transformData->World = world;
            part->transformData->WVP = Multiply(world,viewProjection);
            part->transformData->WorldInverseTranspose = Transpose(Inverse(world));
        }
    };
    const float spread = 1.09f + pose.charge*0.14f + pose.death*0.40f;
    const float opening = 0.08f + pose.charge*0.42f + pose.death*0.55f;
    addReferencePart("twin_shield_panel","/left-shield",{-spread,-pose.death*0.30f,0},-opening,false);
    addReferencePart("twin_shield_panel","/right-shield",{spread,-pose.death*0.30f,0},opening,false);
    addReferencePart("twin_shield_core","/sensor-core",{},0,true);
}
