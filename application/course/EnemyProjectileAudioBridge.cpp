#include "EnemyProjectileAudioBridge.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {

Vector3 Subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Length(Vector3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

float TrajectoryPitch(EnemyProjectileTrajectory trajectory) noexcept {
    switch (trajectory) {
    case EnemyProjectileTrajectory::Direct: return 1.00f;
    case EnemyProjectileTrajectory::Predictive: return 1.12f;
    case EnemyProjectileTrajectory::Homing: return 1.24f;
    case EnemyProjectileTrajectory::Arc: return 0.88f;
    }
    return 1.0f;
}

} // namespace

void EnemyProjectileAudioBridge::Reset() {
    frame_ = {};
    flyByPlayed_.clear();
    revision_ = 0;
}

void EnemyProjectileAudioBridge::Update(
    const EnemyProjectileAudioInput& input) {
    frame_ = {};
    if (!input.settings.enabled || !input.gameplayActive ||
        input.presentation == nullptr) {
        if (input.presentation == nullptr) flyByPlayed_.clear();
        frame_.revision = ++revision_;
        return;
    }
    frame_.cues.reserve(input.settings.maximumCuesPerFrame);
    for (const EnemyProjectilePresentationEvent& event :
         input.presentation->events) {
        switch (event.kind) {
        case EnemyProjectilePresentationEventKind::Spawned:
            PushCue(
                EnemyProjectileAudioCueKind::Launch,
                event.projectileId,
                event.worldPosition,
                event.trajectory,
                input,
                0.56f);
            break;
        case EnemyProjectilePresentationEventKind::Impacted:
            PushCue(
                EnemyProjectileAudioCueKind::Impact,
                event.projectileId,
                event.worldPosition,
                event.trajectory,
                input,
                event.lethal ? 1.0f : 0.86f);
            flyByPlayed_.insert(event.projectileId);
            break;
        case EnemyProjectilePresentationEventKind::Intercepted:
            PushCue(
                EnemyProjectileAudioCueKind::Impact,
                event.projectileId,
                event.worldPosition,
                event.trajectory,
                input,
                0.68f);
            flyByPlayed_.insert(event.projectileId);
            break;
        case EnemyProjectilePresentationEventKind::Expired:
        case EnemyProjectilePresentationEventKind::ShellCracked:
            break;
        }
    }

    std::unordered_set<uint64_t> activeIds;
    activeIds.reserve(input.presentation->projectiles.size());
    for (const EnemyProjectilePresentation& projectile :
         input.presentation->projectiles) {
        activeIds.insert(projectile.projectileId);
    }
    for (const PlayerNearMissResult& result : input.nearMissResults) {
        if (!result.accepted ||
            flyByPlayed_.contains(result.request.projectileId)) {
            continue;
        }
        const auto visual = std::find_if(
            input.presentation->projectiles.begin(),
            input.presentation->projectiles.end(),
            [&](const EnemyProjectilePresentation& projectile) {
                return projectile.projectileId == result.request.projectileId;
            });
        if (visual == input.presentation->projectiles.end()) continue;
        PushCue(
            EnemyProjectileAudioCueKind::FlyBy,
            visual->projectileId,
            visual->worldPosition,
            result.request.trajectory,
            input,
            0.54f + 0.28f * (std::clamp)(
                result.request.closeness,
                0.0f,
                1.0f));
        flyByPlayed_.insert(visual->projectileId);
    }
    for (auto it = flyByPlayed_.begin(); it != flyByPlayed_.end();) {
        if (!activeIds.contains(*it)) {
            it = flyByPlayed_.erase(it);
        } else {
            ++it;
        }
    }
    frame_.sourcePresentationRevision = input.presentation->revision;
    frame_.revision = ++revision_;
}

void EnemyProjectileAudioBridge::PushCue(
    EnemyProjectileAudioCueKind kind,
    uint64_t projectileId,
    Vector3 worldPosition,
    EnemyProjectileTrajectory trajectory,
    const EnemyProjectileAudioInput& input,
    float gain) {
    if (frame_.cues.size() >= input.settings.maximumCuesPerFrame) {
        ++frame_.droppedCues;
        return;
    }
    const Vector3 listenerToSource = Subtract(
        worldPosition,
        input.listenerPosition);
    const float distance = Length(listenerToSource);
    const float referenceDistance = (std::max)(
        1.0f,
        input.settings.referenceDistance);
    const float distanceRatio = distance / referenceDistance;
    const float attenuation = 1.0f /
        (1.0f + distanceRatio * distanceRatio);
    const float panWidth = (std::max)(1.0f, input.settings.spatialPanWidth);
    const float pan = (std::clamp)(
        Dot(listenerToSource, input.listenerRight) / panWidth,
        -1.0f,
        1.0f);
    const float variation =
        (static_cast<float>(projectileId % 7u) - 3.0f) * 0.018f;
    EnemyProjectileAudioCue cue{};
    cue.kind = kind;
    cue.projectileId = projectileId;
    cue.worldPosition = worldPosition;
    cue.volume = (std::clamp)(
        input.settings.masterVolume * gain * attenuation,
        0.0f,
        1.0f);
    cue.pitch = (std::clamp)(
        TrajectoryPitch(trajectory) + variation,
        0.55f,
        1.8f);
    cue.pan = pan;
    if (cue.volume > 0.0001f) frame_.cues.push_back(cue);
}

const char* ToString(EnemyProjectileAudioCueKind kind) noexcept {
    switch (kind) {
    case EnemyProjectileAudioCueKind::Launch: return "Launch";
    case EnemyProjectileAudioCueKind::FlyBy: return "FlyBy";
    case EnemyProjectileAudioCueKind::Impact: return "Impact";
    }
    return "Unknown";
}
