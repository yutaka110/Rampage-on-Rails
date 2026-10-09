#include "RailTravelPrediction.h"

#include "RailVehicleMovementSystem.h"
#include "RailSpeedDirector.h"
#include "RailRideDirector.h"
#include "RailRideMotionEnvelope.h"

#include <algorithm>
#include <cmath>

float RailTravelPrediction::HorizonSeconds() const noexcept {
    return samples.empty() ? 0.0f : samples.back().seconds;
}

float RailTravelPrediction::DistanceAt(float seconds) const noexcept {
    if (samples.empty()) return 0.0f;
    if (seconds <= 0.0f) return samples.front().distance;
    const auto next = std::lower_bound(samples.begin(), samples.end(), seconds,
        [](const RailTravelPredictionSample& sample, float time) { return sample.seconds < time; });
    if (next == samples.end()) return samples.back().distance;
    if (next == samples.begin()) return next->distance;
    const auto& previous = *(next - 1);
    const float blend = (seconds - previous.seconds) / (next->seconds - previous.seconds);
    return previous.distance + (next->distance - previous.distance) * blend;
}

std::shared_ptr<const RailTravelPrediction> BuildRailTravelPrediction(
        const RailTravelPredictionInput& input, float horizonSeconds) {
    if (!input.vehicle || !input.vehicle->IsInitialized() || !input.speedDirector ||
        !input.courseRuntime || !input.course || !input.railPath || input.railPath->Length() <= 0.0f ||
        !std::isfinite(horizonSeconds) || horizonSeconds <= 0.0f ||
        !std::isfinite(input.simulationStepSeconds) || input.simulationStepSeconds <= 0.0f) return {};
    auto vehicle = *input.vehicle;
    auto speedDirector = *input.speedDirector;
    auto courseRuntime = *input.courseRuntime;
    RailRideDirector rideDirector;
    RailRideMotionEnvelope envelope;
    auto result = std::make_shared<RailTravelPrediction>();
    result->sourceSpeed = vehicle.State().speed;
    result->samples.push_back({0.0f, courseRuntime.Distance()});
    // Bound forecast work independently of drone count and authored lifetimes.
    // A request outside this horizon is rejected by the intercept solver.
    const float horizon = (std::min)(60.0f, horizonSeconds);
    const float step = (std::max)(horizon / 1800.0f,
        (std::clamp)(input.simulationStepSeconds, 1.0f / 120.0f, 1.0f / 30.0f));
    const int count = static_cast<int>(std::ceil(horizon / step));
    result->samples.reserve(count + 1);
    for (int index = 1; index <= count; ++index) {
        const float time = index == count ? horizon : step * index;
        const float dt = time - result->samples.back().seconds;
        RailSpeedDirectorFrameInput speedInput;
        speedInput.course = input.course;
        speedInput.railPath = input.railPath;
        speedInput.section = courseRuntime.CurrentSection();
        speedInput.rideProfile = input.course->FindRideProfile(courseRuntime.Distance());
        speedInput.distance = courseRuntime.Distance();
        speedInput.deltaTime = dt;
        const auto speed = speedDirector.Evaluate(speedInput);
        RailRideDirectorInput rideInput;
        rideInput.course = input.course;
        rideInput.railPath = input.railPath;
        rideInput.distance = courseRuntime.Distance();
        rideInput.baseRequestedSpeed = speed.requestedSpeed;
        const auto& ride = rideDirector.Evaluate(rideInput);
        RailRideMotionEnvelopeInput envelopeInput;
        envelopeInput.vehicleDefinition = &vehicle.Definition();
        envelopeInput.vehicleState = &vehicle.State();
        envelopeInput.ride = &ride;
        envelopeInput.railPath = input.railPath;
        const auto& motion = envelope.Evaluate(envelopeInput);
        RailVehicleMovementInput movement;
        movement.deltaTime = dt;
        movement.requestedSpeed = motion.requestedSpeed;
        movement.movementEnabled = vehicle.State().movementEnabled;
        movement.emergencyBrake = vehicle.State().emergencyBraking;
        movement.motionEnvelopeActive = motion.active;
        movement.accelerationLimit = motion.accelerationLimit;
        movement.brakingLimit = motion.brakingLimit;
        movement.jerkLimit = motion.jerkLimit;
        movement.courseRuntime = &courseRuntime;
        movement.railPath = input.railPath;
        const auto& frame = vehicle.Update(movement);
        speedDirector.NotifyCourseEvents(frame.triggeredEvents);
        result->samples.push_back({time, courseRuntime.Distance()});
    }
    return result;
}
