#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include "../terrain/RailPath.h"

struct CourseAsset;
struct TerrainGenerationSettings;

enum class EnemyEntrySource : unsigned char { Automatic, Carrier, RockLeft, RockRight, High, SideOpening };
enum class RailEnemyIngressKind : unsigned char { Rock, Cliff, SideOpening };

// Coordinates are (rail lateral, rail vertical, absolute rail distance).
// The first three controls belong to a fixed terrain anchor. Only the final
// combat destination follows the cart, so the launch never slides with it.
struct RailEnemyIngressRoute {
    std::string id;
    RailEnemyIngressKind kind = RailEnemyIngressKind::Rock;
    float distance = 0;
    float side = 1;
    Vector3 start{}, departure{}, merge{};
};
struct RailEnemyIngressState {
    bool waiting = false, initialized = false, finished = false;
    RailEnemyIngressRoute route{};
    float elapsed = 0, delay = 0, duration = 1.2f;
    float forward = 76, lateral = 0, vertical = 6;
    uint32_t routeProbeCursor = 0;
    bool pathPrepared = false;
    float targetDistance = 0, pathLength = 0;
    std::array<float,65> arcDistances{};
    bool worldPrepared = false;
    std::array<Vector3,6> worldControls{};
};
struct RailEnemyIngressFlight {
    Vector3 railPosition{}, rotation{};
    Vector3 worldPosition{};
    bool hasWorldPosition = false;
    float progress = 0;
    bool released = false;
};

// Finds exterior approaches through existing terrain cutouts. No launch
// structures are added. Run once when the course is applied.
std::vector<RailEnemyIngressRoute> BuildRailEnemyIngressRoutes(
    CourseAsset& course, const RailPath& rail, const TerrainGenerationSettings& terrain);
RailEnemyIngressFlight EvaluateRailEnemyIngress(const RailEnemyIngressState& state, float playerDistance,
    const RailPath* rail = nullptr);
void PrepareRailEnemyIngress(RailEnemyIngressState& state, float playerDistance,
    float playerSpeed, const RailPath* rail = nullptr);
Vector3 ResolveRailEnemyIngressRotation(const RailEnemyIngressState& state,
    const RailPath& rail, float playerDistance, float playerSpeed,
    Vector3 cameraPosition, bool hasCameraPosition);
bool ValidateRailEnemyIngress(const RailEnemyIngressState& state);
