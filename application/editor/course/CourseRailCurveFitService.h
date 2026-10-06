#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../terrain/RailPath.h"

namespace editor {

enum class CourseRailCurveFitDiagnosticSeverity : uint8_t {
    Info,
    Warning,
    Error,
};

struct CourseRailCurveFitDiagnostic final {
    CourseRailCurveFitDiagnosticSeverity severity =
        CourseRailCurveFitDiagnosticSeverity::Info;
    std::string code;
    std::string message;
};

struct CourseRailCurveFitSettings final {
    float minimumInputSpacing = 0.35f;
    float simplificationTolerance = 0.8f;
    float maximumControlPointSpacing = 12.0f;
    float minimumSegmentLength = 0.20f;
    float minimumTurnRadius = 3.0f;
    float smoothingStrength = 0.18f;
    uint32_t smoothingIterations = 2;
    uint32_t maximumInputSamples = 4096;
    uint32_t maximumControlPoints = 256;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseRailCurveFitResult final {
    bool succeeded = false;
    std::vector<RailPathControlPoint> controlPoints;
    std::vector<CourseRailCurveFitDiagnostic> diagnostics;
    uint32_t inputPoints = 0;
    uint32_t filteredPoints = 0;
    uint32_t simplifiedPoints = 0;
    float sourceLength = 0.0f;
    float fittedLength = 0.0f;
    float minimumObservedRadius = 0.0f;
    std::string message;
};

// Deterministic production curve fitting for pointer strokes: de-noise,
// RDP simplify, distance resample, smooth, and validate curvature.
class CourseRailCurveFitService final {
public:
    CourseRailCurveFitResult Fit(
        const std::vector<Vector3>& samples,
        CourseRailCurveFitSettings settings,
        float corridorRadius,
        float speed) const;
};

const char* ToString(CourseRailCurveFitDiagnosticSeverity severity);

} // namespace editor
