#include "CourseOverviewMapProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {
namespace {

Vector3 Add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float Dot(Vector3 a, Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Cross(Vector3 a, Vector3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

Vector3 Normalize(Vector3 value, Vector3 fallback) {
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f) return fallback;
    return Scale(value, 1.0f / std::sqrt(lengthSquared));
}

} // namespace

bool CourseOverviewMapProjectionSettings::Validate(std::string* errorMessage) const {
    const bool valid = mode <= CourseOverviewMapProjectionMode::Free &&
        std::isfinite(zoom) && zoom >= 0.05f && zoom <= 64.0f &&
        std::isfinite(panPixels.x) && std::isfinite(panPixels.y) &&
        std::isfinite(paddingPixels) && paddingPixels >= 0.0f && paddingPixels <= 256.0f &&
        std::isfinite(freeYawRadians) && std::isfinite(freePitchRadians) &&
        fitSamplesPerSegment >= 2 && fitSamplesPerSegment <= 256;
    if (errorMessage != nullptr) *errorMessage = valid ? "" : "Map projection requires finite values, zoom 0.05..64, padding 0..256 and 2..256 samples.";
    return valid;
}

bool CourseOverviewMapProjection::Configure(
    const CourseRailAuthoringModel* rail,
    CourseOverviewMapRect rect,
    CourseOverviewMapProjectionSettings settings,
    std::string* errorMessage,
    const CourseRailAuthoringModel* boundsRail,
    const std::vector<Vector3>* additionalFitPoints) {
    if (!settings.Validate(errorMessage)) return false;
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height)) {
        if (errorMessage != nullptr) *errorMessage = "Map viewport must be finite.";
        return false;
    }
    ownedRailSnapshot_.reset();
    rail_ = rail;
    boundsRail_ = boundsRail != nullptr ? boundsRail : rail;
    additionalFitPoints_ = additionalFitPoints;
    settings_ = settings;
    settings_.zoom = (std::clamp)(settings_.zoom, 0.05f, 64.0f);
    settings_.paddingPixels = (std::clamp)(settings_.paddingPixels, 0.0f, 256.0f);
    settings_.fitSamplesPerSegment = (std::clamp)(settings_.fitSamplesPerSegment, 2u, 256u);
    state_ = {};
    state_.rect = rect;
    if (rail_ == nullptr || !rail_->IsValid() || rail_->Length() <= 0.0f ||
        boundsRail_ == nullptr || !boundsRail_->IsValid() ||
        boundsRail_->Length() <= 0.0f || !rect.Valid()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Overview Map requires a valid Rail authoring model and viewport rect.";
        }
        return false;
    }
    BuildFreeBasis();
    BuildRawBounds();
    const float availableWidth = (std::max)(1.0f, rect.width - settings_.paddingPixels * 2.0f);
    const float availableHeight = (std::max)(1.0f, rect.height - settings_.paddingPixels * 2.0f);
    const float rawWidth = (std::max)(0.001f, state_.rawMaximum.x - state_.rawMinimum.x);
    const float rawHeight = (std::max)(0.001f, state_.rawMaximum.y - state_.rawMinimum.y);
    state_.baseScale = (std::max)(0.000001f,
        (std::min)(availableWidth / rawWidth, availableHeight / rawHeight));
    state_.effectiveScale = state_.baseScale * settings_.zoom;
    state_.valid = std::isfinite(state_.effectiveScale) && state_.effectiveScale > 0.0f;
    if (!state_.valid && errorMessage != nullptr) {
        *errorMessage = "Overview Map projection produced an invalid scale.";
    }
    return state_.valid;
}

CourseOverviewMapProjectedPoint CourseOverviewMapProjection::ProjectWorld(
    const Vector3& world) const {
    CourseOverviewMapProjectedPoint result = ProjectWorldScreenOnly(world);
    if (!result.valid || settings_.mode ==
            CourseOverviewMapProjectionMode::RailUnwrapped) {
        return result;
    }
    // Full editor queries preserve the canonical rail distance used by
    // picking and authoring. Render-only callers use ProjectWorldScreenOnly.
    const RailAnchorProjection projected = rail_->Project(world, 12);
    if (projected.valid) {
        result.railDistance = projected.resolution.railSample.distance;
    }
    return result;
}

CourseOverviewMapProjectedPoint
CourseOverviewMapProjection::ProjectWorldScreenOnly(
    const Vector3& world) const {
    CourseOverviewMapProjectedPoint result{};
    if (!state_.valid) return result;
    float depth = 0.0f;
    Vector2 raw{};
    float railDistance = 0.0f;
    if (settings_.mode == CourseOverviewMapProjectionMode::RailUnwrapped) {
        const RailAnchorProjection projected = rail_->Project(world);
        if (!projected.valid) return result;
        railDistance = projected.resolution.railSample.distance + projected.anchor.forwardOffset;
        raw = {railDistance, projected.anchor.lateralOffset};
        depth = projected.anchor.verticalOffset;
    } else {
        raw = ProjectRaw(world, &depth);
    }
    result.valid = true;
    result.mapPosition = RawToMap(raw);
    result.rawPosition = raw;
    result.worldPosition = world;
    result.railDistance = railDistance;
    result.depth = depth;
    return result;
}

CourseOverviewMapProjection
CourseOverviewMapProjection::MakeBackgroundSnapshot() const {
    CourseOverviewMapProjection snapshot = *this;
    snapshot.additionalFitPoints_ = nullptr;
    if (rail_ == nullptr) {
        snapshot.rail_ = nullptr;
        snapshot.boundsRail_ = nullptr;
        snapshot.ownedRailSnapshot_.reset();
        return snapshot;
    }
    snapshot.ownedRailSnapshot_ =
        std::make_shared<CourseRailAuthoringModel>(*rail_);
    snapshot.rail_ = snapshot.ownedRailSnapshot_.get();
    snapshot.boundsRail_ = snapshot.rail_;
    return snapshot;
}

CourseOverviewMapProjectedPoint CourseOverviewMapProjection::ProjectRail(
    float railDistance,
    float lateralOffset,
    float verticalOffset) const {
    CourseOverviewMapProjectedPoint result{};
    if (!state_.valid) return result;
    const float distance = (std::clamp)(railDistance, 0.0f, rail_->Length());
    const RailPathSample sample = rail_->RuntimePath().Evaluate(distance);
    const Vector3 world = Add(
        Add(sample.position, Scale(sample.right, lateralOffset)),
        Scale(sample.up, verticalOffset));
    float depth = 0.0f;
    const Vector2 raw = settings_.mode == CourseOverviewMapProjectionMode::RailUnwrapped
        ? Vector2{distance, lateralOffset}
        : ProjectRaw(world, &depth);
    result.valid = true;
    result.mapPosition = RawToMap(raw);
    result.rawPosition = raw;
    result.worldPosition = world;
    result.railDistance = distance;
    result.depth = settings_.mode == CourseOverviewMapProjectionMode::RailUnwrapped
        ? verticalOffset : depth;
    return result;
}

Vector3 CourseOverviewMapProjection::Unproject(
    Vector2 mapPosition,
    float depth) const {
    if (!state_.valid) return {};
    const Vector2 raw = MapToRaw(mapPosition);
    switch (settings_.mode) {
    case CourseOverviewMapProjectionMode::Top:
        return {raw.x, depth, raw.y};
    case CourseOverviewMapProjectionMode::Side:
        return {depth, raw.y, raw.x};
    case CourseOverviewMapProjectionMode::RailUnwrapped: {
        const RailPathSample sample = rail_->RuntimePath().Evaluate(
            (std::clamp)(raw.x, 0.0f, rail_->Length()));
        return Add(Add(sample.position, Scale(sample.right, raw.y)), Scale(sample.up, depth));
    }
    case CourseOverviewMapProjectionMode::Free:
        return Add(Add(Scale(state_.freeRight, raw.x), Scale(state_.freeUp, raw.y)),
            Scale(state_.freeNormal, depth));
    }
    return {};
}

Vector2 CourseOverviewMapProjection::RawToMap(Vector2 rawPosition) const {
    const float centerX = state_.rect.x + state_.rect.width * 0.5f + settings_.panPixels.x;
    const float centerY = state_.rect.y + state_.rect.height * 0.5f + settings_.panPixels.y;
    return {
        centerX + (rawPosition.x - state_.rawCenter.x) * state_.effectiveScale,
        centerY - (rawPosition.y - state_.rawCenter.y) * state_.effectiveScale};
}

Vector2 CourseOverviewMapProjection::MapToRaw(Vector2 mapPosition) const {
    if (!state_.valid) return {};
    const float centerX = state_.rect.x + state_.rect.width * 0.5f + settings_.panPixels.x;
    const float centerY = state_.rect.y + state_.rect.height * 0.5f + settings_.panPixels.y;
    return {
        state_.rawCenter.x + (mapPosition.x - centerX) / state_.effectiveScale,
        state_.rawCenter.y - (mapPosition.y - centerY) / state_.effectiveScale};
}

Vector2 CourseOverviewMapProjection::ProjectRaw(
    const Vector3& world,
    float* depth) const {
    switch (settings_.mode) {
    case CourseOverviewMapProjectionMode::Top:
        if (depth != nullptr) *depth = world.y;
        return {world.x, world.z};
    case CourseOverviewMapProjectionMode::Side:
        if (depth != nullptr) *depth = world.x;
        return {world.z, world.y};
    case CourseOverviewMapProjectionMode::RailUnwrapped:
        break;
    case CourseOverviewMapProjectionMode::Free:
        if (depth != nullptr) *depth = Dot(world, state_.freeNormal);
        return {Dot(world, state_.freeRight), Dot(world, state_.freeUp)};
    }
    if (depth != nullptr) *depth = 0.0f;
    return {};
}

void CourseOverviewMapProjection::BuildFreeBasis() {
    const float yaw = settings_.freeYawRadians;
    const float pitch = settings_.freePitchRadians;
    state_.freeNormal = Normalize({
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)}, {0.0f, -1.0f, 0.0f});
    state_.freeRight = Normalize(
        Cross({0.0f, 1.0f, 0.0f}, state_.freeNormal), {1.0f, 0.0f, 0.0f});
    state_.freeUp = Normalize(
        Cross(state_.freeNormal, state_.freeRight), {0.0f, 1.0f, 0.0f});
}

void CourseOverviewMapProjection::BuildRawBounds() {
    const float maximum = (std::numeric_limits<float>::max)();
    state_.rawMinimum = {maximum, maximum};
    state_.rawMaximum = {-maximum, -maximum};
    const auto include = [&](Vector2 raw) {
        state_.rawMinimum.x = (std::min)(state_.rawMinimum.x, raw.x);
        state_.rawMinimum.y = (std::min)(state_.rawMinimum.y, raw.y);
        state_.rawMaximum.x = (std::max)(state_.rawMaximum.x, raw.x);
        state_.rawMaximum.y = (std::max)(state_.rawMaximum.y, raw.y);
    };
    float maximumCorridor = 1.0f;
    const uint32_t sampleCount = (std::max)(
        2u,
        boundsRail_->RuntimePath().SegmentCount() * settings_.fitSamplesPerSegment + 1u);
    for (uint32_t index = 0; index < sampleCount; ++index) {
        const float distance = boundsRail_->Length() *
            static_cast<float>(index) / static_cast<float>(sampleCount - 1u);
        const RailPathSample sample = boundsRail_->RuntimePath().Evaluate(distance);
        maximumCorridor = (std::max)(maximumCorridor, sample.corridorRadius);
        if (settings_.mode == CourseOverviewMapProjectionMode::RailUnwrapped) {
            include({distance, -sample.corridorRadius});
            include({distance, sample.corridorRadius});
        } else {
            include(ProjectRaw(sample.position, nullptr));
        }
    }
    if (additionalFitPoints_ != nullptr) {
        for (const Vector3& world : *additionalFitPoints_) {
            if (settings_.mode == CourseOverviewMapProjectionMode::RailUnwrapped) {
                const RailAnchorProjection projected = rail_->Project(world, 24);
                if (!projected.valid) continue;
                include({projected.resolution.railSample.distance +
                    projected.anchor.forwardOffset,
                    projected.anchor.lateralOffset});
            } else {
                include(ProjectRaw(world, nullptr));
            }
        }
    }
    if (settings_.mode != CourseOverviewMapProjectionMode::RailUnwrapped) {
        // Rail-anchored actors may legally use the full authored corridor.
        // Keep them inside Frame All even before renderer-specific bounds exist.
        const float expand = (std::max)(1.0f, maximumCorridor);
        state_.rawMinimum.x -= expand;
        state_.rawMinimum.y -= expand;
        state_.rawMaximum.x += expand;
        state_.rawMaximum.y += expand;
    }
    if (state_.rawMaximum.x - state_.rawMinimum.x < 0.001f) {
        state_.rawMinimum.x -= 0.5f;
        state_.rawMaximum.x += 0.5f;
    }
    if (state_.rawMaximum.y - state_.rawMinimum.y < 0.001f) {
        state_.rawMinimum.y -= 0.5f;
        state_.rawMaximum.y += 0.5f;
    }
    state_.rawCenter = {
        (state_.rawMinimum.x + state_.rawMaximum.x) * 0.5f,
        (state_.rawMinimum.y + state_.rawMaximum.y) * 0.5f};
}

const char* ToString(CourseOverviewMapProjectionMode mode) {
    switch (mode) {
    case CourseOverviewMapProjectionMode::Top: return "Top";
    case CourseOverviewMapProjectionMode::Side: return "Side";
    case CourseOverviewMapProjectionMode::RailUnwrapped: return "Rail Unwrapped";
    case CourseOverviewMapProjectionMode::Free: return "Free";
    }
    return "Unknown";
}

} // namespace editor
