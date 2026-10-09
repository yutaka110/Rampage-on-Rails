#pragma once

#include <memory>
#include <vector>

class RailVehicleMovementSystem;
class RailSpeedDirector;
class CourseRuntime;
class RailPath;
struct CourseAsset;

struct RailTravelPredictionInput final {
    const RailVehicleMovementSystem* vehicle = nullptr;
    const RailSpeedDirector* speedDirector = nullptr;
    const CourseRuntime* courseRuntime = nullptr;
    const CourseAsset* course = nullptr;
    const RailPath* railPath = nullptr;
    float simulationStepSeconds = 1.0f / 60.0f;
};

struct RailTravelPredictionSample final {
    float seconds = 0.0f;
    float distance = 0.0f;
};

// One immutable forecast shared by every enemy, warning and launch this frame.
// Checkpoints retain ownership; no pointer into a temporary movement state.
struct RailTravelPrediction final {
    std::vector<RailTravelPredictionSample> samples;
    float sourceSpeed = 0.0f;
    float DistanceAt(float seconds) const noexcept;
    float HorizonSeconds() const noexcept;
};

// Runs the production movement/policy systems on private copies only. Authored
// events affect the copied speed policy without dispatching gameplay effects.
std::shared_ptr<const RailTravelPrediction> BuildRailTravelPrediction(
    const RailTravelPredictionInput& input, float horizonSeconds);
