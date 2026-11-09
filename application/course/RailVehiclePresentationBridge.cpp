#include "RailVehiclePresentationBridge.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 Add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 Scale(Vector3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

} // namespace

void RailVehiclePresentationBridge::Reset() {
    frame_ = {};
    smoothedBankDegrees_ = 0.0f;
    smoothedPitchDegrees_ = 0.0f;
    previousJointIndex_ = 0;
    jointInitialized_ = false;
    revision_ = 0;
}

void RailVehiclePresentationBridge::Update(
    const RailVehiclePresentationInput& input) {
    frame_ = {};
    if (!input.settings.enabled || input.definition == nullptr ||
        input.state == nullptr || !input.state->initialized) {
        return;
    }
    const RailVehicleDefinition& definition = *input.definition;
    const RailVehicleRuntimeState& state = *input.state;
    const RailVehiclePresentationSettings& settings = input.settings;
    const float deltaTime = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    const float maximumSpeed = (std::max)(1.0f, definition.maximumSpeed);
    const float speedNormalized = (std::clamp)(state.speed / maximumSpeed, 0.0f, 1.0f);
    const float rideBlend = input.rideProfileActive
        ? (std::clamp)(input.rideProfileBlend, 0.0f, 1.0f) : 0.0f;
    const float bankCurvature = state.signedCurvature +
        (input.rideAnticipatedSignedCurvature - state.signedCurvature) * rideBlend;
    const float bankScale = 1.0f +
        ((std::max)(0.0f, input.rideVisualBankScale) - 1.0f) * rideBlend;
    const float maximumBankDegrees = settings.maximumVisualBankDegrees +
        ((std::max)(0.0f, input.rideMaximumVisualBankDegrees) -
            settings.maximumVisualBankDegrees) * rideBlend;
    const float lateralAcceleration =
        bankCurvature * state.speed * state.speed * bankScale;
    const float bankTarget = (std::clamp)(
        -std::atan2(lateralAcceleration, 9.81f) * 180.0f / kPi,
        -maximumBankDegrees,
        maximumBankDegrees);
    const float gradePitch = std::asin((std::clamp)(state.grade, -1.0f, 1.0f)) *
        180.0f / kPi;
    const float accelerationPitch =
        -(std::clamp)(state.acceleration / definition.emergencyBrakeDeceleration,
            -1.0f, 1.0f) * settings.maximumVisualPitchDegrees * 0.45f;
    const float pitchTarget = (std::clamp)(
        gradePitch + accelerationPitch,
        -settings.maximumVisualPitchDegrees,
        settings.maximumVisualPitchDegrees);
    const RailVehicleRideDynamicsFrame* dynamics =
        input.rideDynamics != nullptr && input.rideDynamics->valid
        ? input.rideDynamics : nullptr;
    if (dynamics != nullptr) {
        smoothedBankDegrees_ = dynamics->visualBankDegrees;
        smoothedPitchDegrees_ = dynamics->visualPitchDegrees;
    } else {
        const float response = 1.0f - std::exp(
            -(std::max)(0.01f, settings.smoothingResponse) * deltaTime);
        smoothedBankDegrees_ += (bankTarget - smoothedBankDegrees_) * response;
        smoothedPitchDegrees_ += (pitchTarget - smoothedPitchDegrees_) * response;
    }

    const float wheelRadius = (std::max)(0.01f, settings.wheelRadius);
    const float jointSpacing = (std::max)(0.05f, settings.railJointSpacing);
    const uint64_t jointIndex = static_cast<uint64_t>(
        (std::max)(0.0f, std::floor(state.distance / jointSpacing)));
    const bool crossedJoint = jointInitialized_ && jointIndex != previousJointIndex_;
    previousJointIndex_ = jointIndex;
    jointInitialized_ = true;
    const float jointPhase = std::fmod(state.distance, jointSpacing) / jointSpacing;
    const float jointPulse = std::exp(-jointPhase * 24.0f);
    const float suspension = dynamics != nullptr
        ? dynamics->suspensionOffset
        : settings.suspensionAmplitude * speedNormalized * jointPulse;
    const RailVehicleTrackContactPoseFrame* contact =
        input.trackContactPose != nullptr && input.trackContactPose->valid &&
        input.trackContactPose->sourceVehicleRevision == state.revision
        ? input.trackContactPose : nullptr;

    frame_.visible = true;
    frame_.forward = contact != nullptr ? contact->forward : state.forward;
    frame_.up = contact != nullptr ? contact->up : state.up;
    frame_.right = contact != nullptr ? contact->right : state.right;
    const Vector3 basePosition = contact != nullptr
        ? contact->visualPosition : state.position;
    frame_.visualPosition = Add(basePosition, Scale(frame_.up, suspension-input.brakeCompression));
    frame_.wheelRotationRadians = std::fmod(state.distance / wheelRadius, 2.0f * kPi);
    frame_.visualBankDegrees = smoothedBankDegrees_;
    frame_.visualPitchDegrees = smoothedPitchDegrees_+input.brakePitchDegrees;
    frame_.visualYawDegrees = dynamics != nullptr
        ? dynamics->visualYawDegrees : 0.0f;
    frame_.suspensionOffset = suspension;
    frame_.speedNormalized = speedNormalized;
    frame_.rollingAudioVolume = speedNormalized;
    frame_.rollingAudioPitch = 0.82f + speedNormalized * 0.48f;
    frame_.brakeAudioVolume = (std::clamp)(
        -state.acceleration / definition.emergencyBrakeDeceleration,
        0.0f,
        1.0f);
    frame_.jointImpactThisFrame =
        crossedJoint && state.speed >= settings.jointImpactSpeedThreshold;
    frame_.brakeSparksActive =
        state.acceleration <= -settings.brakeSparkDecelerationThreshold &&
        state.speed > 4.0f;
    frame_.crossedJointIndex = jointIndex;
    frame_.sourceVehicleRevision = state.revision;
    frame_.revision = ++revision_;
}
