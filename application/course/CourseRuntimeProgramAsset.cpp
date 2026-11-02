#include "CourseRuntimeProgramAsset.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace {

constexpr char kMagic[] = {'C', 'R', 'T', 'P', 'R', 'O', 'G', '1'};
constexpr uint32_t kMaximumCollectionCount = 1'000'000;
constexpr uint32_t kMaximumStringBytes = 16 * 1024 * 1024;
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

class BinaryWriter final {
public:
    void Bytes(const void* value, std::size_t size) {
        bytes_.append(static_cast<const char*>(value), size);
    }
    void U8(uint8_t value) { Bytes(&value, sizeof(value)); }
    void U32(uint32_t value) { Bytes(&value, sizeof(value)); }
    void I32(int32_t value) { Bytes(&value, sizeof(value)); }
    void U64(uint64_t value) { Bytes(&value, sizeof(value)); }
    void F32(float value) { Bytes(&value, sizeof(value)); }
    void Bool(bool value) { U8(value ? 1u : 0u); }
    void String(std::string_view value) {
        U32(static_cast<uint32_t>(value.size()));
        Bytes(value.data(), value.size());
    }
    std::string Take() { return std::move(bytes_); }

private:
    std::string bytes_;
};

class BinaryReader final {
public:
    explicit BinaryReader(std::string_view bytes) : bytes_(bytes) {}

    bool Bytes(void* value, std::size_t size) {
        if (size > bytes_.size() - offset_) return Fail("Unexpected end of runtime program.");
        std::memcpy(value, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }
    bool U8(uint8_t& value) { return Bytes(&value, sizeof(value)); }
    bool U32(uint32_t& value) { return Bytes(&value, sizeof(value)); }
    bool I32(int32_t& value) { return Bytes(&value, sizeof(value)); }
    bool U64(uint64_t& value) { return Bytes(&value, sizeof(value)); }
    bool F32(float& value) { return Bytes(&value, sizeof(value)); }
    bool Bool(bool& value) {
        uint8_t encoded = 0;
        if (!U8(encoded)) return false;
        if (encoded > 1u) return Fail("Runtime program contains an invalid Boolean value.");
        value = encoded != 0;
        return true;
    }
    bool String(std::string& value) {
        uint32_t size = 0;
        if (!U32(size)) return false;
        if (size > kMaximumStringBytes || size > bytes_.size() - offset_) {
            return Fail("Runtime program string exceeds its validated bounds.");
        }
        value.assign(bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }
    bool Count(uint32_t& value) {
        if (!U32(value)) return false;
        return value <= kMaximumCollectionCount ||
            Fail("Runtime program collection exceeds its validated bounds.");
    }
    bool AtEnd() const noexcept { return offset_ == bytes_.size(); }
    const std::string& Error() const noexcept { return error_; }

private:
    bool Fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    std::string_view bytes_;
    std::size_t offset_ = 0;
    std::string error_;
};

void WriteVector3(BinaryWriter& writer, const Vector3& value) {
    writer.F32(value.x); writer.F32(value.y); writer.F32(value.z);
}

void WriteVector4(BinaryWriter& writer, const Vector4& value) {
    writer.F32(value.x); writer.F32(value.y); writer.F32(value.z); writer.F32(value.w);
}

bool ReadVector3(BinaryReader& reader, Vector3& value) {
    return reader.F32(value.x) && reader.F32(value.y) && reader.F32(value.z);
}

bool ReadVector4(BinaryReader& reader, Vector4& value) {
    return reader.F32(value.x) && reader.F32(value.y) &&
        reader.F32(value.z) && reader.F32(value.w);
}

void WriteBehaviorDefinition(
    BinaryWriter& writer,
    const EnemyBehaviorDefinition& value) {
    writer.String(value.definitionId);
    writer.U8(static_cast<uint8_t>(value.archetype));
    writer.F32(value.entryDurationSeconds);
    writer.F32(value.positioningDurationSeconds);
    writer.F32(value.aimingDurationSeconds);
    writer.F32(value.attackLeadSeconds);
    writer.F32(value.attackCooldownSeconds);
    writer.F32(value.evadeDurationSeconds);
    writer.F32(value.repositionDurationSeconds);
    writer.F32(value.lateralAmplitude);
    writer.F32(value.verticalAmplitude);
    writer.F32(value.movementFrequency);
    writer.F32(value.forwardMotionScale);
    writer.F32(value.maximumBankRadians);
    writer.Bool(value.commercialBehavior);
    writer.Bool(value.movementEnabled);
    writer.Bool(value.requireTelegraphPresentation);
    writer.Bool(value.maintainForwardEngagementBand);
    writer.F32(value.engagementBandMinimumForwardDistance);
    writer.F32(value.engagementBandPreferredForwardDistance);
    writer.F32(value.engagementBandMaximumForwardDistance);
    writer.F32(value.engagementBandDisengageForwardDistance);
    writer.F32(value.engagementBandPositionGain);
    writer.F32(value.engagementBandMaximumCorrectionSpeed);
    writer.F32(value.engagementBandVelocityResponse);
    writer.Bool(value.choreographedAttackPass);
    writer.F32(value.attackPassStartDelaySeconds);
    writer.F32(value.attackPassRecoilSeconds);
    writer.F32(value.attackPassHandoffLeadSeconds);
}

bool ReadBehaviorDefinition(
    BinaryReader& reader,
    EnemyBehaviorDefinition& value) {
    uint8_t archetype = 0;
    if (!reader.String(value.definitionId) || !reader.U8(archetype) ||
        archetype > static_cast<uint8_t>(EnemyBehaviorArchetype::Boss) ||
        !reader.F32(value.entryDurationSeconds) ||
        !reader.F32(value.positioningDurationSeconds) ||
        !reader.F32(value.aimingDurationSeconds) ||
        !reader.F32(value.attackLeadSeconds) ||
        !reader.F32(value.attackCooldownSeconds) ||
        !reader.F32(value.evadeDurationSeconds) ||
        !reader.F32(value.repositionDurationSeconds) ||
        !reader.F32(value.lateralAmplitude) ||
        !reader.F32(value.verticalAmplitude) ||
        !reader.F32(value.movementFrequency) ||
        !reader.F32(value.forwardMotionScale) ||
        !reader.F32(value.maximumBankRadians) ||
        !reader.Bool(value.commercialBehavior) ||
        !reader.Bool(value.movementEnabled) ||
        !reader.Bool(value.requireTelegraphPresentation) ||
        !reader.Bool(value.maintainForwardEngagementBand) ||
        !reader.F32(value.engagementBandMinimumForwardDistance) ||
        !reader.F32(value.engagementBandPreferredForwardDistance) ||
        !reader.F32(value.engagementBandMaximumForwardDistance) ||
        !reader.F32(value.engagementBandDisengageForwardDistance) ||
        !reader.F32(value.engagementBandPositionGain) ||
        !reader.F32(value.engagementBandMaximumCorrectionSpeed) ||
        !reader.F32(value.engagementBandVelocityResponse) ||
        !reader.Bool(value.choreographedAttackPass) ||
        !reader.F32(value.attackPassStartDelaySeconds) ||
        !reader.F32(value.attackPassRecoilSeconds) ||
        !reader.F32(value.attackPassHandoffLeadSeconds)) {
        return false;
    }
    value.archetype = static_cast<EnemyBehaviorArchetype>(archetype);
    return value.definitionId.empty() || value.Validate(nullptr);
}

void WriteFormationDefinition(
    BinaryWriter& writer,
    const EnemyFormationDefinition& value) {
    writer.String(value.definitionId);
    writer.U8(static_cast<uint8_t>(value.pattern));
    writer.U8(static_cast<uint8_t>(value.entranceStyle));
    writer.U8(static_cast<uint8_t>(value.exitStyle));
    writer.F32(value.slotSpacing);
    writer.F32(value.verticalSpacing);
    writer.F32(value.cohesionResponse);
    writer.F32(value.maximumCorrection);
    writer.F32(value.attackStaggerSeconds);
    writer.F32(value.entranceDurationSeconds);
    writer.F32(value.entranceStaggerSeconds);
    writer.F32(value.entranceForwardDistance);
    writer.F32(value.entranceSideDistance);
    writer.F32(value.exitDurationSeconds);
    writer.F32(value.exitForwardDistance);
    writer.F32(value.exitSideDistance);
    writer.Bool(value.preserveAuthoredSlots);
    writer.Bool(value.commercialFormation);
}

bool ReadFormationDefinition(
    BinaryReader& reader,
    EnemyFormationDefinition& value) {
    uint8_t pattern = 0;
    uint8_t entranceStyle = 0;
    uint8_t exitStyle = 0;
    if (!reader.String(value.definitionId) ||
        !reader.U8(pattern) ||
        pattern > static_cast<uint8_t>(EnemyFormationPattern::Ring) ||
        !reader.U8(entranceStyle) ||
        entranceStyle > static_cast<uint8_t>(EnemyEntranceStyle::RearChase) ||
        !reader.U8(exitStyle) ||
        exitStyle > static_cast<uint8_t>(EnemyExitStyle::RearRetreat) ||
        !reader.F32(value.slotSpacing) ||
        !reader.F32(value.verticalSpacing) ||
        !reader.F32(value.cohesionResponse) ||
        !reader.F32(value.maximumCorrection) ||
        !reader.F32(value.attackStaggerSeconds) ||
        !reader.F32(value.entranceDurationSeconds) ||
        !reader.F32(value.entranceStaggerSeconds) ||
        !reader.F32(value.entranceForwardDistance) ||
        !reader.F32(value.entranceSideDistance) ||
        !reader.F32(value.exitDurationSeconds) ||
        !reader.F32(value.exitForwardDistance) ||
        !reader.F32(value.exitSideDistance) ||
        !reader.Bool(value.preserveAuthoredSlots) ||
        !reader.Bool(value.commercialFormation)) {
        return false;
    }
    value.pattern = static_cast<EnemyFormationPattern>(pattern);
    value.entranceStyle = static_cast<EnemyEntranceStyle>(entranceStyle);
    value.exitStyle = static_cast<EnemyExitStyle>(exitStyle);
    return value.definitionId.empty() || value.Validate(nullptr);
}

void WriteProjectileDefinition(
    BinaryWriter& writer,
    const EnemyProjectileDefinitionAsset& value) {
    writer.String(value.id);
    writer.String(value.displayName);
    writer.U8(static_cast<uint8_t>(value.trajectory));
    writer.F32(value.initialSpeed);
    writer.F32(value.acceleration);
    writer.F32(value.maximumSpeed);
    writer.F32(value.homingTurnRateRadians);
    writer.F32(value.predictionScale);
    writer.F32(value.maximumPredictionSeconds);
    writer.F32(value.arcGravity);
    writer.F32(value.radius);
    writer.F32(value.lifetime);
    writer.F32(value.damage);
    WriteVector4(writer, value.color);
    writer.String(value.trailEffectId);
    writer.String(value.impactEffectId);
    writer.U32(static_cast<uint32_t>(value.defenseResponses));
    writer.F32(value.shootDownHitPoints);
    writer.F32(value.shootDownRadiusScale);
}

bool ReadProjectileDefinition(
    BinaryReader& reader,
    EnemyProjectileDefinitionAsset& value) {
    uint8_t trajectory = 0;
    uint32_t defenseResponses = 0;
    if (!reader.String(value.id) || !reader.String(value.displayName) ||
        !reader.U8(trajectory) ||
        trajectory > static_cast<uint8_t>(EnemyProjectileTrajectory::Arc) ||
        !reader.F32(value.initialSpeed) ||
        !reader.F32(value.acceleration) ||
        !reader.F32(value.maximumSpeed) ||
        !reader.F32(value.homingTurnRateRadians) ||
        !reader.F32(value.predictionScale) ||
        !reader.F32(value.maximumPredictionSeconds) ||
        !reader.F32(value.arcGravity) ||
        !reader.F32(value.radius) ||
        !reader.F32(value.lifetime) ||
        !reader.F32(value.damage) ||
        !ReadVector4(reader, value.color) ||
        !reader.String(value.trailEffectId) ||
        !reader.String(value.impactEffectId) ||
        !reader.U32(defenseResponses) ||
        !reader.F32(value.shootDownHitPoints) ||
        !reader.F32(value.shootDownRadiusScale)) {
        return false;
    }
    value.trajectory = static_cast<EnemyProjectileTrajectory>(trajectory);
    value.defenseResponses = static_cast<EnemyAttackDefenseResponse>(
        defenseResponses);
    return value.id.empty() || value.Validate(nullptr);
}

void WriteActorDesc(BinaryWriter& writer, const CourseEnemyActorDesc& value) {
    writer.String(value.waveId);
    writer.String(value.sourcePlacementGuid);
    writer.String(value.actorAssetId);
    writer.String(value.meshId);
    writer.String(value.bulletPatternId);
    writer.String(value.projectileDefinitionId);
    writer.String(value.role);
    writer.F32(value.spawnDistance);
    writer.F32(value.distanceOffset);
    writer.F32(value.lateralOffset);
    writer.F32(value.verticalOffset);
    writer.F32(value.forwardSpeed);
    writer.F32(value.radius);
    writer.F32(value.lifetime);
    writer.F32(value.hitPoints);
    writer.F32(value.fireInterval);
    writer.F32(value.firstShotDelay);
    writer.F32(value.bulletSpeed);
    writer.I32(value.bulletCount);
    writer.F32(value.bulletLateralSpreadSpeed);
    writer.F32(value.bulletVerticalSpreadSpeed);
    writer.F32(value.bulletRadius);
    writer.F32(value.bulletLifetime);
    writer.F32(value.bulletDamage);
    WriteVector4(writer, value.bulletColor);
    WriteProjectileDefinition(writer, value.projectileDefinition);
    writer.U32(static_cast<uint32_t>(value.firePattern));
    WriteVector4(writer, value.color);
    WriteVector3(writer, value.localRotation);
    WriteVector3(writer, value.localScale);
    WriteBehaviorDefinition(writer, value.behaviorDefinition);
    WriteFormationDefinition(writer, value.formationDefinition);
    writer.Bool(value.previewOnly);
    writer.Bool(value.suppressFire);
    writer.Bool(value.energyShield.enabled);
    writer.F32(value.energyShield.maximumHitPoints);
}

bool ReadActorDesc(BinaryReader& reader, CourseEnemyActorDesc& value) {
    int32_t bulletCount = 0;
    uint32_t firePattern = 0;
    if (!reader.String(value.waveId) ||
        !reader.String(value.sourcePlacementGuid) ||
        !reader.String(value.actorAssetId) ||
        !reader.String(value.meshId) ||
        !reader.String(value.bulletPatternId) ||
        !reader.String(value.projectileDefinitionId) ||
        !reader.String(value.role) ||
        !reader.F32(value.spawnDistance) ||
        !reader.F32(value.distanceOffset) ||
        !reader.F32(value.lateralOffset) ||
        !reader.F32(value.verticalOffset) ||
        !reader.F32(value.forwardSpeed) ||
        !reader.F32(value.radius) ||
        !reader.F32(value.lifetime) ||
        !reader.F32(value.hitPoints) ||
        !reader.F32(value.fireInterval) ||
        !reader.F32(value.firstShotDelay) ||
        !reader.F32(value.bulletSpeed) ||
        !reader.I32(bulletCount) ||
        !reader.F32(value.bulletLateralSpreadSpeed) ||
        !reader.F32(value.bulletVerticalSpreadSpeed) ||
        !reader.F32(value.bulletRadius) ||
        !reader.F32(value.bulletLifetime) ||
        !reader.F32(value.bulletDamage) ||
        !ReadVector4(reader, value.bulletColor) ||
        !ReadProjectileDefinition(reader, value.projectileDefinition) ||
        !reader.U32(firePattern) ||
        !ReadVector4(reader, value.color) ||
        !ReadVector3(reader, value.localRotation) ||
        !ReadVector3(reader, value.localScale) ||
        !ReadBehaviorDefinition(reader, value.behaviorDefinition) ||
        !ReadFormationDefinition(reader, value.formationDefinition) ||
        !reader.Bool(value.previewOnly) ||
        !reader.Bool(value.suppressFire) ||
        !reader.Bool(value.energyShield.enabled) ||
        !reader.F32(value.energyShield.maximumHitPoints) || !value.energyShield.Validate()) {
        return false;
    }
    if (firePattern > static_cast<uint32_t>(CourseEnemyFirePattern::BossArc)) return false;
    value.bulletCount = bulletCount;
    value.firePattern = static_cast<CourseEnemyFirePattern>(firePattern);
    return true;
}

bool IsFiniteVector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

const CourseRuntimeWaveNode* CourseRuntimeProgramAsset::FindWave(
    std::string_view guid) const {
    const auto found = std::find_if(waves.begin(), waves.end(), [&](const auto& value) {
        return value.waveGuid == guid;
    });
    return found != waves.end() ? &*found : nullptr;
}

const CourseRuntimeActorRecord* CourseRuntimeProgramAsset::FindActor(
    std::string_view placementGuid) const {
    const auto found = std::find_if(actors.begin(), actors.end(), [&](const auto& value) {
        return value.placementGuid == placementGuid;
    });
    return found != actors.end() ? &*found : nullptr;
}

bool CourseRuntimeProgramAsset::Validate(std::string* errorMessage) const {
    const auto fail = [errorMessage](std::string message) {
        if (errorMessage != nullptr) *errorMessage = std::move(message);
        return false;
    };
    if (formatVersion != kCourseRuntimeProgramFormatVersion) {
        return fail("Unsupported Course runtime program format version.");
    }
    if (schemaVersion != kCourseRuntimeAuthoringSchemaVersion) {
        return fail("Course runtime program was cooked from an unsupported authoring schema.");
    }
    if (compilerVersion != kCourseRuntimeCompilerVersion) {
        return fail("Course runtime program compiler version is stale.");
    }
    if (sourceCourseName.empty() || sourceAssetHash == 0 || sourceFingerprint == 0) {
        return fail("Course runtime program identity is incomplete.");
    }
    if (!std::isfinite(railLength) || railLength <= 0.0f) {
        return fail("Course runtime program RailPath length is invalid.");
    }
    std::unordered_set<std::string> waveGuids;
    for (std::size_t index = 0; index < waves.size(); ++index) {
        const CourseRuntimeWaveNode& wave = waves[index];
        if (wave.waveGuid.empty() || !waveGuids.insert(wave.waveGuid).second) {
            return fail("Course runtime program contains an empty or duplicate Wave GUID.");
        }
        if (!std::isfinite(wave.triggerRailDistance) ||
            wave.triggerRailDistance < 0.0f || wave.triggerRailDistance > railLength ||
            !std::isfinite(wave.prewarmDistance) || wave.prewarmDistance < 0.0f ||
            !std::isfinite(wave.timeoutSeconds) || wave.timeoutSeconds < 0.0f) {
            return fail("Course runtime program contains invalid Wave timing data.");
        }
        if (wave.nextWaveIndex < -1 ||
            wave.nextWaveIndex >= static_cast<int32_t>(waves.size())) {
            return fail("Course runtime program contains an invalid Wave transition index.");
        }
        for (const uint32_t actorIndex : wave.actorIndices) {
            if (actorIndex >= actors.size() || actors[actorIndex].waveIndex != index) {
                return fail("Course runtime Wave contains an invalid Actor index.");
            }
        }
    }
    std::unordered_set<std::string> actorGuids;
    for (const CourseRuntimeActorRecord& actor : actors) {
        if (actor.placementGuid.empty() || !actorGuids.insert(actor.placementGuid).second ||
            actor.waveIndex >= waves.size() ||
            actor.waveGuid != waves[actor.waveIndex].waveGuid ||
            actor.actor.sourcePlacementGuid != actor.placementGuid) {
            return fail("Course runtime program contains an invalid Actor identity or Wave link.");
        }
        if (!IsFiniteVector(actor.authoredRotation) || !IsFiniteVector(actor.authoredScale) ||
            actor.authoredScale.x <= 0.0f || actor.authoredScale.y <= 0.0f ||
            actor.authoredScale.z <= 0.0f ||
            !std::isfinite(actor.actor.spawnDistance) ||
            actor.actor.spawnDistance < 0.0f || actor.actor.spawnDistance > railLength ||
            !std::isfinite(actor.actor.radius) || actor.actor.radius <= 0.0f ||
            !std::isfinite(actor.actor.hitPoints) || actor.actor.hitPoints <= 0.0f || !actor.actor.energyShield.Validate()) {
            return fail("Course runtime program contains invalid Actor transform or combat data.");
        }
        if (!actor.actor.behaviorDefinition.definitionId.empty() &&
            !actor.actor.behaviorDefinition.Validate(nullptr)) {
            return fail("Course runtime program contains invalid Actor behavior data.");
        }
        if (!actor.actor.formationDefinition.definitionId.empty() &&
            !actor.actor.formationDefinition.Validate(nullptr)) {
            return fail("Course runtime program contains invalid Actor formation data.");
        }
        if (!actor.actor.projectileDefinition.id.empty() &&
            !actor.actor.projectileDefinition.Validate(nullptr)) {
            return fail("Course runtime program contains invalid Actor projectile data.");
        }
    }
    return true;
}

bool CourseRuntimeProgramAsset::IsSourceCurrent(uint64_t expectedSourceHash) const noexcept {
    return expectedSourceHash != 0 && sourceAssetHash == expectedSourceHash &&
        formatVersion == kCourseRuntimeProgramFormatVersion &&
        compilerVersion == kCourseRuntimeCompilerVersion &&
        schemaVersion == kCourseRuntimeAuthoringSchemaVersion;
}

bool CourseRuntimeProgramAsset::AreDependenciesCurrent() const {
    for (const auto& dependency : dependencies) {
        std::ifstream file(dependency.sourcePath, std::ios::binary);
        if (!file.is_open()) return false;
        const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if ((!file.good() && !file.eof()) || dependency.contentHash == 0 ||
            ComputeCourseRuntimeFileHash(bytes) != dependency.contentHash) return false;
    }
    return true;
}

bool CourseRuntimeProgramAsset::SaveToString(
    std::string* bytes,
    std::string* errorMessage) const {
    if (bytes == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "Course runtime output buffer is null.";
        return false;
    }
    if (!Validate(errorMessage)) return false;
    BinaryWriter writer;
    writer.Bytes(kMagic, sizeof(kMagic));
    writer.U32(formatVersion);
    writer.U32(schemaVersion);
    writer.U32(compilerVersion);
    writer.U8(static_cast<uint8_t>(buildConfiguration));
    writer.String(sourceCourseName);
    writer.U64(sourceAssetHash);
    writer.U64(sourceFingerprint);
    writer.F32(railLength);

    writer.U32(static_cast<uint32_t>(dependencies.size()));
    for (const CourseRuntimeProgramDependency& dependency : dependencies) {
        writer.U8(static_cast<uint8_t>(dependency.kind));
        writer.String(dependency.id);
        writer.String(dependency.sourcePath);
        writer.U64(dependency.contentHash);
    }
    writer.U32(static_cast<uint32_t>(diagnostics.size()));
    for (const CourseRuntimeProgramDiagnostic& diagnostic : diagnostics) {
        writer.U8(static_cast<uint8_t>(diagnostic.severity));
        writer.String(diagnostic.code);
        writer.String(diagnostic.objectGuid);
        writer.String(diagnostic.message);
    }
    writer.U32(static_cast<uint32_t>(waves.size()));
    for (const CourseRuntimeWaveNode& wave : waves) {
        writer.String(wave.waveGuid);
        writer.String(wave.displayName);
        writer.F32(wave.triggerRailDistance);
        writer.F32(wave.prewarmDistance);
        writer.F32(wave.timeoutSeconds);
        writer.U32(static_cast<uint32_t>(wave.completionCondition));
        writer.U32(static_cast<uint32_t>(wave.executionPolicy));
        writer.I32(wave.nextWaveIndex);
        writer.String(wave.triggerEventId);
        writer.U32(static_cast<uint32_t>(wave.actorIndices.size()));
        for (const uint32_t actorIndex : wave.actorIndices) writer.U32(actorIndex);
        writer.Bool(wave.enabled);
    }
    writer.U32(static_cast<uint32_t>(actors.size()));
    for (const CourseRuntimeActorRecord& actor : actors) {
        writer.String(actor.placementGuid);
        writer.String(actor.waveGuid);
        writer.U32(actor.waveIndex);
        WriteActorDesc(writer, actor.actor);
        WriteVector3(writer, actor.authoredRotation);
        WriteVector3(writer, actor.authoredScale);
        writer.Bool(actor.actorAssetResolved);
        writer.Bool(actor.enabled);
    }
    *bytes = writer.Take();
    return true;
}

bool CourseRuntimeProgramAsset::LoadFromString(
    std::string_view bytes,
    std::string* errorMessage) {
    CourseRuntimeProgramAsset loaded{};
    BinaryReader reader(bytes);
    char magic[sizeof(kMagic)]{};
    uint8_t configuration = 0;
    uint32_t dependencyCount = 0;
    uint32_t diagnosticCount = 0;
    uint32_t waveCount = 0;
    uint32_t actorCount = 0;
    if (!reader.Bytes(magic, sizeof(magic)) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0 ||
        !reader.U32(loaded.formatVersion) ||
        !reader.U32(loaded.schemaVersion) ||
        !reader.U32(loaded.compilerVersion) ||
        !reader.U8(configuration) ||
        configuration > static_cast<uint8_t>(CourseRuntimeBuildConfiguration::Release) ||
        !reader.String(loaded.sourceCourseName) ||
        !reader.U64(loaded.sourceAssetHash) ||
        !reader.U64(loaded.sourceFingerprint) ||
        !reader.F32(loaded.railLength) ||
        !reader.Count(dependencyCount)) {
        if (errorMessage != nullptr) {
            *errorMessage = reader.Error().empty()
                ? "Course runtime program header is invalid." : reader.Error();
        }
        return false;
    }
    loaded.buildConfiguration = static_cast<CourseRuntimeBuildConfiguration>(configuration);
    loaded.dependencies.resize(dependencyCount);
    for (auto& dependency : loaded.dependencies) {
        uint8_t kind = 0;
        if (!reader.U8(kind) ||
            kind > static_cast<uint8_t>(CourseRuntimeDependencyKind::EnemyProjectile) ||
            !reader.String(dependency.id) ||
            !reader.String(dependency.sourcePath) ||
            !reader.U64(dependency.contentHash)) {
            if (errorMessage != nullptr) *errorMessage = "Course runtime dependency table is invalid.";
            return false;
        }
        dependency.kind = static_cast<CourseRuntimeDependencyKind>(kind);
    }
    if (!reader.Count(diagnosticCount)) {
        if (errorMessage != nullptr) *errorMessage = reader.Error();
        return false;
    }
    loaded.diagnostics.resize(diagnosticCount);
    for (auto& diagnostic : loaded.diagnostics) {
        uint8_t severity = 0;
        if (!reader.U8(severity) ||
            severity > static_cast<uint8_t>(CourseRuntimeProgramDiagnosticSeverity::Error) ||
            !reader.String(diagnostic.code) ||
            !reader.String(diagnostic.objectGuid) ||
            !reader.String(diagnostic.message)) {
            if (errorMessage != nullptr) *errorMessage = "Course runtime diagnostic table is invalid.";
            return false;
        }
        diagnostic.severity = static_cast<CourseRuntimeProgramDiagnosticSeverity>(severity);
    }
    if (!reader.Count(waveCount)) {
        if (errorMessage != nullptr) *errorMessage = reader.Error();
        return false;
    }
    loaded.waves.resize(waveCount);
    for (CourseRuntimeWaveNode& wave : loaded.waves) {
        uint32_t completion = 0;
        uint32_t policy = 0;
        uint32_t indexCount = 0;
        if (!reader.String(wave.waveGuid) || !reader.String(wave.displayName) ||
            !reader.F32(wave.triggerRailDistance) || !reader.F32(wave.prewarmDistance) ||
            !reader.F32(wave.timeoutSeconds) || !reader.U32(completion) ||
            !reader.U32(policy) || !reader.I32(wave.nextWaveIndex) ||
            !reader.String(wave.triggerEventId) || !reader.Count(indexCount) ||
            completion > static_cast<uint32_t>(CourseWaveCompletionCondition::ScriptedEvent) ||
            policy > static_cast<uint32_t>(CourseWaveExecutionPolicy::Exclusive)) {
            if (errorMessage != nullptr) *errorMessage = "Course runtime Wave table is invalid.";
            return false;
        }
        wave.completionCondition = static_cast<CourseWaveCompletionCondition>(completion);
        wave.executionPolicy = static_cast<CourseWaveExecutionPolicy>(policy);
        wave.actorIndices.resize(indexCount);
        for (uint32_t& index : wave.actorIndices) {
            if (!reader.U32(index)) {
                if (errorMessage != nullptr) *errorMessage = reader.Error();
                return false;
            }
        }
        if (!reader.Bool(wave.enabled)) {
            if (errorMessage != nullptr) *errorMessage = reader.Error();
            return false;
        }
    }
    if (!reader.Count(actorCount)) {
        if (errorMessage != nullptr) *errorMessage = reader.Error();
        return false;
    }
    loaded.actors.resize(actorCount);
    for (CourseRuntimeActorRecord& actor : loaded.actors) {
        if (!reader.String(actor.placementGuid) || !reader.String(actor.waveGuid) ||
            !reader.U32(actor.waveIndex) || !ReadActorDesc(reader, actor.actor) ||
            !ReadVector3(reader, actor.authoredRotation) ||
            !ReadVector3(reader, actor.authoredScale) ||
            !reader.Bool(actor.actorAssetResolved) || !reader.Bool(actor.enabled)) {
            if (errorMessage != nullptr) {
                *errorMessage = reader.Error().empty()
                    ? "Course runtime Actor table is invalid." : reader.Error();
            }
            return false;
        }
    }
    if (!reader.AtEnd()) {
        if (errorMessage != nullptr) *errorMessage = "Course runtime program has trailing data.";
        return false;
    }
    if (!loaded.Validate(errorMessage)) return false;
    *this = std::move(loaded);
    return true;
}

bool CourseRuntimeProgramAsset::SaveToFile(
    const std::string& path,
    std::string* errorMessage) const {
    std::string bytes;
    if (!SaveToString(&bytes, errorMessage)) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (errorMessage != nullptr) *errorMessage = "Could not write Course runtime program: " + path;
        return false;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) {
        if (errorMessage != nullptr) *errorMessage = "Failed while writing Course runtime program: " + path;
        return false;
    }
    return true;
}

bool CourseRuntimeProgramAsset::LoadFromFile(
    const std::string& path,
    std::string* errorMessage) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (errorMessage != nullptr) *errorMessage = "Could not open Course runtime program: " + path;
        return false;
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (!file.good() && !file.eof()) {
        if (errorMessage != nullptr) *errorMessage = "Failed while reading Course runtime program: " + path;
        return false;
    }
    return LoadFromString(bytes, errorMessage);
}

uint64_t ComputeCourseRuntimeFileHash(std::string_view bytes) noexcept {
    uint64_t hash = kFnvOffset;
    for (const unsigned char value : bytes) {
        hash ^= value;
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t ComputeCourseAssetSourceHash(
    const CourseAsset& source,
    std::string* errorMessage) {
    CourseAsset canonical = source;
    canonical.SortForRuntime();
    std::string text;
    if (!canonical.SaveToString(&text, errorMessage)) return 0;
    return ComputeCourseRuntimeFileHash(text);
}

const char* ToString(CourseRuntimeBuildConfiguration configuration) {
    switch (configuration) {
    case CourseRuntimeBuildConfiguration::Debug: return "Debug";
    case CourseRuntimeBuildConfiguration::Release: return "Release";
    }
    return "Unknown";
}

const char* ToString(CourseRuntimeDependencyKind kind) {
    switch (kind) {
    case CourseRuntimeDependencyKind::ActorAsset: return "ActorAsset";
    case CourseRuntimeDependencyKind::BulletPattern: return "BulletPattern";
    case CourseRuntimeDependencyKind::EnemyProjectile: return "EnemyProjectile";
    }
    return "Unknown";
}

const char* ToString(CourseRuntimeProgramDiagnosticSeverity severity) {
    switch (severity) {
    case CourseRuntimeProgramDiagnosticSeverity::Info: return "Info";
    case CourseRuntimeProgramDiagnosticSeverity::Warning: return "Warning";
    case CourseRuntimeProgramDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}
