#include "RailLockResolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct ProjectedPoint {
    Vector2 screen{};
    float depth = 0.0f;
    bool behind = false;
};

ProjectedPoint ProjectToScreen(
    const Vector3& point,
    const Matrix4x4& matrix,
    uint32_t width,
    uint32_t height) {
    const float x =
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
    const float y =
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
    const float z =
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
    const float w =
        point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
    ProjectedPoint result{};
    if (w <= 0.00001f) {
        result.behind = true;
        return result;
    }
    const float ndcX = x / w;
    const float ndcY = y / w;
    result.depth = z / w;
    result.screen = {
        (ndcX * 0.5f + 0.5f) * static_cast<float>(width),
        (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height),
    };
    return result;
}

float DistancePointToSegment(Vector2 point, Vector2 start, Vector2 end) {
    const float vx = end.x - start.x;
    const float vy = end.y - start.y;
    const float wx = point.x - start.x;
    const float wy = point.y - start.y;
    const float len2 = vx * vx + vy * vy;
    if (len2 <= 0.0001f) {
        const float dx = point.x - end.x;
        const float dy = point.y - end.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = (std::clamp)((wx * vx + wy * vy) / len2, 0.0f, 1.0f);
    const float cx = start.x + vx * t;
    const float cy = start.y + vy * t;
    const float dx = point.x - cx;
    const float dy = point.y - cy;
    return std::sqrt(dx * dx + dy * dy);
}

bool SameTarget(const RailLockTargetHandle& a, const RailLockTargetHandle& b) {
    return a.kind == b.kind && a.actorId == b.actorId && a.generationId == b.generationId;
}

float Clamp01(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
}

float SafeRange01(float value, float minValue, float maxValue) {
    const float range = maxValue - minValue;
    if (range <= 0.0001f) {
        return 0.0f;
    }
    return Clamp01((value - minValue) / range);
}

float Distance(Vector2 a, Vector2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float KindPriorityBonus(RailLockTargetKind kind, const RailLockSettings& settings) {
    switch (kind) {
    case RailLockTargetKind::Enemy:
        return settings.lockPriorityEnemyBonus;
    case RailLockTargetKind::Obstacle:
        return settings.lockPriorityObstacleBonus;
    }
    return 0.0f;
}

void ResolvePriorityScore(
    RailLockCandidate& candidate,
    const RailReticleState& reticle,
    const RailLockSettings& settings,
    uint32_t viewportWidth,
    uint32_t viewportHeight) {
    const float lockRadius = (std::max)(1.0f, candidate.anchor.screenRadius + settings.assistRadius);
    candidate.reticlePriorityScore = 1.0f - Clamp01(candidate.distanceToReticle / lockRadius);

    const Vector2 screenCenter{
        static_cast<float>(viewportWidth) * 0.5f,
        static_cast<float>(viewportHeight) * 0.5f};
    const float halfDiagonal = (std::max)(
        1.0f,
        std::sqrt(screenCenter.x * screenCenter.x + screenCenter.y * screenCenter.y));
    candidate.centerPriorityScore = 1.0f - Clamp01(Distance(candidate.anchor.screenPosition, screenCenter) / halfDiagonal);

    candidate.forwardPriorityScore =
        1.0f - SafeRange01(candidate.anchor.forwardDistance, settings.minForwardDistance, settings.maxForwardDistance);
    candidate.kindPriorityScore = KindPriorityBonus(candidate.anchor.target.kind, settings);
    candidate.anchorPriorityScore = candidate.anchor.priority;

    candidate.score =
        candidate.reticlePriorityScore * settings.lockPriorityReticleWeight +
        candidate.centerPriorityScore * settings.lockPriorityCenterWeight +
        candidate.forwardPriorityScore * settings.lockPriorityForwardThreatWeight +
        candidate.anchorPriorityScore * settings.lockPriorityAnchorWeight +
        candidate.kindPriorityScore -
        candidate.distanceToReticle * settings.lockPriorityDistanceTieBreak;

    const float currentReticleDistance = Distance(candidate.anchor.screenPosition, reticle.currentScreenPosition);
    candidate.score -= currentReticleDistance * settings.lockPriorityDistanceTieBreak * 0.25f;
}
} // namespace

void RailLockResolver::Reset() {
    tokens_.clear();
    candidates_.clear();
    acceptedTokensThisFrame_.clear();
    acceptedThisFrame_ = 0;
}

void RailLockResolver::LimitTokenCapacity(size_t capacity) {
    if (tokens_.size() <= capacity) return;
    tokens_.resize(capacity);
    acceptedTokensThisFrame_.clear();
    acceptedThisFrame_ = 0;
}

void RailLockResolver::Update(const RailLockResolverFrameInput& input) {
    candidates_.clear();
    acceptedTokensThisFrame_.clear();
    acceptedThisFrame_ = 0;
    if (input.anchors == nullptr || input.reticle == nullptr || input.viewProjection == nullptr ||
        input.viewportWidth == 0 || input.viewportHeight == 0) {
        return;
    }

    if (input.reticle->lockPressed) {
        tokens_.clear();
    }

    RailLockCandidate best{};
    best.score = -(std::numeric_limits<float>::max)();
    bool hasBest = false;

    for (RailLockAnchor anchor : *input.anchors) {
        RailLockCandidate candidate{};
        candidate.anchor = anchor;

        if (!input.reticle->lockHeld) {
            candidate.rejectReason = RailLockRejectReason::LockModeInactive;
            candidates_.push_back(candidate);
            continue;
        }
        if (static_cast<int>(tokens_.size()) >= input.settings.maxLocks) {
            candidate.rejectReason = RailLockRejectReason::LockContainerFull;
            candidates_.push_back(candidate);
            continue;
        }
        if (anchor.forwardDistance < input.settings.minForwardDistance ||
            anchor.forwardDistance > input.settings.maxForwardDistance) {
            candidate.rejectReason = RailLockRejectReason::OutOfForwardRange;
            candidates_.push_back(candidate);
            continue;
        }

        const ProjectedPoint projected =
            ProjectToScreen(anchor.worldPosition, *input.viewProjection, input.viewportWidth, input.viewportHeight);
        candidate.anchor.screenPosition = projected.screen;
        if (projected.behind) {
            candidate.rejectReason = RailLockRejectReason::BehindCamera;
            candidates_.push_back(candidate);
            continue;
        }
        if (projected.depth < 0.0f || projected.depth > 1.0f) {
            candidate.rejectReason = RailLockRejectReason::OutOfDepth;
            candidates_.push_back(candidate);
            continue;
        }
        const float margin = anchor.screenRadius + input.settings.assistRadius;
        if (projected.screen.x < -margin ||
            projected.screen.y < -margin ||
            projected.screen.x > static_cast<float>(input.viewportWidth) + margin ||
            projected.screen.y > static_cast<float>(input.viewportHeight) + margin) {
            candidate.rejectReason = RailLockRejectReason::Offscreen;
            candidates_.push_back(candidate);
            continue;
        }
        if (anchor.lineOfSightBlocked) {
            candidate.rejectReason = RailLockRejectReason::Occluded;
            candidates_.push_back(candidate);
            continue;
        }

        const float lockRadius = anchor.screenRadius + input.settings.assistRadius;
        candidate.distanceToReticle = DistancePointToSegment(
            projected.screen,
            input.reticle->previousScreenPosition,
            input.reticle->currentScreenPosition);
        if (candidate.distanceToReticle > lockRadius) {
            candidate.rejectReason = RailLockRejectReason::NotSwept;
            candidates_.push_back(candidate);
            continue;
        }
        if (HasToken(anchor)) {
            candidate.rejectReason = RailLockRejectReason::AlreadyLocked;
            candidates_.push_back(candidate);
            continue;
        }
        if (StackCount(anchor) >= anchor.maxStack) {
            candidate.rejectReason = RailLockRejectReason::StackLimit;
            candidates_.push_back(candidate);
            continue;
        }

        candidate.lockable = true;
        candidate.rejectReason = RailLockRejectReason::None;
        ResolvePriorityScore(
            candidate,
            *input.reticle,
            input.settings,
            input.viewportWidth,
            input.viewportHeight);
        if (!hasBest || candidate.score > best.score) {
            best = candidate;
            hasBest = true;
        }
        candidates_.push_back(candidate);
    }

    if (hasBest) {
        TryAcceptCandidate(best, input.elapsedTime);
    }
}

std::vector<RailLockToken> RailLockResolver::ConsumeTokens() {
    std::vector<RailLockToken> consumed = std::move(tokens_);
    tokens_.clear();
    return consumed;
}

bool RailLockResolver::HasToken(const RailLockAnchor& anchor) const {
    for (const RailLockToken& token : tokens_) {
        if (SameTarget(token.target, anchor.target) && token.anchorId == anchor.anchorId) {
            return true;
        }
    }
    return false;
}

int RailLockResolver::StackCount(const RailLockAnchor& anchor) const {
    int count = 0;
    for (const RailLockToken& token : tokens_) {
        if (SameTarget(token.target, anchor.target) && token.anchorId == anchor.anchorId) {
            ++count;
        }
    }
    return count;
}

void RailLockResolver::TryAcceptCandidate(const RailLockCandidate& candidate, float elapsedTime) {
    RailLockToken token{};
    token.target = candidate.anchor.target;
    token.anchorId = candidate.anchor.anchorId;
    token.stackIndex = StackCount(candidate.anchor);
    token.acquiredTime = elapsedTime;
    token.acquiredScreenPosition = candidate.anchor.screenPosition;
    token.acquiredScreenDistance = candidate.distanceToReticle;
    token.label = candidate.anchor.label;
    acceptedTokensThisFrame_.push_back(token);
    tokens_.push_back(std::move(token));
    acceptedThisFrame_ = 1;
}
