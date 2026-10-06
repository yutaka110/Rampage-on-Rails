#include "CourseMap3DViewportRenderer.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>

namespace editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 Scale(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}
Vector3 Normalize(Vector3 value, Vector3 fallback) {
    const float lengthSquared = Dot(value, value);
    return lengthSquared > 1.0e-8f
        ? Scale(value, 1.0f / std::sqrt(lengthSquared)) : fallback;
}

EditorObjectHandle MakeHandle(EditorDomainId domain, std::string stableId,
    uint64_t index, uint32_t generation, std::string name) {
    return {domain, std::move(stableId), index, generation, std::move(name)};
}

bool Selected(const EditorSelection* selection, const EditorObjectHandle& handle) {
    return selection != nullptr && selection->Contains(handle);
}

float PixelsToWorld(float pixels, float depth, const CourseMap3DCamera& camera,
    CourseOverviewMapRect viewport) {
    return pixels * 2.0f * depth * std::tan(camera.verticalFovRadians * 0.5f) /
        (std::max)(1.0f, viewport.height);
}

} // namespace

CourseMap3DCameraBasis BuildCourseMap3DCameraBasis(
    const CourseMap3DCamera& camera) noexcept {
    CourseMap3DCameraBasis basis{};
    const float cp = std::cos(camera.pitchRadians);
    basis.forward = Normalize({std::sin(camera.yawRadians) * cp,
        std::sin(camera.pitchRadians), std::cos(camera.yawRadians) * cp},
        {0.0f, 0.0f, 1.0f});
    basis.right = Normalize(Cross({0.0f, 1.0f, 0.0f}, basis.forward),
        {1.0f, 0.0f, 0.0f});
    basis.up = Normalize(Cross(basis.forward, basis.right),
        {0.0f, 1.0f, 0.0f});
    basis.position = Sub(camera.target, Scale(basis.forward, camera.distance));
    return basis;
}

CourseMap3DProjectedPoint ProjectCourseMap3DPoint(Vector3 world,
    const CourseMap3DCamera& camera, CourseOverviewMapRect viewport) noexcept {
    CourseMap3DProjectedPoint result{};
    if (!viewport.Valid()) return result;
    const CourseMap3DCameraBasis basis = BuildCourseMap3DCameraBasis(camera);
    const Vector3 relative = Sub(world, basis.position);
    const float depth = Dot(relative, basis.forward);
    if (!std::isfinite(depth) || depth <= camera.nearPlane || depth >= camera.farPlane) {
        return result;
    }
    const float tanHalf = std::tan(camera.verticalFovRadians * 0.5f);
    const float aspect = viewport.width / viewport.height;
    const float nx = Dot(relative, basis.right) / (depth * tanHalf * aspect);
    const float ny = Dot(relative, basis.up) / (depth * tanHalf);
    if (!std::isfinite(nx) || !std::isfinite(ny)) return result;
    result.valid = true;
    result.screen = {viewport.x + viewport.width * (0.5f + nx * 0.5f),
        viewport.y + viewport.height * (0.5f - ny * 0.5f)};
    result.depth = depth;
    return result;
}

CourseMap3DRay BuildCourseMap3DScreenRay(Vector2 screen,
    const CourseMap3DCamera& camera, CourseOverviewMapRect viewport) noexcept {
    CourseMap3DRay result{};
    if (!viewport.Valid() || !viewport.Contains(screen)) return result;
    const CourseMap3DCameraBasis basis = BuildCourseMap3DCameraBasis(camera);
    const float nx = ((screen.x - viewport.x) / viewport.width) * 2.0f - 1.0f;
    const float ny = 1.0f - ((screen.y - viewport.y) / viewport.height) * 2.0f;
    const float tanHalf = std::tan(camera.verticalFovRadians * 0.5f);
    const float aspect = viewport.width / viewport.height;
    result.valid = true;
    result.origin = basis.position;
    result.direction = Normalize(Add(basis.forward,
        Add(Scale(basis.right, nx * tanHalf * aspect),
            Scale(basis.up, ny * tanHalf))), basis.forward);
    return result;
}

CourseMap3DFrame CourseMap3DViewportRenderer::Build(
    const CourseMap3DRenderInput& input) const {
    CourseMap3DFrame frame{};
    frame.viewport = input.viewport;
    frame.camera = input.camera;
    frame.basis = BuildCourseMap3DCameraBasis(input.camera);
    if (!input.viewport.Valid() || input.rail == nullptr || !input.rail->IsValid()) {
        frame.message = "3D Course View requires a valid viewport and rail.";
        return frame;
    }
    frame.valid = true;

    if (settings_.showGrid) {
        const float extent = settings_.gridExtent;
        const float step = settings_.gridStep;
        for (float offset = -extent; offset <= extent + 0.01f; offset += step) {
            const Vector3 a{input.camera.target.x - extent, input.camera.target.y,
                input.camera.target.z + offset};
            const Vector3 b{input.camera.target.x + extent, input.camera.target.y,
                input.camera.target.z + offset};
            const Vector3 c{input.camera.target.x + offset, input.camera.target.y,
                input.camera.target.z - extent};
            const Vector3 d{input.camera.target.x + offset, input.camera.target.y,
                input.camera.target.z + extent};
            for (const auto pair : {std::pair{a, b}, std::pair{c, d}}) {
                const auto pa = ProjectCourseMap3DPoint(pair.first, input.camera, input.viewport);
                const auto pb = ProjectCourseMap3DPoint(pair.second, input.camera, input.viewport);
                if (pa.valid && pb.valid) {
                    frame.gridLines.push_back({CourseOverviewMapItemKind::None,
                        pa.screen, pb.screen, pair.first, pair.second, pa.depth, pb.depth,
                        0x28387278u, 1.0f});
                }
            }
        }
    }

    if (settings_.showTerrain && input.terrain != nullptr) {
        const CourseTerrainMapLod* lod = input.terrain->FindLod(0u);
        if (lod == nullptr && !input.terrain->lods.empty()) lod = &input.terrain->lods.front();
        uint32_t inspected = 0;
        if (lod != nullptr) {
            for (const CourseTerrainMapTile& tile : lod->tiles) {
                const std::size_t triangleCount = tile.indices.size() / 3u;
                for (std::size_t i = 0; i < triangleCount &&
                     inspected < settings_.terrainTriangleBudget; ++i, ++inspected) {
                    const std::size_t sample = triangleCount <=
                            settings_.terrainTriangleBudget
                        ? i : i * triangleCount / settings_.terrainTriangleBudget;
                    const std::size_t offset = sample * 3u;
                    const Vector3 a = tile.vertices[tile.indices[offset]].position;
                    const Vector3 b = tile.vertices[tile.indices[offset + 1u]].position;
                    const Vector3 c = tile.vertices[tile.indices[offset + 2u]].position;
                    const auto pa = ProjectCourseMap3DPoint(a, input.camera, input.viewport);
                    const auto pb = ProjectCourseMap3DPoint(b, input.camera, input.viewport);
                    const auto pc = ProjectCourseMap3DPoint(c, input.camera, input.viewport);
                    if (!pa.valid || !pb.valid || !pc.valid) continue;
                    frame.terrainTriangles.push_back({pa.screen, pb.screen, pc.screen,
                        (pa.depth + pb.depth + pc.depth) / 3.0f, 0x442c8272u});
                }
            }
        }
        frame.stats.terrainTrianglesInspected = inspected;
        frame.stats.terrainTrianglesDrawn =
            static_cast<uint32_t>(frame.terrainTriangles.size());
        std::stable_sort(frame.terrainTriangles.begin(), frame.terrainTriangles.end(),
            [](const CourseMap3DTriangle& a, const CourseMap3DTriangle& b) {
                return a.depth > b.depth;
        });
    }

    if (input.sceneVisualization != nullptr && input.sceneVisualization->valid) {
        for (const CourseMapScreenSpaceProxy& proxy :
            input.sceneVisualization->screenSpaceProxies) {
            const auto projected = ProjectCourseMap3DPoint(
                proxy.worldPosition, input.camera, input.viewport);
            if (!projected.valid) continue;
            const EditorObjectHandle handle = MakeHandle(proxy.domain,
                proxy.stableId, proxy.localIndex, 0u, proxy.displayName);
            const float radius = proxy.selected ? 9.0f : 7.0f;
            CourseMap3DMarker marker{};
            marker.kind = CourseOverviewMapItemKind::None;
            marker.screen = projected.screen;
            marker.world = proxy.worldPosition;
            marker.depth = projected.depth;
            marker.radiusPixels = radius;
            marker.worldRadius = PixelsToWorld(
                radius + 4.0f, projected.depth, input.camera, input.viewport);
            marker.color = proxy.fillColor;
            marker.handle = handle;
            marker.guid = proxy.stableId;
            marker.selectable = proxy.domain != EditorDomainId::Unknown;
            marker.selected = proxy.selected;
            marker.locked = proxy.locked;
            marker.visualKind = proxy.kind;
            frame.markers.push_back(std::move(marker));
            if (settings_.showLabels && proxy.selected) {
                frame.labels.push_back({{projected.screen.x + radius + 4.0f,
                    projected.screen.y - radius}, projected.depth,
                    proxy.outlineColor, proxy.displayName});
            }
            ++frame.stats.sceneProxies;
        }
        for (const CourseMapSceneActorProxy& actor :
            input.sceneVisualization->actors) {
            // Authored placements already have canonical handles below. The
            // semantic scene frame contributes legacy/encounter preview craft.
            if (actor.kind != CourseMapSceneVisualKind::EncounterEnemy) continue;
            const auto projected = ProjectCourseMap3DPoint(
                actor.worldPosition, input.camera, input.viewport);
            if (!projected.valid) continue;
            const float radius = (std::clamp)(actor.radiusPixels, 7.0f, 16.0f);
            CourseMap3DMarker marker{};
            marker.kind = CourseOverviewMapItemKind::EnemyPlacement;
            marker.screen = projected.screen;
            marker.world = actor.worldPosition;
            marker.depth = projected.depth;
            marker.radiusPixels = radius;
            marker.worldRadius = PixelsToWorld(
                radius + 4.0f, projected.depth, input.camera, input.viewport);
            marker.color = actor.fillColor;
            marker.guid = actor.stableId;
            marker.enabled = actor.enabled;
            marker.locked = actor.locked;
            marker.visualKind = actor.kind;
            frame.markers.push_back(std::move(marker));

            const auto heading = ProjectCourseMap3DPoint(
                actor.worldHeadingEnd, input.camera, input.viewport);
            if (heading.valid) {
                CourseMap3DLine line{};
                line.start = projected.screen;
                line.end = heading.screen;
                line.worldStart = actor.worldPosition;
                line.worldEnd = actor.worldHeadingEnd;
                line.startDepth = projected.depth;
                line.endDepth = heading.depth;
                line.color = actor.outlineColor;
                line.thickness = 1.4f;
                frame.lines.push_back(std::move(line));
            }
            if (settings_.showLabels &&
                (actor.clusterCount > 1u || projected.depth < input.camera.distance)) {
                frame.labels.push_back({{projected.screen.x + radius + 4.0f,
                    projected.screen.y - radius}, projected.depth,
                    actor.outlineColor, actor.displayName});
            }
            frame.stats.encounterEnemies += actor.clusterCount;
        }
    }

    const RailPath& path = input.rail->RuntimePath();
    const auto& segments = input.rail->Segments();
    const uint32_t samples = (std::max)(2u, settings_.samplesPerRailSegment);
    for (uint32_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
        const CourseRailSegment& segment = segments[segmentIndex];
        const EditorObjectHandle handle = MakeHandle(EditorDomainId::CourseRailSegment,
            "course-rail-segment:" + segment.guid, segmentIndex,
            input.railGeneration, "Rail Segment");
        const bool selected = Selected(input.selection, handle);
        for (uint32_t sample = 1; sample < samples; ++sample) {
            const RailPathSample a = path.EvaluateSegmentAt(segmentIndex,
                static_cast<float>(sample - 1u) / static_cast<float>(samples - 1u));
            const RailPathSample b = path.EvaluateSegmentAt(segmentIndex,
                static_cast<float>(sample) / static_cast<float>(samples - 1u));
            const auto pa = ProjectCourseMap3DPoint(a.position, input.camera, input.viewport);
            const auto pb = ProjectCourseMap3DPoint(b.position, input.camera, input.viewport);
            if (!pa.valid || !pb.valid) continue;
            frame.lines.push_back({CourseOverviewMapItemKind::RailSegment,
                pa.screen, pb.screen, a.position, b.position, pa.depth, pb.depth,
                selected ? 0xff52ff8au : 0xffd89b44u, selected ? 4.0f : 2.8f,
                7.0f, handle, segment.guid, true, selected});
            ++frame.stats.railLines;
        }
    }

    const auto& points = path.ControlPoints();
    for (uint32_t index = 0; index < points.size(); ++index) {
        const auto projected = ProjectCourseMap3DPoint(
            points[index].position, input.camera, input.viewport);
        if (!projected.valid) continue;
        const EditorObjectHandle handle = MakeHandle(
            EditorDomainId::CourseRailControlPoint,
            "course-rail-point:" + points[index].editorGuid, index,
            input.railGeneration, "Rail Control Point");
        const bool selected = Selected(input.selection, handle);
        const float radius = selected ? 7.0f : 5.0f;
        frame.markers.push_back({CourseOverviewMapItemKind::RailControlPoint,
            projected.screen, points[index].position, projected.depth, radius,
            PixelsToWorld(radius + 3.0f, projected.depth, input.camera, input.viewport),
            index + 1u < points.size() ? segments[index].startDistance : input.rail->Length(),
            selected ? 0xff52ff8au : 0xffffd37au, handle,
            points[index].editorGuid, true, selected, true, false});
        ++frame.stats.controlPoints;
    }

    if (input.enemies != nullptr && input.enemies->IsValid()) {
        const auto& placements = input.enemies->Placements();
        for (uint32_t index = 0; index < placements.size(); ++index) {
            const CourseEnemyPlacement& placement = placements[index];
            if (!placement.editorVisible) continue;
            const CourseEnemyPlacementResolution resolved = input.enemies->Resolve(placement);
            if (!resolved.valid) continue;
            const auto projected = ProjectCourseMap3DPoint(
                resolved.worldPosition, input.camera, input.viewport);
            if (!projected.valid) continue;
            const EditorObjectHandle handle = MakeHandle(EditorDomainId::CourseEnemyPlacement,
                "course-enemy-placement:" + placement.editorGuid, index,
                input.enemyGeneration, placement.actorAssetId.empty()
                    ? "Enemy Placement" : placement.actorAssetId);
            const bool selected = Selected(input.selection, handle);
            const float radius = selected ? 9.0f : 7.0f;
            uint32_t color = placement.enabled ? 0xff55dfffu : 0xff6a6a6au;
            if (placement.editorLocked) color = 0xffb36cffu;
            if (selected) color = 0xff52ff8au;
            frame.markers.push_back({CourseOverviewMapItemKind::EnemyPlacement,
                projected.screen, resolved.worldPosition, projected.depth, radius,
                PixelsToWorld(radius + 4.0f, projected.depth, input.camera, input.viewport),
                resolved.runtimeDistance, color, handle, placement.editorGuid,
                true, selected, placement.enabled, placement.editorLocked});
            if (settings_.showLabels && (selected || projected.depth < input.camera.distance * 1.8f)) {
                frame.labels.push_back({{projected.screen.x + radius + 4.0f,
                    projected.screen.y - radius}, projected.depth, color, handle.displayName});
            }
            ++frame.stats.enemies;
        }
    }

    if (input.waves != nullptr && input.waves->IsValid()) {
        const auto& waves = input.waves->Waves();
        for (uint32_t index = 0; index < waves.size(); ++index) {
            const CourseWaveDefinition& wave = waves[index];
            if (!wave.editorVisible) continue;
            const RailPathSample sample = path.Evaluate((std::clamp)(
                wave.triggerRailDistance, 0.0f, input.rail->Length()));
            const auto projected = ProjectCourseMap3DPoint(
                sample.position, input.camera, input.viewport);
            if (!projected.valid) continue;
            const EditorObjectHandle handle = MakeHandle(EditorDomainId::CourseWaveDefinition,
                "course-wave:" + wave.editorGuid, index, input.waveGeneration,
                wave.displayName.empty() ? "Course Wave" : wave.displayName);
            const bool selected = Selected(input.selection, handle);
            const float radius = selected ? 10.0f : 8.0f;
            frame.markers.push_back({CourseOverviewMapItemKind::Wave,
                projected.screen, sample.position, projected.depth, radius,
                PixelsToWorld(radius + 4.0f, projected.depth, input.camera, input.viewport),
                sample.distance, selected ? 0xff52ff8au : 0xffffb84fu,
                handle, wave.editorGuid, true, selected, wave.enabled,
                wave.editorLocked});
            ++frame.stats.waves;
        }
    }
    frame.message = "Perspective 3D Course View";
    return frame;
}

bool CourseMap3DRenderSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(samplesPerRailSegment, 4u, 128u) &&
        settings::InRange(terrainTriangleBudget, 100u, 100000u) &&
        settings::InRange(gridExtent, 10.0f, 100000.0f) &&
        settings::InRange(gridStep, 1.0f, gridExtent);
    return settings::Result(valid, errorMessage,
        "CourseMap3DRenderSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMap3DViewportRenderer::SetSettings(
    CourseMap3DRenderSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    ++settingsRevision_;
    return true;
}

} // namespace editor
