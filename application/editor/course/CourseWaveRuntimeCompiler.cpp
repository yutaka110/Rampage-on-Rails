#include "CourseWaveRuntimeCompiler.h"

#include "CourseEnemyAuthoringModel.h"
#include "CourseRailAuthoringModel.h"
#include "CourseWaveAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace editor {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& hash, const void* value, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(value);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void HashValue(uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(value));
}

void HashString(uint64_t& hash, std::string_view value) {
    HashBytes(hash, value.data(), value.size());
    constexpr unsigned char separator = 0xffu;
    HashBytes(hash, &separator, sizeof(separator));
}

void AddDiagnostic(
    CourseWaveRuntimeCompileResult& result,
    CourseWaveRuntimeDiagnosticSeverity severity,
    std::string code,
    std::string objectGuid,
    std::string message) {
    if (severity == CourseWaveRuntimeDiagnosticSeverity::Error) ++result.errors;
    if (severity == CourseWaveRuntimeDiagnosticSeverity::Warning) ++result.warnings;
    result.diagnostics.push_back({
        severity,
        std::move(code),
        std::move(objectGuid),
        std::move(message)});
}

void ApplyActorAsset(CourseEnemyActorDesc& actor, const CourseActorAsset& asset) {
    actor.actorAssetId = asset.id;
    actor.meshId = asset.meshId;
    actor.bulletPatternId = asset.bulletPatternId;
    actor.radius = asset.radius;
    actor.hitPoints = asset.hitPoints;
    actor.lifetime = asset.lifetime;
    actor.forwardSpeed = asset.forwardSpeed;
    actor.fireInterval = asset.fireInterval;
    actor.firstShotDelay = asset.firstShotDelay;
    actor.bulletSpeed = asset.bulletSpeed;
    actor.color = asset.color;
    actor.behaviorDefinition = asset.behaviorDefinition;
    actor.energyShield = asset.energyShield;
}

void ApplyBulletPattern(CourseEnemyActorDesc& actor, const BulletPatternAsset& asset) {
    actor.bulletPatternId = asset.id;
    actor.projectileDefinitionId = asset.projectileDefinitionId;
    actor.firePattern = asset.firePattern;
    actor.bulletCount = (std::max)(1, asset.bulletCount);
    actor.bulletLateralSpreadSpeed = asset.lateralSpreadSpeed;
    actor.bulletVerticalSpreadSpeed = asset.verticalSpreadSpeed;
    actor.bulletRadius = asset.bulletRadius;
    actor.bulletLifetime = asset.bulletLifetime;
    actor.bulletDamage = asset.damage;
    actor.bulletColor = asset.color;
}

} // namespace

CourseWaveRuntimeCompileResult CourseWaveRuntimeCompiler::Compile(
    const CourseAsset& source,
    const CourseWaveRuntimeCompileOptions& options) const {
    CourseWaveRuntimeCompileResult result{};
    result.program.sourceCourseName = source.name;
    result.program.schemaVersion = kCourseRuntimeAuthoringSchemaVersion;
    result.program.formatVersion = kCourseRuntimeProgramFormatVersion;
    result.program.compilerVersion = kCourseRuntimeCompilerVersion;
    result.program.sourceAssetHash = ComputeCourseAssetSourceHash(source);

    const CourseRailAuthoringModel rail(source);
    const CourseWaveAuthoringModel waves(source);
    const CourseEnemyAuthoringModel enemies(source);
    if (!rail.IsValid()) {
        AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
            "rail.invalid", {}, rail.ValidationError());
    }
    if (!waves.IsValid()) {
        AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
            "waves.invalid", {}, waves.ValidationError());
    }
    if (!enemies.IsValid()) {
        AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
            "enemies.invalid", {}, enemies.ValidationError());
    }
    if (result.errors != 0) {
        result.message = "Course Wave runtime compilation failed authoring validation.";
        return result;
    }

    result.program.railLength = rail.Length();
    std::vector<const CourseWaveDefinition*> sortedWaves;
    sortedWaves.reserve(waves.Waves().size());
    for (const CourseWaveDefinition& wave : waves.Waves()) {
        if (wave.enabled || options.compileDisabledObjects) sortedWaves.push_back(&wave);
    }
    std::stable_sort(sortedWaves.begin(), sortedWaves.end(), [](const auto* a, const auto* b) {
        if (a->triggerRailDistance == b->triggerRailDistance) {
            return a->editorGuid < b->editorGuid;
        }
        return a->triggerRailDistance < b->triggerRailDistance;
    });

    std::unordered_map<std::string, uint32_t> waveIndices;
    for (const CourseWaveDefinition* sourceWave : sortedWaves) {
        if (!std::isfinite(sourceWave->triggerRailDistance) ||
            sourceWave->triggerRailDistance < 0.0f ||
            sourceWave->triggerRailDistance > rail.Length()) {
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
                "wave.trigger_out_of_range", sourceWave->editorGuid,
                "Wave trigger distance is outside the compiled RailPath.");
            continue;
        }
        CompiledCourseWaveNode node{};
        node.waveGuid = sourceWave->editorGuid;
        node.displayName = sourceWave->displayName;
        node.triggerRailDistance = sourceWave->triggerRailDistance;
        node.prewarmDistance = (std::max)(0.0f, sourceWave->prewarmDistance);
        node.timeoutSeconds = (std::max)(0.0f, sourceWave->timeoutSeconds);
        node.completionCondition = sourceWave->completionCondition;
        node.executionPolicy = sourceWave->executionPolicy;
        node.triggerEventId = sourceWave->triggerEventId;
        node.enabled = sourceWave->enabled;
        waveIndices[node.waveGuid] = static_cast<uint32_t>(result.program.waves.size());
        result.program.waves.push_back(std::move(node));
    }

    for (std::size_t index = 0; index < result.program.waves.size(); ++index) {
        const CourseWaveDefinition* sourceWave = waves.Find(
            result.program.waves[index].waveGuid);
        if (sourceWave == nullptr || sourceWave->nextWaveGuid.empty()) continue;
        const auto next = waveIndices.find(sourceWave->nextWaveGuid);
        if (next == waveIndices.end()) {
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
                "wave.next_not_compiled", sourceWave->editorGuid,
                "Wave transition references a disabled or unavailable runtime Wave.");
            continue;
        }
        result.program.waves[index].nextWaveIndex = static_cast<int32_t>(next->second);
    }

    for (const CourseEnemyPlacement& placement : enemies.Placements()) {
        if (!placement.enabled && !options.compileDisabledObjects) continue;
        if (placement.waveGroupGuid.empty()) {
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Warning,
                "actor.wave_unassigned", placement.editorGuid,
                "Enemy placement is not assigned to a Wave and was omitted from the Wave program.");
            continue;
        }
        const auto wave = waveIndices.find(placement.waveGroupGuid);
        if (wave == waveIndices.end()) {
            const CourseWaveDefinition* sourceWave = waves.Find(placement.waveGroupGuid);
            if (sourceWave != nullptr && !sourceWave->enabled &&
                !options.compileDisabledObjects) {
                AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Info,
                    "actor.wave_disabled", placement.editorGuid,
                    "Enemy placement belongs to a disabled Wave and was omitted.");
                continue;
            }
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
                "actor.wave_not_compiled", placement.editorGuid,
                "Enemy placement references a Wave that is not in the runtime program.");
            continue;
        }
        const CourseEnemyPlacementResolution resolved = enemies.Resolve(placement);
        if (!resolved.valid) {
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
                "actor.anchor_invalid", placement.editorGuid,
                "Enemy RailAnchor could not be resolved during runtime compilation.");
            continue;
        }

        CompiledCourseWaveActor compiled{};
        compiled.placementGuid = placement.editorGuid;
        compiled.waveGuid = placement.waveGroupGuid;
        compiled.waveIndex = wave->second;
        compiled.authoredRotation = placement.localRotation;
        compiled.authoredScale = placement.localScale;
        compiled.enabled = placement.enabled;
        compiled.actor.sourcePlacementGuid = placement.editorGuid;
        compiled.actor.waveId = placement.waveGroupGuid;
        compiled.actor.actorAssetId = placement.actorAssetId;
        compiled.actor.role = placement.actorAssetId;
        compiled.actor.spawnDistance = resolved.railSample.distance;
        compiled.actor.distanceOffset = placement.railAnchor.forwardOffset;
        compiled.actor.lateralOffset = placement.railAnchor.lateralOffset;
        compiled.actor.verticalOffset = placement.railAnchor.verticalOffset;
        compiled.actor.localRotation = placement.localRotation;
        compiled.actor.localScale = placement.localScale;

        CourseActorAsset actorAsset{};
        std::string assetError;
        if (ResolveActorAsset(
                placement.actorAssetId, options, actorAsset, assetError)) {
            ApplyActorAsset(compiled.actor, actorAsset);
            compiled.actorAssetResolved = true;
        } else if (options.allowFallbackActorAssets) {
            compiled.actor.actorAssetId = placement.actorAssetId;
            compiled.actor.meshId = "ball";
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Warning,
                "actor.asset_fallback", placement.editorGuid,
                assetError.empty() ? "ActorAsset missing; packaged fallback mesh selected."
                                   : assetError);
        } else {
            AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Error,
                "actor.asset_missing", placement.editorGuid,
                assetError.empty() ? "ActorAsset could not be resolved."
                                   : assetError);
            continue;
        }

        const std::string patternId = placement.bulletPatternOverrideId.empty()
            ? compiled.actor.bulletPatternId
            : placement.bulletPatternOverrideId;
        if (!patternId.empty()) {
            BulletPatternAsset pattern{};
            std::string patternError;
            if (ResolveBulletPattern(patternId, options, pattern, patternError)) {
                ApplyBulletPattern(compiled.actor, pattern);
                if (!pattern.projectileDefinitionId.empty()) {
                    EnemyProjectileDefinitionAsset projectile{};
                    std::string projectileError;
                    if (ResolveProjectileDefinition(
                            pattern.projectileDefinitionId,
                            options,
                            projectile,
                            projectileError)) {
                        compiled.actor.projectileDefinitionId = projectile.id;
                        compiled.actor.projectileDefinition = std::move(projectile);
                    } else {
                        AddDiagnostic(result,
                            CourseWaveRuntimeDiagnosticSeverity::Warning,
                            "actor.projectile_fallback",
                            placement.editorGuid,
                            projectileError.empty()
                                ? "Projectile definition could not be resolved; runtime-safe fallback selected."
                                : projectileError);
                    }
                }
            } else {
                AddDiagnostic(result, CourseWaveRuntimeDiagnosticSeverity::Warning,
                    "actor.pattern_fallback", placement.editorGuid,
                    patternError.empty()
                        ? "Bullet pattern could not be resolved; ActorAsset defaults retained."
                        : patternError);
            }
        }
        const uint32_t actorIndex = static_cast<uint32_t>(result.program.actors.size());
        result.program.actors.push_back(std::move(compiled));
        result.program.waves[wave->second].actorIndices.push_back(actorIndex);
    }

    uint64_t fingerprint = kFnvOffset;
    HashString(fingerprint, result.program.sourceCourseName);
    HashValue(fingerprint, result.program.railLength);
    for (const CompiledCourseWaveNode& wave : result.program.waves) {
        HashString(fingerprint, wave.waveGuid);
        HashValue(fingerprint, wave.triggerRailDistance);
        HashValue(fingerprint, wave.prewarmDistance);
        HashValue(fingerprint, wave.timeoutSeconds);
        HashValue(fingerprint, wave.completionCondition);
        HashValue(fingerprint, wave.executionPolicy);
        HashValue(fingerprint, wave.nextWaveIndex);
        HashString(fingerprint, wave.triggerEventId);
        HashValue(fingerprint, wave.enabled);
        for (const uint32_t actorIndex : wave.actorIndices) {
            HashValue(fingerprint, actorIndex);
        }
    }
    for (const CompiledCourseWaveActor& actor : result.program.actors) {
        HashString(fingerprint, actor.placementGuid);
        HashString(fingerprint, actor.actor.actorAssetId);
        HashString(fingerprint, actor.actor.meshId);
        HashValue(fingerprint, actor.actor.spawnDistance);
        HashValue(fingerprint, actor.actor.distanceOffset);
        HashValue(fingerprint, actor.actor.lateralOffset);
        HashValue(fingerprint, actor.actor.verticalOffset);
        HashValue(fingerprint, actor.actor.radius);
        HashValue(fingerprint, actor.actor.hitPoints);
        HashValue(fingerprint, actor.actor.lifetime);
        HashValue(fingerprint, actor.actor.forwardSpeed);
        HashValue(fingerprint, actor.actor.fireInterval);
        HashValue(fingerprint, actor.actor.firstShotDelay);
        HashValue(fingerprint, actor.actor.bulletSpeed);
        HashValue(fingerprint, actor.actor.bulletCount);
        HashValue(fingerprint, actor.actor.bulletDamage);
        HashString(fingerprint, actor.actor.projectileDefinitionId);
        HashValue(fingerprint, actor.actor.projectileDefinition.trajectory);
        HashValue(fingerprint, actor.actor.projectileDefinition.initialSpeed);
        HashValue(fingerprint, actor.actor.projectileDefinition.maximumSpeed);
        HashValue(fingerprint, actor.actor.projectileDefinition.homingTurnRateRadians);
        HashValue(fingerprint, actor.actor.color);
        HashValue(fingerprint, actor.authoredRotation);
        HashValue(fingerprint, actor.authoredScale);
    }
    result.program.sourceFingerprint = fingerprint;
    result.succeeded = result.errors == 0;
    result.message = result.succeeded
        ? "Compiled " + std::to_string(result.program.waves.size()) +
            " Waves and " + std::to_string(result.program.actors.size()) +
            " Actor placements."
        : "Course Wave runtime compilation failed with " +
            std::to_string(result.errors) + " error(s).";
    return result;
}

bool CourseWaveRuntimeCompiler::ResolveActorAsset(
    std::string_view id,
    const CourseWaveRuntimeCompileOptions& options,
    CourseActorAsset& asset,
    std::string& errorMessage) {
    if (id.empty()) {
        errorMessage = "Enemy placement has no ActorAsset ID.";
        return false;
    }
    if (options.actorAssetResolver) {
        return options.actorAssetResolver(id, asset, errorMessage);
    }
    const std::filesystem::path path =
        std::filesystem::path(options.actorAssetDirectory) /
        (std::string(id) + ".actor");
    return asset.LoadFromFile(path.generic_string(), &errorMessage);
}

bool CourseWaveRuntimeCompiler::ResolveBulletPattern(
    std::string_view id,
    const CourseWaveRuntimeCompileOptions& options,
    BulletPatternAsset& asset,
    std::string& errorMessage) {
    if (id.empty()) return false;
    if (options.bulletPatternResolver) {
        return options.bulletPatternResolver(id, asset, errorMessage);
    }
    const std::filesystem::path path =
        std::filesystem::path(options.bulletPatternDirectory) /
        (std::string(id) + ".pattern");
    return asset.LoadFromFile(path.generic_string(), &errorMessage);
}

bool CourseWaveRuntimeCompiler::ResolveProjectileDefinition(
    std::string_view id,
    const CourseWaveRuntimeCompileOptions& options,
    EnemyProjectileDefinitionAsset& asset,
    std::string& errorMessage) {
    if (id.empty()) return false;
    if (options.projectileDefinitionResolver) {
        return options.projectileDefinitionResolver(id, asset, errorMessage);
    }
    const std::filesystem::path path =
        std::filesystem::path(options.projectileDefinitionDirectory) /
        (std::string(id) + ".projectile");
    return asset.LoadFromFile(path.generic_string(), &errorMessage);
}

const char* ToString(CourseWaveRuntimeDiagnosticSeverity severity) {
    switch (severity) {
    case CourseWaveRuntimeDiagnosticSeverity::Info: return "Info";
    case CourseWaveRuntimeDiagnosticSeverity::Warning: return "Warning";
    case CourseWaveRuntimeDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}

} // namespace editor
