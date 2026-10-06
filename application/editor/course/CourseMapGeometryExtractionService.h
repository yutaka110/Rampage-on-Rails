#pragma once

#include "CourseMapVisualAsset.h"
#include "CourseTerrainProductionMeshResolver.h"
#include "../EditorAssetRegistry.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace editor {

struct CourseMapExtractedGeometry final {
    std::string stableId;
    std::string sourceMeshId;
    CourseMapVisualLayer layer = CourseMapVisualLayer::HeroLandmark;
    std::vector<Vector3> worldVertices;
    std::vector<uint32_t> indices;
    Vector3 worldMinimum{};
    Vector3 worldMaximum{};
    uint64_t sourceGeometryHash = 0;
    CourseTerrainMeshResolutionSource meshResolutionSource =
        CourseTerrainMeshResolutionSource::None;
    bool exactSourceGeometry = false;
    bool locked = false;
};

struct CourseMapGeometryExtractionSettings final {
    bool allowBoxFallback = true;
    uint32_t maximumVerticesPerSource = 65535;
    uint32_t maximumTrianglesPerSource = 131072;
    uint32_t maximumSources = 16384;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapGeometryExtractionInput final {
    const CourseMapVisualAsset* visualAsset = nullptr;
    const EditorAssetRegistry* assets = nullptr;
};

struct CourseMapGeometryExtractionStats final {
    uint32_t requestedSources = 0;
    uint32_t exactSources = 0;
    uint32_t productionAssetSources = 0;
    uint32_t importedAuthoringSources = 0;
    uint32_t fallbackSources = 0;
    uint32_t skippedSources = 0;
    uint32_t resolverCacheHits = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
};

struct CourseMapGeometryExtractionResult final {
    bool succeeded = false;
    bool usedFallback = false;
    uint64_t geometryFingerprint = 0;
    std::vector<CourseMapExtractedGeometry> sources;
    CourseMapGeometryExtractionStats stats{};
    std::vector<std::string> diagnostics;
    std::string message;
};

// Read-only bridge from Course Map visual placements to authoring mesh data.
// Production Mesh vertices are normalized into the oriented placement volume;
// unresolved legacy mesh ids retain the explicit box proxy as migration data.
class CourseMapGeometryExtractionService final {
public:
    explicit CourseMapGeometryExtractionService(
        std::filesystem::path projectRoot = std::filesystem::current_path());

    CourseMapGeometryExtractionResult Extract(
        const CourseMapGeometryExtractionInput& input) const;

    bool SetSettings(CourseMapGeometryExtractionSettings settings, std::string* errorMessage = nullptr);
    const CourseMapGeometryExtractionSettings& Settings() const noexcept {
        return settings_;
    }
    const std::filesystem::path& ProjectRoot() const noexcept {
        return projectRoot_;
    }

private:
    bool ExtractProductionMesh(
        const CourseMapVisualPrimitive& primitive,
        const EditorAssetRegistry& assets,
        CourseMapExtractedGeometry& output,
        std::string& diagnostic,
        bool* resolverCacheHit = nullptr) const;
    bool ExtractFallbackBox(
        const CourseMapVisualPrimitive& primitive,
        CourseMapExtractedGeometry& output) const;

    std::filesystem::path projectRoot_;
    CourseMapGeometryExtractionSettings settings_{};
    mutable CourseTerrainProductionMeshResolver meshResolver_;
};

} // namespace editor
