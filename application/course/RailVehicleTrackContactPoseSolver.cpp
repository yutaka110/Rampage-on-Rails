#include "RailVehicleTrackContactPoseSolver.h"

#include <algorithm>
#include <cmath>

namespace {
float Dot(Vector3 a, Vector3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vector3 Add(Vector3 a, Vector3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vector3 Sub(Vector3 a, Vector3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vector3 Scale(Vector3 v, float s) { return {v.x*s, v.y*s, v.z*s}; }
Vector3 Cross(Vector3 a, Vector3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
float Length(Vector3 v) { return std::sqrt(Dot(v, v)); }
Vector3 NormalizeOr(Vector3 v, Vector3 fallback) {
    const float length = Length(v);
    return length > 0.00001f ? Scale(v, 1.0f/length) : fallback;
}
bool Finite(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

bool RailVehicleTrackContactPoseSettings::Validate(std::string* errorMessage) const {
    if (!std::isfinite(wheelbase) || wheelbase <= 0.05f || wheelbase > 100.0f) {
        if (errorMessage) *errorMessage = "Track contact wheelbase is outside commercial limits.";
        return false;
    }
    if (!std::isfinite(trackGauge) || trackGauge <= 0.05f || trackGauge > 100.0f) {
        if (errorMessage) *errorMessage = "Track contact gauge is outside commercial limits.";
        return false;
    }
    if (!std::isfinite(bodyPivotHeightAboveContacts) ||
        bodyPivotHeightAboveContacts < 0.0f ||
        bodyPivotHeightAboveContacts > 100.0f) {
        if (errorMessage) *errorMessage = "Track contact body pivot height is invalid.";
        return false;
    }
    if (!std::isfinite(contactClearance) || contactClearance < 0.0f ||
        contactClearance > 2.0f || !std::isfinite(railHeadVerticalOffset) ||
        std::abs(railHeadVerticalOffset) > 10.0f) {
        if (errorMessage) *errorMessage = "Track contact rail-head offsets are invalid.";
        return false;
    }
    if (!std::isfinite(maximumSuspensionTravel) ||
        maximumSuspensionTravel < 0.0f || maximumSuspensionTravel > 10.0f) {
        if (errorMessage) *errorMessage = "Track contact suspension travel is invalid.";
        return false;
    }
    if (!std::isfinite(minimumContactSeparation) || minimumContactSeparation <= 0.0f) {
        if (errorMessage) *errorMessage = "Track contact minimum separation must be positive.";
        return false;
    }
    return true;
}

void RailVehicleTrackContactPoseSolver::Reset() {
    frame_ = {};
    revision_ = 0;
}

const RailVehicleTrackContactPoseFrame& RailVehicleTrackContactPoseSolver::Solve(
    const RailVehicleTrackContactPoseInput& input) {
    RailVehicleTrackContactPoseFrame next{};
    next.revision = ++revision_;
    if (!input.settings.enabled || input.definition == nullptr || input.state == nullptr ||
        input.railPath == nullptr || !input.state->initialized ||
        input.railPath->Length() <= input.settings.minimumContactSeparation ||
        !input.settings.Validate()) {
        frame_ = next;
        return frame_;
    }

    const float railLength = input.railPath->Length();
    const float halfWheelbase = input.settings.wheelbase * 0.5f;
    next.rearDistance = (std::clamp)(input.state->distance-halfWheelbase, 0.0f, railLength);
    next.frontDistance = (std::clamp)(input.state->distance+halfWheelbase, 0.0f, railLength);
    next.clampedAtRailStart = next.rearDistance <= 0.0f;
    next.clampedAtRailEnd = next.frontDistance >= railLength;
    // Wheel contacts stay on the authored open rail, including its endpoint.
    const auto evaluateOpenRail = [input, railLength](float distance) {
        const float sampleDistance = distance >= railLength
            ? std::nextafter(railLength, 0.0f)
            : distance;
        return input.railPath->Evaluate(sampleDistance);
    };
    const RailPathSample rear = evaluateOpenRail(next.rearDistance);
    const RailPathSample front = evaluateOpenRail(next.frontDistance);
    const Vector3 rearRight = NormalizeOr(rear.right, input.state->right);
    const Vector3 frontRight = NormalizeOr(front.right, input.state->right);
    const Vector3 rearUp = NormalizeOr(rear.up, input.state->up);
    const Vector3 frontUp = NormalizeOr(front.up, input.state->up);
    const Vector3 rearRailHead = Add(
        rear.position, Scale(rearUp, input.settings.railHeadVerticalOffset));
    const Vector3 frontRailHead = Add(
        front.position, Scale(frontUp, input.settings.railHeadVerticalOffset));
    const float halfGauge = input.settings.trackGauge * 0.5f;
    next.rearLeftContact = Sub(rearRailHead, Scale(rearRight, halfGauge));
    next.rearRightContact = Add(rearRailHead, Scale(rearRight, halfGauge));
    next.frontLeftContact = Sub(frontRailHead, Scale(frontRight, halfGauge));
    next.frontRightContact = Add(frontRailHead, Scale(frontRight, halfGauge));
    next.rearContact = Scale(
        Add(next.rearLeftContact, next.rearRightContact), 0.5f);
    next.frontContact = Scale(
        Add(next.frontLeftContact, next.frontRightContact), 0.5f);
    next.contactCentroid = Scale(
        Add(Add(next.rearLeftContact, next.rearRightContact),
            Add(next.frontLeftContact, next.frontRightContact)),
        0.25f);

    const Vector3 chord = Sub(next.frontContact, next.rearContact);
    const float contactSeparation = Length(chord);
    next.forward = NormalizeOr(chord, input.state->forward);
    const Vector3 rearAxle = Sub(next.rearRightContact, next.rearLeftContact);
    const Vector3 frontAxle = Sub(next.frontRightContact, next.frontLeftContact);
    Vector3 rightHint = Add(rearAxle, frontAxle);
    rightHint = Sub(rightHint, Scale(next.forward, Dot(rightHint, next.forward)));
    next.right = NormalizeOr(rightHint, input.state->right);
    Vector3 averagedUp = NormalizeOr(Add(rearUp, frontUp), input.state->up);
    next.up = NormalizeOr(Cross(next.forward, next.right), averagedUp);
    if (Dot(next.up, averagedUp) < 0.0f) {
        next.up = Scale(next.up, -1.0f);
        next.right = Scale(next.right, -1.0f);
    }

    const auto suspensionOffset = [&next](Vector3 contact) {
        return -Dot(Sub(contact, next.contactCentroid), next.up);
    };
    next.rearLeftSuspensionOffset = suspensionOffset(next.rearLeftContact);
    next.rearRightSuspensionOffset = suspensionOffset(next.rearRightContact);
    next.frontLeftSuspensionOffset = suspensionOffset(next.frontLeftContact);
    next.frontRightSuspensionOffset = suspensionOffset(next.frontRightContact);
    next.maximumContactPlaneError = (std::max)({
        std::abs(next.rearLeftSuspensionOffset),
        std::abs(next.rearRightSuspensionOffset),
        std::abs(next.frontLeftSuspensionOffset),
        std::abs(next.frontRightSuspensionOffset)});
    next.allWheelsSupported = next.maximumContactPlaneError <=
        input.settings.maximumSuspensionTravel;
    next.contactCount = 4;
    next.visualPosition = Add(
        next.contactCentroid,
        Scale(next.up,
            input.settings.bodyPivotHeightAboveContacts +
            input.settings.contactClearance));
    next.grade = std::asin((std::clamp)(next.forward.y, -1.0f, 1.0f));
    next.sourceVehicleRevision = input.state->revision;
    next.valid = contactSeparation >= input.settings.minimumContactSeparation &&
        Length(rearAxle) >= input.settings.minimumContactSeparation &&
        Length(frontAxle) >= input.settings.minimumContactSeparation &&
        Finite(next.rearLeftContact) && Finite(next.rearRightContact) &&
        Finite(next.frontLeftContact) && Finite(next.frontRightContact) &&
        Finite(next.visualPosition) && Finite(next.forward) &&
        Finite(next.up) && Finite(next.right);
    frame_ = next;
    return frame_;
}
