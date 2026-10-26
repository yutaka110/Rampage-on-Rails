#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "BulletPatternAsset.h"
#include "CourseAsset.h"
#include "CourseActorAsset.h"
#include "CourseSpawnRuntime.h"
#include "EnemyWaveAsset.h"
#include "EnemyProjectileDefinitionAsset.h"
#include "ObstacleAsset.h"

class CourseEventDispatcher {
public:
    void Dispatch(
        const std::vector<CourseEventMarker>& events,
        CourseSpawnRuntime& spawnRuntime,
        float currentDistance);
    bool SpawnAuthoredEnemy(
        const std::string& actorAssetId,
        std::string waveId,
        float spawnDistance,
        float lateralOffset,
        float verticalOffset,
        CourseSpawnRuntime& spawnRuntime,
        std::string* errorMessage = nullptr);
    // Resolve authored fixed positions before their combat events fire.
    // This creates no actors and reserves no encounter/attack slots.
    std::vector<CourseEnemyActorDesc> GroundBatteryPlacements(const CourseAsset& course);
    void PlaceGroundBatteries(const CourseAsset& course, CourseSpawnRuntime& runtime);

private:
    const EnemyWaveAsset* LoadEnemyWave(const std::string& id);
    const CourseActorAsset* LoadActorAsset(const std::string& id);
    const BulletPatternAsset* LoadBulletPatternAsset(const std::string& id);
    const EnemyProjectileDefinitionAsset* LoadProjectileDefinitionAsset(
        const std::string& id);
    const ObstacleAsset* LoadObstacleAsset(const std::string& id);
    void ApplyActorAsset(CourseEnemyActorDesc& desc, const CourseActorAsset& asset);
    void ApplyBulletPatternAsset(CourseEnemyActorDesc& desc, const BulletPatternAsset& asset);
    void SpawnEnemyWave(
        const CourseEventMarker& event,
        const EnemyWaveAsset& wave,
        CourseSpawnRuntime& spawnRuntime);
    void SpawnEventActor(
        const CourseEventMarker& event,
        CourseSpawnRuntime& spawnRuntime,
        float currentDistance);
    void LogDispatch(const CourseEventMarker& event, const char* result);

    std::unordered_map<std::string, EnemyWaveAsset> enemyWaveCache_;
    std::unordered_map<std::string, CourseActorAsset> actorAssetCache_;
    std::unordered_map<std::string, BulletPatternAsset> bulletPatternAssetCache_;
    std::unordered_map<std::string, EnemyProjectileDefinitionAsset>
        projectileDefinitionAssetCache_;
    std::unordered_map<std::string, ObstacleAsset> obstacleAssetCache_;
};
