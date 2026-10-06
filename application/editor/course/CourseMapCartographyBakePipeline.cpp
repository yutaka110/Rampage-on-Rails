#include "CourseMapCartographyBakePipeline.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

namespace editor {
namespace {

struct FootprintPoint final {
    float x = 0.0f;
    float z = 0.0f;
};

uint64_t HashBytes(uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t HashString(uint64_t hash, std::string_view value) {
    return HashBytes(hash, value.data(), value.size());
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
    return HashBytes(hash, &value, sizeof(value));
}

float Cross(FootprintPoint origin, FootprintPoint a, FootprintPoint b) {
    return (a.x - origin.x) * (b.z - origin.z) -
        (a.z - origin.z) * (b.x - origin.x);
}

std::vector<FootprintPoint> ConvexHull(const std::vector<Vector3>& vertices) {
    std::vector<FootprintPoint> points;
    points.reserve(vertices.size());
    for (const Vector3& vertex : vertices) points.push_back({vertex.x, vertex.z});
    std::sort(points.begin(), points.end(), [](FootprintPoint a, FootprintPoint b) {
        return a.x < b.x || (a.x == b.x && a.z < b.z);
    });
    points.erase(std::unique(points.begin(), points.end(),
        [](FootprintPoint a, FootprintPoint b) {
            return std::abs(a.x - b.x) < 0.0001f &&
                std::abs(a.z - b.z) < 0.0001f;
        }), points.end());
    if (points.size() < 3u) return points;
    std::vector<FootprintPoint> hull(points.size() * 2u);
    std::size_t count = 0;
    for (FootprintPoint point : points) {
        while (count >= 2u &&
            Cross(hull[count - 2u], hull[count - 1u], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    const std::size_t lowerCount = count;
    for (std::size_t i = points.size() - 1u; i > 0u; --i) {
        const FootprintPoint point = points[i - 1u];
        while (count > lowerCount &&
            Cross(hull[count - 2u], hull[count - 1u], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    if (count > 1u) --count;
    hull.resize(count);
    return hull;
}

float PointSegmentDistance(
    FootprintPoint point, FootprintPoint start, FootprintPoint end) {
    const float dx = end.x - start.x;
    const float dz = end.z - start.z;
    const float lengthSquared = dx * dx + dz * dz;
    if (lengthSquared <= 0.0000001f) {
        const float px = point.x - start.x;
        const float pz = point.z - start.z;
        return std::sqrt(px * px + pz * pz);
    }
    const float t = (std::clamp)(
        ((point.x - start.x) * dx + (point.z - start.z) * dz) /
            lengthSquared,
        0.0f, 1.0f);
    const float px = point.x - (start.x + dx * t);
    const float pz = point.z - (start.z + dz * t);
    return std::sqrt(px * px + pz * pz);
}

void SimplifyRange(
    const std::vector<FootprintPoint>& points,
    std::size_t first,
    std::size_t last,
    float tolerance,
    std::vector<bool>& keep) {
    if (last <= first + 1u) return;
    float maximumDistance = 0.0f;
    std::size_t maximumIndex = first;
    for (std::size_t i = first + 1u; i < last; ++i) {
        const float distance = PointSegmentDistance(
            points[i], points[first], points[last]);
        if (distance > maximumDistance) {
            maximumDistance = distance;
            maximumIndex = i;
        }
    }
    if (maximumDistance <= tolerance) return;
    keep[maximumIndex] = true;
    SimplifyRange(points, first, maximumIndex, tolerance, keep);
    SimplifyRange(points, maximumIndex, last, tolerance, keep);
}

std::vector<FootprintPoint> SimplifyClosed(
    std::vector<FootprintPoint> points,
    float tolerance,
    uint32_t maximumPoints) {
    if (points.size() <= 3u) return points;
    points.push_back(points.front());
    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    SimplifyRange(points, 0u, points.size() - 1u, tolerance, keep);
    std::vector<FootprintPoint> simplified;
    for (std::size_t i = 0; i + 1u < points.size(); ++i) {
        if (keep[i]) simplified.push_back(points[i]);
    }
    if (simplified.size() < 3u) {
        points.pop_back();
        simplified = std::move(points);
    }
    if (simplified.size() > maximumPoints) {
        std::vector<FootprintPoint> limited;
        limited.reserve(maximumPoints);
        for (uint32_t i = 0; i < maximumPoints; ++i) {
            const std::size_t index =
                (static_cast<std::size_t>(i) * simplified.size()) /
                maximumPoints;
            limited.push_back(simplified[index]);
        }
        simplified = std::move(limited);
    }
    return simplified;
}

float Area(const std::vector<FootprintPoint>& points) {
    if (points.size() < 3u) return 0.0f;
    float twiceArea = 0.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const FootprintPoint a = points[i];
        const FootprintPoint b = points[(i + 1u) % points.size()];
        twiceArea += a.x * b.z - b.x * a.z;
    }
    return std::abs(twiceArea) * 0.5f;
}

void ReduceTriangles(
    const CourseMapExtractedGeometry& source,
    uint32_t maximumTriangles,
    CourseMapRegion& region) {
    const std::size_t sourceTriangleCount = source.indices.size() / 3u;
    const std::size_t targetTriangleCount = (std::min)(
        sourceTriangleCount, static_cast<std::size_t>(maximumTriangles));
    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(targetTriangleCount * 3u);
    region.indices.reserve(targetTriangleCount * 3u);
    region.worldVertices.reserve((std::min)(
        source.worldVertices.size(), targetTriangleCount * 3u));
    auto remapVertex = [&](uint32_t sourceIndex) {
        if (const auto found = remap.find(sourceIndex); found != remap.end()) {
            return found->second;
        }
        const uint32_t index = static_cast<uint32_t>(region.worldVertices.size());
        remap.emplace(sourceIndex, index);
        region.worldVertices.push_back(source.worldVertices[sourceIndex]);
        return index;
    };
    for (std::size_t triangle = 0; triangle < targetTriangleCount; ++triangle) {
        const std::size_t sourceTriangle = targetTriangleCount == sourceTriangleCount
            ? triangle : (triangle * sourceTriangleCount) / targetTriangleCount;
        const std::size_t offset = sourceTriangle * 3u;
        region.indices.push_back(remapVertex(source.indices[offset]));
        region.indices.push_back(remapVertex(source.indices[offset + 1u]));
        region.indices.push_back(remapVertex(source.indices[offset + 2u]));
    }
}

std::string SafeName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const unsigned char value : name) {
        if ((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '-' || value == '_') {
            result.push_back(static_cast<char>(value));
        } else if (value == ' ') {
            result.push_back('_');
        }
    }
    return result.empty() ? "course" : result;
}

bool SameSettings(
    const CourseMapCartographyBakeSettings& a,
    const CourseMapCartographyBakeSettings& b) {
    return a.autoBake == b.autoBake &&
        a.persistDerivedAsset == b.persistDerivedAsset &&
        a.allowBoxFallback == b.allowBoxFallback &&
        a.tileWorldSize == b.tileWorldSize &&
        a.footprintSimplificationTolerance == b.footprintSimplificationTolerance &&
        a.minimumProjectedArea == b.minimumProjectedArea &&
        a.maximumRegions == b.maximumRegions &&
        a.maximumTrianglesPerRegion == b.maximumTrianglesPerRegion &&
        a.maximumFootprintPoints == b.maximumFootprintPoints;
}

} // namespace

CourseMapCartographyBakePipeline::CourseMapCartographyBakePipeline(
    std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)), extractionService_(projectRoot_) {}

const CourseMapCartographyBakeResult& CourseMapCartographyBakePipeline::Ensure(
    const CourseMapCartographyBakeInput& input) {
    lastResult_ = {};
    if (input.visualAsset == nullptr) {
        lastResult_.status = CourseMapCartographyBakeStatus::Failed;
        lastResult_.message = "Cartography bake requires a current Course Map visual asset.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    lastResult_.cachePath = CachePath(input);
    const uint64_t settingsHash = ComputeSettingsHash();
    const uint64_t meshRegistryFingerprint =
        ResolveMeshRegistryFingerprint(input.assets);

    bool cacheStillPending = false;
    if (cacheLoadPending_) {
        if (PollCacheLoad(input, meshRegistryFingerprint, settingsHash,
                cacheStillPending) && !forceRebuild_) {
            lastResult_.status = CourseMapCartographyBakeStatus::Current;
            lastResult_.assetAvailable = true;
            lastResult_.loadedFromCache = true;
            lastResult_.fallbackRequired = false;
            lastResult_.expectedFingerprint = asset_.sourceFingerprint;
            lastResult_.message =
                "Course Map cartography asset loaded asynchronously from cache.";
            lastResult_.stats = lifetimeStats_;
            return lastResult_;
        }
        if (cacheStillPending) {
            lastResult_.status = CourseMapCartographyBakeStatus::Loading;
            lastResult_.message =
                "Loading Course Map cartography in the background; visual fallback remains interactive.";
            lastResult_.stats = lifetimeStats_;
            return lastResult_;
        }
    }

    const bool fastCurrent = !asset_.Empty() && IsFastSourceCurrent(
        asset_, input, meshRegistryFingerprint, settingsHash);
    const CourseMapCartographyBakeStatus status = asset_.Empty()
        ? CourseMapCartographyBakeStatus::Missing
        : fastCurrent ? CourseMapCartographyBakeStatus::Current
                      : CourseMapCartographyBakeStatus::Stale;
    if (!forceRebuild_ && status == CourseMapCartographyBakeStatus::Current) {
        ++lifetimeStats_.currentHits;
        lastResult_.status = status;
        lastResult_.assetAvailable = true;
        lastResult_.fallbackRequired = false;
        lastResult_.expectedFingerprint = asset_.sourceFingerprint;
        lastResult_.message = "Course Map cartography asset is current.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }

    if (!forceRebuild_ && !cacheAttempted_ && asset_.Empty()) {
        BeginCacheLoad(input, meshRegistryFingerprint, settingsHash);
        lastResult_.status = CourseMapCartographyBakeStatus::Loading;
        lastResult_.message =
            "Loading Course Map cartography in the background; visual fallback remains interactive.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }

    lastResult_.status = forceRebuild_
        ? CourseMapCartographyBakeStatus::Stale : status;
    lastResult_.fallbackRequired = true;
    if (!settings_.autoBake && !forceRebuild_) {
        lastResult_.message = std::string("Course Map cartography asset is ") +
            ToString(status) +
            "; visual fallback remains active until Rebake Map Shape is requested.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    lastResult_ = Bake(input);
    return lastResult_;
}

CourseMapCartographyBakeResult CourseMapCartographyBakePipeline::Bake(
    const CourseMapCartographyBakeInput& input) {
    CourseMapCartographyBakeResult result{};
    result.cachePath = CachePath(input);
    const CourseMapGeometryExtractionResult& extraction = ResolveExtraction(input);
    if (input.visualAsset == nullptr || !extraction.succeeded) {
        result.status = CourseMapCartographyBakeStatus::Failed;
        result.message = extraction.message.empty()
            ? "Course Map cartography source is unavailable." : extraction.message;
        result.stats = lifetimeStats_;
        return result;
    }
    uint64_t settingsHash = 0;
    const uint64_t fingerprint = ResolveFingerprint(
        input, extraction, &settingsHash);
    result.expectedFingerprint = fingerprint;
    result.usedGeometryFallback = extraction.usedFallback;

    CourseMapRegionAsset baked{};
    baked.sourceCourseName = input.visualAsset->sourceCourseName;
    baked.sourceVisualFingerprint = input.visualAsset->sourceFingerprint;
    baked.sourceGeometryHash = extraction.geometryFingerprint;
    baked.bakeSettingsHash = settingsHash;
    baked.sourceMeshRegistryFingerprint =
        ResolveMeshRegistryFingerprint(input.assets);
    baked.sourceFingerprint = fingerprint;
    baked.tileWorldSize = settings_.tileWorldSize;
    const float maximum = (std::numeric_limits<float>::max)();
    baked.worldMinimum = {maximum, maximum, maximum};
    baked.worldMaximum = {-maximum, -maximum, -maximum};

    std::vector<const CourseMapExtractedGeometry*> sorted;
    sorted.reserve(extraction.sources.size());
    for (const CourseMapExtractedGeometry& source : extraction.sources) {
        sorted.push_back(&source);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const CourseMapExtractedGeometry* a,
           const CourseMapExtractedGeometry* b) {
            return a->stableId < b->stableId;
        });
    for (const CourseMapExtractedGeometry* source : sorted) {
        if (source == nullptr || baked.regions.size() >= settings_.maximumRegions) break;
        std::vector<FootprintPoint> footprint = SimplifyClosed(
            ConvexHull(source->worldVertices),
            settings_.footprintSimplificationTolerance,
            settings_.maximumFootprintPoints);
        const float area = Area(footprint);
        if (footprint.size() < 3u || area < settings_.minimumProjectedArea) continue;
        CourseMapRegion region{};
        region.stableId = "region:" + source->stableId;
        region.sourceStableId = source->stableId;
        region.sourceMeshId = source->sourceMeshId;
        region.layer = source->layer;
        region.minimumHeight = source->worldMinimum.y;
        region.maximumHeight = source->worldMaximum.y;
        region.projectedArea = area;
        region.exactSourceGeometry = source->exactSourceGeometry;
        region.locked = source->locked;
        ReduceTriangles(*source, settings_.maximumTrianglesPerRegion, region);
        if (region.indices.empty()) continue;
        region.worldCentroid = {};
        for (const Vector3& point : region.worldVertices) {
            region.worldCentroid.x += point.x;
            region.worldCentroid.y += point.y;
            region.worldCentroid.z += point.z;
        }
        const float inverseCount = 1.0f /
            static_cast<float>(region.worldVertices.size());
        region.worldCentroid.x *= inverseCount;
        region.worldCentroid.y *= inverseCount;
        region.worldCentroid.z *= inverseCount;
        region.footprint.reserve(footprint.size());
        for (FootprintPoint point : footprint) {
            region.footprint.push_back(
                {point.x, region.worldCentroid.y, point.z});
        }
        baked.worldMinimum.x = (std::min)(baked.worldMinimum.x, source->worldMinimum.x);
        baked.worldMinimum.y = (std::min)(baked.worldMinimum.y, source->worldMinimum.y);
        baked.worldMinimum.z = (std::min)(baked.worldMinimum.z, source->worldMinimum.z);
        baked.worldMaximum.x = (std::max)(baked.worldMaximum.x, source->worldMaximum.x);
        baked.worldMaximum.y = (std::max)(baked.worldMaximum.y, source->worldMaximum.y);
        baked.worldMaximum.z = (std::max)(baked.worldMaximum.z, source->worldMaximum.z);
        result.stats.sourceTriangles += static_cast<uint32_t>(source->indices.size() / 3u);
        result.stats.bakedTriangles += static_cast<uint32_t>(region.indices.size() / 3u);
        result.stats.footprintPoints += static_cast<uint32_t>(region.footprint.size());
        if (region.exactSourceGeometry) ++result.stats.exactRegions;
        else ++result.stats.fallbackRegions;
        baked.regions.push_back(std::move(region));
    }
    if (baked.regions.empty()) {
        result.status = CourseMapCartographyBakeStatus::Missing;
        result.message = "Course Map cartography bake produced no readable regions.";
        result.fallbackRequired = true;
        result.stats = lifetimeStats_;
        return result;
    }
    std::map<std::pair<int32_t, int32_t>, CourseMapRegionTile> tiles;
    for (uint32_t index = 0; index < baked.regions.size(); ++index) {
        const CourseMapRegion& region = baked.regions[index];
        const int32_t x = static_cast<int32_t>(std::floor(
            region.worldCentroid.x / settings_.tileWorldSize));
        const int32_t z = static_cast<int32_t>(std::floor(
            region.worldCentroid.z / settings_.tileWorldSize));
        CourseMapRegionTile& tile = tiles[{x, z}];
        tile.x = x;
        tile.z = z;
        tile.regionIndices.push_back(index);
    }
    for (auto& [key, tile] : tiles) {
        (void)key;
        tile.worldMinimum = {tile.x * settings_.tileWorldSize,
            baked.worldMinimum.y, tile.z * settings_.tileWorldSize};
        tile.worldMaximum = {(tile.x + 1) * settings_.tileWorldSize,
            baked.worldMaximum.y, (tile.z + 1) * settings_.tileWorldSize};
        baked.tiles.push_back(std::move(tile));
    }
    result.stats.regions = static_cast<uint32_t>(baked.regions.size());
    result.stats.tiles = static_cast<uint32_t>(baked.tiles.size());
    baked.contentRevision = asset_.contentRevision + 1u;
    std::string validationError;
    if (!baked.Validate(&validationError)) {
        result.status = CourseMapCartographyBakeStatus::Failed;
        result.message = "Course Map region validation failed: " + validationError;
        result.fallbackRequired = true;
        result.stats = lifetimeStats_;
        return result;
    }
    asset_ = std::move(baked);
    lifetimeStats_.regions = result.stats.regions;
    lifetimeStats_.exactRegions = result.stats.exactRegions;
    lifetimeStats_.fallbackRegions = result.stats.fallbackRegions;
    lifetimeStats_.sourceTriangles = result.stats.sourceTriangles;
    lifetimeStats_.bakedTriangles = result.stats.bakedTriangles;
    lifetimeStats_.footprintPoints = result.stats.footprintPoints;
    lifetimeStats_.tiles = result.stats.tiles;
    forceRebuild_ = false;
    cacheAttempted_ = true;
    ++lifetimeStats_.bakes;
    result.stats.bakes = lifetimeStats_.bakes;
    result.stats.cacheLoads = lifetimeStats_.cacheLoads;
    result.stats.currentHits = lifetimeStats_.currentHits;
    result.stats.extractionBuilds = lifetimeStats_.extractionBuilds;
    result.stats.extractionCacheHits = lifetimeStats_.extractionCacheHits;
    result.stats.asynchronousCacheLoadStarts =
        lifetimeStats_.asynchronousCacheLoadStarts;
    result.stats.asynchronousCacheLoadCompletions =
        lifetimeStats_.asynchronousCacheLoadCompletions;
    if (settings_.persistDerivedAsset) {
        std::string saveError;
        if (!asset_.SaveToFile(result.cachePath.string(), &saveError)) {
            result.message = "Cartography baked in memory; cache write failed: " + saveError;
        }
    }
    result.status = CourseMapCartographyBakeStatus::Current;
    result.assetAvailable = true;
    result.bakedThisCall = true;
    result.fallbackRequired = false;
    if (result.message.empty()) {
        result.message = "Course Map cartography baked: " +
            std::to_string(asset_.regions.size()) + " regions, " +
            std::to_string(result.stats.bakedTriangles) + " triangles.";
    }
    lastResult_ = result;
    return result;
}

CourseMapCartographyBakeStatus CourseMapCartographyBakePipeline::Assess(
    const CourseMapRegionAsset& asset,
    const CourseMapCartographyBakeInput& input,
    uint64_t* expectedFingerprint) {
    const CourseMapGeometryExtractionResult& extraction = ResolveExtraction(input);
    if (!extraction.succeeded) return CourseMapCartographyBakeStatus::Failed;
    const uint64_t fingerprint = ResolveFingerprint(input, extraction);
    if (expectedFingerprint != nullptr) *expectedFingerprint = fingerprint;
    if (asset.Empty()) return CourseMapCartographyBakeStatus::Missing;
    if (asset.schemaVersion != kCourseMapRegionAssetSchemaVersion ||
        asset.bakerVersion != kCourseMapCartographyBakerVersion) {
        return CourseMapCartographyBakeStatus::Incompatible;
    }
    std::string error;
    if (!asset.Validate(&error)) return CourseMapCartographyBakeStatus::Incompatible;
    return asset.IsSourceCurrent(fingerprint)
        ? CourseMapCartographyBakeStatus::Current
        : CourseMapCartographyBakeStatus::Stale;
}

const CourseMapGeometryExtractionResult&
CourseMapCartographyBakePipeline::ResolveExtraction(
    const CourseMapCartographyBakeInput& input) {
    const SourceKey key{input.visualAsset, input.assets,
        input.visualAsset != nullptr ? input.visualAsset->sourceFingerprint : 0u,
        input.visualRevision, settingsRevision_,
        input.assets != nullptr ? input.assets->Revision()
            : input.assetRegistryRevision};
    if (extractionValid_ && SameSourceKey(extractionKey_, key)) {
        ++lifetimeStats_.extractionCacheHits;
        return extraction_;
    }
    CourseMapGeometryExtractionSettings extractionSettings =
        extractionService_.Settings();
    extractionSettings.allowBoxFallback = settings_.allowBoxFallback;
    extractionSettings.maximumSources = settings_.maximumRegions;
    extractionService_.SetSettings(extractionSettings);
    extraction_ = extractionService_.Extract({input.visualAsset, input.assets});
    extractionKey_ = key;
    extractionValid_ = true;
    ++lifetimeStats_.extractionBuilds;
    return extraction_;
}

uint64_t CourseMapCartographyBakePipeline::ComputeSettingsHash() const {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, settings_.allowBoxFallback);
    hash = HashValue(hash, settings_.tileWorldSize);
    hash = HashValue(hash, settings_.footprintSimplificationTolerance);
    hash = HashValue(hash, settings_.minimumProjectedArea);
    hash = HashValue(hash, settings_.maximumRegions);
    hash = HashValue(hash, settings_.maximumTrianglesPerRegion);
    hash = HashValue(hash, settings_.maximumFootprintPoints);
    return hash;
}

uint64_t CourseMapCartographyBakePipeline::ResolveMeshRegistryFingerprint(
    const EditorAssetRegistry* assets) const {
    if (assets == nullptr) {
        return HashString(1469598103934665603ull, "no-mesh-registry");
    }
    if (fingerprintRegistry_ == assets &&
        fingerprintRegistryRevision_ == assets->Revision() &&
        meshRegistryFingerprint_ != 0u) {
        return meshRegistryFingerprint_;
    }
    std::vector<const EditorAssetRecord*> meshes;
    meshes.reserve(assets->Count(EditorAssetKind::Mesh));
    for (const EditorAssetRecord& record : assets->Records()) {
        if (record.kind == EditorAssetKind::Mesh) meshes.push_back(&record);
    }
    std::sort(meshes.begin(), meshes.end(),
        [](const EditorAssetRecord* lhs, const EditorAssetRecord* rhs) {
            return std::tie(lhs->guid, lhs->id, lhs->sourcePath) <
                std::tie(rhs->guid, rhs->id, rhs->sourcePath);
        });
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, meshes.size());
    for (const EditorAssetRecord* record : meshes) {
        hash = HashString(hash, record->guid);
        hash = HashString(hash, record->id);
        hash = HashString(hash, record->logicalPath);
        hash = HashString(hash, record->sourcePath);
        hash = HashString(hash, record->metadataPath);
        hash = HashValue(hash, record->sourceTimestamp);
        hash = HashValue(hash, record->missing);
        hash = HashValue(hash, record->referenceable);
    }
    fingerprintRegistry_ = assets;
    fingerprintRegistryRevision_ = assets->Revision();
    meshRegistryFingerprint_ = hash;
    return hash;
}

bool CourseMapCartographyBakePipeline::IsFastSourceCurrent(
    const CourseMapRegionAsset& asset,
    const CourseMapCartographyBakeInput& input,
    uint64_t meshRegistryFingerprint,
    uint64_t settingsHash) const noexcept {
    return input.visualAsset != nullptr &&
        asset.schemaVersion == kCourseMapRegionAssetSchemaVersion &&
        asset.bakerVersion == kCourseMapCartographyBakerVersion &&
        asset.sourceVisualFingerprint == input.visualAsset->sourceFingerprint &&
        asset.sourceMeshRegistryFingerprint == meshRegistryFingerprint &&
        asset.bakeSettingsHash == settingsHash;
}

uint64_t CourseMapCartographyBakePipeline::ResolveFingerprint(
    const CourseMapCartographyBakeInput& input,
    const CourseMapGeometryExtractionResult& extraction,
    uint64_t* settingsHash) const {
    if (input.visualAsset == nullptr || !extraction.succeeded) return 0u;
    const uint64_t resolvedSettingsHash = ComputeSettingsHash();
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, input.visualAsset->sourceFingerprint);
    hash = HashValue(hash, extraction.geometryFingerprint);
    hash = HashValue(hash, resolvedSettingsHash);
    hash = HashValue(hash, kCourseMapRegionAssetSchemaVersion);
    hash = HashValue(hash, kCourseMapCartographyBakerVersion);
    if (settingsHash != nullptr) *settingsHash = resolvedSettingsHash;
    return hash;
}

std::filesystem::path CourseMapCartographyBakePipeline::CachePath(
    const CourseMapCartographyBakeInput& input) const {
    const std::string name = input.visualAsset != nullptr
        ? SafeName(input.visualAsset->sourceCourseName) : "course";
    return cacheRoot_ / (name + ".coursemapregions");
}

void CourseMapCartographyBakePipeline::BeginCacheLoad(
    const CourseMapCartographyBakeInput& input,
    uint64_t meshRegistryFingerprint,
    uint64_t settingsHash) {
    if (cacheLoadPending_) return;
    cacheLoadVisualFingerprint_ = input.visualAsset != nullptr
        ? input.visualAsset->sourceFingerprint : 0u;
    cacheLoadMeshRegistryFingerprint_ = meshRegistryFingerprint;
    cacheLoadSettingsHash_ = settingsHash;
    cacheLoadEpoch_ = invalidationEpoch_;
    const std::filesystem::path path = CachePath(input);
    try {
        cacheLoadFuture_ = std::async(std::launch::async,
            [path]() {
                AsyncCacheLoadResult result{};
                result.loaded = result.asset.LoadFromFile(
                    path.string(), &result.error);
                return result;
            });
        cacheLoadPending_ = true;
        ++lifetimeStats_.asynchronousCacheLoadStarts;
    } catch (const std::exception&) {
        cacheAttempted_ = true;
        cacheLoadPending_ = false;
    }
}

bool CourseMapCartographyBakePipeline::PollCacheLoad(
    const CourseMapCartographyBakeInput& input,
    uint64_t meshRegistryFingerprint,
    uint64_t settingsHash,
    bool& stillPending) {
    stillPending = false;
    if (!cacheLoadPending_ || !cacheLoadFuture_.valid()) return false;
    if (cacheLoadFuture_.wait_for(std::chrono::milliseconds(0)) !=
        std::future_status::ready) {
        stillPending = true;
        return false;
    }
    AsyncCacheLoadResult loaded{};
    try {
        loaded = cacheLoadFuture_.get();
    } catch (const std::exception& error) {
        loaded.error = error.what();
    }
    cacheLoadPending_ = false;
    cacheAttempted_ = true;
    ++lifetimeStats_.asynchronousCacheLoadCompletions;
    const uint64_t visualFingerprint = input.visualAsset != nullptr
        ? input.visualAsset->sourceFingerprint : 0u;
    if (cacheLoadEpoch_ != invalidationEpoch_ ||
        cacheLoadVisualFingerprint_ != visualFingerprint ||
        cacheLoadMeshRegistryFingerprint_ != meshRegistryFingerprint ||
        cacheLoadSettingsHash_ != settingsHash || !loaded.loaded ||
        !IsFastSourceCurrent(
            loaded.asset, input, meshRegistryFingerprint, settingsHash)) {
        return false;
    }
    ApplyLoadedAsset(std::move(loaded.asset));
    return true;
}

void CourseMapCartographyBakePipeline::ApplyLoadedAsset(
    CourseMapRegionAsset loaded) {
    asset_ = std::move(loaded);
    lifetimeStats_.regions = static_cast<uint32_t>(asset_.regions.size());
    lifetimeStats_.exactRegions = 0;
    lifetimeStats_.fallbackRegions = 0;
    lifetimeStats_.bakedTriangles = 0;
    lifetimeStats_.footprintPoints = 0;
    for (const CourseMapRegion& region : asset_.regions) {
        if (region.exactSourceGeometry) ++lifetimeStats_.exactRegions;
        else ++lifetimeStats_.fallbackRegions;
        lifetimeStats_.bakedTriangles +=
            static_cast<uint32_t>(region.indices.size() / 3u);
        lifetimeStats_.footprintPoints +=
            static_cast<uint32_t>(region.footprint.size());
    }
    lifetimeStats_.tiles = static_cast<uint32_t>(asset_.tiles.size());
    ++lifetimeStats_.cacheLoads;
}

bool CourseMapCartographyBakePipeline::SameSourceKey(
    const SourceKey& a, const SourceKey& b) noexcept {
    return a.visualAsset == b.visualAsset && a.assets == b.assets &&
        a.visualFingerprint == b.visualFingerprint &&
        a.visualRevision == b.visualRevision &&
        a.settingsRevision == b.settingsRevision &&
        a.assetRegistryRevision == b.assetRegistryRevision;
}

void CourseMapCartographyBakePipeline::RequestRebuild() noexcept {
    forceRebuild_ = true;
}

void CourseMapCartographyBakePipeline::InvalidateAsset() noexcept {
    asset_ = {};
    lastResult_ = {};
    cacheAttempted_ = false;
    forceRebuild_ = false;
    ++invalidationEpoch_;
}

bool CourseMapCartographyBakeSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(tileWorldSize, 16.0f, 8192.0f) &&
        settings::InRange(footprintSimplificationTolerance, 0.0f, 1000.0f) &&
        settings::InRange(minimumProjectedArea, 0.0f, 1000000.0f) &&
        settings::InRange(maximumRegions, 1u, 262144u) &&
        settings::InRange(maximumTrianglesPerRegion, 12u, 2097152u) &&
        settings::InRange(maximumFootprintPoints, 3u, 4096u);
    return settings::Result(valid, errorMessage,
        "CourseMapCartographyBakeSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapCartographyBakePipeline::SetSettings(
    CourseMapCartographyBakeSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    extractionValid_ = false;
    cacheAttempted_ = false;
    // A settings edit marks the derived data stale, but must not compile it
    // synchronously from the next viewport draw. Rebuild remains explicit (or
    // opt-in through autoBake).
    forceRebuild_ = false;
    ++invalidationEpoch_;
    return true;
}

void CourseMapCartographyBakePipeline::SetCacheRoot(
    std::filesystem::path path) {
    if (path.empty() || path == cacheRoot_) return;
    cacheRoot_ = std::move(path);
    cacheAttempted_ = false;
    ++invalidationEpoch_;
}

const CourseMapRegionAsset*
CourseMapCartographyBakePipeline::CurrentAsset() const noexcept {
    return lastResult_.status == CourseMapCartographyBakeStatus::Current &&
        !asset_.Empty() ? &asset_ : nullptr;
}

const char* ToString(CourseMapCartographyBakeStatus status) noexcept {
    switch (status) {
    case CourseMapCartographyBakeStatus::Missing: return "Missing";
    case CourseMapCartographyBakeStatus::Loading: return "Loading";
    case CourseMapCartographyBakeStatus::Current: return "Current";
    case CourseMapCartographyBakeStatus::Stale: return "Stale";
    case CourseMapCartographyBakeStatus::Incompatible: return "Incompatible";
    case CourseMapCartographyBakeStatus::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace editor
