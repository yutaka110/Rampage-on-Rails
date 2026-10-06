#include "CourseTerrainMapBakePipeline.h"
#include "../EditorSettingsValidation.h"

#include "../../terrain/TerrainVolumeField.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace editor {
namespace {

constexpr float kTau = 6.28318530717958647692f;

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

uint64_t HashRail(const RailPath& rail) {
    uint64_t hash = 1469598103934665603ull;
    for (const RailPathControlPoint& point : rail.ControlPoints()) {
        hash = HashValue(hash, point.position);
        hash = HashValue(hash, point.corridorRadius);
        hash = HashValue(hash, point.tangentMode);
        hash = HashValue(hash, point.incomingTangent);
        hash = HashValue(hash, point.outgoingTangent);
    }
    return hash;
}

uint64_t HashTerrainSettings(const TerrainGenerationSettings& value) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, value.seed);
    hash = HashValue(hash, value.chunkLength);
    hash = HashValue(hash, value.corridorRadius);
    hash = HashValue(hash, value.canyonHalfWidth);
    hash = HashValue(hash, value.wallHeight);
    hash = HashValue(hash, value.volumeRoughness);
    hash = HashValue(hash, value.volumeArchScale);
    hash = HashValue(hash, value.sdfCarveDensity);
    hash = HashValue(hash, value.sdfCarveStrength);
    hash = HashValue(hash, value.sdfCarveScale);
    hash = HashValue(hash, value.openingSilhouetteStrength);
    hash = HashValue(hash, value.openingSilhouetteScale);
    hash = HashValue(hash, value.openCanyonStartDistance);
    hash = HashValue(hash, value.openCanyonTransitionLength);
    hash = HashValue(hash, value.openCanyonStrength);
    hash = HashValue(hash, value.archDensity);
    hash = HashValue(hash, value.motherRockErosionStrength);
    hash = HashValue(hash, value.largeScaleErosionStrength);
    hash = HashValue(hash, value.surfaceBreakupDensity);
    return hash;
}

uint64_t HashBakeSettings(const CourseTerrainMapBakeSettings& value) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, value.tileLength);
    for (uint32_t segments : value.longitudinalSegments) {
        hash = HashValue(hash, segments);
    }
    for (uint32_t segments : value.radialSegments) {
        hash = HashValue(hash, segments);
    }
    return hash;
}

std::string SanitizedName(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            result.push_back(static_cast<char>(c));
        } else if (c == ' ' || c == '.') {
            result.push_back('_');
        }
    }
    return result.empty() ? "course" : result;
}

void ExpandBounds(Vector3 point, Vector3& minimum, Vector3& maximum) {
    minimum.x = (std::min)(minimum.x, point.x);
    minimum.y = (std::min)(minimum.y, point.y);
    minimum.z = (std::min)(minimum.z, point.z);
    maximum.x = (std::max)(maximum.x, point.x);
    maximum.y = (std::max)(maximum.y, point.y);
    maximum.z = (std::max)(maximum.z, point.z);
}

template <typename SnapshotT>
CourseTerrainMapTile BuildTile(
    const SnapshotT& snapshot,
    float startDistance,
    float endDistance,
    uint32_t longitudinalSteps,
    uint32_t radialSegments) {
    CourseTerrainMapTile tile{};
    tile.startDistance = startDistance;
    tile.endDistance = endDistance;
    const float maximum = (std::numeric_limits<float>::max)();
    tile.worldMinimum = {maximum, maximum, maximum};
    tile.worldMaximum = {-maximum, -maximum, -maximum};
    TerrainVolumeField field(snapshot.rail, snapshot.terrainSettings,
        &snapshot.terrainEdits, nullptr);
    const uint32_t columns = radialSegments + 1u;
    tile.vertices.reserve(static_cast<std::size_t>(longitudinalSteps + 1u) * columns);
    std::vector<float> openingMasks;
    openingMasks.reserve(tile.vertices.capacity());
    for (uint32_t row = 0; row <= longitudinalSteps; ++row) {
        const float t = static_cast<float>(row) /
            static_cast<float>(longitudinalSteps);
        const float distance = startDistance + (endDistance - startDistance) * t;
        for (uint32_t column = 0; column <= radialSegments; ++column) {
            const float angle = kTau * static_cast<float>(column) /
                static_cast<float>(radialSegments);
            Vector3 normal{};
            const Vector3 position = field.SurfacePoint(distance, angle, &normal);
            tile.vertices.push_back({position, normal});
            openingMasks.push_back(field.OpeningMask(distance, angle));
            ExpandBounds(position, tile.worldMinimum, tile.worldMaximum);
        }
    }
    tile.indices.reserve(longitudinalSteps * radialSegments * 6u);
    for (uint32_t row = 0; row < longitudinalSteps; ++row) {
        for (uint32_t column = 0; column < radialSegments; ++column) {
            const uint32_t a = row * columns + column;
            const uint32_t b = a + 1u;
            const uint32_t c = a + columns;
            const uint32_t d = c + 1u;
            const float average = (openingMasks[a] + openingMasks[b] +
                openingMasks[c] + openingMasks[d]) * 0.25f;
            const float maximumMask = (std::max)(
                (std::max)(openingMasks[a], openingMasks[b]),
                (std::max)(openingMasks[c], openingMasks[d]));
            if (average > 0.42f ||
                (maximumMask > 0.74f && average > 0.28f)) continue;
            tile.indices.insert(tile.indices.end(), {a, c, b, b, c, d});
        }
    }
    return tile;
}

} // namespace

CourseTerrainMapBakePipeline::CourseTerrainMapBakePipeline(
    std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)) {}

const CourseTerrainMapBakeResult& CourseTerrainMapBakePipeline::Ensure(
    const CourseTerrainMapBakeInput& input) {
    lastResult_ = {};
    std::string error;
    Snapshot snapshot{};
    if (!MakeSnapshot(input, forceRebuild_, snapshot, &error)) {
        lastResult_.status = CourseTerrainMapBakeStatus::Failed;
        lastResult_.message = std::move(error);
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    latestRequestedFingerprint_ = snapshot.sourceFingerprint;
    lastResult_.expectedFingerprint = snapshot.sourceFingerprint;
    lastResult_.cachePath = snapshot.cachePath;
    PollWorker(snapshot.sourceFingerprint);
    const bool current = !asset_.Empty() &&
        asset_.IsSourceCurrent(snapshot.sourceFingerprint);
    if (current && !forceRebuild_ && !pendingFingerprint_.has_value()) {
        ++lifetimeStats_.currentHits;
        lastResult_.status = CourseTerrainMapBakeStatus::Current;
        lastResult_.assetAvailable = true;
        lastResult_.message = "Course Terrain Map asset is current.";
        lastResult_.stats = lifetimeStats_;
        return lastResult_;
    }
    if (!pendingFingerprint_.has_value()) {
        if (!settings_.autoBake && !forceRebuild_) {
            lastResult_.status = asset_.Empty()
                ? CourseTerrainMapBakeStatus::Missing
                : CourseTerrainMapBakeStatus::Stale;
            lastResult_.assetAvailable = !asset_.Empty();
            lastResult_.message = "Course Terrain Map is stale; request a terrain rebake.";
            lastResult_.stats = lifetimeStats_;
            return lastResult_;
        }
        StartWorker(std::move(snapshot));
        forceRebuild_ = false;
    }
    lastResult_.status = asset_.Empty()
        ? CourseTerrainMapBakeStatus::Building
        : CourseTerrainMapBakeStatus::Stale;
    lastResult_.assetAvailable = !asset_.Empty();
    lastResult_.message = asset_.Empty()
        ? "Building full-course terrain map in the background."
        : "Rebuilding terrain map; retained terrain remains visible.";
    lastResult_.stats = lifetimeStats_;
    return lastResult_;
}

CourseTerrainMapBakeResult CourseTerrainMapBakePipeline::Bake(
    const CourseTerrainMapBakeInput& input) {
    CourseTerrainMapBakeResult result{};
    Snapshot snapshot{};
    std::string error;
    if (!MakeSnapshot(input, true, snapshot, &error)) {
        result.status = CourseTerrainMapBakeStatus::Failed;
        result.message = std::move(error);
        return result;
    }
    snapshot.forceRebuild = true;
    WorkerResult worker = RunWorker(std::move(snapshot));
    result.expectedFingerprint = worker.sourceFingerprint;
    result.cachePath = CachePath(input.courseName);
    if (!worker.succeeded) {
        result.status = CourseTerrainMapBakeStatus::Failed;
        result.message = std::move(worker.message);
        return result;
    }
    asset_ = std::move(worker.asset);
    ++lifetimeStats_.buildsStarted;
    ++lifetimeStats_.buildsCompleted;
    if (worker.wroteCache) ++lifetimeStats_.cacheWrites;
    RefreshAssetStats();
    result.status = CourseTerrainMapBakeStatus::Current;
    result.assetAvailable = true;
    result.builtThisCall = true;
    result.message = std::move(worker.message);
    result.stats = lifetimeStats_;
    lastResult_ = result;
    return result;
}

CourseTerrainMapBakeStatus CourseTerrainMapBakePipeline::Assess(
    const CourseTerrainMapAsset& asset,
    const CourseTerrainMapBakeInput& input,
    uint64_t* expectedFingerprint) const {
    const uint64_t expected = ResolveFingerprint(input);
    if (expectedFingerprint != nullptr) *expectedFingerprint = expected;
    std::string error;
    if (!asset.Validate(&error)) return CourseTerrainMapBakeStatus::Failed;
    return asset.IsSourceCurrent(expected)
        ? CourseTerrainMapBakeStatus::Current
        : CourseTerrainMapBakeStatus::Stale;
}

void CourseTerrainMapBakePipeline::RequestRebuild() noexcept {
    forceRebuild_ = true;
}

void CourseTerrainMapBakePipeline::InvalidateAsset() noexcept {
    asset_ = {};
    forceRebuild_ = true;
}

bool CourseTerrainMapBakeSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(tileLength, 40.0f, 2000.0f) &&
        std::all_of(longitudinalSegments.begin(), longitudinalSegments.end(), [](uint32_t n) { return settings::InRange(n, 16u, 2048u); }) &&
        std::all_of(radialSegments.begin(), radialSegments.end(), [](uint32_t n) { return settings::InRange(n, 8u, 128u); });
    return settings::Result(valid, errorMessage,
        "CourseTerrainMapBakeSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseTerrainMapBakePipeline::SetSettings(
    CourseTerrainMapBakeSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    forceRebuild_ = true;
    return true;
}

void CourseTerrainMapBakePipeline::SetCacheRoot(std::filesystem::path path) {
    if (path.empty() || path == cacheRoot_) return;
    cacheRoot_ = std::move(path);
    forceRebuild_ = true;
}

const CourseTerrainMapAsset* CourseTerrainMapBakePipeline::CurrentAsset() const noexcept {
    return asset_.Empty() ? nullptr : &asset_;
}

bool CourseTerrainMapBakePipeline::MakeSnapshot(
    const CourseTerrainMapBakeInput& input,
    bool forceRebuild,
    Snapshot& snapshot,
    std::string* errorMessage) const {
    if (input.rail == nullptr || !input.rail->IsValid() ||
        input.terrainSettings == nullptr || input.rail->Length() <= 0.0f) {
        if (errorMessage != nullptr) {
            *errorMessage = "Course Terrain Map requires a valid rail and terrain settings.";
        }
        return false;
    }
    snapshot.rail = input.rail->RuntimePath();
    snapshot.terrainSettings = *input.terrainSettings;
    if (input.terrainEdits != nullptr) snapshot.terrainEdits = *input.terrainEdits;
    snapshot.bakeSettings = settings_;
    snapshot.courseName = input.courseName.empty() ? "Course" : input.courseName;
    snapshot.sourceFingerprint = ResolveFingerprint(input,
        &snapshot.sourceRailHash, &snapshot.sourceTerrainSettingsHash,
        &snapshot.sourceTerrainEditHash, &snapshot.bakeSettingsHash);
    snapshot.cachePath = CachePath(snapshot.courseName);
    snapshot.forceRebuild = forceRebuild;
    return snapshot.sourceFingerprint != 0u;
}

CourseTerrainMapBakePipeline::WorkerResult
CourseTerrainMapBakePipeline::RunWorker(Snapshot snapshot) {
    WorkerResult result{};
    result.sourceFingerprint = snapshot.sourceFingerprint;
    if (!snapshot.forceRebuild && snapshot.bakeSettings.persistDerivedAsset) {
        CourseTerrainMapAsset cached{};
        std::string ignored;
        if (cached.LoadFromFile(snapshot.cachePath.string(), &ignored) &&
            cached.IsSourceCurrent(snapshot.sourceFingerprint)) {
            result.succeeded = true;
            result.loadedFromCache = true;
            result.asset = std::move(cached);
            result.message = "Loaded full-course terrain map from derived data.";
            return result;
        }
    }
    result.asset = BuildAsset(snapshot);
    std::string error;
    if (!result.asset.Validate(&error)) {
        result.message = "Course Terrain Map bake failed validation: " + error;
        return result;
    }
    if (snapshot.bakeSettings.persistDerivedAsset) {
        if (!result.asset.SaveToFile(snapshot.cachePath.string(), &error)) {
            result.message = "Course Terrain Map bake could not persist: " + error;
            return result;
        }
        result.wroteCache = true;
    }
    result.succeeded = true;
    result.message = "Baked full-course procedural terrain map.";
    return result;
}

CourseTerrainMapAsset CourseTerrainMapBakePipeline::BuildAsset(
    const Snapshot& snapshot) {
    CourseTerrainMapAsset asset{};
    asset.sourceCourseName = snapshot.courseName;
    asset.sourceRailHash = snapshot.sourceRailHash;
    asset.sourceTerrainSettingsHash = snapshot.sourceTerrainSettingsHash;
    asset.sourceTerrainEditHash = snapshot.sourceTerrainEditHash;
    asset.bakeSettingsHash = snapshot.bakeSettingsHash;
    asset.sourceFingerprint = snapshot.sourceFingerprint;
    asset.contentRevision = snapshot.sourceFingerprint;
    asset.railLength = snapshot.rail.Length();
    const float maximum = (std::numeric_limits<float>::max)();
    asset.worldMinimum = {maximum, maximum, maximum};
    asset.worldMaximum = {-maximum, -maximum, -maximum};
    const uint32_t tileCount = (std::max)(1u, static_cast<uint32_t>(
        std::ceil(asset.railLength / snapshot.bakeSettings.tileLength)));
    for (uint32_t level = 0; level < 3u; ++level) {
        CourseTerrainMapLod lod{};
        lod.level = level;
        lod.longitudinalSegments = snapshot.bakeSettings.longitudinalSegments[level];
        lod.radialSegments = snapshot.bakeSettings.radialSegments[level];
        lod.tiles.reserve(tileCount);
        for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
            const float startDistance = asset.railLength *
                static_cast<float>(tileIndex) / static_cast<float>(tileCount);
            const float endDistance = asset.railLength *
                static_cast<float>(tileIndex + 1u) / static_cast<float>(tileCount);
            const uint32_t steps = (std::max)(2u, static_cast<uint32_t>(
                std::ceil(static_cast<float>(lod.longitudinalSegments) *
                    (endDistance - startDistance) / asset.railLength)));
            CourseTerrainMapTile tile = BuildTile(snapshot, startDistance,
                endDistance, steps, lod.radialSegments);
            if (tile.indices.empty()) continue;
            ExpandBounds(tile.worldMinimum, asset.worldMinimum, asset.worldMaximum);
            ExpandBounds(tile.worldMaximum, asset.worldMinimum, asset.worldMaximum);
            lod.tiles.push_back(std::move(tile));
        }
        asset.lods.push_back(std::move(lod));
    }
    return asset;
}

void CourseTerrainMapBakePipeline::StartWorker(Snapshot snapshot) {
    pendingFingerprint_ = snapshot.sourceFingerprint;
    ++lifetimeStats_.buildsStarted;
    worker_ = std::async(std::launch::async,
        [snapshot = std::move(snapshot)]() mutable {
            return RunWorker(std::move(snapshot));
        });
}

bool CourseTerrainMapBakePipeline::PollWorker(uint64_t expectedFingerprint) {
    if (!pendingFingerprint_.has_value() || !worker_.valid() ||
        worker_.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready) return false;
    WorkerResult completed{};
    try {
        completed = worker_.get();
    } catch (const std::exception& error) {
        completed.message = std::string("Course Terrain Map worker failed: ") + error.what();
    } catch (...) {
        completed.message = "Course Terrain Map worker failed with an unknown error.";
    }
    pendingFingerprint_.reset();
    ++lifetimeStats_.buildsCompleted;
    if (!completed.succeeded || completed.sourceFingerprint != expectedFingerprint ||
        completed.sourceFingerprint != latestRequestedFingerprint_) {
        if (completed.succeeded) ++lifetimeStats_.staleBuildsDiscarded;
        return true;
    }
    if (completed.loadedFromCache) ++lifetimeStats_.cacheLoads;
    if (completed.wroteCache) ++lifetimeStats_.cacheWrites;
    asset_ = std::move(completed.asset);
    RefreshAssetStats();
    lastResult_.loadedFromCache = completed.loadedFromCache;
    lastResult_.builtThisCall = !completed.loadedFromCache;
    return true;
}

void CourseTerrainMapBakePipeline::RefreshAssetStats() {
    lifetimeStats_.lods = static_cast<uint32_t>(asset_.lods.size());
    lifetimeStats_.tiles = 0;
    lifetimeStats_.vertices = 0;
    lifetimeStats_.triangles = 0;
    for (const CourseTerrainMapLod& lod : asset_.lods) {
        lifetimeStats_.tiles += static_cast<uint32_t>(lod.tiles.size());
        for (const CourseTerrainMapTile& tile : lod.tiles) {
            lifetimeStats_.vertices += static_cast<uint32_t>(tile.vertices.size());
            lifetimeStats_.triangles += static_cast<uint32_t>(tile.indices.size() / 3u);
        }
    }
}

uint64_t CourseTerrainMapBakePipeline::ResolveFingerprint(
    const CourseTerrainMapBakeInput& input,
    uint64_t* railHash,
    uint64_t* terrainSettingsHash,
    uint64_t* terrainEditHash,
    uint64_t* bakeSettingsHash) const {
    if (input.rail == nullptr || input.terrainSettings == nullptr) return 0u;
    const uint64_t resolvedRailHash = HashRail(input.rail->RuntimePath());
    const uint64_t resolvedSettingsHash = HashTerrainSettings(*input.terrainSettings);
    const uint64_t resolvedEditHash = input.terrainEdits != nullptr
        ? input.terrainEdits->ContentHashForRange(0.0f, input.rail->Length()) : 0u;
    const uint64_t resolvedBakeHash = HashBakeSettings(settings_);
    uint64_t hash = 1469598103934665603ull;
    hash = HashString(hash, input.courseName);
    hash = HashValue(hash, resolvedRailHash);
    hash = HashValue(hash, resolvedSettingsHash);
    hash = HashValue(hash, resolvedEditHash);
    hash = HashValue(hash, resolvedBakeHash);
    hash = HashValue(hash, kCourseTerrainMapAssetSchemaVersion);
    hash = HashValue(hash, kCourseTerrainMapBakerVersion);
    if (railHash != nullptr) *railHash = resolvedRailHash;
    if (terrainSettingsHash != nullptr) *terrainSettingsHash = resolvedSettingsHash;
    if (terrainEditHash != nullptr) *terrainEditHash = resolvedEditHash;
    if (bakeSettingsHash != nullptr) *bakeSettingsHash = resolvedBakeHash;
    return hash;
}

std::filesystem::path CourseTerrainMapBakePipeline::CachePath(
    std::string_view courseName) const {
    return projectRoot_ / cacheRoot_ /
        (SanitizedName(courseName) + ".courseterrainmap");
}

const char* ToString(CourseTerrainMapBakeStatus status) noexcept {
    switch (status) {
    case CourseTerrainMapBakeStatus::Missing: return "Missing";
    case CourseTerrainMapBakeStatus::Building: return "Building";
    case CourseTerrainMapBakeStatus::Current: return "Current";
    case CourseTerrainMapBakeStatus::Stale: return "Stale";
    case CourseTerrainMapBakeStatus::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace editor
