#pragma once

#include <cstddef>
#include <vector>

#include "RailLockOnTypes.h"

struct RailLockResolverFrameInput {
    const std::vector<RailLockAnchor>* anchors = nullptr;
    const RailReticleState* reticle = nullptr;
    const Matrix4x4* viewProjection = nullptr;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    float elapsedTime = 0.0f;
    RailLockSettings settings{};
};

class RailLockResolver {
public:
    void Reset();
    void Update(const RailLockResolverFrameInput& input);
    std::vector<RailLockToken> ConsumeTokens();
    void LimitTokenCapacity(size_t capacity);

    const std::vector<RailLockToken>& Tokens() const { return tokens_; }
    const std::vector<RailLockCandidate>& Candidates() const { return candidates_; }
    const std::vector<RailLockToken>& AcceptedTokensThisFrame() const { return acceptedTokensThisFrame_; }
    int AcceptedThisFrame() const { return acceptedThisFrame_; }

private:
    bool HasToken(const RailLockAnchor& anchor) const;
    int StackCount(const RailLockAnchor& anchor) const;
    void TryAcceptCandidate(const RailLockCandidate& candidate, float elapsedTime);

    std::vector<RailLockToken> tokens_;
    std::vector<RailLockCandidate> candidates_;
    std::vector<RailLockToken> acceptedTokensThisFrame_;
    int acceptedThisFrame_ = 0;
};
