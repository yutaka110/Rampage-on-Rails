#pragma once

#include "CourseAsset.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace RailCrystalCavern {
inline constexpr const char* CoursePath = "Resources/courses/CrystalCavern.course";
inline constexpr const char* CrystalMesh = "crystal_cavern_cluster";
inline constexpr const char* CrystalLandmarkMesh = "crystal_cavern_landmark";
inline constexpr float TimeBudgetSeconds = 100.0f;

inline bool IsCrystalMesh(std::string_view mesh) {
    return mesh == CrystalMesh || mesh == CrystalLandmarkMesh;
}

// Keep the original cubic up to the branch, including the bridge landing.
// The left half of a subdivided Bezier has the same points and tangents as
// the original curve on that interval.
inline bool Append(CourseAsset& course, const RailPath& incoming,
                   const CourseAsset& cavern, float joinDistance) {
    if (!cavern.IsValid() || joinDistance <= 0.0f || joinDistance >= incoming.Length()) return false;

    uint32_t index = 0;
    while (index + 1 < incoming.SegmentCount() &&
           incoming.SegmentStartDistance(index + 1) < joinDistance) ++index;
    const float segmentLength = incoming.SegmentLength(index);
    if (segmentLength <= 0.001f) return false;

    const float t = (joinDistance - incoming.SegmentStartDistance(index)) / segmentLength;
    const auto splice = incoming.EvaluateSegmentAt(index, t);
    const auto& prefix = incoming.ControlPoints();
    const Vector3 outgoing = incoming.TangentHandlePosition(index, false);
    const Vector3 nextIncoming = incoming.TangentHandlePosition(index + 1, true);
    const auto lerp = [t](Vector3 a, Vector3 b) {
        return Vector3{std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t)};
    };
    const auto offset = [](Vector3 from, Vector3 to) {
        return Vector3{to.x - from.x, to.y - from.y, to.z - from.z};
    };
    const Vector3 leftHandle = lerp(prefix[index].position, outgoing);
    const Vector3 rightHandle = lerp(lerp(outgoing, nextIncoming),
                                     lerp(nextIncoming, prefix[index + 1].position));
    const Vector3 spliceIncoming = lerp(leftHandle, lerp(outgoing, nextIncoming));
    const Vector3 splicePosition = lerp(spliceIncoming, rightHandle);

    course.railPoints.assign(prefix.begin(), prefix.begin() + index + 1);
    auto& last = course.railPoints.back();
    last.tangentMode = RailPathTangentMode::Broken;
    last.incomingTangent = offset(last.position, incoming.TangentHandlePosition(index, true));
    last.outgoingTangent = offset(last.position, leftHandle);

    RailPath localPath;
    cavern.ApplyToRailPath(localPath);
    const Vector3 origin = cavern.railPoints.front().position;
    const auto orient = [&](Vector3 local) {
        return Vector3{splice.right.x * local.x + splice.up.x * local.y + splice.tangent.x * local.z,
                       splice.right.y * local.x + splice.up.y * local.y + splice.tangent.y * local.z,
                       splice.right.z * local.x + splice.up.z * local.y + splice.tangent.z * local.z};
    };
    for (auto point : cavern.railPoints) {
        const Vector3 relative = orient(offset(origin, point.position));
        point.position = {splicePosition.x + relative.x, splicePosition.y + relative.y, splicePosition.z + relative.z};
        point.incomingTangent = orient(point.incomingTangent);
        point.outgoingTangent = orient(point.outgoingTangent);
        course.railPoints.push_back(std::move(point));
    }
    auto& entry = course.railPoints[index + 1];
    entry.tangentMode = RailPathTangentMode::Broken;
    entry.incomingTangent = offset(splicePosition, spliceIncoming);
    entry.outgoingTangent = orient(offset(origin, localPath.TangentHandlePosition(0, false)));
    entry.corridorRadius = splice.corridorRadius;
    entry.speed = splice.speed;

    const auto appendRanges = [joinDistance](auto& destination, const auto& source) {
        std::erase_if(destination, [joinDistance](const auto& item) { return item.startDistance >= joinDistance; });
        for (auto& item : destination) item.endDistance = (std::min)(item.endDistance, joinDistance);
        for (auto item : source) {
            item.startDistance += joinDistance;
            item.endDistance += joinDistance;
            destination.push_back(std::move(item));
        }
    };
    const auto appendKeys = [joinDistance](auto& destination, const auto& source) {
        std::erase_if(destination, [joinDistance](const auto& item) { return item.distance >= joinDistance; });
        for (auto item : source) {
            item.distance += joinDistance;
            destination.push_back(std::move(item));
        }
    };

    auto approach = course.EvaluateCamera(joinDistance - 55.0f);
    approach.distance = joinDistance - 55.0f;
    approach.editorGuid = "crystal-camera-handoff";
    auto caveLights = cavern.lightingPresets;
    auto caveMaterials = cavern.terrainMaterialPresets;
    if (!caveLights.empty()) caveLights.front().blendDistance = 75.0f;
    if (!caveMaterials.empty()) caveMaterials.front().blendDistance = 75.0f;
    appendRanges(course.sections, cavern.sections);
    appendRanges(course.rideProfiles, cavern.rideProfiles);
    appendRanges(course.cinematicCameraShots, cavern.cinematicCameraShots);
    appendRanges(course.cinematicShotSets, cavern.cinematicShotSets);
    appendKeys(course.cameraKeys, cavern.cameraKeys);
    if (!cavern.cameraKeys.empty()) course.cameraKeys.push_back(std::move(approach));
    appendKeys(course.lightingPresets, caveLights);
    appendKeys(course.terrainMaterialPresets, caveMaterials);
    appendKeys(course.terrainPlacements, cavern.terrainPlacements);
    const auto& stamps = cavern.terrainEditLayer.Stamps();
    for (auto first = stamps.begin(); first != stamps.end();) {
        const auto end = std::find_if(first, stamps.end(), [&](const auto& stamp) {
            return stamp.strokeGuid != first->strokeGuid;
        });
        std::vector<TerrainBrushStamp> shifted(first, end);
        for (auto& stamp : shifted) stamp.distance += joinDistance;
        if (!course.terrainEditLayer.ApplyStroke(shifted)) return false;
        first = end;
    }
    course.SortForRuntime();
    return true;
}
}
