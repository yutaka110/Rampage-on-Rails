#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "RailVehicleActor.h"

enum class RailVehicleAudioCueKind {
    RollingPulse,
    RailJointImpact,
    BrakeScrape,
    StopImpact,
};

struct RailVehicleAudioCue final {
    RailVehicleAudioCueKind kind = RailVehicleAudioCueKind::RollingPulse;
    float volume = 0.0f;
    float pitch = 1.0f;
    float pan = 0.0f;
};

struct RailVehicleAudioSettings final {
    bool enabled = true;
    float masterVolume = 0.65f;
    float rollingPulseSlowInterval = 0.28f;
    float rollingPulseFastInterval = 0.075f;
    float brakeRetriggerInterval = 0.18f;
    float referenceDistance = 18.0f;
    float spatialPanWidth = 14.0f;
};

struct RailVehicleAudioFrame final {
    static constexpr size_t kMaximumCueCount = 4;

    std::array<RailVehicleAudioCue, kMaximumCueCount> cues{};
    size_t cueCount = 0;
    float distanceAttenuation = 1.0f;
    bool rolling = false;
    float rollingLoopPitch = 1.0f;
    float rollingLoopGain = 1.0f;
    uint64_t sourceActorRevision = 0;
    uint64_t sourcePresentationRevision = 0;
    uint64_t revision = 0;
};

struct RailVehicleAudioInput final {
    const RailVehicleActorFrame* actor = nullptr;
    const RailVehiclePresentationFrame* presentation = nullptr;
    float deltaTime = 0.0f;
    float manualBoostStrength = 0.0f;
    Vector3 listenerPosition{};
    Vector3 listenerRight{1.0f, 0.0f, 0.0f};
    RailVehicleAudioSettings settings{};
};

// Converts vehicle presentation into bounded, backend-agnostic sound cues.
// Granular rolling pulses avoid creating/restarting a looping voice per frame.
class RailVehicleAudioBridge final {
public:
    void Reset();
    void Update(const RailVehicleAudioInput& input);

    const RailVehicleAudioFrame& Frame() const noexcept { return frame_; }

private:
    void PushCue(RailVehicleAudioCueKind kind, float volume, float pitch, float pan);

    RailVehicleAudioFrame frame_{};
    float rollingAccumulator_ = 0.0f;
    float brakeCooldown_ = 0.0f;
    bool wasMoving_ = false;
    uint64_t revision_ = 0;
};
