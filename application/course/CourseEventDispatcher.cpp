#include "CourseEventDispatcher.h"
#include "../AppLogFile.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace {
Vector4 ColorForEventType(const std::string& type) {
    if (type == "obstacle") {
        return {1.0f, 0.62f, 0.12f, 1.0f};
    }
    if (type == "setpiece") {
        return {0.75f, 0.38f, 1.0f, 1.0f};
    }
    if (type == "boss" || type == "boss_phase") {
        return {1.0f, 0.08f, 0.26f, 1.0f};
    }
    if (type == "checkpoint") {
        return {0.24f, 1.0f, 0.56f, 1.0f};
    }
    if (type == "vfx") {
        return {0.30f, 0.82f, 1.0f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

float RadiusForEventType(const std::string& type) {
    if (type == "boss" || type == "boss_phase") {
        return 7.5f;
    }
    if (type == "setpiece") {
        return 6.0f;
    }
    if (type == "obstacle") {
        return 3.0f;
    }
    return 2.5f;
}

float LifetimeForEventType(const std::string& type) {
    if (type == "boss" || type == "boss_phase") {
        return 16.0f;
    }
    if (type == "setpiece") {
        return 12.0f;
    }
    if (type == "checkpoint") {
        return 10.0f;
    }
    return 7.0f;
}

bool RoleContains(const std::string& role, const char* token) {
    return role.find(token) != std::string::npos;
}

CourseEnemyFirePattern FirePatternForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return CourseEnemyFirePattern::BossArc;
    }
    if (RoleContains(role, "turret") || RoleContains(role, "crossfire")) {
        return CourseEnemyFirePattern::Spread;
    }
    if (RoleContains(role, "chase") || RoleContains(role, "pursuit")) {
        return CourseEnemyFirePattern::Twin;
    }
    return CourseEnemyFirePattern::Single;
}

float HitPointsForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return 520.0f;
    }
    if (RoleContains(role, "turret")) {
        return 90.0f;
    }
    if (RoleContains(role, "lead")) {
        return 45.0f;
    }
    return 28.0f;
}

float FireIntervalForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return 0.72f;
    }
    if (RoleContains(role, "turret") || RoleContains(role, "crossfire")) {
        return 0.58f;
    }
    if (RoleContains(role, "chase") || RoleContains(role, "pursuit")) {
        return 0.46f;
    }
    return 0.82f;
}

float BulletSpeedForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return 58.0f;
    }
    if (RoleContains(role, "chase") || RoleContains(role, "pursuit")) {
        return 64.0f;
    }
    if (RoleContains(role, "turret") || RoleContains(role, "crossfire")) {
        return 52.0f;
    }
    return 46.0f;
}

bool ContainsToken(const std::string& value, const char* token) {
    return value.find(token) != std::string::npos;
}

std::string EffectForCourseEvent(const CourseEventMarker& event) {
    const std::string& type = event.type;
    if (type == "boss_phase" || type == "checkpoint") {
        return "hit_ring";
    }
    if (type == "setpiece") {
        return "hit_plane_burst";
    }
    if (type == "vfx") {
        if (ContainsToken(event.id, "dust") ||
            ContainsToken(event.id, "shockwave") ||
            ContainsToken(event.id, "debris")) {
            return "hit_plane_burst";
        }
        return "hit_ring";
    }
    return "hit_ring";
}

std::string DefaultActorAssetForRole(const std::string& role) {
    if (RoleContains(role, "boss") || RoleContains(role, "gatekeeper")) {
        return "gatekeeper_boss";
    }
    if (RoleContains(role, "turret") || RoleContains(role, "crossfire")) {
        return "cliff_turret";
    }
    if (RoleContains(role, "chase") || RoleContains(role, "pursuit")) {
        return "drone_chaser";
    }
    return "drone_basic";
}

int DefaultBulletCountForPattern(CourseEnemyFirePattern pattern) {
    if (pattern == CourseEnemyFirePattern::BossArc) {
        return 5;
    }
    if (pattern == CourseEnemyFirePattern::Spread) {
        return 3;
    }
    if (pattern == CourseEnemyFirePattern::Twin) {
        return 2;
    }
    return 1;
}
} // namespace

void CourseEventDispatcher::Dispatch(
    const std::vector<CourseEventMarker>& events,
    CourseSpawnRuntime& spawnRuntime,
    float currentDistance) {
    for (const CourseEventMarker& event : events) {
        if (event.type == "enemy_wave") {
            const EnemyWaveAsset* wave = LoadEnemyWave(event.id);
            if (wave != nullptr) {
                SpawnEnemyWave(event, *wave, spawnRuntime);
                LogDispatch(event, "enemy actors spawned");
            } else {
                SpawnEventActor(event, spawnRuntime, currentDistance);
                LogDispatch(event, "enemy_wave missing, fallback actor spawned");
            }
            continue;
        }

        SpawnEventActor(event, spawnRuntime, currentDistance);
        LogDispatch(event, "event actor spawned");
    }
}

bool CourseEventDispatcher::SpawnAuthoredEnemy(
    const std::string& actorAssetId,
    std::string waveId,
    float spawnDistance,
    float lateralOffset,
    float verticalOffset,
    CourseSpawnRuntime& spawnRuntime,
    std::string* errorMessage) {
    const CourseActorAsset* actorAsset = LoadActorAsset(actorAssetId);
    if (actorAsset == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "Course actor asset \"" + actorAssetId + "\" could not be loaded.";
        }
        return false;
    }

    CourseEnemyActorDesc desc{};
    desc.waveId = std::move(waveId);
    desc.role = actorAssetId;
    desc.spawnDistance = spawnDistance;
    desc.lateralOffset = lateralOffset;
    desc.verticalOffset = verticalOffset;
    ApplyActorAsset(desc, *actorAsset);
    if (const BulletPatternAsset* patternAsset =
            LoadBulletPatternAsset(desc.bulletPatternId)) {
        ApplyBulletPatternAsset(desc, *patternAsset);
    } else {
        desc.firePattern = FirePatternForRole(desc.role);
        desc.bulletCount = DefaultBulletCountForPattern(desc.firePattern);
    }
    spawnRuntime.SpawnEnemyActor(std::move(desc));
    return true;
}

const EnemyWaveAsset* CourseEventDispatcher::LoadEnemyWave(const std::string& id) {
    const auto cached = enemyWaveCache_.find(id);
    if (cached != enemyWaveCache_.end()) {
        return cached->second.units.empty() ? nullptr : &cached->second;
    }

    EnemyWaveAsset wave;
    std::string error;
    const std::string path = "Resources/courses/waves/" + id + ".wave";
    if (!wave.LoadFromFile(path, &error)) {
        enemyWaveCache_[id] = {};
        OutputDebugStringA(("[CourseEventDispatcher] " + error + "\n").c_str());
        return nullptr;
    }

    auto [it, inserted] = enemyWaveCache_.emplace(id, std::move(wave));
    (void)inserted;
    return &it->second;
}

const CourseActorAsset* CourseEventDispatcher::LoadActorAsset(const std::string& id) {
    if (id.empty()) {
        return nullptr;
    }
    const auto cached = actorAssetCache_.find(id);
    if (cached != actorAssetCache_.end()) {
        return cached->second.id.empty() ? nullptr : &cached->second;
    }

    CourseActorAsset asset;
    std::string error;
    const std::string path = "Resources/courses/actors/" + id + ".actor";
    if (!asset.LoadFromFile(path, &error)) {
        actorAssetCache_[id] = {};
        OutputDebugStringA(("[CourseEventDispatcher] " + error + "\n").c_str());
        return nullptr;
    }

    auto [it, inserted] = actorAssetCache_.emplace(id, std::move(asset));
    (void)inserted;
    return &it->second;
}

const BulletPatternAsset* CourseEventDispatcher::LoadBulletPatternAsset(const std::string& id) {
    if (id.empty()) {
        return nullptr;
    }
    const auto cached = bulletPatternAssetCache_.find(id);
    if (cached != bulletPatternAssetCache_.end()) {
        return cached->second.id.empty() ? nullptr : &cached->second;
    }

    BulletPatternAsset asset;
    std::string error;
    const std::string path = "Resources/courses/bullet_patterns/" + id + ".pattern";
    if (!asset.LoadFromFile(path, &error)) {
        bulletPatternAssetCache_[id] = {};
        OutputDebugStringA(("[CourseEventDispatcher] " + error + "\n").c_str());
        return nullptr;
    }

    auto [it, inserted] = bulletPatternAssetCache_.emplace(id, std::move(asset));
    (void)inserted;
    return &it->second;
}

const ObstacleAsset* CourseEventDispatcher::LoadObstacleAsset(const std::string& id) {
    if (id.empty()) {
        return nullptr;
    }
    const auto cached = obstacleAssetCache_.find(id);
    if (cached != obstacleAssetCache_.end()) {
        return cached->second.id.empty() ? nullptr : &cached->second;
    }

    ObstacleAsset asset;
    std::string error;
    const std::string path = "Resources/courses/obstacles/" + id + ".obstacle";
    if (!asset.LoadFromFile(path, &error)) {
        obstacleAssetCache_[id] = {};
        OutputDebugStringA(("[CourseEventDispatcher] " + error + "\n").c_str());
        return nullptr;
    }

    auto [it, inserted] = obstacleAssetCache_.emplace(id, std::move(asset));
    (void)inserted;
    return &it->second;
}

void CourseEventDispatcher::ApplyActorAsset(CourseEnemyActorDesc& desc, const CourseActorAsset& asset) {
    desc.actorAssetId = asset.id;
    desc.meshId = asset.meshId;
    desc.bulletPatternId = asset.bulletPatternId;
    desc.radius = asset.radius;
    desc.lifetime = asset.lifetime;
    desc.hitPoints = asset.hitPoints;
    desc.forwardSpeed = asset.forwardSpeed;
    desc.fireInterval = asset.fireInterval;
    desc.firstShotDelay = asset.firstShotDelay;
    desc.bulletSpeed = asset.bulletSpeed;
    desc.color = asset.color;
    desc.behaviorDefinition = asset.behaviorDefinition;
}

const EnemyProjectileDefinitionAsset*
CourseEventDispatcher::LoadProjectileDefinitionAsset(const std::string& id) {
    if (id.empty()) return nullptr;
    const auto cached = projectileDefinitionAssetCache_.find(id);
    if (cached != projectileDefinitionAssetCache_.end()) {
        return cached->second.id.empty() ? nullptr : &cached->second;
    }
    EnemyProjectileDefinitionAsset asset{};
    std::string error;
    const std::string path =
        "Resources/courses/projectiles/" + id + ".projectile";
    if (!asset.LoadFromFile(path, &error)) {
        projectileDefinitionAssetCache_[id] = {};
        OutputDebugStringA(("[CourseEventDispatcher] " + error + "\n").c_str());
        return nullptr;
    }
    auto [it, inserted] =
        projectileDefinitionAssetCache_.emplace(id, std::move(asset));
    (void)inserted;
    return &it->second;
}

void CourseEventDispatcher::ApplyBulletPatternAsset(
    CourseEnemyActorDesc& desc,
    const BulletPatternAsset& asset) {
    desc.bulletPatternId = asset.id;
    desc.projectileDefinitionId = asset.projectileDefinitionId;
    desc.firePattern = asset.firePattern;
    desc.bulletCount = asset.bulletCount > 0
        ? asset.bulletCount
        : DefaultBulletCountForPattern(asset.firePattern);
    desc.bulletLateralSpreadSpeed = asset.lateralSpreadSpeed;
    desc.bulletVerticalSpreadSpeed = asset.verticalSpreadSpeed;
    desc.bulletRadius = asset.bulletRadius;
    desc.bulletLifetime = asset.bulletLifetime;
    desc.bulletDamage = asset.damage;
    desc.bulletColor = asset.color;
    if (const EnemyProjectileDefinitionAsset* projectile =
            LoadProjectileDefinitionAsset(asset.projectileDefinitionId)) {
        desc.projectileDefinition = *projectile;
        desc.projectileDefinitionId = projectile->id;
    }
}

std::vector<CourseEnemyActorDesc> CourseEventDispatcher::GroundBatteryPlacements(const CourseAsset& course) {
    std::vector<CourseEnemyActorDesc> placements;
    for(const auto& event:course.events) {
        if(event.type!="enemy_wave" || event.payload!="route:turret") continue;
        const auto* wave=LoadEnemyWave(event.id);
        if(!wave) continue;
        for(const auto& unit:wave->units) {
            const auto* asset=LoadActorAsset(unit.actorAssetId);
            if(!asset || asset->id!="short_turret") continue;
            CourseEnemyActorDesc desc{};ApplyActorAsset(desc,*asset);
            desc.spawnDistance=event.distance;desc.distanceOffset=unit.distanceOffset;
            desc.lateralOffset=unit.lateralOffset;desc.verticalOffset=unit.verticalOffset;
            placements.push_back(std::move(desc));
        }
    }
    return placements;
}

void CourseEventDispatcher::PlaceGroundBatteries(const CourseAsset& course,CourseSpawnRuntime& runtime) {
    for(auto event:course.events) {
        if(event.type!="enemy_wave" || event.payload!="route:turret") continue;
        event.payload="preplaced:turret";
        Dispatch({event},runtime,0);
    }
}

void CourseEventDispatcher::SpawnEnemyWave(
    const CourseEventMarker& event,
    const EnemyWaveAsset& wave,
    CourseSpawnRuntime& spawnRuntime) {
    for (const EnemyWaveUnit& unit : wave.units) {
        CourseEnemyActorDesc desc{};
        desc.waveId = event.payload == "route:adaptive" || event.payload == "route:support" ? event.editorGuid : event.id;
        if (wave.formationDefinition.has_value()) {
            desc.formationDefinition = *wave.formationDefinition;
            // Adaptive instances retain a distinct occurrence identity even
            // when they load the same left/right wave asset.
            desc.formationDefinition.definitionId = desc.waveId;
        }
        desc.role = unit.role;
        desc.spawnDistance = event.distance;
        desc.entrySource = wave.entrySource;
        desc.establishedEntrance = wave.localApproach;
        if(event.payload=="preplaced:turret") desc.stationaryActivationDistance=event.distance;
        const std::string actorAssetId = !unit.actorAssetId.empty()
            ? unit.actorAssetId
            : DefaultActorAssetForRole(unit.role);
        if (const CourseActorAsset* actorAsset = LoadActorAsset(actorAssetId)) {
            ApplyActorAsset(desc, *actorAsset);
        }
        desc.distanceOffset = unit.distanceOffset;
        desc.lateralOffset = unit.lateralOffset;
        desc.verticalOffset = unit.verticalOffset;
        if (unit.forwardSpeed != 0.0f) {
            desc.forwardSpeed = unit.forwardSpeed;
        }
        if (unit.actorAssetId.empty()) {
            desc.radius = unit.radius;
            desc.lifetime = unit.lifetime;
            desc.color = unit.color;
        }
        if (desc.actorAssetId.empty()) {
            desc.hitPoints = HitPointsForRole(unit.role);
            desc.fireInterval = FireIntervalForRole(unit.role);
            desc.bulletSpeed = BulletSpeedForRole(unit.role);
            desc.firePattern = FirePatternForRole(unit.role);
        }
        desc.firstShotDelay += (std::abs(unit.lateralOffset) * 0.015f);
        const std::string patternId = !unit.bulletPatternId.empty()
            ? unit.bulletPatternId
            : desc.bulletPatternId;
        if (const BulletPatternAsset* patternAsset = LoadBulletPatternAsset(patternId)) {
            ApplyBulletPatternAsset(desc, *patternAsset);
        } else {
            desc.firePattern = FirePatternForRole(unit.role);
            desc.bulletCount = DefaultBulletCountForPattern(desc.firePattern);
        }
        spawnRuntime.SpawnEnemyActor(std::move(desc));
    }
}

void CourseEventDispatcher::SpawnEventActor(
    const CourseEventMarker& event,
    CourseSpawnRuntime& spawnRuntime,
    float currentDistance) {
    const float spawnDistance = event.distance > 0.0f ? event.distance : currentDistance;

    if (event.type == "obstacle") {
        CourseObstacleActorDesc desc{};
        desc.id = event.id;
        desc.meshId = event.id;
        desc.payload = event.payload;
        desc.spawnDistance = spawnDistance;
        desc.distanceOffset = 22.0f;
        desc.lifetime = LifetimeForEventType(event.type);
        desc.halfExtents = {4.2f, 4.8f, 2.4f};
        desc.color = ColorForEventType(event.type);
        if (const ObstacleAsset* obstacleAsset = LoadObstacleAsset(event.id)) {
            desc.meshId = obstacleAsset->meshId;
            desc.distanceOffset = obstacleAsset->distanceOffset;
            desc.lateralOffset = obstacleAsset->lateralOffset;
            desc.verticalOffset = obstacleAsset->verticalOffset;
            desc.forwardSpeed = obstacleAsset->forwardSpeed;
            desc.lifetime = obstacleAsset->lifetime;
            desc.hitPoints = obstacleAsset->hitPoints;
            desc.breakable = obstacleAsset->breakable;
            desc.vfxCueId = obstacleAsset->vfxCueId;
            desc.halfExtents = obstacleAsset->halfExtents;
            desc.color = obstacleAsset->color;
        }
        spawnRuntime.SpawnObstacle(std::move(desc));
        return;
    }

    if (event.type == "boss") {
        CourseEnemyActorDesc desc{};
        desc.waveId = event.id;
        desc.role = event.id.empty() ? "gatekeeper_boss" : event.id;
        desc.spawnDistance = spawnDistance;
        if (const CourseActorAsset* actorAsset = LoadActorAsset("gatekeeper_boss")) {
            ApplyActorAsset(desc, *actorAsset);
        }
        desc.distanceOffset = 36.0f;
        desc.lateralOffset = 0.0f;
        desc.verticalOffset = 8.0f;
        if (desc.actorAssetId.empty()) {
            desc.forwardSpeed = -2.0f;
            desc.radius = RadiusForEventType(event.type);
            desc.lifetime = LifetimeForEventType(event.type);
            desc.hitPoints = HitPointsForRole("gatekeeper_boss");
            desc.fireInterval = FireIntervalForRole("gatekeeper_boss");
            desc.firstShotDelay = 0.45f;
            desc.bulletSpeed = BulletSpeedForRole("gatekeeper_boss");
            desc.firePattern = CourseEnemyFirePattern::BossArc;
            desc.color = ColorForEventType(event.type);
        }
        if (const BulletPatternAsset* patternAsset = LoadBulletPatternAsset(desc.bulletPatternId)) {
            ApplyBulletPatternAsset(desc, *patternAsset);
        } else {
            desc.bulletCount = DefaultBulletCountForPattern(desc.firePattern);
        }
        spawnRuntime.SpawnEnemyActor(std::move(desc));
    }

    CourseVfxCueDesc cue{};
    cue.id = event.id;
    cue.effectName = EffectForCourseEvent(event);
    cue.payload = event.payload;
    cue.spawnDistance = spawnDistance;
    cue.distanceOffset = event.type == "boss_phase" ? 30.0f : 22.0f;
    cue.verticalOffset = event.type == "checkpoint" ? 4.0f : 5.5f;
    cue.radius = RadiusForEventType(event.type);
    cue.lifetime = (std::min)(LifetimeForEventType(event.type), 5.0f);
    cue.color = ColorForEventType(event.type);
    spawnRuntime.SpawnVfxCue(std::move(cue));
}

void CourseEventDispatcher::LogDispatch(const CourseEventMarker& event, const char* result) {
    std::ostringstream line;
    line << "[CourseDispatch] distance=" << event.distance
         << " type=" << event.type
         << " id=" << event.id
         << " result=\"" << result << "\"\n";
    OutputDebugStringA(line.str().c_str());
    std::ofstream log = app::OpenRotatingLog("logs/course_dispatch.log");
    if (log) {
        log << line.str();
    }
}
