#include "CourseMapGeometryExtractionService.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

namespace editor {
namespace {

Vector3 Add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 Scale(Vector3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashString(uint64_t hash, std::string_view value) {
    return HashBytes(hash, value.data(), value.size());
}

void ResolveBounds(CourseMapExtractedGeometry& source) {
    const float maximum = (std::numeric_limits<float>::max)();
    source.worldMinimum = {maximum, maximum, maximum};
    source.worldMaximum = {-maximum, -maximum, -maximum};
    for (const Vector3& point : source.worldVertices) {
        source.worldMinimum.x = (std::min)(source.worldMinimum.x, point.x);
        source.worldMinimum.y = (std::min)(source.worldMinimum.y, point.y);
        source.worldMinimum.z = (std::min)(source.worldMinimum.z, point.z);
        source.worldMaximum.x = (std::max)(source.worldMaximum.x, point.x);
        source.worldMaximum.y = (std::max)(source.worldMaximum.y, point.y);
        source.worldMaximum.z = (std::max)(source.worldMaximum.z, point.z);
    }
}

Vector3 SafeNormalizedPosition(Vector3 position, Vector3 minimum, Vector3 maximum) {
    const auto axis = [](float value, float low, float high) {
        const float extent = high - low;
        return std::abs(extent) > 0.00001f
            ? ((value - low) / extent) * 2.0f - 1.0f : 0.0f;
    };
    return {axis(position.x, minimum.x, maximum.x),
        axis(position.y, minimum.y, maximum.y),
        axis(position.z, minimum.z, maximum.z)};
}

} // namespace

CourseMapGeometryExtractionService::CourseMapGeometryExtractionService(
    std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)), meshResolver_(projectRoot_) {}

CourseMapGeometryExtractionResult CourseMapGeometryExtractionService::Extract(
    const CourseMapGeometryExtractionInput& input) const {
    CourseMapGeometryExtractionResult result{};
    if (input.visualAsset == nullptr) {
        result.message = "Course Map geometry extraction requires a visual asset.";
        return result;
    }
    std::string validationError;
    if (!input.visualAsset->Validate(&validationError)) {
        result.message = "Course Map visual source is invalid: " + validationError;
        return result;
    }
    result.stats.requestedSources = static_cast<uint32_t>(
        (std::min)(input.visualAsset->primitives.size(),
            static_cast<std::size_t>(settings_.maximumSources)));
    result.sources.reserve(result.stats.requestedSources);
    for (std::size_t i = 0;
         i < input.visualAsset->primitives.size() &&
             result.sources.size() < settings_.maximumSources;
         ++i) {
        const CourseMapVisualPrimitive& primitive =
            input.visualAsset->primitives[i];
        CourseMapExtractedGeometry source{};
        std::string diagnostic;
        bool resolverCacheHit = false;
        bool extracted = input.assets != nullptr &&
            ExtractProductionMesh(primitive, *input.assets, source, diagnostic,
                &resolverCacheHit);
        if (resolverCacheHit) ++result.stats.resolverCacheHits;
        if (extracted) {
            ++result.stats.exactSources;
            if (source.meshResolutionSource ==
                CourseTerrainMeshResolutionSource::ProductionAsset) {
                ++result.stats.productionAssetSources;
            } else if (source.meshResolutionSource ==
                CourseTerrainMeshResolutionSource::ImportedAuthoringSource) {
                ++result.stats.importedAuthoringSources;
            }
        } else if (settings_.allowBoxFallback &&
            ExtractFallbackBox(primitive, source)) {
            extracted = true;
            result.usedFallback = true;
            ++result.stats.fallbackSources;
            if (!diagnostic.empty()) {
                result.diagnostics.push_back(primitive.stableId + ": " + diagnostic);
            }
        } else {
            ++result.stats.skippedSources;
            result.diagnostics.push_back(primitive.stableId + ": " +
                (diagnostic.empty() ? "No extractable geometry." : diagnostic));
        }
        if (!extracted) continue;
        result.stats.vertices += static_cast<uint32_t>(source.worldVertices.size());
        result.stats.triangles += static_cast<uint32_t>(source.indices.size() / 3u);
        result.sources.push_back(std::move(source));
    }
    if (result.sources.empty()) {
        result.message = "Course Map geometry extraction produced no sources.";
        return result;
    }
    uint64_t fingerprint = 1469598103934665603ull;
    fingerprint = HashValue(fingerprint, input.visualAsset->sourceFingerprint);
    for (const CourseMapExtractedGeometry& source : result.sources) {
        fingerprint = HashString(fingerprint, source.stableId);
        fingerprint = HashString(fingerprint, source.sourceMeshId);
        fingerprint = HashValue(fingerprint, source.sourceGeometryHash);
        fingerprint = HashValue(fingerprint, source.exactSourceGeometry);
        for (const Vector3& point : source.worldVertices) {
            fingerprint = HashValue(fingerprint, point);
        }
        for (uint32_t index : source.indices) {
            fingerprint = HashValue(fingerprint, index);
        }
    }
    result.geometryFingerprint = fingerprint;
    result.succeeded = true;
    result.message = "Course Map geometry extracted: " +
        std::to_string(result.stats.exactSources) + " exact, " +
        std::to_string(result.stats.productionAssetSources) + " Production Assets, " +
        std::to_string(result.stats.importedAuthoringSources) + " imported sources, " +
        std::to_string(result.stats.fallbackSources) + " fallback sources.";
    return result;
}

bool CourseMapGeometryExtractionService::ExtractProductionMesh(
    const CourseMapVisualPrimitive& primitive,
    const EditorAssetRegistry& assets,
    CourseMapExtractedGeometry& output,
    std::string& diagnostic,
    bool* resolverCacheHit) const {
    const CourseTerrainProductionMeshResolution& resolution =
        meshResolver_.Resolve(assets, primitive.sourceMeshId, resolverCacheHit);
    if (!resolution.Resolved()) {
        diagnostic = resolution.message.empty()
            ? "Production Mesh reference was not resolved; using placement fallback."
            : resolution.message;
        return false;
    }
    const EditorGeometryMesh& mesh = resolution.geometry;
    if (mesh.vertices.empty() || mesh.triangles.empty() ||
        mesh.vertices.size() > settings_.maximumVerticesPerSource ||
        mesh.triangles.size() > settings_.maximumTrianglesPerSource ||
        primitive.worldCorners.size() < 8u) {
        diagnostic = "Production Mesh exceeds extraction limits or placement basis is incomplete.";
        return false;
    }
    Vector3 localMinimum = mesh.vertices.front().position;
    Vector3 localMaximum = localMinimum;
    for (const EditorGeometryVertex& vertex : mesh.vertices) {
        localMinimum.x = (std::min)(localMinimum.x, vertex.position.x);
        localMinimum.y = (std::min)(localMinimum.y, vertex.position.y);
        localMinimum.z = (std::min)(localMinimum.z, vertex.position.z);
        localMaximum.x = (std::max)(localMaximum.x, vertex.position.x);
        localMaximum.y = (std::max)(localMaximum.y, vertex.position.y);
        localMaximum.z = (std::max)(localMaximum.z, vertex.position.z);
    }
    const Vector3 axisX = Scale(Subtract(
        primitive.worldCorners[1], primitive.worldCorners[0]), 0.5f);
    const Vector3 axisY = Scale(Subtract(
        primitive.worldCorners[2], primitive.worldCorners[0]), 0.5f);
    const Vector3 axisZ = Scale(Subtract(
        primitive.worldCorners[4], primitive.worldCorners[0]), 0.5f);
    output.stableId = primitive.stableId;
    output.sourceMeshId = primitive.sourceMeshId;
    output.layer = primitive.layer;
    output.sourceGeometryHash = resolution.sourceGeometryHash;
    output.meshResolutionSource = resolution.source;
    output.exactSourceGeometry = true;
    output.locked = primitive.locked;
    output.worldVertices.reserve(mesh.vertices.size());
    for (const EditorGeometryVertex& vertex : mesh.vertices) {
        const Vector3 normalized = SafeNormalizedPosition(
            vertex.position, localMinimum, localMaximum);
        Vector3 world = primitive.worldCenter;
        world = Add(world, Scale(axisX, normalized.x));
        world = Add(world, Scale(axisY, normalized.y));
        world = Add(world, Scale(axisZ, normalized.z));
        output.worldVertices.push_back(world);
    }
    output.indices.reserve(mesh.triangles.size() * 3u);
    for (const EditorGeometryTriangle& triangle : mesh.triangles) {
        output.indices.push_back(triangle.vertices[0]);
        output.indices.push_back(triangle.vertices[1]);
        output.indices.push_back(triangle.vertices[2]);
    }
    ResolveBounds(output);
    return true;
}

bool CourseMapGeometryExtractionService::ExtractFallbackBox(
    const CourseMapVisualPrimitive& primitive,
    CourseMapExtractedGeometry& output) const {
    if (primitive.worldCorners.size() < 8u ||
        primitive.worldCorners.size() > settings_.maximumVerticesPerSource ||
        settings_.maximumTrianglesPerSource < 12u) return false;
    static constexpr uint32_t indices[] = {
        0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6,
        0, 1, 5, 0, 5, 4, 2, 6, 7, 2, 7, 3,
        0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5,
    };
    output.stableId = primitive.stableId;
    output.sourceMeshId = primitive.sourceMeshId;
    output.layer = primitive.layer;
    output.worldVertices.assign(primitive.worldCorners.begin(),
        primitive.worldCorners.begin() + 8);
    output.indices.assign(std::begin(indices), std::end(indices));
    output.exactSourceGeometry = false;
    output.locked = primitive.locked;
    uint64_t hash = 1469598103934665603ull;
    hash = HashString(hash, primitive.stableId);
    for (const Vector3& point : output.worldVertices) {
        hash = HashValue(hash, point);
    }
    output.sourceGeometryHash = hash;
    ResolveBounds(output);
    return true;
}

bool CourseMapGeometryExtractionSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(maximumVerticesPerSource, 8u, 1048576u) &&
        settings::InRange(maximumTrianglesPerSource, 12u, 2097152u) &&
        settings::InRange(maximumSources, 1u, 262144u);
    return settings::Result(valid, errorMessage,
        "CourseMapGeometryExtractionSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapGeometryExtractionService::SetSettings(
    CourseMapGeometryExtractionSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

} // namespace editor
