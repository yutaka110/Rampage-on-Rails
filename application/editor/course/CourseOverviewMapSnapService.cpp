#include "CourseOverviewMapSnapService.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

float SquaredDistance(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

} // namespace

bool CourseOverviewMapSnapSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(worldGridSize, .001f, 10000.0f) &&
        settings::InRange(railDistanceStep, .001f, 10000.0f) &&
        settings::InRange(lateralOffsetStep, .001f, 10000.0f) &&
        settings::InRange(controlPointMagnetPixels, 0.0f, 128.0f);
    return settings::Result(valid, errorMessage,
        "CourseOverviewMapSnapSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseOverviewMapSnapService::SetSettings(
    CourseOverviewMapSnapSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    settings_ = settings;
    return true;
}

CourseOverviewMapSnapResult CourseOverviewMapSnapService::SnapControlPoint(
    Vector2 mapPosition,
    float preservedDepth,
    const CourseOverviewMapProjection& projection,
    const CourseRailAuthoringModel& rail,
    std::string_view ignoredPointGuid) const {
    CourseOverviewMapSnapResult result{};
    if (!projection.State().valid || !rail.IsValid()) return result;
    result.valid = true;
    result.depth = preservedDepth;
    result.worldPosition = projection.Unproject(mapPosition, preservedDepth);
    if (settings_.worldGridEnabled) {
        result.worldPosition.x = SnapScalar(result.worldPosition.x, settings_.worldGridSize);
        result.worldPosition.y = SnapScalar(result.worldPosition.y, settings_.worldGridSize);
        result.worldPosition.z = SnapScalar(result.worldPosition.z, settings_.worldGridSize);
        result.flags = result.flags | CourseOverviewMapSnapFlags::WorldGrid;
    }
    auto projected = projection.ProjectWorld(result.worldPosition);
    if (projected.valid) result.mapPosition = projected.mapPosition;

    if (settings_.controlPointMagnetEnabled && settings_.controlPointMagnetPixels > 0.0f) {
        float closest = settings_.controlPointMagnetPixels * settings_.controlPointMagnetPixels;
        const RailPathControlPoint* target = nullptr;
        CourseOverviewMapProjectedPoint targetProjected{};
        for (const RailPathControlPoint& point : rail.RuntimePath().ControlPoints()) {
            if (point.editorGuid == ignoredPointGuid) continue;
            const auto candidate = projection.ProjectWorld(point.position);
            if (!candidate.valid) continue;
            const float distance = SquaredDistance(mapPosition, candidate.mapPosition);
            if (distance <= closest) {
                closest = distance;
                target = &point;
                targetProjected = candidate;
            }
        }
        if (target != nullptr) {
            result.worldPosition = target->position;
            result.mapPosition = targetProjected.mapPosition;
            result.depth = targetProjected.depth;
            result.snappedPointGuid = target->editorGuid;
            result.flags = result.flags | CourseOverviewMapSnapFlags::ControlPoint;
        }
    }
    const RailAnchorProjection anchor = rail.Project(result.worldPosition, 64);
    if (anchor.valid) {
        result.railAnchor = anchor.anchor;
        result.railDistance = anchor.resolution.railSample.distance;
    }
    return result;
}

CourseOverviewMapSnapResult CourseOverviewMapSnapService::SnapRailAnchor(
    Vector2 mapPosition,
    float preservedDepth,
    const CourseOverviewMapProjection& projection,
    const CourseRailAuthoringModel& rail) const {
    CourseOverviewMapSnapResult result{};
    if (!projection.State().valid || !rail.IsValid()) return result;

    RailAnchorProjection projected{};
    if (projection.Settings().mode == CourseOverviewMapProjectionMode::RailUnwrapped) {
        if (!std::isfinite(preservedDepth)) preservedDepth = 0.0f;
        const Vector2 raw = projection.MapToRaw(mapPosition);
        float distance = (std::clamp)(raw.x, 0.0f, rail.Length());
        float lateral = raw.y;
        if (settings_.railDistanceEnabled) {
            distance = SnapScalar(distance, settings_.railDistanceStep);
            result.flags = result.flags | CourseOverviewMapSnapFlags::RailDistance;
        }
        if (settings_.lateralOffsetEnabled) {
            lateral = SnapScalar(lateral, settings_.lateralOffsetStep);
            result.flags = result.flags | CourseOverviewMapSnapFlags::LateralOffset;
        }
        result.railAnchor = AnchorAtDistance(distance, rail);
        result.railAnchor.lateralOffset = lateral;
        result.railAnchor.verticalOffset = preservedDepth;
        const RailAnchorResolution resolved = rail.Resolve(result.railAnchor);
        if (!resolved.valid) return {};
        result.valid = true;
        result.worldPosition = resolved.worldPosition;
        result.railDistance = resolved.railSample.distance;
        result.depth = preservedDepth;
        result.mapPosition = projection.ProjectWorld(result.worldPosition).mapPosition;
        return result;
    }

    if (!std::isfinite(preservedDepth)) {
        const Vector3 probeWorld = projection.Unproject(mapPosition, 0.0f);
        const RailAnchorProjection probe = rail.Project(probeWorld, 64);
        preservedDepth = probe.valid
            ? projection.ProjectWorld(probe.resolution.railSample.position).depth
            : 0.0f;
    }
    Vector3 world = projection.Unproject(mapPosition, preservedDepth);
    if (settings_.worldGridEnabled) {
        world.x = SnapScalar(world.x, settings_.worldGridSize);
        world.y = SnapScalar(world.y, settings_.worldGridSize);
        world.z = SnapScalar(world.z, settings_.worldGridSize);
        result.flags = result.flags | CourseOverviewMapSnapFlags::WorldGrid;
    }
    projected = rail.Project(world, 64);
    if (!projected.valid) return result;
    float distance = projected.resolution.railSample.distance;
    if (settings_.railDistanceEnabled) {
        distance = SnapScalar(distance, settings_.railDistanceStep);
        result.flags = result.flags | CourseOverviewMapSnapFlags::RailDistance;
    }
    result.railAnchor = AnchorAtDistance(distance, rail);
    result.railAnchor.lateralOffset = projected.anchor.lateralOffset;
    result.railAnchor.verticalOffset = projected.anchor.verticalOffset;
    if (settings_.lateralOffsetEnabled) {
        result.railAnchor.lateralOffset = SnapScalar(
            result.railAnchor.lateralOffset, settings_.lateralOffsetStep);
        result.flags = result.flags | CourseOverviewMapSnapFlags::LateralOffset;
    }
    const RailAnchorResolution resolved = rail.Resolve(result.railAnchor);
    if (!resolved.valid) return {};
    result.valid = true;
    result.worldPosition = resolved.worldPosition;
    result.railDistance = resolved.railSample.distance;
    const auto finalProjected = projection.ProjectWorld(result.worldPosition);
    result.mapPosition = finalProjected.mapPosition;
    result.depth = finalProjected.depth;
    return result;
}

CourseOverviewMapSnapResult CourseOverviewMapSnapService::SnapRailDistance(
    Vector2 mapPosition,
    const CourseOverviewMapProjection& projection,
    const CourseRailAuthoringModel& rail) const {
    CourseOverviewMapSnapResult result = SnapRailAnchor(
        mapPosition, (std::numeric_limits<float>::quiet_NaN)(), projection, rail);
    if (!result.valid) return result;
    result.railAnchor.lateralOffset = 0.0f;
    result.railAnchor.verticalOffset = 0.0f;
    const RailAnchorResolution resolved = rail.Resolve(result.railAnchor);
    if (resolved.valid) {
        result.worldPosition = resolved.worldPosition;
        result.railDistance = resolved.railSample.distance;
        const auto projected = projection.ProjectWorld(result.worldPosition);
        result.mapPosition = projected.mapPosition;
        result.depth = projected.depth;
    }
    return result;
}

float CourseOverviewMapSnapService::SnapScalar(float value, float step) const {
    return std::round(value / step) * step;
}

RailAnchor CourseOverviewMapSnapService::AnchorAtDistance(
    float distance,
    const CourseRailAuthoringModel& rail) const {
    RailAnchor anchor{};
    if (rail.Segments().empty()) return anchor;
    distance = (std::clamp)(distance, 0.0f, rail.Length());
    const CourseRailSegment* selected = &rail.Segments().back();
    for (const CourseRailSegment& segment : rail.Segments()) {
        if (distance <= segment.startDistance + segment.length) {
            selected = &segment;
            break;
        }
    }
    float low = 0.0f;
    float high = 1.0f;
    for (uint32_t iteration = 0; iteration < 18; ++iteration) {
        const float middle = (low + high) * 0.5f;
        const float sampleDistance = rail.RuntimePath().EvaluateSegmentAt(
            selected->pointIndex, middle).distance;
        if (sampleDistance < distance) low = middle;
        else high = middle;
    }
    anchor.segmentGuid = selected->guid;
    anchor.normalizedT = (low + high) * 0.5f;
    return anchor;
}

} // namespace editor
