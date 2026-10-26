#include "RailVehicleAudioBridge.h"

#include <algorithm>
#include <cmath>

namespace {

float Dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector3 Subtract(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float Length(Vector3 value) noexcept {
    return std::sqrt(Dot(value, value));
}

} // namespace

void RailVehicleAudioBridge::Reset() {
    frame_ = {};
    rollingAccumulator_ = 0.0f;
    brakeCooldown_ = 0.0f;
    wasMoving_ = false;
    revision_ = 0;
}

void RailVehicleAudioBridge::Update(const RailVehicleAudioInput& input) {
    frame_ = {};
    const float deltaTime = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    brakeCooldown_ = (std::max)(0.0f, brakeCooldown_ - deltaTime);
    if (!input.settings.enabled || input.actor == nullptr ||
        input.presentation == nullptr || !input.actor->active) {
        wasMoving_ = false;
        rollingAccumulator_ = 0.0f;
        return;
    }

    const RailVehicleActorFrame& actor = *input.actor;
    const RailVehiclePresentationFrame& presentation = *input.presentation;
    const Vector3 listenerToVehicle = Subtract(actor.position, input.listenerPosition);
    const float distance = Length(listenerToVehicle);
    const float referenceDistance = (std::max)(1.0f, input.settings.referenceDistance);
    const float distanceRatio = distance / referenceDistance;
    const float attenuation = 1.0f / (1.0f + distanceRatio * distanceRatio);
    const float panWidth = (std::max)(1.0f, input.settings.spatialPanWidth);
    const float pan = (std::clamp)(
        Dot(listenerToVehicle, input.listenerRight) / panWidth,
        -1.0f,
        1.0f);
    const float master = (std::clamp)(input.settings.masterVolume, 0.0f, 1.0f);
    const float speed = (std::clamp)(presentation.speedNormalized, 0.0f, 1.0f);
    const bool moving = speed > 0.015f;
    const float boost = std::isfinite(input.manualBoostStrength)
        ? (std::clamp)(input.manualBoostStrength, 0.0f, 1.0f) : 0.0f;
    frame_.rollingLoopPitch = 1.0f + 0.26f * boost;
    frame_.rollingLoopGain = 1.0f + 0.40f * boost;

    if (moving) {
        rollingAccumulator_ += deltaTime;
        const float slowInterval = (std::max)(
            input.settings.rollingPulseSlowInterval,
            input.settings.rollingPulseFastInterval);
        const float fastInterval = (std::clamp)(
            input.settings.rollingPulseFastInterval,
            0.02f,
            slowInterval);
        const float interval = slowInterval + (fastInterval - slowInterval) * speed;
        if (rollingAccumulator_ >= interval) {
            rollingAccumulator_ = std::fmod(rollingAccumulator_, interval);
            PushCue(
                RailVehicleAudioCueKind::RollingPulse,
                master * attenuation * presentation.rollingAudioVolume * 0.42f,
                presentation.rollingAudioPitch * frame_.rollingLoopPitch,
                pan);
        }
    } else {
        rollingAccumulator_ = 0.0f;
    }

    if (presentation.jointImpactThisFrame) {
        PushCue(
            RailVehicleAudioCueKind::RailJointImpact,
            master * attenuation * (0.38f + speed * 0.42f),
            0.9f + speed * 0.2f,
            pan);
    }
    if (presentation.brakeAudioVolume > 0.08f && brakeCooldown_ <= 0.0f) {
        PushCue(
            RailVehicleAudioCueKind::BrakeScrape,
            master * attenuation * presentation.brakeAudioVolume * 0.65f,
            0.82f + speed * 0.24f,
            pan);
        brakeCooldown_ = (std::max)(0.05f, input.settings.brakeRetriggerInterval);
    }
    if (wasMoving_ && !moving) {
        PushCue(
            RailVehicleAudioCueKind::StopImpact,
            master * attenuation * 0.48f,
            0.86f,
            pan);
    }

    wasMoving_ = moving;
    frame_.distanceAttenuation = attenuation;
    frame_.rolling = moving;
    frame_.sourceActorRevision = actor.revision;
    frame_.sourcePresentationRevision = presentation.revision;
    frame_.revision = ++revision_;
}

void RailVehicleAudioBridge::PushCue(
    RailVehicleAudioCueKind kind,
    float volume,
    float pitch,
    float pan) {
    if (frame_.cueCount >= frame_.cues.size() || volume <= 0.0001f) return;
    frame_.cues[frame_.cueCount++] = {
        kind,
        (std::clamp)(volume, 0.0f, 1.0f),
        (std::clamp)(pitch, 0.5f, 2.0f),
        (std::clamp)(pan, -1.0f, 1.0f),
    };
}
