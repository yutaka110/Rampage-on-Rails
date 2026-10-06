#include "CourseRailCurveFitService.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace editor {
namespace {

Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Subtract(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 Scale(Vector3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}
float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vector3 value) { return std::sqrt(Dot(value, value)); }
float Distance(Vector3 a, Vector3 b) { return Length(Subtract(a, b)); }
bool Finite(Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float PointSegmentDistance(Vector3 point, Vector3 a, Vector3 b) {
    const Vector3 ab = Subtract(b, a);
    const float denominator = Dot(ab, ab);
    const float t = denominator > 0.000001f
        ? (std::clamp)(Dot(Subtract(point, a), ab) / denominator, 0.0f, 1.0f)
        : 0.0f;
    return Distance(point, Add(a, Scale(ab, t)));
}

void SimplifyPolyline(
    const std::vector<Vector3>& points,
    float tolerance,
    std::vector<bool>& keep) {
    if (points.size() < 3) return;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    ranges.emplace_back(0, points.size() - 1);
    while (!ranges.empty()) {
        const auto [first, last] = ranges.back();
        ranges.pop_back();
        if (last <= first + 1) continue;
        float greatest = -1.0f;
        std::size_t greatestIndex = first;
        for (std::size_t index = first + 1; index < last; ++index) {
            const float distance = PointSegmentDistance(
                points[index], points[first], points[last]);
            if (distance > greatest) {
                greatest = distance;
                greatestIndex = index;
            }
        }
        if (greatest <= tolerance) continue;
        keep[greatestIndex] = true;
        ranges.emplace_back(first, greatestIndex);
        ranges.emplace_back(greatestIndex, last);
    }
}

float PolylineLength(const std::vector<Vector3>& points) {
    float length = 0.0f;
    for (std::size_t index = 1; index < points.size(); ++index) {
        length += Distance(points[index - 1], points[index]);
    }
    return length;
}

float TriangleRadius(Vector3 a, Vector3 b, Vector3 c) {
    const float ab = Distance(a, b);
    const float bc = Distance(b, c);
    const float ca = Distance(c, a);
    const Vector3 cross{
        (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
        (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
        (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
    const float doubleArea = Length(cross);
    if (doubleArea <= 0.00001f) return (std::numeric_limits<float>::max)();
    return (ab * bc * ca) / (2.0f * doubleArea);
}

} // namespace

bool CourseRailCurveFitSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(minimumInputSpacing, .001f, 1000.0f) &&
        settings::InRange(simplificationTolerance, 0.0f, 1000.0f) &&
        settings::InRange(maximumControlPointSpacing, .01f, 10000.0f) &&
        settings::InRange(minimumSegmentLength, .001f, 1000.0f) &&
        settings::InRange(minimumTurnRadius, 0.0f, 100000.0f) &&
        settings::InRange(smoothingStrength, 0.0f, .49f) &&
        settings::InRange(smoothingIterations, 0u, 16u) &&
        settings::InRange(maximumInputSamples, 2u, 65536u) &&
        settings::InRange(maximumControlPoints, 2u, 4096u);
    return settings::Result(valid, errorMessage,
        "CourseRailCurveFitSettings requires finite values, valid ranges and bounded work budgets.");
}

CourseRailCurveFitResult CourseRailCurveFitService::Fit(
    const std::vector<Vector3>& samples,
    CourseRailCurveFitSettings settings,
    float corridorRadius,
    float speed) const {
    CourseRailCurveFitResult result{};
    if (!settings.Validate(&result.message)) return result;
    result.inputPoints = static_cast<uint32_t>(samples.size());
    settings.minimumInputSpacing =
        (std::clamp)(settings.minimumInputSpacing, 0.001f, 1000.0f);
    settings.simplificationTolerance =
        (std::clamp)(settings.simplificationTolerance, 0.0f, 1000.0f);
    settings.maximumControlPointSpacing =
        (std::clamp)(settings.maximumControlPointSpacing, 0.01f, 10000.0f);
    settings.minimumSegmentLength =
        (std::clamp)(settings.minimumSegmentLength, 0.001f, 1000.0f);
    settings.minimumTurnRadius = (std::max)(0.0f, settings.minimumTurnRadius);
    settings.smoothingStrength = (std::clamp)(settings.smoothingStrength, 0.0f, 0.49f);
    settings.smoothingIterations = (std::min)(settings.smoothingIterations, 16u);
    settings.maximumInputSamples =
        (std::clamp)(settings.maximumInputSamples, 2u, 65536u);
    settings.maximumControlPoints = (std::clamp)(settings.maximumControlPoints, 2u, 4096u);
    corridorRadius = (std::max)(corridorRadius, 0.01f);
    speed = (std::max)(speed, 0.0f);

    std::vector<Vector3> filtered;
    filtered.reserve(samples.size());
    const std::size_t inputLimit = (std::min)(
        samples.size(), static_cast<std::size_t>(settings.maximumInputSamples));
    for (std::size_t limitedIndex = 0; limitedIndex < inputLimit; ++limitedIndex) {
        const std::size_t sourceIndex = inputLimit == samples.size()
            ? limitedIndex
            : static_cast<std::size_t>(std::round(
                static_cast<double>(limitedIndex) * static_cast<double>(samples.size() - 1) /
                static_cast<double>(inputLimit - 1)));
        const Vector3 sample = samples[sourceIndex];
        if (!Finite(sample)) continue;
        if (!filtered.empty() && Distance(filtered.back(), sample) < settings.minimumInputSpacing) {
            filtered.back() = sample;
            continue;
        }
        filtered.push_back(sample);
    }
    if (samples.size() > inputLimit) {
        result.diagnostics.push_back({CourseRailCurveFitDiagnosticSeverity::Warning,
            "stroke.input_cap", "Stroke samples were deterministically reduced to the safety limit."});
    }
    result.filteredPoints = static_cast<uint32_t>(filtered.size());
    result.sourceLength = PolylineLength(filtered);
    if (filtered.size() < 2 || result.sourceLength < settings.minimumSegmentLength) {
        result.message = "Rail stroke is too short to form a valid curve.";
        result.diagnostics.push_back({CourseRailCurveFitDiagnosticSeverity::Error,
            "stroke.too_short", result.message});
        return result;
    }

    std::vector<bool> keep(filtered.size(), false);
    keep.front() = true;
    keep.back() = true;
    SimplifyPolyline(filtered, settings.simplificationTolerance, keep);
    std::vector<Vector3> simplified;
    for (std::size_t index = 0; index < filtered.size(); ++index) {
        if (keep[index]) simplified.push_back(filtered[index]);
    }
    result.simplifiedPoints = static_cast<uint32_t>(simplified.size());

    std::vector<Vector3> resampled;
    resampled.push_back(simplified.front());
    for (std::size_t index = 1; index < simplified.size(); ++index) {
        const Vector3 a = simplified[index - 1];
        const Vector3 b = simplified[index];
        const float length = Distance(a, b);
        const uint32_t divisions = (std::max)(1u, static_cast<uint32_t>(
            std::ceil(length / settings.maximumControlPointSpacing)));
        for (uint32_t division = 1; division <= divisions; ++division) {
            const float t = static_cast<float>(division) / static_cast<float>(divisions);
            resampled.push_back(Add(a, Scale(Subtract(b, a), t)));
        }
    }
    if (resampled.size() > settings.maximumControlPoints) {
        std::vector<Vector3> capped;
        capped.reserve(settings.maximumControlPoints);
        for (uint32_t index = 0; index < settings.maximumControlPoints; ++index) {
            const std::size_t sourceIndex = static_cast<std::size_t>(std::round(
                static_cast<double>(index) * static_cast<double>(resampled.size() - 1) /
                static_cast<double>(settings.maximumControlPoints - 1)));
            capped.push_back(resampled[sourceIndex]);
        }
        resampled = std::move(capped);
        result.diagnostics.push_back({CourseRailCurveFitDiagnosticSeverity::Warning,
            "curve.point_cap", "Curve was reduced to the configured control-point limit."});
    }

    for (uint32_t iteration = 0; iteration < settings.smoothingIterations; ++iteration) {
        std::vector<Vector3> smoothed = resampled;
        for (std::size_t index = 1; index + 1 < resampled.size(); ++index) {
            const Vector3 average = Scale(Add(resampled[index - 1], resampled[index + 1]), 0.5f);
            smoothed[index] = Add(
                Scale(resampled[index], 1.0f - settings.smoothingStrength),
                Scale(average, settings.smoothingStrength));
        }
        resampled = std::move(smoothed);
    }

    result.minimumObservedRadius = (std::numeric_limits<float>::max)();
    for (std::size_t index = 1; index + 1 < resampled.size(); ++index) {
        result.minimumObservedRadius = (std::min)(result.minimumObservedRadius,
            TriangleRadius(resampled[index - 1], resampled[index], resampled[index + 1]));
    }
    if (!std::isfinite(result.minimumObservedRadius) ||
        result.minimumObservedRadius == (std::numeric_limits<float>::max)()) {
        result.minimumObservedRadius = 0.0f;
    }
    if (result.minimumObservedRadius > 0.0f &&
        result.minimumObservedRadius < settings.minimumTurnRadius) {
        result.diagnostics.push_back({CourseRailCurveFitDiagnosticSeverity::Warning,
            "curve.tight_radius", "Fitted stroke contains a turn below the recommended radius."});
    }

    result.controlPoints.reserve(resampled.size());
    for (Vector3 position : resampled) {
        RailPathControlPoint point{};
        point.position = position;
        point.corridorRadius = corridorRadius;
        point.speed = speed;
        point.tangentMode = RailPathTangentMode::Auto;
        result.controlPoints.push_back(std::move(point));
    }
    result.fittedLength = PolylineLength(resampled);
    result.succeeded = result.controlPoints.size() >= 2;
    result.message = result.succeeded
        ? "Rail stroke fitted successfully."
        : "Rail stroke fitting did not produce enough control points.";
    return result;
}

const char* ToString(CourseRailCurveFitDiagnosticSeverity severity) {
    switch (severity) {
    case CourseRailCurveFitDiagnosticSeverity::Info: return "Info";
    case CourseRailCurveFitDiagnosticSeverity::Warning: return "Warning";
    case CourseRailCurveFitDiagnosticSeverity::Error: return "Error";
    }
    return "Unknown";
}

} // namespace editor
