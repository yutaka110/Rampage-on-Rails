#include "RailVehicleCameraInertiaBridge.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 Scale(Vector3 v,float s) { return {v.x*s,v.y*s,v.z*s}; }
float ClampAbs(float value,float maximum) { return (std::clamp)(value,-maximum,maximum); }
}

bool RailVehicleCameraInertiaSettings::Validate(std::string* errorMessage) const {
    const bool valid = positionResponseHz > 0.0f && targetResponseHz > 0.0f &&
        fovResponseHz > 0.0f && accelerationLagDistance >= 0.0f &&
        brakingLeadDistance >= 0.0f && maximumLateralLag >= 0.0f &&
        maximumTurnLead >= 0.0f && maximumPresentationOffset >= 0.0f &&
        releaseBeatLagDistance >= 0.0f && releaseBeatFovDegrees >= 0.0f &&
        distanceDiscontinuityThreshold > 0.0f && maximumSubstepSeconds > 0.0f;
    if (!valid && errorMessage) *errorMessage = "Camera inertia settings contain an invalid range.";
    return valid;
}

void RailVehicleCameraInertiaBridge::Reset() {
    frame_={}; longitudinal_={}; lateral_={}; targetLead_={}; roll_={}; fov_={};
    previousDistance_=0.0f; previousVehicleId_.clear(); initialized_=false; revision_=0;
}

void RailVehicleCameraInertiaBridge::StepSpring(
    Spring& spring,float target,float frequencyHz,float dt) {
    const float omega=2.0f*kPi*(std::max)(0.01f,frequencyHz);
    const float acceleration=omega*omega*(target-spring.value)-2.0f*omega*spring.velocity;
    spring.velocity += acceleration*dt;
    spring.value += spring.velocity*dt;
}

const RailVehicleCameraInertiaFrame& RailVehicleCameraInertiaBridge::Update(
    const RailVehicleCameraInertiaInput& input) {
    RailVehicleCameraInertiaFrame next{};
    next.revision=++revision_;
    if (!input.settings.enabled || !input.gameplayActive || input.definition==nullptr ||
        input.state==nullptr || !input.state->initialized || !input.settings.Validate()) {
        initialized_=false; frame_=next; return frame_;
    }

    const bool discontinuity=!initialized_ || previousVehicleId_!=input.state->vehicleId ||
        std::abs(input.state->distance-previousDistance_)>input.settings.distanceDiscontinuityThreshold;
    if (discontinuity) {
        longitudinal_={}; lateral_={}; targetLead_={}; roll_={}; fov_={};
        next.historyResetThisFrame=true;
    }
    initialized_=true;
    previousVehicleId_=input.state->vehicleId;
    previousDistance_=input.state->distance;

    Vector3 forward=input.state->forward, up=input.state->up, right=input.state->right;
    if (input.trackContact && input.trackContact->valid &&
        input.trackContact->sourceVehicleRevision==input.state->revision) {
        forward=input.trackContact->forward; up=input.trackContact->up; right=input.trackContact->right;
        next.sourceTrackContactRevision=input.trackContact->revision;
    }
    const float accelDenominator=input.state->acceleration>=0.0f
        ? (std::max)(1.0f,input.definition->acceleration)
        : (std::max)(1.0f,input.definition->serviceBrakeDeceleration);
    const float normalizedAcceleration=(std::clamp)(input.state->acceleration/accelDenominator,-1.0f,1.0f);
    const float focusScale=input.aimFocusActive
        ? 1.0f-(std::clamp)(input.settings.aimFocusSuppression,0.0f,1.0f):1.0f;
    float longitudinalTarget=normalizedAcceleration>=0.0f
        ? -normalizedAcceleration*input.settings.accelerationLagDistance
        : -normalizedAcceleration*input.settings.brakingLeadDistance;
    const float lateralForce=input.state->signedCurvature*input.state->speed*input.state->speed;
    float lateralTarget=ClampAbs(-lateralForce*input.settings.lateralForceScale,
        input.settings.maximumLateralLag);
    float leadTarget=ClampAbs(lateralForce*input.settings.turnLeadScale,
        input.settings.maximumTurnLead);
    const float speedNormalized=input.definition->maximumSpeed>0.0f
        ? (std::clamp)(input.state->speed/input.definition->maximumSpeed,0.0f,1.0f):0.0f;
    float fovTarget=(input.settings.speedFovDegrees*speedNormalized+
        input.settings.accelerationFovDegrees*(std::max)(0.0f,normalizedAcceleration))*kPi/180.0f;
    if (input.ride && input.ride->speedBeatActive) {
        const bool release=input.ride->speedBeatType==RailRideSpeedBeatType::ReleaseBoost ||
            input.ride->speedBeatType==RailRideSpeedBeatType::ExitBoost;
        if (release) {
            longitudinalTarget-=input.settings.releaseBeatLagDistance*input.ride->speedBeatBlend;
            fovTarget+=input.settings.releaseBeatFovDegrees*input.ride->speedBeatBlend*kPi/180.0f;
        }
        next.sourceRideRevision=input.ride->revision;
    }
    float rollTarget=0.0f;
    if (input.rideDynamics && input.rideDynamics->valid &&
        input.rideDynamics->sourceVehicleRevision==input.state->revision) {
        rollTarget=input.rideDynamics->visualBankDegrees*input.settings.rollInheritance*kPi/180.0f;
    }
    longitudinalTarget*=focusScale; lateralTarget*=focusScale; leadTarget*=focusScale;
    fovTarget*=focusScale; rollTarget*=focusScale;
    // Keep the boost readable while locking on, with a smaller, stable FOV
    // expansion. Both aiming and display use the director's final projection.
    const float ignition = (std::clamp)(input.manualBoostIgnition, 0.0f, 1.0f);
    longitudinalTarget -= (input.aimFocusActive ? 0.22f : 0.65f) * ignition;
    fovTarget += (input.aimFocusActive ? 3.0f : 5.0f) *
        (std::clamp)(input.manualBoostStrength, 0.0f, 1.0f) * kPi / 180.0f;
    fovTarget += (input.aimFocusActive ? 0.45f : 0.9f) * ignition * kPi / 180.0f;

    float remaining=(std::clamp)(input.deltaTime,0.0f,0.25f);
    while (remaining>0.0f) {
        const float step=(std::min)(remaining,input.settings.maximumSubstepSeconds);
        StepSpring(longitudinal_,longitudinalTarget,input.settings.positionResponseHz,step);
        StepSpring(lateral_,lateralTarget,input.settings.positionResponseHz,step);
        StepSpring(targetLead_,leadTarget,input.settings.targetResponseHz,step);
        StepSpring(roll_,rollTarget,input.settings.positionResponseHz,step);
        StepSpring(fov_,fovTarget,input.settings.fovResponseHz,step);
        remaining-=step;
    }
    next.gameplayPositionOffset=Add(Scale(forward,longitudinal_.value),Scale(right,lateral_.value));
    next.gameplayTargetOffset=Scale(right,targetLead_.value);
    next.rollOffsetRadians=roll_.value;
    next.fovOffsetRadians=fov_.value;
    if (input.rideDynamics && input.rideDynamics->valid) {
        const float suspension=ClampAbs(
            -input.rideDynamics->suspensionOffset*input.settings.suspensionFollow,
            input.settings.maximumPresentationOffset);
        next.presentationPositionOffset=Scale(up,suspension);
        next.presentationTargetOffset=Scale(up,suspension*0.20f);
        if (next.sourceRideRevision==0) next.sourceRideRevision=input.rideDynamics->sourceRideRevision;
    }
    next.sourceVehicleRevision=input.state->revision;
    next.active=true;
    frame_=next;
    return frame_;
}
