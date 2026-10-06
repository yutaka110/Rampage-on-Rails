#include "CourseMapLabelLayoutSystem.h"
#include "../EditorSettingsValidation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace editor {
namespace {

struct LabelRect final {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

bool SameRect(const CourseOverviewMapRect& a, const CourseOverviewMapRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool SameSettings(const CourseMapLabelLayoutSettings& a,
    const CourseMapLabelLayoutSettings& b) {
    return a.maximumLabels == b.maximumLabels &&
        a.estimatedGlyphWidth == b.estimatedGlyphWidth &&
        a.estimatedLineHeight == b.estimatedLineHeight &&
        a.paddingPixels == b.paddingPixels &&
        a.edgePaddingPixels == b.edgePaddingPixels &&
        a.displacementStepPixels == b.displacementStepPixels &&
        a.displacementRings == b.displacementRings &&
        a.leaderLineThresholdPixels == b.leaderLineThresholdPixels;
}

bool Intersects(const LabelRect& a, const LabelRect& b) {
    return a.left < b.right && a.right > b.left &&
        a.top < b.bottom && a.bottom > b.top;
}

float DistanceSquared(Vector2 a, Vector2 b) {
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    return x * x + y * y;
}

Vector2 ClampPosition(Vector2 position, Vector2 size,
    const CourseOverviewMapRect& rect, float edgePadding) {
    const float minimumX = rect.x + edgePadding;
    const float minimumY = rect.y + edgePadding;
    const float maximumX = (std::max)(minimumX,
        rect.x + rect.width - edgePadding - size.x);
    const float maximumY = (std::max)(minimumY,
        rect.y + rect.height - edgePadding - size.y);
    return {(std::clamp)(position.x, minimumX, maximumX),
        (std::clamp)(position.y, minimumY, maximumY)};
}

LabelRect MakeRect(Vector2 position, Vector2 size, float padding) {
    return {position.x - padding, position.y - padding,
        position.x + size.x + padding, position.y + size.y + padding};
}

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

uint64_t HashString(uint64_t hash, const std::string& value) {
    return HashBytes(hash, value.data(), value.size());
}

} // namespace

const CourseMapLabelLayoutFrame& CourseMapLabelLayoutSystem::Build(
    const std::vector<CourseMapLabelCandidate>& candidates,
    CourseOverviewMapRect rect) {
    const uint64_t signature = CandidateSignature(candidates);
    if (frame_.valid && cachedSettingsRevision_ == settingsRevision_ &&
        cachedCandidateSignature_ == signature && SameRect(cachedRect_, rect)) {
        ++frame_.stats.cacheHits;
        ++lifetimeStats_.cacheHits;
        return frame_;
    }

    frame_ = {};
    cachedSettingsRevision_ = settingsRevision_;
    cachedCandidateSignature_ = signature;
    cachedRect_ = rect;
    if (!rect.Valid()) return frame_;
    frame_.valid = true;
    frame_.rect = rect;
    frame_.revision = lifetimeStats_.builds + 1u;
    frame_.stats.candidates = static_cast<uint32_t>(candidates.size());

    std::vector<uint32_t> order(candidates.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t lhs, uint32_t rhs) {
        const CourseMapLabelCandidate& a = candidates[lhs];
        const CourseMapLabelCandidate& b = candidates[rhs];
        if (a.selected != b.selected) return a.selected;
        if (a.priority != b.priority) {
            return static_cast<uint16_t>(a.priority) > static_cast<uint16_t>(b.priority);
        }
        return a.stableId < b.stableId;
    });

    std::vector<LabelRect> occupied;
    occupied.reserve((std::min)(order.size(),
        static_cast<std::size_t>(settings_.maximumLabels)));
    const float step = settings_.displacementStepPixels;
    for (uint32_t index : order) {
        const CourseMapLabelCandidate& candidate = candidates[index];
        if (candidate.text.empty()) continue;
        if (!candidate.selected && frame_.labels.size() >= settings_.maximumLabels) {
            ++frame_.stats.suppressedByBudget;
            continue;
        }
        const Vector2 size{
            (std::max)(settings_.estimatedGlyphWidth,
                static_cast<float>(candidate.text.size()) * settings_.estimatedGlyphWidth),
            settings_.estimatedLineHeight};
        std::vector<Vector2> attempts;
        attempts.reserve(1u + settings_.displacementRings * 8u);
        attempts.push_back(candidate.preferredPosition);
        for (uint32_t ring = 1; ring <= settings_.displacementRings; ++ring) {
            const float d = step * static_cast<float>(ring);
            attempts.push_back({candidate.preferredPosition.x + d, candidate.preferredPosition.y});
            attempts.push_back({candidate.preferredPosition.x - d, candidate.preferredPosition.y});
            attempts.push_back({candidate.preferredPosition.x, candidate.preferredPosition.y + d});
            attempts.push_back({candidate.preferredPosition.x, candidate.preferredPosition.y - d});
            attempts.push_back({candidate.preferredPosition.x + d, candidate.preferredPosition.y + d});
            attempts.push_back({candidate.preferredPosition.x + d, candidate.preferredPosition.y - d});
            attempts.push_back({candidate.preferredPosition.x - d, candidate.preferredPosition.y + d});
            attempts.push_back({candidate.preferredPosition.x - d, candidate.preferredPosition.y - d});
        }

        bool placed = false;
        Vector2 position{};
        LabelRect bounds{};
        for (Vector2 attempt : attempts) {
            attempt = ClampPosition(attempt, size, rect, settings_.edgePaddingPixels);
            const LabelRect test = MakeRect(attempt, size, settings_.paddingPixels);
            bool collision = false;
            for (const LabelRect& other : occupied) {
                if (Intersects(test, other)) {
                    collision = true;
                    break;
                }
            }
            if (!collision) {
                placed = true;
                position = attempt;
                bounds = test;
                break;
            }
        }
        if (!placed && candidate.selected) {
            position = ClampPosition(candidate.preferredPosition, size, rect,
                settings_.edgePaddingPixels);
            bounds = MakeRect(position, size, settings_.paddingPixels);
            placed = true;
        }
        if (!placed) {
            ++frame_.stats.suppressedByCollision;
            continue;
        }

        CourseMapPlacedLabel label{};
        label.anchor = candidate.anchor;
        label.position = position;
        label.size = size;
        label.color = candidate.color;
        label.text = candidate.text;
        label.stableId = candidate.stableId;
        label.priority = candidate.priority;
        label.selected = candidate.selected;
        const float displacementSquared = DistanceSquared(position,
            candidate.preferredPosition);
        if (displacementSquared > 0.25f) ++frame_.stats.displaced;
        label.drawLeaderLine = candidate.allowLeaderLine &&
            DistanceSquared(label.anchor, label.position) >=
                settings_.leaderLineThresholdPixels * settings_.leaderLineThresholdPixels;
        if (label.drawLeaderLine) {
            label.leaderStart = label.anchor;
            label.leaderEnd = {
                (std::clamp)(label.anchor.x, position.x, position.x + size.x),
                (std::clamp)(label.anchor.y, position.y, position.y + size.y)};
            ++frame_.stats.leaderLines;
        }
        occupied.push_back(bounds);
        frame_.labels.push_back(std::move(label));
    }

    frame_.stats.placed = static_cast<uint32_t>(frame_.labels.size());
    ++lifetimeStats_.builds;
    frame_.stats.builds = lifetimeStats_.builds;
    frame_.stats.cacheHits = lifetimeStats_.cacheHits;
    return frame_;
}

bool CourseMapLabelLayoutSettings::Validate(std::string* errorMessage) const {
    const bool valid = settings::InRange(maximumLabels, 1u, 4096u) &&
        settings::InRange(estimatedGlyphWidth, 2.0f, 32.0f) &&
        settings::InRange(estimatedLineHeight, 6.0f, 64.0f) &&
        settings::InRange(paddingPixels, 0.0f, 24.0f) &&
        settings::InRange(edgePaddingPixels, 0.0f, 64.0f) &&
        settings::InRange(displacementStepPixels, 4.0f, 64.0f) &&
        settings::InRange(displacementRings, 0u, 12u) &&
        settings::InRange(leaderLineThresholdPixels, 0.0f, 128.0f);
    return settings::Result(valid, errorMessage,
        "CourseMapLabelLayoutSettings requires finite values, valid ranges and bounded work budgets.");
}

bool CourseMapLabelLayoutSystem::SetSettings(
    CourseMapLabelLayoutSettings settings, std::string* errorMessage) {
    if (!settings.Validate(errorMessage)) return false;
    if (SameSettings(settings_, settings)) return true;
    settings_ = settings;
    ++settingsRevision_;
    Invalidate();
    return true;
}

void CourseMapLabelLayoutSystem::Invalidate() noexcept {
    frame_ = {};
    cachedSettingsRevision_ = 0;
    cachedCandidateSignature_ = 0;
}

uint64_t CourseMapLabelLayoutSystem::CandidateSignature(
    const std::vector<CourseMapLabelCandidate>& candidates) {
    uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, candidates.size());
    for (const CourseMapLabelCandidate& candidate : candidates) {
        hash = HashValue(hash, candidate.anchor);
        hash = HashValue(hash, candidate.preferredPosition);
        hash = HashValue(hash, candidate.color);
        hash = HashString(hash, candidate.text);
        hash = HashString(hash, candidate.stableId);
        hash = HashValue(hash, candidate.priority);
        hash = HashValue(hash, candidate.selected);
        hash = HashValue(hash, candidate.allowLeaderLine);
    }
    return hash;
}

} // namespace editor
