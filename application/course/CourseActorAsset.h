#pragma once

#include <string>

#include "utils/math/Vector.h"
#include "EnemyBehaviorSystem.h"
#include "EnemyEnergyShield.h"

struct CourseActorAsset {
    std::string id;
    std::string displayName;
    std::string meshId = "ball";
    std::string bulletPatternId = "single_red";
    float radius = 1.2f;
    float hitPoints = 30.0f;
    float lifetime = 8.0f;
    float forwardSpeed = 0.0f;
    float fireInterval = 0.8f;
    float firstShotDelay = 0.35f;
    float bulletSpeed = 48.0f;
    Vector4 color{1.0f, 0.25f, 0.18f, 1.0f};
    EnemyBehaviorDefinition behaviorDefinition{};
    EnemyEnergyShieldDefinition energyShield{};

    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
};
