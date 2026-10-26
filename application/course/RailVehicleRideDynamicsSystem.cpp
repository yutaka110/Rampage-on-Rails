#include "RailVehicleRideDynamicsSystem.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadiansToDegrees = 180.0f / kPi;

bool FiniteRange(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

void SetError(std::string* errorMessage, const char* message) {
    if (errorMessage != nullptr) *errorMessage = message;
}

} // namespace

bool RailVehicleRideDynamicsSettings::Validate(
    std::string* errorMessage) const {
    if (!FiniteRange(rollFrequencyHz, 0.1f, 30.0f) ||
        !FiniteRange(pitchFrequencyHz, 0.1f, 30.0f) ||
        !FiniteRange(yawFrequencyHz, 0.1f, 30.0f) ||
        !FiniteRange(suspensionFrequencyHz, 0.1f, 60.0f) ||
        !FiniteRange(rollDampingRatio, 0.1f, 3.0f) ||
        !FiniteRange(pitchDampingRatio, 0.1f, 3.0f) ||
        !FiniteRange(yawDampingRatio, 0.1f, 3.0f) ||
        !FiniteRange(suspensionDampingRatio, 0.1f, 3.0f) ||
        !FiniteRange(maximumBankDegrees, 0.0f, 60.0f) ||
        !FiniteRange(maximumPitchDegrees, 0.0f, 45.0f) ||
        !FiniteRange(maximumYawLagDegrees, 0.0f, 30.0f) ||
        !FiniteRange(yawLagSeconds, 0.0f, 2.0f) ||
        !FiniteRange(gradePitchScale, 0.0f, 4.0f) ||
        !FiniteRange(accelerationPitchScale, 0.0f, 4.0f) ||
        !FiniteRange(jerkPitchDegreesPerUnit, 0.0f, 2.0f) ||
        !FiniteRange(maximumJerkPitchDegrees, 0.0f, 20.0f) ||
        !FiniteRange(suspensionAmplitude, 0.0f, 5.0f) ||
        !FiniteRange(railJointSpacing, 0.05f, 1000.0f) ||
        !FiniteRange(distanceDiscontinuityThreshold, 0.1f, 100000.0f) ||
        !FiniteRange(maximumSubstepSeconds, 1.0f / 1000.0f, 1.0f / 30.0f)) {
        SetError(errorMessage,
            "rail vehicle ride dynamics setting is outside commercial limits");
        return false;
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return true;
}

void RailVehicleRideDynamicsSystem::Reset() {
    frame_ = {};
    roll_ = {};
    pitch_ = {};
    yaw_ = {};
    suspension_ = {};
    previousAcceleration_ = 0.0f;
    previousDistance_ = 0.0f;
    previousVehicleId_.clear();
    initialized_ = false;
    revision_ = 0;
}

void RailVehicleRideDynamicsSystem::StepSpring(
    SpringState& spring,
    float target,
    float frequencyHz,
    float dampingRatio,
    float deltaTime) {
    const float omega = 2.0f * kPi * frequencyHz;
    const float acceleration = omega * omega * (target - spring.value) -
        2.0f * dampingRatio * omega * spring.velocity;
    spring.velocity += acceleration * deltaTime;
    spring.value += spring.velocity * deltaTime;
}

const RailVehicleRideDynamicsFrame& RailVehicleRideDynamicsSystem::Update(
    const RailVehicleRideDynamicsInput& input) {
    frame_ = {};
    std::string validationError;
    if (!input.settings.enabled || !input.settings.Validate(&validationError) ||
        input.definition == nullptr || input.state == nullptr ||
        !input.state->initialized) {
        return frame_;
    }

    const RailVehicleDefinition& definition = *input.definition;
    const RailVehicleRuntimeState& state = *input.state;
    const RailVehicleRideDynamicsSettings& settings = input.settings;
    const float deltaTime = std::isfinite(input.deltaTime)
        ? (std::clamp)(input.deltaTime, 0.0f, 0.25f)
        : 0.0f;
    const float rideBlend = input.ride != nullptr && input.ride->active
        ? (std::clamp)(input.ride->profileBlend, 0.0f, 1.0f)
        : 0.0f;
    const float anticipatedCurvature = input.ride != nullptr
        ? input.ride->anticipatedSignedCurvature
        : state.signedCurvature;
    const float bankCurvature = state.signedCurvature +
        (anticipatedCurvature - state.signedCurvature) * rideBlend;
    const float profileBankScale = input.ride != nullptr
        ? (std::max)(0.0f, input.ride->visualBankScale)
        : 1.0f;
    const float bankScale = 1.0f + (profileBankScale - 1.0f) * rideBlend;
    const float maximumBank = input.ride != nullptr
        ? settings.maximumBankDegrees +
            ((std::max)(0.0f, input.ride->maximumVisualBankDegrees) -
                settings.maximumBankDegrees) * rideBlend
        : settings.maximumBankDegrees;
    const float rawBank = -std::atan2(
        bankCurvature * state.speed * state.speed * bankScale,
        9.81f) * kRadiansToDegrees;
    float bankTarget = (std::clamp)(rawBank, -maximumBank, maximumBank);
    const RailTrackFeedbackFrame* trackFeedback =
        input.trackFeedback != nullptr && input.trackFeedback->valid
        ? input.trackFeedback : nullptr;
    if (trackFeedback != nullptr) {
        if (trackFeedback->bankOverrideActive) {
            const float blend = (std::clamp)(
                trackFeedback->bankOverrideBlend, 0.0f, 1.0f);
            bankTarget += (trackFeedback->bankOverrideDegrees - bankTarget) * blend;
        }
        bankTarget += trackFeedback->additiveBankDegrees;
        bankTarget = (std::clamp)(bankTarget, -45.0f, 45.0f);
    }

    const float accelerationDenominator =
        (std::max)(1.0f, definition.emergencyBrakeDeceleration);
    const float gradePitch = std::asin(
        (std::clamp)(state.grade, -1.0f, 1.0f)) * kRadiansToDegrees *
        settings.gradePitchScale;
    const float accelerationPitch =
        -(state.acceleration / accelerationDenominator) *
        settings.maximumPitchDegrees * settings.accelerationPitchScale;
    const float jerk = deltaTime > 0.000001f
        ? (state.acceleration - previousAcceleration_) / deltaTime
        : 0.0f;
    const float jerkPitch = (std::clamp)(
        -jerk * settings.jerkPitchDegreesPerUnit,
        -settings.maximumJerkPitchDegrees,
        settings.maximumJerkPitchDegrees);
    // A small sustained rearward load complements the existing acceleration
    // spring. Collision, mounts and weapon origins remain authoritative.
    const float boostPitch = -1.8f * (std::clamp)(input.manualBoostStrength, 0.0f, 1.0f)
        - 1.5f * (std::clamp)(input.manualBoostIgnition, 0.0f, 1.0f);
    const float rawPitch = gradePitch + accelerationPitch + jerkPitch + boostPitch;
    const float pitchTarget = (std::clamp)(
        rawPitch,
        -settings.maximumPitchDegrees,
        settings.maximumPitchDegrees);

    const float rawYaw = -bankCurvature * state.speed *
        settings.yawLagSeconds * kRadiansToDegrees;
    const float yawTarget = (std::clamp)(
        rawYaw,
        -settings.maximumYawLagDegrees,
        settings.maximumYawLagDegrees);

    const float maximumSpeed = (std::max)(1.0f, definition.maximumSpeed);
    const float speedNormalized = (std::clamp)(
        state.speed / maximumSpeed, 0.0f, 1.0f);
    const float jointSpacing = (std::max)(0.05f, settings.railJointSpacing);
    const float jointPhase = std::fmod((std::max)(0.0f, state.distance),
        jointSpacing) / jointSpacing;
    const float jointPulse = std::exp(-jointPhase * 24.0f);
    const float suspensionTarget =
        settings.suspensionAmplitude * speedNormalized * jointPulse +
        (trackFeedback != nullptr ? trackFeedback->suspensionOffset : 0.0f);

    const bool discontinuity = !initialized_ ||
        previousVehicleId_ != state.vehicleId ||
        std::abs(state.distance - previousDistance_) >
            settings.distanceDiscontinuityThreshold;
    if (discontinuity) {
        roll_ = {bankTarget, 0.0f};
        pitch_ = {pitchTarget, 0.0f};
        yaw_ = {yawTarget, 0.0f};
        suspension_ = {suspensionTarget, 0.0f};
        frame_.historyResetThisFrame = true;
    } else if (deltaTime > 0.0f) {
        float remaining = deltaTime;
        uint32_t substeps = 0;
        while (remaining > 0.000001f && substeps++ < 64u) {
            const float step = (std::min)(remaining, settings.maximumSubstepSeconds);
            StepSpring(roll_, bankTarget, settings.rollFrequencyHz,
                settings.rollDampingRatio, step);
            StepSpring(pitch_, pitchTarget, settings.pitchFrequencyHz,
                settings.pitchDampingRatio, step);
            StepSpring(yaw_, yawTarget, settings.yawFrequencyHz,
                settings.yawDampingRatio, step);
            StepSpring(suspension_, suspensionTarget,
                settings.suspensionFrequencyHz,
                settings.suspensionDampingRatio, step);
            remaining -= step;
        }
    }

    const float effectiveMaximumBank = trackFeedback != nullptr && trackFeedback->active
        ? (std::max)(maximumBank, 45.0f)
        : maximumBank;
    roll_.value = (std::clamp)(
        roll_.value, -effectiveMaximumBank, effectiveMaximumBank);
    pitch_.value = (std::clamp)(pitch_.value,
        -settings.maximumPitchDegrees, settings.maximumPitchDegrees);
    yaw_.value = (std::clamp)(yaw_.value,
        -settings.maximumYawLagDegrees, settings.maximumYawLagDegrees);
    suspension_.value = (std::clamp)(suspension_.value,
        -settings.suspensionAmplitude * 0.35f,
        settings.suspensionAmplitude * 1.35f);

    frame_.valid = true;
    frame_.comfortClamped =
        (trackFeedback == nullptr && std::abs(rawBank - bankTarget) > 0.001f) ||
        std::abs(rawPitch - pitchTarget) > 0.001f ||
        std::abs(rawYaw - yawTarget) > 0.001f;
    frame_.targetBankDegrees = bankTarget;
    frame_.targetPitchDegrees = pitchTarget;
    frame_.targetYawDegrees = yawTarget;
    frame_.visualBankDegrees = roll_.value;
    frame_.visualPitchDegrees = pitch_.value;
    frame_.visualYawDegrees = yaw_.value;
    frame_.suspensionOffset = suspension_.value;
    frame_.accelerationJerk = jerk;
    frame_.sourceVehicleRevision = state.revision;
    frame_.sourceRideRevision = input.ride != nullptr ? input.ride->revision : 0;
    frame_.revision = ++revision_;

    previousAcceleration_ = state.acceleration;
    previousDistance_ = state.distance;
    previousVehicleId_ = state.vehicleId;
    initialized_ = true;
    return frame_;
}
