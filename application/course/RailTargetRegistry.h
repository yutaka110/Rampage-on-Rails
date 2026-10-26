#pragma once

#include <vector>

#include "RailLockOnTypes.h"

class CourseSpawnRuntime;
class RailPath;

struct RailTargetRegistryFrameInput {
    const CourseSpawnRuntime* spawnRuntime = nullptr;
    const RailPath* railPath = nullptr;
    float playerDistance = 0.0f;
    RailLockSettings settings{};
    Vector3 cameraPosition{};
    bool viewRelativeTargeting = false;
    Vector3 viewForward{0, 0, 1};
};

class RailTargetRegistry {
public:
    void Build(const RailTargetRegistryFrameInput& input);

    const std::vector<RailLockAnchor>& Anchors() const { return anchors_; }

private:
    std::vector<RailLockAnchor> anchors_;
};

