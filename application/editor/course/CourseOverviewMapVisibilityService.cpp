#include "CourseOverviewMapVisibilityService.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace editor {
namespace {

struct Bounds final {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

Bounds ExpandedBounds(const CourseOverviewMapRect& rect, float amount) {
    return {rect.x - amount, rect.y - amount,
        rect.x + rect.width + amount, rect.y + rect.height + amount};
}

bool Contains(const Bounds& bounds, Vector2 point, float radius = 0.0f) {
    return point.x + radius >= bounds.left && point.x - radius <= bounds.right &&
        point.y + radius >= bounds.top && point.y - radius <= bounds.bottom;
}

bool Intersects(const Bounds& bounds, Vector2 a, Vector2 b) {
    const float left = (std::min)(a.x, b.x);
    const float right = (std::max)(a.x, b.x);
    const float top = (std::min)(a.y, b.y);
    const float bottom = (std::max)(a.y, b.y);
    return right >= bounds.left && left <= bounds.right &&
        bottom >= bounds.top && top <= bounds.bottom;
}

float DistanceSquared(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

float PointSegmentDistanceSquared(Vector2 point, Vector2 a, Vector2 b) {
    const float x = b.x - a.x;
    const float y = b.y - a.y;
    const float lengthSquared = x * x + y * y;
    if (lengthSquared <= 0.000001f) return DistanceSquared(point, a);
    const float t = (std::clamp)(
        ((point.x - a.x) * x + (point.y - a.y) * y) / lengthSquared,
        0.0f, 1.0f);
    return DistanceSquared(point, {a.x + x * t, a.y + y * t});
}

void SimplifyRange(
    const std::vector<Vector2>& source,
    std::size_t first,
    std::size_t last,
    float errorSquared,
    std::vector<uint8_t>& keep) {
    if (last <= first + 1u) return;
    float maximum = -1.0f;
    std::size_t split = first;
    for (std::size_t index = first + 1u; index < last; ++index) {
        const float distance = PointSegmentDistanceSquared(
            source[index], source[first], source[last]);
        if (distance > maximum) {
            maximum = distance;
            split = index;
        }
    }
    if (maximum <= errorSquared) return;
    keep[split] = 1u;
    SimplifyRange(source, first, split, errorSquared, keep);
    SimplifyRange(source, split, last, errorSquared, keep);
}

std::vector<Vector2> SimplifyPolyline(
    const std::vector<Vector2>& source,
    float pixelError) {
    if (source.size() <= 2u || pixelError <= 0.0f) return source;
    std::vector<uint8_t> keep(source.size(), 0u);
    keep.front() = keep.back() = 1u;
    SimplifyRange(source, 0u, source.size() - 1u,
        pixelError * pixelError, keep);
    std::vector<Vector2> result;
    result.reserve(source.size());
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (keep[index] != 0u) result.push_back(source[index]);
    }
    return result;
}

bool SameRailBatch(
    const CourseOverviewMapDrawBatch& batch,
    const CourseOverviewMapLine& line,
    const CourseOverviewMapLine& previous) {
    return batch.kind == line.kind && batch.color == line.color &&
        batch.thickness == line.thickness && previous.guid == line.guid &&
        previous.handle.SameObject(line.handle) &&
        DistanceSquared(batch.points.back(), line.start) <= 0.01f;
}

bool SameSettings(
    const CourseOverviewMapVisibilitySettings& lhs,
    const CourseOverviewMapVisibilitySettings& rhs) {
    return lhs.overscanPixels == rhs.overscanPixels &&
        lhs.railLodPixelError == rhs.railLodPixelError &&
        lhs.maximumLabels == rhs.maximumLabels &&
        lhs.labelCellPixels == rhs.labelCellPixels &&
        lhs.estimatedGlyphWidth == rhs.estimatedGlyphWidth &&
        lhs.estimatedLabelHeight == rhs.estimatedLabelHeight &&
        lhs.labelPaddingPixels == rhs.labelPaddingPixels &&
        lhs.pixelsPerLabel == rhs.pixelsPerLabel;
}

int LabelPriority(const CourseOverviewMapLabel& label) {
    if (label.selected) return 10000;
    switch (label.kind) {
    case CourseOverviewMapItemKind::Wave: return 300;
    case CourseOverviewMapItemKind::EnemyPlacement: return 200;
    default: return 100;
    }
}

uint64_t CellKey(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u) |
        static_cast<uint32_t>(y);
}

} // namespace

const CourseOverviewMapVisibleFrame& CourseOverviewMapVisibilityService::Build(
    const CourseOverviewMapFrame& source,
    uint64_t sourceRevision) {
    if (frame_.valid && source.valid && cachedSourceRevision_ == sourceRevision &&
        cachedSettingsRevision_ == state_.settingsRevision) {
        ++state_.cacheHits;
        return frame_;
    }

    frame_ = {};
    cachedSourceRevision_ = sourceRevision;
    cachedSettingsRevision_ = state_.settingsRevision;
    if (!source.valid || !source.rect.Valid()) return frame_;
    frame_.valid = true;
    frame_.rect = source.rect;
    frame_.sourceRevision = sourceRevision;
    frame_.revision = ++state_.revision;
    const Bounds visible = ExpandedBounds(source.rect,
        (std::max)(0.0f, settings_.overscanPixels));

    frame_.stats.sourceLines = static_cast<uint32_t>(source.lines.size());
    CourseOverviewMapDrawBatch railBatch{};
    const CourseOverviewMapLine* previousRail = nullptr;
    const auto flushRail = [&]() {
        if (railBatch.points.size() < 2u) {
            railBatch = {};
            previousRail = nullptr;
            return;
        }
        frame_.stats.sourceRailPoints +=
            static_cast<uint32_t>(railBatch.points.size());
        railBatch.points = SimplifyPolyline(
            railBatch.points, settings_.railLodPixelError);
        frame_.stats.renderedRailPoints +=
            static_cast<uint32_t>(railBatch.points.size());
        frame_.lineBatches.push_back(std::move(railBatch));
        railBatch = {};
        previousRail = nullptr;
    };

    for (const CourseOverviewMapLine& line : source.lines) {
        if (!Intersects(visible, line.start, line.end)) {
            ++frame_.stats.culledLines;
            flushRail();
            continue;
        }
        ++frame_.stats.visibleLines;
        if (line.kind != CourseOverviewMapItemKind::RailSegment) {
            flushRail();
            CourseOverviewMapDrawBatch batch{};
            batch.topology = CourseOverviewMapDrawBatchTopology::Segment;
            batch.kind = line.kind;
            batch.color = line.color;
            batch.thickness = line.thickness;
            batch.points = {line.start, line.end};
            frame_.lineBatches.push_back(std::move(batch));
            continue;
        }
        if (railBatch.points.empty() || previousRail == nullptr ||
            !SameRailBatch(railBatch, line, *previousRail)) {
            flushRail();
            railBatch.topology = CourseOverviewMapDrawBatchTopology::Polyline;
            railBatch.kind = line.kind;
            railBatch.color = line.color;
            railBatch.thickness = line.thickness;
            railBatch.points.push_back(line.start);
        }
        railBatch.points.push_back(line.end);
        previousRail = &line;
    }
    flushRail();
    frame_.stats.drawBatches = static_cast<uint32_t>(frame_.lineBatches.size());

    frame_.markerIndices.reserve(source.markers.size());
    for (uint32_t index = 0; index < source.markers.size(); ++index) {
        const CourseOverviewMapMarker& marker = source.markers[index];
        if (Contains(visible, marker.position, marker.radius + 2.0f)) {
            frame_.markerIndices.push_back(index);
        }
    }
    frame_.stats.visibleMarkers =
        static_cast<uint32_t>(frame_.markerIndices.size());

    std::vector<uint32_t> labelCandidates;
    labelCandidates.reserve(source.labels.size());
    for (uint32_t index = 0; index < source.labels.size(); ++index) {
        if (Contains(visible, source.labels[index].position)) {
            labelCandidates.push_back(index);
        }
    }
    std::stable_sort(labelCandidates.begin(), labelCandidates.end(),
        [&source](uint32_t lhs, uint32_t rhs) {
            return LabelPriority(source.labels[lhs]) >
                LabelPriority(source.labels[rhs]);
        });
    frame_.stats.labelsConsidered =
        static_cast<uint32_t>(labelCandidates.size());

    const float cellSize = (std::max)(4.0f, settings_.labelCellPixels);
    const float area = source.rect.width * source.rect.height;
    const uint32_t areaBudget = settings_.pixelsPerLabel > 0.0f
        ? static_cast<uint32_t>((std::max)(1.0f, area / settings_.pixelsPerLabel))
        : settings_.maximumLabels;
    const uint32_t budget = (std::min)(settings_.maximumLabels, areaBudget);
    std::unordered_set<uint64_t> occupied;
    occupied.reserve(static_cast<std::size_t>(budget) * 4u);
    for (uint32_t index : labelCandidates) {
        const CourseOverviewMapLabel& label = source.labels[index];
        if (!label.selected && frame_.labelIndices.size() >= budget) continue;
        const float width = (std::max)(settings_.estimatedGlyphWidth,
            static_cast<float>(label.text.size()) * settings_.estimatedGlyphWidth) +
            settings_.labelPaddingPixels * 2.0f;
        const float height = settings_.estimatedLabelHeight +
            settings_.labelPaddingPixels * 2.0f;
        const int firstX = static_cast<int>(std::floor(
            (label.position.x - settings_.labelPaddingPixels) / cellSize));
        const int lastX = static_cast<int>(std::floor(
            (label.position.x + width) / cellSize));
        const int firstY = static_cast<int>(std::floor(
            (label.position.y - settings_.labelPaddingPixels) / cellSize));
        const int lastY = static_cast<int>(std::floor(
            (label.position.y + height) / cellSize));
        bool collides = false;
        if (!label.selected) {
            for (int y = firstY; y <= lastY && !collides; ++y) {
                for (int x = firstX; x <= lastX; ++x) {
                    if (occupied.find(CellKey(x, y)) != occupied.end()) {
                        collides = true;
                        break;
                    }
                }
            }
        }
        if (collides) continue;
        frame_.labelIndices.push_back(index);
        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                occupied.insert(CellKey(x, y));
            }
        }
    }
    frame_.stats.labelsDrawn = static_cast<uint32_t>(frame_.labelIndices.size());
    frame_.stats.labelsBudgetedOut = frame_.stats.labelsConsidered -
        frame_.stats.labelsDrawn;
    return frame_;
}

bool CourseOverviewMapVisibilitySettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(overscanPixels, 0.0f, 100000.0f) &&
        settings::InRange(railLodPixelError, 0.0f, 16.0f) &&
        settings::InRange(maximumLabels, 0u, 4096u) &&
        settings::InRange(labelCellPixels, 4.0f, 256.0f) &&
        settings::InRange(estimatedGlyphWidth, 1.0f, 64.0f) &&
        settings::InRange(estimatedLabelHeight, 1.0f, 128.0f) &&
        settings::InRange(labelPaddingPixels, 0.0f, 64.0f) &&
        settings::InRange(pixelsPerLabel, 1.0f, 1000000.0f);
    return settings::Result(valid, errorMessage,
        "CourseOverviewMapVisibilitySettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseOverviewMapVisibilityService::SetSettings(
    CourseOverviewMapVisibilitySettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++state_.settingsRevision;
    Invalidate();
    return true;
}

void CourseOverviewMapVisibilityService::Invalidate() noexcept {
    cachedSourceRevision_ = 0;
    cachedSettingsRevision_ = 0;
    frame_ = {};
}

} // namespace editor
