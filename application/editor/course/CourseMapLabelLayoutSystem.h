#pragma once

#include "CourseOverviewMapProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace editor {

enum class CourseMapLabelPriority : uint16_t {
    Decoration = 50,
    Structure = 150,
    Enemy = 250,
    Wave = 350,
    Landmark = 450,
    Warning = 700,
    Selected = 1000,
};

struct CourseMapLabelCandidate final {
    Vector2 anchor{};
    Vector2 preferredPosition{};
    uint32_t color = 0xffffffffu;
    std::string text;
    std::string stableId;
    CourseMapLabelPriority priority = CourseMapLabelPriority::Decoration;
    bool selected = false;
    bool allowLeaderLine = true;
};

struct CourseMapPlacedLabel final {
    Vector2 anchor{};
    Vector2 position{};
    Vector2 size{};
    Vector2 leaderStart{};
    Vector2 leaderEnd{};
    uint32_t color = 0xffffffffu;
    std::string text;
    std::string stableId;
    CourseMapLabelPriority priority = CourseMapLabelPriority::Decoration;
    bool selected = false;
    bool drawLeaderLine = false;
};

struct CourseMapLabelLayoutSettings final {
    uint32_t maximumLabels = 96;
    float estimatedGlyphWidth = 7.0f;
    float estimatedLineHeight = 14.0f;
    float paddingPixels = 3.0f;
    float edgePaddingPixels = 5.0f;
    float displacementStepPixels = 14.0f;
    uint32_t displacementRings = 4;
    float leaderLineThresholdPixels = 12.0f;

    bool Validate(std::string* errorMessage = nullptr) const;
};

struct CourseMapLabelLayoutStats final {
    uint32_t candidates = 0;
    uint32_t placed = 0;
    uint32_t suppressedByBudget = 0;
    uint32_t suppressedByCollision = 0;
    uint32_t displaced = 0;
    uint32_t leaderLines = 0;
    uint64_t builds = 0;
    uint64_t cacheHits = 0;
};

struct CourseMapLabelLayoutFrame final {
    bool valid = false;
    CourseOverviewMapRect rect{};
    std::vector<CourseMapPlacedLabel> labels;
    CourseMapLabelLayoutStats stats{};
    uint64_t revision = 0;
};

// Deterministic screen-space label solver. Selected and high semantic priority
// labels reserve space first; lower priority labels are displaced or culled.
class CourseMapLabelLayoutSystem final {
public:
    const CourseMapLabelLayoutFrame& Build(
        const std::vector<CourseMapLabelCandidate>& candidates,
        CourseOverviewMapRect rect);
    bool SetSettings(CourseMapLabelLayoutSettings settings, std::string* errorMessage = nullptr);
    const CourseMapLabelLayoutSettings& Settings() const noexcept { return settings_; }
    void Invalidate() noexcept;

private:
    static uint64_t CandidateSignature(
        const std::vector<CourseMapLabelCandidate>& candidates);

    CourseMapLabelLayoutSettings settings_{};
    CourseMapLabelLayoutFrame frame_{};
    uint64_t settingsRevision_ = 1;
    uint64_t cachedSettingsRevision_ = 0;
    uint64_t cachedCandidateSignature_ = 0;
    CourseOverviewMapRect cachedRect_{};
    CourseMapLabelLayoutStats lifetimeStats_{};
};

} // namespace editor
