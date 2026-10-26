#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "utils/math/Vector.h"

enum class RailPathTangentMode : uint8_t {
    Auto,
    Mirrored,
    Broken,
};

struct RailPathControlPoint {
    Vector3 position{};
    float corridorRadius = 18.0f;
    float speed = 32.0f;
    // Authoring identity. Runtime evaluation ignores it, but keeping it in the
    // source point prevents index-based references from breaking after edits.
    std::string editorGuid;
    // Handle offsets are relative to position. incomingTangent points toward
    // the previous segment and outgoingTangent toward the next segment.
    RailPathTangentMode tangentMode = RailPathTangentMode::Auto;
    Vector3 incomingTangent{};
    Vector3 outgoingTangent{};
};

struct RailPathSample {
    Vector3 position{};
    Vector3 tangent{0.0f, 0.0f, 1.0f};
    Vector3 up{0.0f, 1.0f, 0.0f};
    Vector3 right{1.0f, 0.0f, 0.0f};
    float corridorRadius = 18.0f;
    float speed = 32.0f;
    float distance = 0.0f;
};

class RailPath {
public:
    void BuildDefaultCanyonPath(float corridorRadius);
    void SetControlPoints(std::vector<RailPathControlPoint> points);

    // Open course sampling: never wrap a terminal pose to the course start.
    RailPathSample Evaluate(float distance) const;
    // Scenery and camera look-ahead continue along the endpoint tangent.
    RailPathSample EvaluateExtended(float distance) const;
    std::vector<Vector3> SamplePolyline(float startDistance, float endDistance, float step) const;

    float Length() const { return totalLength_; }
    uint32_t SegmentCount() const {
        return controlPoints_.size() > 1
            ? static_cast<uint32_t>(controlPoints_.size() - 1)
            : 0;
    }
    float SegmentStartDistance(uint32_t segmentIndex) const;
    float SegmentLength(uint32_t segmentIndex) const;
    RailPathSample EvaluateSegmentAt(uint32_t segmentIndex, float normalizedT) const;
    Vector3 TangentHandlePosition(uint32_t pointIndex, bool incoming) const;
    const std::vector<RailPathControlPoint>& ControlPoints() const { return controlPoints_; }

private:
    void RebuildArcTable();

    Vector3 EvaluateSegment(uint32_t segmentIndex, float t) const;
    Vector3 EvaluateSegmentTangent(uint32_t segmentIndex, float t) const;
    Vector3 AutoTangentHandlePosition(uint32_t pointIndex, bool incoming) const;
    float EvaluateRadius(uint32_t segmentIndex, float t) const;
    float EvaluateSpeed(uint32_t segmentIndex, float t) const;

    std::vector<RailPathControlPoint> controlPoints_;
    std::vector<float> cumulativeLengths_;
    float totalLength_ = 0.0f;
};
