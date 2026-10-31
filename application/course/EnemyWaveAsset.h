#pragma once

#include <optional>
#include <string>
#include <vector>

#include "EnemyFormationDefinition.h"
#include "utils/math/Vector.h"
#include "RailEnemyIngress.h"

struct EnemyWaveUnit {
    std::string role = "drone";
    std::string actorAssetId;
    std::string bulletPatternId;
    float distanceOffset = 0.0f;
    float lateralOffset = 0.0f;
    float verticalOffset = 0.0f;
    float forwardSpeed = 0.0f;
    float radius = 1.2f;
    float lifetime = 8.0f;
    Vector4 color{1.0f, 0.25f, 0.18f, 1.0f};
};

struct EnemyWaveAsset {
    std::string id;
    std::string displayName;
    std::optional<EnemyFormationDefinition> formationDefinition;
    std::vector<EnemyWaveUnit> units;
    EnemyEntrySource entrySource = EnemyEntrySource::Automatic;
    bool localApproach = false;

    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
};
