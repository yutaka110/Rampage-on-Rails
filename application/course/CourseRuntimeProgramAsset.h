#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "CourseAsset.h"
#include "CourseSpawnRuntime.h"

inline constexpr uint32_t kCourseRuntimeProgramFormatVersion = 7;
inline constexpr uint32_t kCourseRuntimeCompilerVersion = 7;
inline constexpr uint32_t kCourseRuntimeAuthoringSchemaVersion = 7;

enum class CourseRuntimeBuildConfiguration : uint8_t {
    Debug,
    Release,
};

enum class CourseRuntimeDependencyKind : uint8_t {
    ActorAsset,
    BulletPattern,
    EnemyProjectile,
};

enum class CourseRuntimeProgramDiagnosticSeverity : uint8_t {
    Info,
    Warning,
    Error,
};

struct CourseRuntimeProgramDependency final {
    CourseRuntimeDependencyKind kind = CourseRuntimeDependencyKind::ActorAsset;
    std::string id;
    std::string sourcePath;
    uint64_t contentHash = 0;
};

struct CourseRuntimeProgramDiagnostic final {
    CourseRuntimeProgramDiagnosticSeverity severity =
        CourseRuntimeProgramDiagnosticSeverity::Info;
    std::string code;
    std::string objectGuid;
    std::string message;
};

struct CourseRuntimeActorRecord final {
    std::string placementGuid;
    std::string waveGuid;
    uint32_t waveIndex = 0;
    CourseEnemyActorDesc actor{};
    Vector3 authoredRotation{};
    Vector3 authoredScale{1.0f, 1.0f, 1.0f};
    bool actorAssetResolved = false;
    bool enabled = true;
};

struct CourseRuntimeWaveNode final {
    std::string waveGuid;
    std::string displayName;
    float triggerRailDistance = 0.0f;
    float prewarmDistance = 0.0f;
    float timeoutSeconds = 0.0f;
    CourseWaveCompletionCondition completionCondition =
        CourseWaveCompletionCondition::AllEnemiesDefeated;
    CourseWaveExecutionPolicy executionPolicy =
        CourseWaveExecutionPolicy::Parallel;
    int32_t nextWaveIndex = -1;
    std::string triggerEventId;
    std::vector<uint32_t> actorIndices;
    bool enabled = true;
};

// Cooked, runtime-only representation of one schema-v7 CourseAsset. The file
// is deterministic and self-contained: gameplay never needs authoring models
// or source Actor/BulletPattern assets after this boundary.
struct CourseRuntimeProgramAsset final {
    uint32_t formatVersion = kCourseRuntimeProgramFormatVersion;
    uint32_t schemaVersion = kCourseRuntimeAuthoringSchemaVersion;
    uint32_t compilerVersion = kCourseRuntimeCompilerVersion;
    CourseRuntimeBuildConfiguration buildConfiguration =
        CourseRuntimeBuildConfiguration::Debug;
    std::string sourceCourseName;
    uint64_t sourceAssetHash = 0;
    uint64_t sourceFingerprint = 0;
    float railLength = 0.0f;
    std::vector<CourseRuntimeProgramDependency> dependencies;
    std::vector<CourseRuntimeProgramDiagnostic> diagnostics;
    std::vector<CourseRuntimeWaveNode> waves;
    std::vector<CourseRuntimeActorRecord> actors;

    const CourseRuntimeWaveNode* FindWave(std::string_view guid) const;
    const CourseRuntimeActorRecord* FindActor(std::string_view placementGuid) const;

    bool Validate(std::string* errorMessage = nullptr) const;
    bool IsSourceCurrent(uint64_t expectedSourceHash) const noexcept;
    bool AreDependenciesCurrent() const;
    bool SaveToString(std::string* bytes, std::string* errorMessage = nullptr) const;
    bool LoadFromString(std::string_view bytes, std::string* errorMessage = nullptr);
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
};

uint64_t ComputeCourseAssetSourceHash(
    const CourseAsset& source,
    std::string* errorMessage = nullptr);
uint64_t ComputeCourseRuntimeFileHash(std::string_view bytes) noexcept;
const char* ToString(CourseRuntimeBuildConfiguration configuration);
const char* ToString(CourseRuntimeDependencyKind kind);
const char* ToString(CourseRuntimeProgramDiagnosticSeverity severity);
