#pragma once

#include <string>

#include <cstdint>
#include <vector>

#include "CourseOverviewMapRenderer.h"

namespace editor {

enum class CourseOverviewMapDrawBatchTopology : uint8_t {
    Polyline,
    Segment,
};

// A draw-ready command. Rail samples that share an authoring segment and
// style are emitted as one polyline instead of one UI command per sample.
struct CourseOverviewMapDrawBatch final {
    CourseOverviewMapDrawBatchTopology topology =
        CourseOverviewMapDrawBatchTopology::Polyline;
    CourseOverviewMapItemKind kind = CourseOverviewMapItemKind::None;
    uint32_t color = 0xffffffffu;
    float thickness = 1.0f;
    std::vector<Vector2> points;
};

struct CourseOverviewMapVisibilitySettings final {
    float overscanPixels = 12.0f;
    float railLodPixelError = 1.25f;
    uint32_t maximumLabels = 96;
    float labelCellPixels = 18.0f;
    float estimatedGlyphWidth = 7.0f;
    float estimatedLabelHeight = 14.0f;
    float labelPaddingPixels = 3.0f;
    float pixelsPerLabel = 5200.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseOverviewMapVisibilityStats final {
    uint32_t sourceLines = 0;
    uint32_t visibleLines = 0;
    uint32_t culledLines = 0;
    uint32_t sourceRailPoints = 0;
    uint32_t renderedRailPoints = 0;
    uint32_t drawBatches = 0;
    uint32_t visibleMarkers = 0;
    uint32_t labelsConsidered = 0;
    uint32_t labelsDrawn = 0;
    uint32_t labelsBudgetedOut = 0;
};

// Lightweight presentation frame. Indices refer to the immutable source
// CourseOverviewMapFrame so marker/label payloads are not copied every frame.
struct CourseOverviewMapVisibleFrame final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    uint64_t sourceRevision = 0;
    uint64_t revision = 0;
    std::vector<CourseOverviewMapDrawBatch> lineBatches;
    std::vector<uint32_t> markerIndices;
    std::vector<uint32_t> labelIndices;
    CourseOverviewMapVisibilityStats stats{};
};

struct CourseOverviewMapVisibilityState final {
    uint64_t revision = 0;
    uint64_t settingsRevision = 1;
    uint64_t cacheHits = 0;
};

// Produces a cached, screen-space visibility set without modifying the full
// interaction frame. Rail LOD is selected by projected pixel error, labels use
// a deterministic priority/collision budget, and contiguous rail lines become
// one draw batch.
class CourseOverviewMapVisibilityService final {
public:
    const CourseOverviewMapVisibleFrame& Build(
        const CourseOverviewMapFrame& source,
        uint64_t sourceRevision);
    bool SetSettings(CourseOverviewMapVisibilitySettings settings, std::string* errorMessage = nullptr);
    void Invalidate() noexcept;

    const CourseOverviewMapVisibilitySettings& Settings() const noexcept {
        return settings_;
    }
    const CourseOverviewMapVisibilityState& State() const noexcept {
        return state_;
    }
    const CourseOverviewMapVisibleFrame& Frame() const noexcept { return frame_; }
    uint64_t SettingsRevision() const noexcept { return state_.settingsRevision; }

private:
    CourseOverviewMapVisibilitySettings settings_{};
    CourseOverviewMapVisibilityState state_{};
    CourseOverviewMapVisibleFrame frame_{};
    uint64_t cachedSourceRevision_ = 0;
    uint64_t cachedSettingsRevision_ = 0;
};

} // namespace editor
